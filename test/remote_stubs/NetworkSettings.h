// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct TestNetworkSettings { bool connected = true; bool isConnected() const { return connected; } };
inline TestNetworkSettings NetworkSettings;
