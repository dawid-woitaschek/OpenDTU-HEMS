import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
let fetcher;
function compile(source, imports = {}) {
    const module = { exports: {} };
    vm.runInNewContext(ts.transpileModule(source, {
        compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
    }).outputText, { module, exports: module.exports, require: name => imports[name] || {},
        fetch: (...args) => fetcher(...args) });
    return module.exports;
}
const p = compile(readFileSync(new URL('../webapp/src/utils/firmwareProvenance.ts', import.meta.url), 'utf8'));
const source = '5efc343f6b89a131b4fc2164d120bfc2df0a2743', merge = 'bfa0492' + '0'.repeat(33), advanced = 'a'.repeat(40);
const manifest = { schema: 'opendtu-hems-update/v1', repository: 'owner/repo', channel: 'pr-25', branch: 'feature/hems-economic-dispatch',
    commit: merge, source_commit: source, tested_merge_commit: merge, published_at: '2026-10-18T12:00:00Z',
    builds: { generic_esp32s3_usb: { file: 'opendtu-hems-generic_esp32s3_usb.bin', size: 100, md5: 'a'.repeat(32), sha256: 'b'.repeat(64) } } };
const installed = { git_sha: merge, source_commit: source, tested_merge_commit: merge, git_branch: manifest.branch,
    pioenv: 'generic_esp32s3_usb', update_repository: manifest.repository, update_channel: manifest.channel };
const legacy = { ...manifest }; delete legacy.source_commit; delete legacy.tested_merge_commit;
const old = { ...installed }; delete old.source_commit; delete old.tested_merge_commit;
assert.ok(p.validManifest(manifest, manifest.repository, manifest.channel));
assert.ok(p.validManifest(legacy, manifest.repository, manifest.channel));
for (const field of ['commit', 'source_commit', 'tested_merge_commit']) {
    assert.equal(p.validManifest({ ...manifest, [field]: source.slice(0, 7) }, manifest.repository, manifest.channel), false);
}
assert.ok(p.exactImageInstalled(installed, manifest));
assert.ok(p.exactImageInstalled(old, legacy));
assert.ok(p.exactImageInstalled({ ...old, git_sha: merge.toUpperCase() }, manifest));
assert.equal(p.sourceComparisonBase(installed, manifest), source);
assert.equal(p.sourceComparisonBase(old, manifest), source); // recover a legacy embedded merge SHA
assert.equal(p.sourceComparisonBase(old, legacy), merge); // old manifests remain usable
assert.equal(p.sourceComparisonBase({ ...old, git_sha: merge.slice(0, 7) }, manifest), undefined);
for (const changed of [{ commit: advanced, source_commit: advanced }, { channel: 'other' }, { repository: 'other/repo' }, { branch: 'other' }, { builds: {} }]) {
    assert.equal(p.exactImageInstalled(installed, { ...manifest, ...changed }), false);
    assert.equal(p.sourceComparisonBase(old, { ...manifest, ...changed }), merge);
}
assert.equal(p.sourceHasAdvanced({ ahead_by: 0 }), false);
assert.equal(p.sourceHasAdvanced({ ahead_by: 1 }), true);
const normal = { ...manifest, commit: source, source_commit: source, tested_merge_commit: source };
assert.ok(p.exactImageInstalled({ ...installed, git_sha: source }, normal));
// Execute the real System Information method with deterministic local/channel/GitHub responses.
const component = readFileSync(new URL('../webapp/src/views/SystemInfoView.vue', import.meta.url), 'utf8');
const options = compile(component.match(/<script lang="ts">([\s\S]*?)<\/script>/)[1], {
    vue: { defineComponent: value => value }, '@/utils/firmwareProvenance': p,
    '@/utils/authentication': { authHeader: () => ({}), handleResponse: response => response.json() },
}).default;
for (const [firmware, channel, ahead] of [[installed, manifest, 0], [old, manifest, 0], [installed, manifest, 1], [old, legacy, 0], [{ ...installed, git_sha: source }, normal, 0]]) {
    const state = { ...options.data(), allowVersionInfo: true, systemDataList: { ...firmware, git_hash: 'g' + merge.slice(0, 7) }, $t: key => key };
    const base = p.sourceComparisonBase(firmware, channel);
    const calls = [];
    fetcher = async url => {
        calls.push(url);
        if (url.endsWith('manifest.json')) return new Response(JSON.stringify(channel));
        assert.ok(url.includes(`/compare/${base}...${encodeURIComponent(firmware.git_branch)}`), url);
        return new Response(JSON.stringify({ ahead_by: ahead, total_commits: ahead, html_url: 'compare-url' }));
    };
    await options.methods.getUpdateInfo.call(state);
    assert.equal(state.systemDataList.update_text, ahead ? 'systeminfo.VersionNew' : 'systeminfo.VersionOk');
    assert.equal(calls.filter(url => url.includes('/compare/')).length, 1);
}
// Same source but a newly tested merge is a different OTA image, without a source update.
assert.equal(p.exactImageInstalled(installed, { ...manifest, commit: advanced, tested_merge_commit: advanced }), false);
assert.equal(p.sourceComparisonBase(installed, { ...manifest, commit: advanced }), source);
console.log('Firmware exact image, source comparison, legacy recovery, advance and non-PR UI tests passed');
