// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Arduino.h>
#include <hems/warp/Settings.h>
#include <hems/Mode.h>
#define CONFIG_VERSION_HEMS 1
struct HemsConfig { unsigned Version = 1; bool Enabled = true; Hems::Mode Mode = Hems::Mode::Monitor; WarpConfig Warp; };
struct TestConfig { HemsConfig Hems; };
struct TestConfiguration { TestConfig data; TestConfig const& get() const { return data; } };
inline TestConfiguration Configuration;
