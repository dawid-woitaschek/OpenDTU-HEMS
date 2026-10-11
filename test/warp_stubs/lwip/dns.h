// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <lwip/ip_addr.h>
using err_t = int;
constexpr int ERR_OK = 0;
constexpr int ERR_INPROGRESS = -5;
constexpr int LWIP_DNS_ADDRTYPE_IPV4 = 0;
using dns_found_callback = void (*)(char const*, ip_addr_t const*, void*);
err_t dns_gethostbyname_addrtype(char const*, ip_addr_t*, dns_found_callback, void*, uint8_t);
