// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
struct ip_addr_t { uint32_t addr; };
#define IP_IS_V4(address) true
#define ip_2_ip4(address) (address)
#define ip4_addr_isany_val(address) ((address).addr == 0)
#define ip4_addr_get_u32(address) ((address)->addr)
