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
