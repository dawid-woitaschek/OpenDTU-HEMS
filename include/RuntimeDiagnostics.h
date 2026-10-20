// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <diagnostics/Breadcrumb.h>
#include <diagnostics/Heap.h>
#include <ArduinoJson.h>
namespace Diagnostics {
#if defined(ESP32)
void init();
uint32_t begin(Component c, Operation op, Phase phase = Phase::Start, int8_t index = -1);
void phase(uint32_t token, Phase phase, int8_t index = -1);
void finish(uint32_t token);
void workerPhase(Phase phase, int8_t index = -1);
void restartRequested(RestartSource source);
void intentionalRestart();
void heapCheckpoint(HeapPoint point, HeapProvider provider = HeapProvider::None, int8_t plane = -1);
void serialize(JsonObject out, bool heapHistory = false);
#else
inline void init() {}
inline uint32_t begin(Component, Operation, Phase = Phase::Start, int8_t = -1) { return 0; }
inline void phase(uint32_t, Phase, int8_t = -1) {}
inline void finish(uint32_t) {}
inline void workerPhase(Phase, int8_t = -1) {}
inline void restartRequested(RestartSource) {}
inline void intentionalRestart() {}
inline void heapCheckpoint(HeapPoint, HeapProvider = HeapProvider::None, int8_t = -1) {}
#endif
class Scope {
public:
    Scope(Component c, Operation op, Phase phase = Phase::Start, int8_t index = -1) : token(begin(c, op, phase, index)) {}
    ~Scope() { finish(token); }
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
    uint32_t const token;
};
} // namespace Diagnostics
