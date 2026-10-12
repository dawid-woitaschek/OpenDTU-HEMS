// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/modbus/Host.h>
#include <cassert>
#include <iostream>

int main()
{
    using Hems::Modbus::validHost;
    using Hems::Modbus::validIpv4;

    assert(validHost("storage.local"));
    assert(validHost("warp-3.internal"));
    assert(validHost("10.0.1.20"));
    assert(validHost("10.0.1.255"));
    assert(validHost("host.123"));

    assert(!validHost(""));
    assert(!validHost("-storage.local"));
    assert(!validHost("storage-.local"));
    assert(!validHost("http://storage"));
    assert(!validHost("10.0.1.1107"));
    assert(!validHost("256.0.0.1"));
    assert(!validHost("10.0.1"));
    assert(!validHost("10.0.1."));
    assert(!validHost("10..1.20"));

    assert(validIpv4("0.0.0.0"));
    assert(validIpv4("255.255.255.255"));
    assert(!validIpv4("256.255.255.255"));
    assert(!validIpv4("1.2.3"));
    assert(!validIpv4("1.2.3.4.5"));

    std::cout << "HEMS Modbus host validation tests passed\n";
}
