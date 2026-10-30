// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <diagnostics/Breadcrumb.h>
#include <diagnostics/Heap.h>
#include <ArduinoJson.h>
#include <remote/Core.h>
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
struct RemoteCursor {
    uint32_t oldest = 0, newest = 0, acknowledged = 0, dropped = 0;
    uint32_t failureOldest = 0, failureNewest = 0, failureDropped = 0;
    size_t pending = 0, pendingFailures = 0;
    bool available = false;
};
// Safe task context only. The allocator callback continues to touch only HeapJournal.
bool remoteStart();
void remoteCollect();
RemoteCursor remoteCursor();
bool remoteSample(uint32_t sequence, HeapSample& out);
bool remoteFailure(uint32_t sequence, HeapFailure& out);
void remoteAcknowledge(uint32_t checkpoint, uint32_t failure);
char const* remoteBootId();
void bootHeapSample(HeapSample& out);
bool previousCapsule(Remote::Capsule& out);
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
