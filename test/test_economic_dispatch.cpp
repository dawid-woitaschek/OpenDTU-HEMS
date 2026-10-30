// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/economic/Dispatch.h>
#include <cassert>
#include <iostream>
#include <cstring>
using namespace Hems;
using namespace Hems::Actuation;
struct Fixture {
    Economic::Dispatch dispatch;
    Economic::Context in;
    ProfileInfo profile;
    Fixture() {
        profile.control = true; profile.operationCount = 4;
        profile.operations[0] = { Action::SetChargePowerW, true, 0, 2500, 10, "W" };
        profile.operations[1] = { Action::SetDischargePowerW, true, 100, 2500, 10, "W" };
        profile.operations[2].action = Action::Stop; profile.operations[3].action = Action::Release;
        in.profile = &profile; in.plan.config.enabled = in.plan.config.dispatchEnabled = true;
        in.plan.config.maxChargeW = in.plan.config.maxDischargeW = 2000;
        in.plan.config.minSoc = 20; in.plan.config.maxSoc = 90;
        in.plan.valid = true; in.plan.start = in.now = 1792180800; in.plan.end = in.now + 900;
        in.plan.revision = in.providerGeneration = 1; in.plan.batteryW = 537;
        in.mode = Mode::Active; in.permission = in.ready = in.cleanupReady = true;
        in.storage.device.enabled = in.storage.device.reachable = in.storage.device.hasUpdate = true;
        in.storage.socPct = Model::Signal<double>::fresh(50);
        in.nowMs = 1000; in.successfulPolls = 2;
    }
    Economic::Decision tick() { return dispatch.update(in); }
    void confirmed() {
        auto d = tick(); assert(d.submit); dispatch.accepted(d.command, 7, in.nowMs);
        in.mailbox = { MailboxState::Succeeded, 7, 1, d.command }; in.owner = Origin::Economic;
        assert(!tick().submit);
    }
};
int main() {
    { Fixture f; unsigned char before[sizeof(f.dispatch)]; memcpy(before, &f.dispatch, sizeof(before));
      auto preview = f.dispatch.preview(f.in);
      for (unsigned i = 0; i < 1000; ++i) { assert(f.dispatch.preview(f.in).valid); }
      assert(!memcmp(before, &f.dispatch, sizeof(before)));
      auto actual = f.tick(); assert(actual.submit && preview.command.action == actual.command.action && preview.command.value == actual.command.value); }
    { Fixture f; auto decision = f.tick(); f.dispatch.accepted(decision.command, 123, f.in.nowMs);
      f.in.mailbox = {MailboxState::Failed, 123, 1, decision.command};
      assert(!strcmp(f.dispatch.preview(f.in).reason, "command_failed_replan_pending")); }
    { Fixture f; f.in.storage.socPct = Model::Signal<double>::stale(50); assert(!f.dispatch.preview(f.in).valid); }
    { Fixture f; f.in.plan.config.flow.enabled = true; assert(!strcmp(f.dispatch.preview(f.in).reason, "waiting_wear_cost")); }
    { Fixture f; f.in.plan.valid = false; assert(!f.dispatch.preview(f.in).valid); }
    { Fixture f; f.in.storage.faultActive = Model::Signal<bool>::fresh(true); assert(!f.dispatch.preview(f.in).valid); }
    { Fixture f; f.in.plan.batteryW = -537; f.in.constraints.dischargeAllowed = false; assert(!strcmp(f.dispatch.preview(f.in).reason, "blocked_protection")); }
    { Fixture f; f.profile.operationCount = 0; assert(!strcmp(f.dispatch.preview(f.in).reason, "unsupported_action")); }
    { Fixture f; f.in.storage.socPct.value = 90; auto p = f.dispatch.preview(f.in); assert(p.valid && p.command.action == Action::Stop && !strcmp(p.reason, "soc_upper_limit")); }

    { Fixture f; f.in.plan.config.flow.enabled=true; assert(!f.tick().submit);
      assert(!strcmp(f.dispatch.snapshot().state,"waiting_wear_cost"));
      f.in.plan.config.wearSource=1; assert(f.tick().submit);
      f.in.plan.config.wearSource=2; f.in.plan.config.degradationMicros=0; assert(f.tick().submit); }
    { Fixture f; f.in.plan.config.dispatchEnabled = false; assert(!f.tick().submit); }
    for (auto mode : { Mode::Disabled, Mode::Monitor }) { Fixture f; f.in.mode = mode; assert(!f.tick().submit); }
    { Fixture f; f.in.permission = false; assert(!f.tick().submit); }
    { Fixture f; auto d = f.tick(); assert(d.submit && d.command.target == ControlTarget::Storage && d.command.origin == Origin::Economic); assert(d.command.action == Action::SetChargePowerW && d.command.value == 530); }
    { Fixture f; f.in.plan.batteryW = -537; auto d = f.tick(); assert(d.submit && d.command.action == Action::SetDischargePowerW && d.command.value == 530); }
    { Fixture f; f.in.plan.batteryW = 0; auto d = f.tick(); assert(d.submit && d.command.action == Action::Stop && !d.command.hasValue); }
    { Fixture f; f.confirmed(); for (int i = 0; i < 1000; ++i) assert(!f.tick().submit); f.in.plan.revision++; assert(!f.tick().submit); f.in.plan.batteryW = 539; assert(!f.tick().submit); }
    { Fixture f; f.confirmed(); f.in.now = f.in.plan.end; assert(!f.tick().submit && f.tick().cleanup); f.in.plan.start = f.in.now; f.in.plan.end += 900; f.in.plan.revision++; f.in.plan.batteryW = -500; assert(f.tick().submit); }
    { Fixture f; f.in.plan.valid = false; assert(!f.tick().submit); }
    {
        Fixture f; f.confirmed(); f.in.plan.valid = false; f.in.plan.inputsUsable = true; f.in.plan.reason = "energy_revision";
        assert(!f.tick().cleanup); f.in.nowMs += 9000; assert(!f.tick().cleanup);
        f.in.plan.valid = true; ++f.in.plan.revision; assert(!f.tick().submit);
        f.in.plan.valid = false; assert(!f.tick().cleanup); f.in.nowMs += 10000; assert(f.tick().cleanup);
    }
    { Fixture f; f.in.now = f.in.plan.start - 1; assert(!f.tick().submit); }
    { Fixture f; f.in.plan.paused = true; assert(!f.tick().submit); f.in.owner = Origin::Manual; assert(!f.tick().cleanup); f.in.plan.paused = false; assert(f.tick().submit); }
    { Fixture f; f.in.constraints.dischargeAllowed = false; f.in.plan.batteryW = -500; assert(!f.tick().submit); f.in.plan.batteryW = 500; assert(f.tick().submit); }
    for (auto owner : { Origin::Protection, Origin::Manual }) { Fixture f; f.in.owner = owner; f.in.plan.valid = false; assert(!f.tick().cleanup); }
    { Fixture f; f.in.owner = Origin::Protection; f.in.protectionOwned = true; f.in.plan.batteryW = 0; assert(!f.tick().submit && !f.tick().cleanup); f.in.plan.batteryW = 500; assert(f.tick().submit); }
    { Fixture f; f.confirmed(); f.in.mode = Mode::Monitor; assert(!f.tick().submit && f.tick().cleanup); f.in.permission = false; assert(f.tick().cleanup); f.in.storage.device.reachable = f.in.cleanupReady = false; for (int i = 0; i < 1000; ++i) assert(!f.tick().submit && !f.tick().cleanup); }
    { Fixture f; auto d = f.tick(); f.dispatch.accepted(d.command, 7, f.in.nowMs); f.in.mailbox = { MailboxState::Running, 7, 1, d.command }; f.in.plan.valid = false; assert(f.tick().cancel); assert(!f.tick().cancel && !f.tick().cleanup); }
    { Fixture f; auto d = f.tick(); f.dispatch.accepted(d.command, 7, f.in.nowMs); f.in.mailbox = { MailboxState::Failed, 7, 1, d.command }; assert(f.tick().replan); assert(!f.tick().submit); f.in.plan.revision++; f.in.nowMs += 30000; assert(!f.tick().submit); f.in.successfulPolls++; assert(f.tick().submit); }
    { Fixture f; f.in.constraints.maxChargePower = { true, 525 }; assert(f.tick().command.value == 520); f.in.plan.batteryW = -50; auto d = f.tick(); assert(d.submit && d.command.action == Action::Stop); }
    { Fixture f; f.in.plan.batteryW = 0; f.profile.operationCount = 2; assert(!f.tick().submit); }
    { Fixture f; f.profile.operations[0].hasInput = false; assert(!f.tick().submit); }
    { Fixture f; f.in.storage.socPct.value = 90; assert(f.tick().command.action == Action::Stop); }
    { Fixture f; f.confirmed(); f.in.providerGeneration++; f.in.owner = Origin::None; assert(f.tick().submit); }
    {
        Fixture f;
        for (uint32_t attempt = 1; attempt <= 3; ++attempt) {
            auto d = f.tick(); assert(d.submit); f.dispatch.accepted(d.command, attempt, f.in.nowMs);
            f.in.mailbox = { MailboxState::Failed, attempt, 1, d.command }; assert(f.tick().replan);
            assert(!f.tick().submit); f.in.nowMs += 30000; ++f.in.successfulPolls; ++f.in.plan.revision;
        }
        assert(!f.tick().submit); f.in.now += 900; f.in.plan.start += 900; f.in.plan.end += 900;
        assert(f.tick().submit); // Slot boundary permits a bounded new attempt, no permanent latch.
    }
    std::cout << "Economic current-slot mapping, live gates, ownership, idempotence, pause, bounded retry and no-write storms passed; Dispatch=" << sizeof(Economic::Dispatch) << " bytes\n";
}
