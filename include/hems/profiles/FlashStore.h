// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/profiles/Store.h>
#include <mutex>

namespace Hems::Profiles {

// Shared LittleFS-backed profile store used by the Web API and runtime providers.
// Callers must hold runtimeStoreMutex() while touching runtimeStore/runtimeFiles.
Store& runtimeStore();
Files& runtimeFiles();
std::mutex& runtimeStoreMutex();

} // namespace Hems::Profiles
