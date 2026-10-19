// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/modbus/Client.h>
#include <Arduino.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>

#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

err_t dns_gethostbyname_addrtype(char const*, ip_addr_t* address,
        dns_found_callback, void*, uint8_t)
{
    address->addr = htonl(INADDR_LOOPBACK);
    return ERR_OK;
}

class WriteServer {
public:
    explicit WriteServer(std::function<void(int, std::vector<uint8_t> const&)> handler)
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

            std::vector<uint8_t> request(7);
            receiveAll(connection, request.data(), request.size());
            size_t length = (static_cast<size_t>(request[4]) << 8) | request[5];
            assert(length >= 2 && length + 6 <= Hems::Modbus::MaxFrame);
            size_t total = length + 6;
            request.resize(total);
            receiveAll(connection, request.data() + 7, total - 7);
            handler(connection, request);
            ::close(connection);
        });
    }

    ~WriteServer() { worker.join(); ::close(listener); }
    uint16_t port;

private:
    static void receiveAll(int socket, uint8_t* data, size_t size)
    {
        size_t offset = 0;
        while (offset < size) {
            int received = recv(socket, data + offset, size - offset, 0);
            assert(received > 0);
            offset += received;
        }
    }

    int listener;
    std::thread worker;
};

static void singleRegister()
{
    WriteServer server([](int socket, std::vector<uint8_t> const& request) {
        assert(request.size() == 12 && request[7] == 6);
        assert(request[8] == 0x12 && request[9] == 0x34);
        assert(request[10] == 0xab && request[11] == 0xcd);
        assert(send(socket, request.data(), request.size(), 0) == static_cast<int>(request.size()));
    });
    Hems::Modbus::Client client;
    assert(client.open("127.0.0.1", server.port));
    uint16_t value = 0xabcd;
    assert(client.writeHoldingRegisters(0x1234, 1, &value));
    client.close();
}

static void multipleRegisters()
{
    WriteServer server([](int socket, std::vector<uint8_t> const& request) {
        assert(request.size() == 17 && request[7] == 16);
        assert(request[8] == 0x03 && request[9] == 0xea);
        assert(request[10] == 0 && request[11] == 2 && request[12] == 4);
        assert(request[13] == 0 && request[14] == 0);
        assert(request[15] == 0x17 && request[16] == 0x70); // 6000 mA
        uint8_t response[] {
            request[0], request[1], 0, 0, 0, 6, request[6], 16,
            request[8], request[9], 0, 2
        };
        assert(send(socket, response, sizeof(response), 0) == sizeof(response));
    });
    Hems::Modbus::Client client;
    assert(client.open("127.0.0.1", server.port));
    uint16_t values[] { 0, 6000 };
    assert(client.writeHoldingRegisters(1002, 2, values));
    client.close();
}

static void exceptionAndMalformed()
{
    {
        WriteServer server([](int socket, std::vector<uint8_t> const& request) {
            uint8_t response[] { request[0], request[1], 0, 0, 0, 3, request[6], 0x90, 2 };
            assert(send(socket, response, sizeof(response), 0) == sizeof(response));
        });
        Hems::Modbus::Client client;
        assert(client.open("127.0.0.1", server.port));
        uint16_t values[] { 0, 6000 };
        auto result = client.writeHoldingRegisters(1002, 2, values);
        assert(result.error == Hems::Modbus::Error::Exception && result.exception == 2);
        client.close();
    }
    {
        WriteServer server([](int socket, std::vector<uint8_t> const& request) {
            uint8_t response[] {
                request[0], request[1], 0, 0, 0, 6, request[6], 6,
                request[8], static_cast<uint8_t>(request[9] + 1), request[10], request[11]
            };
            assert(send(socket, response, sizeof(response), 0) == sizeof(response));
        });
        Hems::Modbus::Client client;
        assert(client.open("127.0.0.1", server.port));
        uint16_t value = 1;
        assert(client.writeHoldingRegisters(10, 1, &value).error == Hems::Modbus::Error::Protocol);
        client.close();
    }
}

int main()
{
    singleRegister();
    multipleRegisters();
    exceptionAndMalformed();
    std::cout << "Production Modbus holding-register write client tests passed\n";
}
