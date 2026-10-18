// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/Control.h>
#include <mutex>

namespace Hems::Actuation {

enum class Action : uint8_t {
    SetCurrentA,
    SetChargePowerW,
    SetDischargePowerW,
    SetTargetSocPct,
    Stop,
    Release,
};

struct Command {
    ControlTarget target = ControlTarget::Evse;
    Action action = Action::Stop;
    bool hasValue = false;
    double value = 0;
    bool automatic = false;
};

enum class MailboxState : uint8_t {
    Idle,
    Pending,
    Running,
    Succeeded,
    Failed,
    Cancelled,
};

struct Envelope {
    uint32_t id = 0;
    uint32_t generation = 0;
    Command command {};
};

struct MailboxSnapshot {
    MailboxState state = MailboxState::Idle;
    uint32_t id = 0;
    uint32_t generation = 0;
    Command command {};
};

bool validCommand(Command const& command);
char const* actionName(Action action);
char const* mailboxStateName(MailboxState state);

// Depth-one handoff. A pending/running command is never overwritten. The
// provider generation travels with the command so a reconfiguration cannot
// retarget already accepted work to a different endpoint.
class Mailbox {
public:
    bool submit(Command const& command, uint32_t& id, uint32_t generation = 0);
    bool take(Envelope& envelope);
    bool complete(uint32_t id, bool success);
    bool cancelPending(uint32_t id = 0);
    void reset();
    MailboxSnapshot snapshot() const;

private:
    mutable std::mutex _mutex;
    MailboxState _state = MailboxState::Idle;
    Command _command {};
    uint32_t _id = 0;
    uint32_t _generation = 0;
    uint32_t _nextId = 1;
};

} // namespace Hems::Actuation
