"""Catalog drift and profile validation use the same production parser as imports."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('catalog', ROOT / 'pio-scripts/create_profile_catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)
VALIDATOR = ROOT / 'work/profile-catalog-validator'
COMMIT = '1' * 40


class CatalogTests(unittest.TestCase):
    def generate(self, root=ROOT / 'device-profiles'):
        return catalog.generate(root, VALIDATOR, COMMIT)

    def test_metadata_counts_and_integrity(self):
        first = self.generate()
        self.assertEqual(first, self.generate())
        entries = json.loads(first)['profiles']
        files = list((ROOT / 'device-profiles').rglob('*.json'))
        self.assertEqual(len(entries), len(files))
        self.assertEqual({p['category'] for p in entries}, {'evse', 'storage'})
        for entry in entries:
            path = ROOT / 'device-profiles' / entry['path']
            raw = path.read_bytes()
            profile = json.loads(raw)
            self.assertEqual(entry['id'], profile['id'])
            self.assertEqual(entry['vendor'], profile['metadata']['vendor'])
            self.assertEqual(entry['model'], profile['metadata']['model'])
            self.assertEqual(entry['size'], len(raw))
            self.assertEqual(entry['sha256'], hashlib.sha256(raw).hexdigest())
            normalized = subprocess.check_output([str(VALIDATOR), str(path)])
            self.assertEqual(entry['installed_sha256'], hashlib.sha256(normalized).hexdigest())
            with tempfile.TemporaryDirectory() as tmp:
                normalized_path = Path(tmp) / 'normalized.json'
                normalized_path.write_bytes(normalized)
                self.assertEqual(normalized, subprocess.check_output([str(VALIDATOR), str(normalized_path)]))
            self.assertNotIn('telemetry', entry)

    def test_added_modified_deleted_and_malformed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'storage').mkdir()
            path = root / 'storage/new.json'
            profile = json.loads((ROOT / 'device-profiles/storage/Marstek-Venus-E-3.0.json').read_text())
            path.write_text(json.dumps(profile, separators=(',', ':')))
            original = self.generate(root)
            profile['metadata']['model'] = 'Modified'
            path.write_text(json.dumps(profile, separators=(',', ':')))
            self.assertNotEqual(original, self.generate(root))
            profile['telemetry'][0]['address'] = 9999999
            path.write_text(json.dumps(profile, separators=(',', ':')))
            with self.assertRaises(subprocess.CalledProcessError):
                self.generate(root)
            path.unlink()
            self.assertEqual(json.loads(self.generate(root))['profiles'], [])

    def test_duplicate_and_category(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'storage').mkdir()
            raw = (ROOT / 'device-profiles/storage/Marstek-Venus-E-3.0.json').read_bytes()
            (root / 'storage/a.json').write_bytes(raw)
            (root / 'storage/b.json').write_bytes(raw)
            with self.assertRaises(ValueError):
                self.generate(root)
            (root / 'storage/b.json').unlink()
            (root / 'evse').mkdir()
            (root / 'storage/a.json').rename(root / 'evse/a.json')
            with self.assertRaises(ValueError):
                self.generate(root)

    def test_drift_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'catalog.json'
            command = ['python3', str(ROOT / 'pio-scripts/create_profile_catalog.py'), '--validator', str(VALIDATOR), '--source-commit', COMMIT, '--output', str(output)]
            subprocess.run(command, check=True)
            subprocess.run(command + ['--check'], check=True)
            output.write_text('{}')
            self.assertNotEqual(subprocess.run(command + ['--check'], capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
