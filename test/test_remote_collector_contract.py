#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the serialized sender contract, optionally with the collector's Go parser.

The native sender test supplies cached hardware inputs but uses the production
payload builder and storage/WARP serializers. No network or collector checkout
is needed for the normal test-remote target. To check a current collector main:

  python3 test/test_remote_collector_contract.py --collector /path/to/collector \
      --go-bin /path/to/go

Only the collector's stdlib-only telemetry.go is copied into a temporary module;
its Parse implementation is unmodified. No collector database/server is started.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
# Contract paths, JSON types and extracted Go metadata/summary keys. These are
# deliberately the collector paths, not last_observations/internal-only fields.
FIELDS = {
    "heap.internal.free_bytes": (int, "Summary.internal_free"),
    "heap.internal.minimum_free_bytes": (int, "Summary.internal_minimum"),
    "heap.internal.largest_free_block": (int, "Summary.internal_largest"),
    "heap.psram.free_bytes": (int, "Summary.psram_free"),
    "heap.psram.minimum_free_bytes": (int, "Summary.psram_minimum"),
    "heap.psram.largest_free_block": (int, "Summary.psram_largest"),
    "diagnostics.allocation_failure_count": (int, "Summary.allocation_failures"),
    "storage.state_of_charge": (float, "Summary.storage_soc"),
    "wallbox.connected": (bool, "Summary.wallbox_connected"),
    "wallbox.charging": (bool, "Summary.wallbox_charging"),
    "system.reset_reason": (str, "ResetReason"),
    "boot_id": (str, "BootID"),
    "telemetry_sequence": (int, "TelemetrySequence"),
    "checkpoint_sequence": (int, "CheckpointSequence"),
    "schema_version": (int, "SchemaVersion"),
}


def at(root, path):
    for key in path.split("."):
        root = root[key]
    return root


def check_collector(collector, go_bin, body, payload, work):
    source = collector / "internal/telemetry/telemetry.go"
    package = work / "telemetry"
    package.mkdir()
    shutil.copyfile(source, package / "telemetry.go")
    # The isolated parser uses stdlib/Go 1.18 features; the collector's full
    # module and its newer runtime/SQLite dependencies are unnecessary here.
    (work / "go.mod").write_text("module sender-contract\n\ngo 1.18\n")
    (work / "main.go").write_text('''package main
import (
    "encoding/json"
    "os"
    "sender-contract/telemetry"
)
func main() {
    raw, err := os.ReadFile(os.Args[1]); if err != nil { panic(err) }
    metadata, err := telemetry.Parse(raw); if err != nil { panic(err) }
    if err := json.NewEncoder(os.Stdout).Encode(metadata); err != nil { panic(err) }
}
''')
    env = dict(os.environ, GOWORK="off", GOTOOLCHAIN="local", GOPROXY="off", GOSUMDB="off",
               GOCACHE=str(ROOT / "work/remote-contract-go-cache"),
               GOPATH=str(ROOT / "work/remote-contract-go-path"))
    result = subprocess.run([go_bin, "run", ".", str(body)], cwd=work, env=env,
                            capture_output=True, text=True, check=True)
    metadata = json.loads(result.stdout)
    for path, (_, key) in FIELDS.items():
        value = at(payload, path)
        expected = value if key == "SchemaVersion" else str(value).lower() if type(value) is bool else str(value)
        assert at(metadata, key) == expected, f"Collector extraction drift: {path} -> {key}"
    assert metadata["AcceptedCheckpointSequence"] == payload["checkpoint_sequence"]
    assert metadata["DedupeKey"], "Collector no longer recognizes sender identity"
    print(f"Current collector Parse extracted all {len(FIELDS)} contract paths ({source})")


