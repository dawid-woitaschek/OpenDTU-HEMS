// SPDX-License-Identifier: GPL-2.0-or-later
// Existing provider/control suite explicitly exercises the legacy dispatch-off case.
// The economic integration suite links the real optimizer service instead.
#include <hems/optimizer/Service.h>
namespace Hems::Optimizer {
Service ShadowOptimizer;
PlanView Service::dispatchPlan() const { return {}; }
bool Service::pauseForManual() { return true; }
void Service::invalidate(char const*) {}
} // namespace Hems::Optimizer
