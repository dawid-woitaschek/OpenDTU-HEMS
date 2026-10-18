// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/Mode.h>
#include <hems/actuation/Capabilities.h>
#include <hems/optimizer/PlanView.h>
namespace Hems::Economic {
struct Context {
    Optimizer::PlanView plan;
    Actuation::ProfileInfo const* profile = nullptr;
    Model::StorageState storage;
    Constraints::StorageConstraints constraints;
    Actuation::MailboxSnapshot mailbox;
    Actuation::Origin owner = Actuation::Origin::None;
    Mode mode = Mode::Disabled;
    bool permission = false, ready = false, protectionOwned = false, cleanupReady = false;
    uint32_t now = 0, nowMs = 0, providerGeneration = 0, successfulPolls = 0;
};
struct Decision {
    bool submit = false, cleanup = false, replan = false, cancel = false;
    Actuation::Command command;
};
struct Snapshot {
    char const* state = "disabled";
    uint32_t planRevision = 0, start = 0, end = 0, commandId = 0;
    uint32_t failures = 0;
    Actuation::Command desired, last;
    bool confirmed = false;
};
// Fixed-size decision state. It never performs I/O or invents profile operations.
class Dispatch {
public:
    Decision update(Context const& input);
    void accepted(Actuation::Command const& command, uint32_t id, uint32_t nowMs);
    Snapshot snapshot() const { return _state; }
private:
    Snapshot _state;
    uint32_t _generation = 0, _pendingId = 0, _lastAttemptMs = 0;
    uint32_t _failurePoll = 0, _failureRevision = 0, _failureSlot = 0;
    uint32_t _revokedId = 0;
};
bool mapCommand(Context const& input, Actuation::Command& command);
} // namespace Hems::Economic
