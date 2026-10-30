// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Remote.h>
#include <algorithm>
namespace Hems::Energy {
namespace {
void intervalJson(Domain domain, Interval const& value, JsonObject out) {
    out["start"] = value.start; out["end"] = value.end;
    if (domain == Domain::Tariff) {
        out["price_micros_per_kwh"] = value.value;
        out["import_price_micros_per_kwh"] = nullptr;
        if (value.hasImportPrice) { out["import_price_micros_per_kwh"] = value.importPrice; }
    } else {
        out["power_w"] = value.value;
        out["energy_wh"] = static_cast<double>(value.value) * (value.end - value.start) / 3600;
    }
}
} // namespace
void remoteJson(Domain domain, char const* provider, Series const& series, Status const& s,
    uint32_t utc, JsonObject out, size_t limit) {
    out["provider"] = provider; out["source"] = remoteSource(series.source);
    out["currency"] = domain == Domain::Tariff ? static_cast<char const*>(series.currency) : nullptr;
    out["enabled"] = s.enabled; out["has_data"] = s.hasData; out["fresh"] = s.fresh;
    out["current_covered"] = s.currentCovered; out["planning_usable"] = s.planningUsable;
    out["partial_horizon"] = s.partial; out["transport_fresh"] = s.transportFresh;
    out["source_fresh"] = nullptr; out["source_age_seconds"] = nullptr;
    out["source_timestamp_known"] = series.sourceTimestampKnown;
    if (series.sourceTimestampKnown && s.hasData) { out["source_fresh"] = s.sourceFresh; out["source_age_seconds"] = s.sourceAgeSeconds; }
    out["transport_age_seconds"] = s.transportAgeSeconds; out["fetched_at_utc"] = s.fetchedAt;
    out["generated_at_utc"] = s.sourceAt; out["state"] = s.state; out["last_error"] = s.error;
    out["current"] = nullptr;
    if (auto current = series.current(utc)) { intervalJson(domain, *current, out["current"].to<JsonObject>()); }
    auto rows = out["intervals"].to<JsonArray>(); size_t available = 0;
    for (size_t i = 0; i < series.count; ++i) {
        auto const& interval = series.intervals[i];
        if (interval.end <= utc) { continue; }
        ++available;
        if (rows.size() < std::min(limit, size_t(96))) { intervalJson(domain, interval, rows.add<JsonObject>()); }
    }
    out["available_interval_count"] = available; out["serialized_interval_count"] = rows.size();
    out["truncated"] = rows.size() < available;
}
} // namespace Hems::Energy
