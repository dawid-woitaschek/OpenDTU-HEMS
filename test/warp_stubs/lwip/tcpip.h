// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <lwip/dns.h>
inline err_t tcpip_callback(void (*callback)(void*), void* context) { callback(context); return ERR_OK; }
