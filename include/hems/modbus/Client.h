// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/modbus/Protocol.h>
#include <atomic>
#include <lwip/ip_addr.h>

namespace Hems::Modbus {

// One owner/worker per client; no calls from scheduler or HTTP handlers.
// Boot lifetime: DNS callbacks may finish after the caller's deadline.
class Client {
public:
    Client() = default;
    Client(Client const&) = delete;
    Client& operator=(Client const&) = delete;
    Result open(char const* host, uint16_t port, uint8_t unit = 1);
    void close();
    Result readInputRegisters(uint16_t address, uint16_t count, uint8_t* payload);
    Result readHoldingRegisters(uint16_t address, uint16_t count, uint8_t* payload);
    Result readDiscreteInputs(uint16_t address, uint16_t count, uint8_t* payload);
    static constexpr uint32_t DnsTimeoutMs = 1000;
    static constexpr uint32_t RequestTimeoutMs = 1500;

private:
    enum class DnsState : uint8_t { Idle, Pending, Ready, Failed };
    Result resolve(char const* host, uint32_t& address);
    static void startDns(void* context);
    static void dnsFound(char const* name, ip_addr_t const* address, void* context);
    Result read(ReadFunction function, uint16_t address, uint16_t count, uint8_t* payload);
    bool transfer(uint8_t* data, size_t size, bool sending, uint32_t started);
    bool waitSocket(bool writing, uint32_t started);

    int _socket = -1;
    uint16_t _transaction = 0;
    uint8_t _unit = 1;
    std::atomic<DnsState> _dnsState { DnsState::Idle };
    char _dnsHost[254] {};
    uint32_t _dnsAddress = 0;
};

} // namespace Hems::Modbus
