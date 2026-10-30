// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Hems::Energy {
inline struct {
    bool (*due)(void*) = nullptr; void (*run)(void*) = nullptr; void* context = nullptr;
    void diagnosticsBackground(bool (*d)(void*), void (*r)(void*), void* c) { due = d; run = r; context = c; }
} EnergyData;
}
