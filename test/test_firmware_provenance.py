#!/usr/bin/env python3
"""Exercise the production manifest writer and compiled constant generator."""
import hashlib
import json
import os
import pathlib
import runpy
import sys
import tempfile
import types
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pio-scripts'))
from firmware_provenance import provenance
from create_firmware_manifest import main

SOURCE = '5efc343f6b89a131b4fc2164d120bfc2df0a2743'
MERGE = 'bfa0492' + '0' * 33

with tempfile.TemporaryDirectory() as tmp:
    dist = pathlib.Path(tmp)
    event_path = dist / 'event.json'
    event_path.write_text(json.dumps({'number': 25, 'pull_request': {'head': {
        'sha': SOURCE, 'repo': {'full_name': 'dawid-woitaschek/OpenDTU-HEMS'}}}}))
    artifact = dist / 'opendtu-hems-generic_esp32s3_usb.bin'
    artifact.write_bytes(b'deterministic OTA image')
    for pr in [True, False]:
        env = {'GITHUB_EVENT_NAME': 'pull_request' if pr else 'push', 'GITHUB_SHA': MERGE,
               'GITHUB_EVENT_PATH': str(event_path), 'GITHUB_HEAD_REF': 'feature/hems-economic-dispatch' if pr else '',
               'GITHUB_REF_NAME': '25/merge' if pr else 'hems', 'DIST': tmp,
               'CHANNEL_ID': 'pr-25' if pr else 'hems', 'SOURCE_BRANCH': 'feature/hems-economic-dispatch' if pr else 'hems',
               'UPDATE_REPOSITORY': 'dawid-woitaschek/OpenDTU-HEMS'}
        with patch.dict(os.environ, env, clear=True):
            main()
            manifest = json.loads((dist / 'manifest.json').read_text())
            expected = SOURCE if pr else MERGE
            assert manifest['commit'] == MERGE
            assert manifest['source_commit'] == expected
            assert manifest['tested_merge_commit'] == MERGE
            build = manifest['builds']['generic_esp32s3_usb']
            assert build['size'] == artifact.stat().st_size
            assert build['sha256'] == hashlib.sha256(artifact.read_bytes()).hexdigest()
            assert f'Source commit: `{expected}`' in (dist / 'README.md').read_text()
            assert f'Tested merge commit: `{MERGE}`' in (dist / 'README.md').read_text()

            class BuildEnv:
                def subst(self, key):
                    return str(ROOT) if key == '$PROJECT_DIR' else tmp
                def AppendUnique(self, **kwargs):
                    assert kwargs['PIOBUILDFILES'] == [str(dist / '__compiled_constants.c')]

            dulwich = types.ModuleType('dulwich')
            dulwich.porcelain = types.SimpleNamespace(describe=lambda _: 'g' + MERGE[:7])
            repo = types.ModuleType('dulwich.repo')
            repo.Repo = lambda _: None
            with patch.dict(sys.modules, {'dulwich': dulwich, 'dulwich.repo': repo}):
                runpy.run_path(str(ROOT / 'pio-scripts/auto_firmware_version.py'),
                               init_globals={'Import': lambda _: None, 'env': BuildEnv()})
            constants = (dist / '__compiled_constants.c').read_text()
            assert f'__COMPILED_GIT_SHA__ = "{MERGE}"' in constants
            assert f'__COMPILED_SOURCE_COMMIT__ = "{expected}"' in constants
            assert f'__COMPILED_TESTED_MERGE_COMMIT__ = "{MERGE}"' in constants
    assert provenance(SOURCE, {}) == {'commit': SOURCE, 'source_commit': SOURCE, 'tested_merge_commit': SOURCE}
    event_path.write_text(json.dumps({'pull_request': {'head': {'sha': SOURCE[:7]}}}))
    try:
        provenance(MERGE, {'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_EVENT_PATH': str(event_path)})
        raise AssertionError('truncated provenance accepted')
    except ValueError:
        pass
print('PR/non-PR manifest, digests and compiled provenance tests passed')
