// SPDX-License-Identifier: GPL-2.0-or-later
#include <RuntimeDiagnostics.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <cassert>
#include <iostream>
#include <string>
int main() {
    using namespace Diagnostics;
    init(); assert(strlen(remoteBootId()) == 32);
    std::string firstBoot = remoteBootId();
    Remote::Capsule capsule; assert(!previousCapsule(capsule));
    testPsram = false; assert(!remoteStart() && !remoteCursor().available && internalAttempts == 0);
    testPsram = true; assert(remoteStart());
    auto scope = begin(Component::RemoteDiagnostics, Operation::Upload);
    heapCheckpoint(HeapPoint::BeforeConnect);
    auto cursor = remoteCursor(); assert(cursor.available && cursor.pending >= 2);
    HeapSample snapshot; assert(remoteSample(cursor.newest, snapshot));
    assert(snapshot.context.component == Component::RemoteDiagnostics && snapshot.context.operation == Operation::Upload);
    assert(snapshot.freeInternal == 120000 && snapshot.freePsram == 120000 && snapshot.stackFree == 4096);
    auto allocations = externalAttempts;
    allocationFailureHook(123456, 42, "test_allocator");
    assert(externalAttempts == allocations); // Hook only touches the existing fixed INTERNAL failure journal.
    remoteCollect(); cursor = remoteCursor(); assert(cursor.pendingFailures >= 1);
    HeapFailure failure; assert(remoteFailure(cursor.failureNewest, failure));
    assert(failure.requested == 123456 && std::string(failure.function) == "test_allocator");
    heapCheckpoint(HeapPoint::Released); intentionalRestart(); finish(scope);
    mockReset = ESP_RST_SW; init();
    assert(firstBoot != remoteBootId() && previousCapsule(capsule) && Remote::valid(capsule));
    assert(capsule.boot == firstBoot && capsule.failureCount >= 1 && capsule.intentionalRestart);
    assert(capsule.sample.context.point == HeapPoint::Released && capsule.failure.requested == 123456);
    mockReset = ESP_RST_POWERON; init(); assert(!previousCapsule(capsule));
    auto oldest = remoteCursor().oldest;
    for (unsigned i = 0; i < Remote::SampleCapacity + 10; ++i) heapCheckpoint(HeapPoint::WorkerIdle);
    cursor = remoteCursor(); assert(cursor.pending == Remote::SampleCapacity && cursor.dropped > 0 && cursor.oldest > oldest);
    remoteAcknowledge(cursor.oldest + 2, cursor.failureNewest);
    assert(remoteCursor().pending == Remote::SampleCapacity - 3 && remoteCursor().pendingFailures == 0);
    mockReset = ESP_RST_SW; init(); assert(previousCapsule(capsule));
    auto pendingBeforeExpiry = remoteCursor().pending;
    auto ack = remoteCursor().acknowledged;
    testNow += Remote::RetentionMs;
    remoteExpire(testNow);
    auto expired = remoteCursor();
    assert(expired.pending == 0 && expired.expired == pendingBeforeExpiry && expired.acknowledged == ack);
    assert(previousCapsule(capsule) && Remote::valid(capsule)); // Retention never discards the previous RTC capsule.
    std::cout << "runtime remote spool/hook/crash capsule/boot identity passed\n";
}
