// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
struct TestMDNS {
    uint32_t queryHost(char const*, unsigned) { return 0; }
};
inline TestMDNS MDNS;
