// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/Mode.h>
#include <hems/actuation/Capabilities.h>
#include <hems/actuation/ProfileSession.h>
#include <hems/optimizer/PlanView.h>
namespace Hems::Economic {
struct Context {
    Optimizer::PlanView plan;
    Actuation::ProfileInfo const* profile = nullptr;
    Model::StorageState storage;
    Constraints::StorageConstraints constraints;
    Actuation::MailboxSnapshot mailbox;
    Actuation::CommandResult result;
    Actuation::Origin owner = Actuation::Origin::None;
    Mode mode = Mode::Disabled;
    bool permission = false, ready = false, protectionOwned = false, cleanupReady = false;
    uint32_t now = 0, nowMs = 0, providerGeneration = 0, successfulPolls = 0;
};
struct Decision {
    bool submit = false, cleanup = false, replan = false, cancel = false;
    Actuation::Command command;
};
struct Preview {
    bool valid = false;
    Actuation::Command command;
    char const* reason = "plan_unavailable";
    uint32_t planRevision = 0;
};
// These same pure eligibility gates are used by the actual dispatcher.
char const* blocker(Context const& input);
struct Snapshot {
    char const* state = "disabled";
    uint32_t planRevision = 0, start = 0, end = 0, commandId = 0;
    uint32_t failures = 0;
    Actuation::Command desired, last;
    Actuation::MailboxState lastState = Actuation::MailboxState::Idle;
    Actuation::CommandResult result;
    bool confirmed = false, cleanupPending = false;
};
// Fixed-size decision state. It never performs I/O or invents profile operations.
class Dispatch {
public:
    Preview preview(Context const& input) const;
    Decision update(Context const& input);
    void accepted(Actuation::Command const& command, uint32_t id, uint32_t nowMs);
    Snapshot snapshot() const { return _state; }
private:
    Snapshot _state;
    uint32_t _generation = 0, _pendingId = 0, _lastAttemptMs = 0;
    uint32_t _failurePoll = 0, _failureRevision = 0, _failureSlot = 0;
    uint32_t _revokedId = 0;
    uint32_t _invalidSinceMs = 0;
    bool _waitingReplan = false;
};
bool mapCommand(Context const& input, Actuation::Command& command);
bool mapPowerCommand(Optimizer::PlanView const& plan, Constraints::StorageConstraints const& constraints,
    double soc, Actuation::OperationInfo const* power, bool stopSupported, Actuation::Command& command);
} // namespace Hems::Economic
