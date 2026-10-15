// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/modbus/Client.h>
#include <Arduino.h>
#include <ESPmDNS.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>

namespace Hems::Modbus {

void Client::dnsFound(char const*, ip_addr_t const* address, void* context)
{
    auto& client = *static_cast<Client*>(context);
    if (address && IP_IS_V4(address) && !ip4_addr_isany_val(*ip_2_ip4(address))) {
        client._dnsAddress = ip4_addr_get_u32(ip_2_ip4(address));
        client._dnsState.store(DnsState::Ready);
    } else {
        client._dnsState.store(DnsState::Failed);
    }
}

void Client::startDns(void* context)
{
    auto& client = *static_cast<Client*>(context);
    ip_addr_t address;
    // Runs in lwIP's thread; avoids Arduino's blocking/shared DNS event bits.
    auto result = dns_gethostbyname_addrtype(client._dnsHost, &address, dnsFound,
            context, LWIP_DNS_ADDRTYPE_IPV4);
    if (result == ERR_OK) { dnsFound(nullptr, &address, context); }
    else if (result != ERR_INPROGRESS) { dnsFound(nullptr, nullptr, context); }
}

Result Client::resolve(char const* host, uint32_t& address)
{
    in_addr literal;
    if (inet_pton(AF_INET, host, &literal) == 1) {
        address = literal.s_addr;
        return {};
    }
    size_t hostLength = strlen(host);
    if (hostLength > 6 && strcmp(host + hostLength - 6, ".local") == 0) {
        char name[254];
        strlcpy(name, host, sizeof(name));
        name[hostLength - 6] = '\0';
        auto resolved = MDNS.queryHost(name, DnsTimeoutMs);
        address = static_cast<uint32_t>(resolved);
        return address ? Result {} : Result { Error::DnsFailure };
    }
    auto state = _dnsState.load();
    // A timed-out lookup retains its stable context until lwIP completes it.
    // Never overwrite a pending hostname or accumulate callbacks/allocations.
    if (state == DnsState::Pending) { return { Error::DnsTimeout }; }
    bool sameHost = strcmp(host, _dnsHost) == 0;
    if (state == DnsState::Ready && sameHost) {
        address = _dnsAddress;
        _dnsState.store(DnsState::Idle);
        return {};
    }
    strlcpy(_dnsHost, host, sizeof(_dnsHost));
    _dnsState.store(DnsState::Pending);
    if (tcpip_callback(startDns, this) != ERR_OK) {
        _dnsState.store(DnsState::Idle);
        return { Error::DnsFailure };
    }
    uint32_t started = millis();
    while (_dnsState.load() == DnsState::Pending && millis() - started < DnsTimeoutMs) { delay(10); }
    state = _dnsState.load();
    if (state == DnsState::Pending) { return { Error::DnsTimeout }; }
    _dnsState.store(DnsState::Idle);
    if (state != DnsState::Ready) { return { Error::DnsFailure }; }
    address = _dnsAddress;
    return {};
}

bool Client::waitSocket(bool writing, uint32_t started)
{
    uint32_t elapsed = millis() - started;
    if (elapsed >= RequestTimeoutMs) { return false; }
    uint32_t remaining = RequestTimeoutMs - elapsed;
    timeval timeout {};
    timeout.tv_sec = remaining / 1000;
    timeout.tv_usec = (remaining % 1000) * 1000;
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(_socket, &descriptors);
    return select(_socket + 1, writing ? nullptr : &descriptors,
            writing ? &descriptors : nullptr, nullptr, &timeout) > 0;
}

Result Client::open(char const* host, uint16_t port, uint8_t unit)
{
    close();
    _unit = unit;
    if (!host || strnlen(host, sizeof(_dnsHost)) >= sizeof(_dnsHost) || !*host || port == 0) {
        return { Error::Protocol };
    }
    uint32_t address;
    auto result = resolve(host, address);
    if (!result) { return result; }
    _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_socket < 0 || fcntl(_socket, F_SETFL, O_NONBLOCK) < 0) {
        close();
        return { Error::Connection };
    }
    sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = address;
    uint32_t started = millis();
    if (connect(_socket, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) == 0) { return {}; }
    if (errno != EINPROGRESS) { close(); return { Error::Connection }; }
    if (!waitSocket(true, started)) { close(); return { Error::ConnectTimeout }; }
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error != 0) {
        close();
        return { Error::Connection };
    }
    return {};
}

void Client::close()
{
    if (_socket >= 0) {
        ::close(_socket);
        _socket = -1;
    }
}

bool Client::transfer(uint8_t* data, size_t size, bool sending, uint32_t started)
{
    size_t offset = 0;
    while (offset < size) {
        if (!waitSocket(sending, started)) { return false; }
        int transferred = sending ? send(_socket, data + offset, size - offset, 0)
            : recv(_socket, data + offset, size - offset, 0);
        if (transferred < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) { continue; }
        if (transferred <= 0) { return false; }
        offset += transferred;
    }
    return true;
}

Result Client::read(ReadFunction function, uint16_t address, uint16_t count, uint8_t* payload)
{
    uint8_t frame[MaxFrame] {};
    uint16_t transaction = ++_transaction;
    if (_socket < 0 || !makeReadRequest(frame, transaction, _unit, function, address, count)) {
        return { Error::Protocol };
    }
    uint32_t started = millis();
    if (!transfer(frame, 12, true, started) || !transfer(frame, 7, false, started)) {
        return { Error::Timeout };
    }
    size_t length = (static_cast<size_t>(frame[4]) << 8) | frame[5];
    if (length < 3 || length + 6 > sizeof(frame)) { return { Error::Protocol, 0, true }; }
    if (!transfer(frame + 7, length - 1, false, started)) { return { Error::Timeout }; }
    return parseReadResponse(frame, length + 6, transaction, _unit, function, count, payload);
}

Result Client::readInputRegisters(uint16_t address, uint16_t count, uint8_t* payload)
{
    return read(ReadFunction::InputRegisters, address, count, payload);
}

Result Client::readHoldingRegisters(uint16_t address, uint16_t count, uint8_t* payload)
{
    return read(ReadFunction::HoldingRegisters, address, count, payload);
}

Result Client::readDiscreteInputs(uint16_t address, uint16_t count, uint8_t* payload)
{
    return read(ReadFunction::DiscreteInputs, address, count, payload);
}

Result Client::writeHoldingRegisters(uint16_t address, uint16_t count, uint16_t const* values)
{
    uint8_t frame[MaxFrame] {};
    size_t requestSize = 0;
    uint16_t transaction = ++_transaction;
    if (_socket < 0 || !makeWriteHoldingRequest(frame, requestSize, transaction,
            _unit, address, values, count)) {
        return { Error::Protocol };
    }

    uint32_t started = millis();
    if (!transfer(frame, requestSize, true, started) || !transfer(frame, 7, false, started)) {
        return { Error::Timeout };
    }
    size_t length = (static_cast<size_t>(frame[4]) << 8) | frame[5];
    if (length < 3 || length + 6 > sizeof(frame)) { return { Error::Protocol, 0, true }; }
    if (!transfer(frame + 7, length - 1, false, started)) { return { Error::Timeout }; }
    return parseWriteHoldingResponse(frame, length + 6, transaction, _unit, address, values, count);
}

} // namespace Hems::Modbus
