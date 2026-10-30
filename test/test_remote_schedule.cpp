// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Core.h>
#include <remote/Schedule.h>
#include <cassert>
#include <iostream>
int main() {
    using namespace Remote;
    Settings s; JsonDocument config;
    settingsJson(s, config.to<JsonObject>(), true); config.remove("quarter_hour");
    s.quarterHour = true;
    assert(parseSettings(config.as<JsonVariantConst>(), s, true) && !s.quarterHour); // Legacy v1.
    config["quarter_hour"] = true; assert(parseSettings(config.as<JsonVariantConst>(), s, true) && s.quarterHour);
    JsonDocument persisted; settingsJson(s, persisted.to<JsonObject>(), true);
    Settings loaded; assert(parseSettings(persisted.as<JsonVariantConst>(), loaded, true) && loaded.quarterHour);
    config["quarter_hour"] = "yes"; assert(!parseSettings(config.as<JsonVariantConst>(), s));
    Schedule clock; constexpr uint32_t utc = 1792180800;
    clock.observe(0, utc, 0, false, false); assert(clock.pending == Periodic);
    clock.captured(clock.pending, 0, 5); assert(!clock.pending && clock.periodicMs == 300000);
    clock.observe(299999, utc + 299, 0, false, false); assert(!clock.pending);
    clock.observe(300000, utc + 300, 0, false, false); assert(clock.pending == Periodic);
    clock.captured(clock.pending, 300000, 5);
    clock.observe(900000, utc + 900, 0, true, false); assert(clock.pending == (Periodic | QuarterHour));
    clock.captured(clock.pending, 900000, 60);
    clock.observe(900001, utc + 900, 0, true, false); assert(!clock.pending);
    clock.observe(900002, utc + 900, 7, true, false); assert(clock.pending == PlanRevision);
    clock.captured(clock.pending, 900002, 60);
    for (unsigned i = 8; i <= 100; ++i) clock.observe(900003, utc + 900, i, true, true);
    assert(clock.pending == PlanRevision && clock.revision == 100 && clock.coalesced >= 92);
    clock.observe(1800000, utc + 1800, 100, true, true); assert(clock.pending == (PlanRevision | QuarterHour));
    Schedule disabledQuarter; disabledQuarter.periodicMs = 3600000; disabledQuarter.slot = utc / 900;
    disabledQuarter.observe(900000, utc + 900, 0, false, false); assert(!disabledQuarter.pending);
    Transmission flight; flight.stage(24, 2, UINT32_MAX - 100);
    assert(!flight.expired(100)); assert(flight.expired(uint32_t(UINT32_MAX - 100 + RetentionMs)));
    flight.abandon(); assert(!flight.pending && flight.sequence == 2);
    flight.stage(31, 4, 99); JsonDocument ack; ack["accepted"] = true; ack["accepted_checkpoint_sequence"] = 24;
    assert(!flight.accepts(ack.as<JsonVariantConst>())); // Lost old ACK cannot acknowledge the replacement.
    ack["accepted_checkpoint_sequence"] = 31; assert(flight.accepts(ack.as<JsonVariantConst>()));
    flight.complete(); assert(flight.sequence == 3);
    std::cout << "remote legacy settings/periodic/UTC slots/coalescing/revision/expiry identity passed\n";
}
