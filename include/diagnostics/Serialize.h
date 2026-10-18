// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <diagnostics/Breadcrumb.h>
#include <ArduinoJson.h>
namespace Diagnostics {
inline void activityJson(Activity const* a, JsonObject out)
{
    out["active"] = a && a->active;
    out["component"] = a ? name(a->component) : "none";
    out["operation"] = a ? name(a->operation) : "none";
    out["phase"] = a ? name(a->phase) : "none";
    out["sub_index"] = a ? a->subIndex : -1;
    out["uptime_ms"] = a ? a->uptimeMs : 0;
    out["sequence"] = a ? a->sequence : 0;
}
inline void recordJson(Record const& r, bool valid, JsonObject out)
{
    out["valid"] = valid;
    if (!valid) { return; }
    out["version"] = r.version; out["generation"] = r.generation; out["sequence"] = r.sequence;
    out["restart_requested"] = static_cast<bool>(r.restartRequested); out["intentional_restart"] = static_cast<bool>(r.intentionalRestart);
    out["shutdown_hook_registered"] = static_cast<bool>(r.shutdownRegistered);
    out["restart_source"] = name(r.restartSource); out["esp_restart_called"] = static_cast<bool>(r.espRestartCalled);
    out["restart_requested_at_ms"] = r.restartRequestedAt; out["restart_at_ms"] = r.restartAt;
    activityJson(selected(r), out);
    auto active = out["active_operations"].to<JsonArray>();
    for (auto const& a : r.activities) { if (a.active) { activityJson(&a, active.add<JsonObject>()); } }
}
} // namespace Diagnostics
