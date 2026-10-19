// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024-2026 Thomas Basler and others
 */
#include "RestartHelper.h"
#include <RuntimeDiagnostics.h>
#include "Display_Graphic.h"
#include "Led_Single.h"
#include <Esp.h>

RestartHelperClass RestartHelper;

RestartHelperClass::RestartHelperClass()
    : _rebootTask(1 * TASK_SECOND, TASK_FOREVER, std::bind(&RestartHelperClass::loop, this))
{
}

void RestartHelperClass::init(Scheduler& scheduler)
{
    scheduler.addTask(_rebootTask);
}

void RestartHelperClass::triggerRestart(Diagnostics::RestartSource source)
{
    _source = source; Diagnostics::restartRequested(source);
    _rebootTask.enable();
    _rebootTask.restart();
}

void RestartHelperClass::loop()
{
    if (_rebootTask.isFirstIteration()) {
        LedSingle.turnAllOff();
        Display.setStatus(false);
    } else {
        Diagnostics::restartRequested(_source);
        Diagnostics::intentionalRestart();
        ESP.restart();
    }
}
