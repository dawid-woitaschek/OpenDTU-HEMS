#!/usr/bin/env python3
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / "firmware-channels.json"

with CONFIG.open("r", encoding="utf-8") as fp:
    config = json.load(fp)

assert config.get("schema") == "opendtu-hems-firmware-channels/v1"
repository = config.get("repository")
assert isinstance(repository, str) and re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository)

channels = config.get("channels")
assert isinstance(channels, dict) and channels
assert "integration/hems-next" in channels
assert "hems" in channels

seen_ids = set()
for branch, entry in channels.items():
    assert isinstance(branch, str) and branch and branch.strip() == branch
    assert isinstance(entry, dict)
    channel_id = entry.get("id")
    label = entry.get("label")
    assert isinstance(channel_id, str) and re.fullmatch(r"[A-Za-z0-9._-]+", channel_id)
    assert channel_id not in seen_ids
    seen_ids.add(channel_id)
    assert isinstance(label, str) and label

print("Firmware update channel configuration tests passed")
