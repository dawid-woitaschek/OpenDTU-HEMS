#!/usr/bin/env python3
"""Generate a small static catalog; validate every file with the firmware parser.

No wall clock or mutable HEAD metadata: output is reproducible for a source commit.
The published catalog pins selected profile fetches to that immutable commit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def generate(root, validator, commit):
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('source commit must be a full SHA')
    entries, ids = [], set()
    for path in sorted(root.rglob('*.json')):
        if path.name == 'catalog.json' and path.parent == root:
            continue
        relative = path.relative_to(root).as_posix()
        if not re.fullmatch(r'[a-z][a-z0-9_-]*/[A-Za-z0-9][A-Za-z0-9._-]*\.json', relative):
            raise ValueError(f'unsupported profile path: {relative}')
        raw = path.read_bytes()
        if not 0 < len(raw) <= 6144:
            raise ValueError(f'profile size: {relative}')
        normalized = subprocess.run([str(validator), str(path)], check=True, capture_output=True).stdout
        profile = json.loads(normalized)
        category = relative.split('/')[0]
        if category not in profile.get('capabilities', []):
            raise ValueError(f'category/capability mismatch: {relative}')
        if profile['id'] in ids:
            raise ValueError(f'duplicate profile id: {profile["id"]}')
        ids.add(profile['id'])
        metadata = profile['metadata']
        entries.append(dict(id=profile['id'], category=category, vendor=metadata['vendor'],
                            model=metadata['model'], schema=profile['schema'],
                            source_revision=metadata['source_revision'], path=relative,
                            size=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                            installed_sha256=hashlib.sha256(normalized).hexdigest()))
    if len(entries) > 256:
        raise ValueError('catalog entry limit exceeded')
    return json.dumps(dict(version=1, source_commit=commit, profiles=entries), indent=2) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT / 'device-profiles')
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--source-commit', default=None)
    parser.add_argument('--output', type=Path, default=ROOT / 'work/profile-catalog/catalog.json')
    parser.add_argument('--check', action='store_true', help='reject output drift')
    args = parser.parse_args()
    commit = args.source_commit or subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    content = generate(args.root.resolve(), args.validator.resolve(), commit)
    if args.check:
        if not args.output.exists() or args.output.read_text() != content:
            raise SystemExit('profile catalog differs from repository contents')
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content)


if __name__ == '__main__':
    main()