def check_hems(hems):
    def typed(path, kind):
        value = at(hems, path)
        assert type(value) is kind, f"HEMS {path}: expected {kind.__name__}"
        return value
    assert typed("read_only", bool)
    typed("global_mode", str)
    for target in ("storage", "evse"):
        typed(f"control.{target}.mode", str)
        typed(f"control.{target}.permission", bool)
        for role in ("manual", "automatic"):
            typed(f"control.{target}.{role}.writes_possible", bool)
            typed(f"control.{target}.{role}.blockers", list)
    paths = ["grid.power_w", "pv.ac_power_w"]
    paths += ["storage." + name for name in ("soc_pct", "ac_power_w", "dc_power_w", "battery_voltage_v", "battery_current_a", "temperature_c", "charging", "discharging", "fault", "alarm")]
    paths += ["evse." + name for name in ("power_w", "vehicle_connected", "charging", "allowed_current_a")]
    for path in paths:
        signal = at(hems, "normalized." + path)
        assert signal["quality"] in ("fresh", "stale", "unknown")
        if signal["quality"] == "unknown":
            assert signal["value"] is None
        else:
            assert type(signal["value"]) in (int, float, bool)
    for domain in ("tariff", "forecast"):
        typed(domain + ".provider", str)
        typed(domain + ".source", str)
        for flag in ("fresh", "current_covered", "planning_usable", "truncated"):
            typed(domain + "." + flag, bool)
        total = typed(domain + ".available_interval_count", int)
        count = typed(domain + ".serialized_interval_count", int)
        rows = typed(domain + ".intervals", list)
        assert len(rows) == count == min(total, 96)
        for row in rows:
            assert type(row["start"]) is int and type(row["end"]) is int
            if domain == "tariff":
                assert type(row["price_micros_per_kwh"]) is int
                assert row["import_price_micros_per_kwh"] is None or type(row["import_price_micros_per_kwh"]) is int
            else:
                assert type(row["power_w"]) is int and type(row["energy_wh"]) in (int, float)
    for path in ("plan_revision", "energy_revision", "configuration_generation", "published_plan_revision", "calculation_us"):
        typed("optimizer." + path, int)
    for path in ("plan_valid", "replan_pending", "manual_paused", "schedule_truncated"):
        typed("optimizer." + path, bool)
    rows = typed("optimizer.schedule", list)
    assert len(rows) == typed("optimizer.schedule_serialized_steps", int) <= 96
    assert typed("optimizer.schedule_total_steps", int) >= len(rows)
    for row in rows:
        for field in ("start", "end", "battery_w", "start_wh", "end_wh", "market_price_micros_per_kwh", "used_price_micros_per_kwh", "grid_import_ws", "grid_export_ws", "cost_nano", "assumed_load_w", "load_coverage_pct"):
            assert type(row[field]) is int, field
    for field in ("global_mode", "state", "owner", "desired_action", "commanded_action", "command_result", "coordination_state"):
        typed("economic_dispatch." + field, str)
    typed("shadow_if_active.valid", bool)
    typed("shadow_if_active.reason", str)
    typed("shadow_if_active.plan_revision", int)
    assert typed("shadow_if_active.assumed_mode", str) == "Active"
    assert typed("shadow_if_active.assumed_storage_permission", bool)
    assert not typed("shadow_if_active.device_writes", bool)
    typed("constraints.physical.known", bool)
    typed("constraints.applicable.charge_allowed", bool)
    typed("constraints.ev_protection.proposed.discharge_allowed", bool)
    typed("flow.dpl_fresh", bool)
    typed("flow.topology_supported", bool)
    print("Rich HEMS signal/energy/plan/economics/preview/constraints field/type contract passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--collector", type=Path, help="Local checkout of current collector main")
    parser.add_argument("--go-bin", default="go", help="Go compiler (not GNU Go)")
    args = parser.parse_args()
    scratch = ROOT / "work"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="remote-contract-", dir=scratch) as temporary:
        work = Path(temporary)
        body = work / "sender.json"
        subprocess.run([str(ROOT / "test/test_remote_sender"), str(body)], check=True, capture_output=True)
        payload = json.loads(body.read_text())
        for path, (kind, _) in FIELDS.items():
            value = at(payload, path)
            # Exact type checks keep JSON booleans from passing as integers.
            assert type(value) is kind, f"{path}: expected {kind.__name__}, got {type(value).__name__}"
        assert payload["schema_version"] == 1 and payload["boot_id"]
        capture = payload["hems"]
        assert type(capture["captured_at_utc"]) is int and type(capture["captured_at_uptime_ms"]) is int
        assert 0 < len(capture["trigger"]) <= 5 and all(flag in ("periodic", "quarter_hour", "plan_revision", "manual", "diagnostic_event") for flag in capture["trigger"])
        rich_body = work / "hems.json"
        subprocess.run([str(ROOT / "test/test_remote_hems"), str(rich_body)], cwd=ROOT / "test", check=True, capture_output=True)
        check_hems(json.loads(rich_body.read_text()))
        assert at(payload, "diagnostics.allocation_failure_count") == 7
        assert at(payload, "diagnostics.runtime.heap.failed_alloc_count") == 7
        assert at(payload, "diagnostics.allocation_failures") == []
        assert at(payload, "storage.state_of_charge") == at(payload, "storage.last_observations.soc_pct") == 63.5
        assert at(payload, "wallbox.connected") == at(payload, "wallbox.reachable") is True
        assert at(payload, "wallbox.charging") == at(payload, "wallbox.last_observations.charging") is True
        assert at(payload, "wallbox.last_observations.charging_state") == 3
        assert at(payload, "system.reset_reason") == at(payload, "system.resetreason_0") == "Software reset CPU"
        print(f"Serialized sender payload: all {len(FIELDS)} collector paths and JSON types passed")
        if args.collector:
            check_collector(args.collector.resolve(), args.go_bin, body, payload, work)


if __name__ == "__main__":
    main()
