// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct TestNetwork {
    bool connected = true;
    bool isConnected() const { return connected; }
};
inline TestNetwork NetworkSettings;
