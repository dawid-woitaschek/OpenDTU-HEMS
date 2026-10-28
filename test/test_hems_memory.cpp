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
    std::cout << "Explicit PSRAM placement, no-PSRAM fallback, no INTERNAL spill, bounded JSON realloc/failure accounting passed\n";
}
