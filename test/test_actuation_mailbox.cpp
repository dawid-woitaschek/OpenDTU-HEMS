// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Mailbox.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace Hems;
using namespace Hems::Actuation;

int main()
{
    assert(validCommand({ ControlTarget::Evse, Action::SetCurrentA, true, 6 }));
    assert(validCommand({ ControlTarget::Evse, Action::Stop, false, 0 }));
    assert(validCommand({ ControlTarget::Evse, Action::Release, false, 0 }));
    assert(!validCommand({ ControlTarget::Evse, Action::SetChargePowerW, true, 500 }));
    assert(!validCommand({ ControlTarget::Evse, Action::SetCurrentA, false, 6 }));
    assert(!validCommand({ ControlTarget::Evse, Action::Stop, true, 0 }));
    assert(!validCommand({ ControlTarget::Evse, Action::SetCurrentA, true,
            std::numeric_limits<double>::infinity() }));

    assert(validCommand({ ControlTarget::Storage, Action::SetChargePowerW, true, 500 }));
    assert(validCommand({ ControlTarget::Storage, Action::SetDischargePowerW, true, 500 }));
    assert(validCommand({ ControlTarget::Storage, Action::SetTargetSocPct, true, 80 }));
    assert(validCommand({ ControlTarget::Storage, Action::Stop, false, 0 }));
    assert(validCommand({ ControlTarget::Storage, Action::Release, false, 0 }));
    assert(!validCommand({ ControlTarget::Storage, Action::SetCurrentA, true, 6 }));

    Mailbox mailbox;
    assert(mailbox.snapshot().state == MailboxState::Idle);

    uint32_t firstId = 0;
    Command first { ControlTarget::Storage, Action::SetChargePowerW, true, 500 };
    assert(mailbox.submit(first, firstId));
    assert(firstId != 0);
    auto pending = mailbox.snapshot();
    assert(pending.state == MailboxState::Pending && pending.id == firstId);
    assert(pending.command.target == ControlTarget::Storage);
    assert(pending.command.action == Action::SetChargePowerW);
    assert(pending.command.value == 500);

    uint32_t rejectedId = 123;
    assert(!mailbox.submit({ ControlTarget::Storage, Action::Stop, false, 0 }, rejectedId));
    assert(rejectedId == 0);

    Envelope envelope;
    assert(mailbox.take(envelope));
    assert(envelope.id == firstId && envelope.command.value == 500);
    assert(mailbox.snapshot().state == MailboxState::Running);
    assert(!mailbox.take(envelope));
    assert(!mailbox.cancelPending(firstId));
    assert(!mailbox.complete(firstId + 1, true));
    assert(mailbox.complete(firstId, true));
    assert(mailbox.snapshot().state == MailboxState::Succeeded);

    uint32_t secondId = 0;
    assert(mailbox.submit({ ControlTarget::Evse, Action::SetCurrentA, true, 6 }, secondId));
    assert(secondId != 0 && secondId != firstId);
    assert(mailbox.cancelPending(secondId));
    assert(mailbox.snapshot().state == MailboxState::Cancelled);

    uint32_t thirdId = 0;
    assert(mailbox.submit({ ControlTarget::Evse, Action::Stop, false, 0 }, thirdId));
    assert(mailbox.take(envelope));
    assert(mailbox.complete(thirdId, false));
    assert(mailbox.snapshot().state == MailboxState::Failed);

    mailbox.reset();
    auto reset = mailbox.snapshot();
    assert(reset.state == MailboxState::Idle && reset.id == 0);

    assert(!std::strcmp(actionName(Action::SetChargePowerW), "set_charge_power_w"));
    assert(!std::strcmp(actionName(Action::Release), "release"));
    assert(!std::strcmp(mailboxStateName(MailboxState::Pending), "pending"));
    assert(!std::strcmp(mailboxStateName(MailboxState::Failed), "failed"));

    std::cout << "Actuation mailbox tests passed\n";
}
