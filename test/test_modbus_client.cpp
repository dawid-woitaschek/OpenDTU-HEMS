// SPDX-License-Identifier: GPL-2.0-or-later
// Production Client.cpp over real loopback TCP sockets. Only ESP32 platform/DNS
// bindings are shimmed; Modbus framing, deadlines and socket I/O are unchanged.
#include <hems/modbus/Client.h>
#include <Arduino.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

static dns_found_callback pendingCallback = nullptr;
static void* pendingContext = nullptr;
static unsigned dnsCalls = 0;

err_t dns_gethostbyname_addrtype(char const* host, ip_addr_t* address,
        dns_found_callback callback, void* context, uint8_t)
{
    ++dnsCalls;
    if (strcmp(host, "pending") == 0) {
        pendingCallback = callback;
        pendingContext = context;
        return ERR_INPROGRESS;
    }
    if (strcmp(host, "missing") == 0) { return -1; }
    address->addr = htonl(INADDR_LOOPBACK);
    return ERR_OK;
}

class Server {
public:
    explicit Server(std::function<void(int, std::array<uint8_t, 12>)> handler)
    {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        assert(listener >= 0);
        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t length = sizeof(address);
        assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        port = ntohs(address.sin_port);
        assert(listen(listener, 1) == 0);
        worker = std::thread([this, handler] {
            int connection = accept(listener, nullptr, nullptr);
            assert(connection >= 0);
            timeval timeout { 3, 0 };
            setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            std::array<uint8_t, 12> request {};
            size_t offset = 0;
            while (offset < request.size()) {
                int received = recv(connection, request.data() + offset, request.size() - offset, 0);
                assert(received > 0);
                offset += received;
            }
            assert(request[7] == 4); // No write function reaches the wire.
            handler(connection, request);
            ::close(connection);
        });
    }
    ~Server() { worker.join(); ::close(listener); }
    uint16_t port;

private:
    int listener;
    std::thread worker;
};

static void testResponse(bool fragmented, bool wrongTransaction, bool exception)
{
    Server server([=](int socket, std::array<uint8_t, 12> request) {
        uint8_t response[] { request[0], request[1], 0, 0, 0, 7, request[6], 4, 4, 0x45, 0xe7, 0xe0, 0 };
        size_t length = sizeof(response);
        if (wrongTransaction) { response[1] ^= 1; }
        if (exception) { response[5] = 3; response[7] = 0x84; response[8] = 2; length = 9; }
        if (fragmented) {
            for (size_t i = 0; i < length; ++i) { assert(send(socket, response + i, 1, 0) == 1); delay(2); }
        } else { assert(send(socket, response, length, 0) == static_cast<int>(length)); }
    });
    Hems::Modbus::Client client;
    assert(client.open("127.0.0.1", server.port));
    uint8_t payload[4] {};
    auto result = client.readInputRegisters(2002, 2, payload);
    if (wrongTransaction) { assert(result.error == Hems::Modbus::Error::Protocol); }
    else if (exception) { assert(result.error == Hems::Modbus::Error::Exception && result.exception == 2); }
    else { assert(result && Hems::Modbus::decodeFloat32(payload) == 7420); }
    client.close();
}

static void testTimeoutAndMalformed()
{
    {
        Server server([](int, std::array<uint8_t, 12>) { delay(1700); });
        Hems::Modbus::Client client;
        assert(client.open("127.0.0.1", server.port));
        uint8_t payload[4];
        auto started = millis();
        auto result = client.readInputRegisters(2002, 2, payload);
        assert(result.error == Hems::Modbus::Error::Timeout && !result.responded);
        assert(millis() - started >= 1400 && millis() - started < 2500);
        client.close();
    }
    for (bool oversized : { false, true }) {
        Server server([=](int socket, std::array<uint8_t, 12> request) {
            uint8_t response[] { request[0], request[1], 0, 0, static_cast<uint8_t>(oversized ? 1 : 0), 1, 1 };
            assert(send(socket, response, sizeof(response), 0) == sizeof(response));
        });
        Hems::Modbus::Client client;
        assert(client.open("127.0.0.1", server.port));
        uint8_t payload[4];
        assert(client.readInputRegisters(2002, 2, payload).error == Hems::Modbus::Error::Protocol);
        client.close();
    }
    Server truncated([](int socket, std::array<uint8_t, 12> request) {
        uint8_t header[] { request[0], request[1], 0, 0, 0, 7, 1 };
        assert(send(socket, header, sizeof(header), 0) == sizeof(header));
    });
    Hems::Modbus::Client client;
    assert(client.open("127.0.0.1", truncated.port));
    uint8_t payload[4];
    assert(client.readInputRegisters(2002, 2, payload).error == Hems::Modbus::Error::Timeout);
    client.close();
}

static void testDnsAndRecovery()
{
    Hems::Modbus::Client client; // Retained until every pending callback completes.
    assert(client.open("missing", 502).error == Hems::Modbus::Error::DnsFailure);
    auto started = millis();
    assert(client.open("pending", 502).error == Hems::Modbus::Error::DnsTimeout);
    assert(millis() - started < 2000);
    auto calls = dnsCalls;
    started = millis();
    assert(client.open("new-host", 502).error == Hems::Modbus::Error::DnsTimeout);
    assert(dnsCalls == calls && millis() - started < 100); // No extra pending lookups.
    ip_addr_t resolved { htonl(INADDR_LOOPBACK) };
    pendingCallback("pending", &resolved, pendingContext); // Late completion is safe.
    assert(client.open("missing", 502).error == Hems::Modbus::Error::DnsFailure);
    Server server([](int socket, std::array<uint8_t, 12> request) {
        uint8_t response[] { request[0], request[1], 0, 0, 0, 7, 1, 4, 4, 0, 0, 0, 5 };
        assert(send(socket, response, sizeof(response), 0) == sizeof(response));
    });
    assert(client.open("new-host", server.port));
    uint8_t payload[4];
    assert(client.readInputRegisters(0, 2, payload) && Hems::Modbus::decodeUint32(payload) == 5);
    client.close();
    assert(client.open("warp.local", 502).error == Hems::Modbus::Error::DnsFailure);
}

int main()
{
    testResponse(true, false, false);
    testResponse(false, true, false);
    testResponse(false, false, true);
    testTimeoutAndMalformed();
    testDnsAndRecovery();
    std::cout << "Production Modbus client loopback TCP, timeout, DNS and recovery tests passed\n";
}
