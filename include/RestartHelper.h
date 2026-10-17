// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <TaskSchedulerDeclarations.h>
#include <diagnostics/Breadcrumb.h>

class RestartHelperClass {
public:
    RestartHelperClass();
    void init(Scheduler& scheduler);
    void triggerRestart(Diagnostics::RestartSource source = Diagnostics::RestartSource::Unspecified);

private:
    void loop();

    Diagnostics::RestartSource _source = Diagnostics::RestartSource::Unspecified;
    Task _rebootTask;
};

extern RestartHelperClass RestartHelper;
