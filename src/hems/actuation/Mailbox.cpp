// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Mailbox.h>
#include <cmath>

namespace Hems::Actuation {

bool validCommand(Command const& command)
{
    bool needsValue = false;
    switch (command.target) {
        case ControlTarget::Evse:
            switch (command.action) {
                case Action::SetCurrentA: needsValue = true; break;
                case Action::Stop:
                case Action::Release: break;
                default: return false;
            }
            break;
        case ControlTarget::Storage:
            switch (command.action) {
                case Action::SetChargePowerW:
                case Action::SetDischargePowerW:
                case Action::SetTargetSocPct: needsValue = true; break;
                case Action::Stop:
                case Action::Release: break;
                default: return false;
            }
            break;
        default:
            return false;
    }

    if (needsValue != command.hasValue) { return false; }
    return !needsValue || std::isfinite(command.value);
}

char const* actionName(Action action)
{
    switch (action) {
        case Action::SetCurrentA: return "set_current_a";
        case Action::SetChargePowerW: return "set_charge_power_w";
        case Action::SetDischargePowerW: return "set_discharge_power_w";
        case Action::SetTargetSocPct: return "set_target_soc_pct";
        case Action::Stop: return "stop";
        case Action::Release: return "release";
    }
    return "unknown";
}

char const* mailboxStateName(MailboxState state)
{
    switch (state) {
        case MailboxState::Idle: return "idle";
        case MailboxState::Pending: return "pending";
        case MailboxState::Running: return "running";
        case MailboxState::Succeeded: return "succeeded";
        case MailboxState::Failed: return "failed";
        case MailboxState::Cancelled: return "cancelled";
    }
    return "unknown";
}

bool Mailbox::submit(Command const& command, uint32_t& id)
{
    id = 0;
    if (!validCommand(command)) { return false; }
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state == MailboxState::Pending || _state == MailboxState::Running) { return false; }

    _command = command;
    _id = _nextId++;
    if (_id == 0) {
        _id = _nextId++;
    }
    if (_nextId == 0) { _nextId = 1; }
    _state = MailboxState::Pending;
    id = _id;
    return true;
}

bool Mailbox::take(Envelope& envelope)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state != MailboxState::Pending) { return false; }
    _state = MailboxState::Running;
    envelope.id = _id;
    envelope.command = _command;
    return true;
}

bool Mailbox::complete(uint32_t id, bool success)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (id == 0 || id != _id || _state != MailboxState::Running) { return false; }
    _state = success ? MailboxState::Succeeded : MailboxState::Failed;
    return true;
}

bool Mailbox::cancelPending(uint32_t id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state != MailboxState::Pending || (id != 0 && id != _id)) { return false; }
    _state = MailboxState::Cancelled;
    return true;
}

void Mailbox::reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _state = MailboxState::Idle;
    _command = {};
    _id = 0;
}

MailboxSnapshot Mailbox::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return { _state, _id, _command };
}

} // namespace Hems::Actuation
