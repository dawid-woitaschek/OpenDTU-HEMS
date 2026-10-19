// SPDX-License-Identifier: GPL-2.0-or-later
#include <RestartHelper.h>
#include <Display_Graphic.h>
#include <Led_Single.h>
#include <Esp.h>
#include <diagnostics/Serialize.h>
#include <cassert>
#include <iostream>
using namespace Diagnostics;
namespace {
Storage retained {};
Journal journal;
uint32_t uptime = 0;
struct SimulatedReset {};
}
DisplayStub Display;
LedStub LedSingle;
EspStub ESP;
namespace Diagnostics {
void restartRequested(RestartSource source) { journal.restartRequested(source, uptime); }
void intentionalRestart() { journal.intentionalRestart(uptime); }
}
void EspStub::restart() { journal.espRestart(); throw SimulatedReset {}; }
int main()
{
    journal.boot(retained, false);
    journal.hookRegistered(true);
    Scheduler scheduler;
    RestartHelper.init(scheduler);
    assert(scheduler.scheduled && scheduler.scheduled->interval == 1000 && scheduler.scheduled->iterations == TASK_FOREVER);
    uptime = 10000;
    RestartHelper.triggerRestart(RestartSource::InverterUnresponsive);
    assert(journal.current.restartRequested && !journal.current.intentionalRestart);
    uptime += 1000; scheduler.scheduled->tick();
    assert(LedSingle.off && !Display.enabled && !journal.current.intentionalRestart);
    uptime += 1000;
    bool reset = false;
    try { scheduler.scheduled->tick(); } catch (SimulatedReset const&) { reset = true; }
    assert(reset);
    Journal nextBoot; nextBoot.boot(retained, true);
    assert(nextBoot.previousValid && nextBoot.previous.intentionalRestart && nextBoot.previous.espRestartCalled);
    assert(nextBoot.previous.restartSource == RestartSource::InverterUnresponsive && nextBoot.previous.restartAt == 12000);
    JsonDocument json; recordJson(nextBoot.previous, true, json.to<JsonObject>());
    assert(json["restart_source"] == "inverter_unresponsive" && json["intentional_restart"] == true);
    std::cout << "Production RestartHelper preserves two-tick timing and explicit unattended inverter restart source across simulated boot\n";
}
