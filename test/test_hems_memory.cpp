// SPDX-License-Identifier: GPL-2.0-or-later
// Compile the ESP32 placement path against a capability allocator seam.
#include <hems/Memory.h>
#include <hems/profiles/Profile.h>
#include <hems/optimizer/Planner.h>
#include <cassert>
#include <cstring>
#include <iostream>
struct Data { unsigned value = 42; };
int main()
{
    namespace Memory = Hems::Memory;
    namespace Profiles = Hems::Profiles;
    namespace Load = Hems::Load;
    namespace Optimizer = Hems::Optimizer;
    for (bool psram : {false, true}) {
        hasPsram = psram;
        auto expected = psram ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_8BIT;
        auto data = Memory::make<Data>(); assert(data && data->value == 42 && lastCaps == expected);
        auto before = attempts; memoryFailure = true;
        assert(!Memory::make<Data>() && attempts == before + 1); // No silent INTERNAL spill.
        memoryFailure = false;
        auto profile = Memory::make<Load::Profile>();
        assert(profile && profile->history.version == 1 && lastCaps == expected);
        auto plan = Memory::make<Optimizer::Plan>();
        assert(plan && plan->count == 0 && lastCaps == expected);
        before = attempts; memoryFailure = true;
        assert(!Memory::make<Load::Profile>() && attempts == before + 1);
        assert(!Memory::make<Optimizer::Plan>() && attempts == before + 2);
        assert(profile->history.version == 1 && plan->count == 0);
        memoryFailure = false;
        Profiles::BoundedAllocator allocator(200, true);
        auto p = allocator.allocate(100); assert(p && lastCaps == expected && allocator.used == 100);
        memset(p, 0x5a, 100);
        memoryFailure = true;
        assert(!allocator.reallocate(p, 150) && allocator.used == 100 && allocator.peak == 100);
        assert(static_cast<unsigned char*>(p)[99] == 0x5a);
        memoryFailure = false;
        p = allocator.reallocate(p, 150); assert(p && lastCaps == expected && allocator.used == 150);
        assert(static_cast<unsigned char*>(p)[99] == 0x5a);
        assert(!allocator.allocate(51) && !allocator.reallocate(p, 201));
        allocator.deallocate(p); assert(allocator.used == 0 && allocator.peak == 150);
    }
    // Run the actual planner allocation through the ESP32 capability path.
    // JSON validation uses ArduinoJson's ordinary small scratch allocations;
    // the planning allocations must stay in PSRAM and fail closed.
    hasPsram = true;
    Optimizer::Config c;
    c.enabled = c.socBoundsConfigured = true; c.capacityWh = 10000;
    c.maxChargeW = c.maxDischargeW = 100; c.chargePermille = c.dischargePermille = 1000;
    c.horizonSeconds = 900; c.terminalPriceConfigured = true;
    Hems::Energy::Series tariff;
    constexpr uint32_t now = 1792195200;
    tariff.count = 1; tariff.intervals[0] = {now, now + 900, -100000, -100000, true};
    Optimizer::Inputs in; in.now = now; in.tariff = &tariff;
    in.tariffStatus.fresh = in.tariffStatus.planningUsable = true;
    in.storage.device.enabled = in.storage.device.reachable = in.storage.device.hasUpdate = true;
    in.storage.socPct = Hems::Model::Signal<double>::fresh(50);
    Optimizer::Plan plan;
    auto before = attempts;
    assert(Optimizer::calculate(c, in, plan));
    assert(attempts == before + 2 && lastCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(lastSize == 38u * plan.states && plan.workspaceBytes <= 8192u + 38u * plan.states);
    assert(plan.finalWh == plan.initialWh + 25);
    std::cout << "Exact DP scratch allocation=" << lastSize << " B, total=" << plan.workspaceBytes << " B in PSRAM\n";
    before = attempts; memoryFailure = true;
    assert(!Optimizer::calculate(c, in, plan) && plan.count == 0 && !std::strcmp(plan.blocker, "insufficient_memory"));
    assert(attempts == before + 1 && lastCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    memoryFailure = false;
    before = attempts; failAttempt = attempts+2;
    assert(!Optimizer::calculate(c, in, plan) && !std::strcmp(plan.blocker, "insufficient_memory"));
    assert(attempts == before+2 && lastCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    failAttempt = 0;
    assert(Optimizer::calculate(c, in, plan) && plan.finalWh == plan.initialWh + 25);
    hasPsram = false; before = attempts;
    assert(!Optimizer::calculate(c, in, plan) && !std::strcmp(plan.blocker, "optimizer_requires_psram"));
    assert(attempts == before && plan.count == 0); // No large INTERNAL attempt.
    hasPsram = true;
    std::cout << "Explicit PSRAM placement, no-PSRAM fallback, no INTERNAL spill, bounded JSON realloc/failure accounting passed\n";
}
