import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
process.chdir(new URL('..', import.meta.url).pathname);
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
let fetcher;
function compile(source, imports = {}) {
    const module = { exports: {} };
    vm.runInNewContext(ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 } }).outputText,
        { module, exports: module.exports, require: name => imports[name] || {}, fetch: (...args) => fetcher(...args), Error });
    return module.exports;
}
const provenance = compile(readFileSync('webapp/src/utils/firmwareProvenance.ts', 'utf8'));
const source = readFileSync('webapp/src/views/FirmwareUpgradeView.vue', 'utf8').match(/<script lang="ts">([\s\S]*?)<\/script>/)[1];
const options = compile(source, { vue: { defineComponent: x => x }, '@/utils/firmwareProvenance': provenance }).default;
const build = { file: 'opendtu-hems-generic_esp32s3_usb.bin', size: 100, md5: 'a'.repeat(32), sha256: 'b'.repeat(64) };
const manifest = { schema: 'opendtu-hems-update/v1', repository: 'owner/repo', channel: 'pr-25', branch: 'feature/hems-economic-dispatch', commit: '1'.repeat(40), source_commit: '2'.repeat(40), published_at: '2026-10-19T10:00:00Z', builds: { generic_esp32s3_usb: build } };
function state() {
    const s = { ...options.data(), $t: key => key };
    for (const [key, method] of Object.entries(options.methods)) s[key] = method.bind(s);
    for (const [key, method] of Object.entries(options.computed)) Object.defineProperty(s, key, { get: method.bind(s) });
    s.firmwareStatus = { update_repository: manifest.repository, update_channel: 'pr-25', git_branch: manifest.branch, git_sha: manifest.commit, source_commit: manifest.source_commit, pioenv: 'generic_esp32s3_usb' };
    let channel = '';
    Object.defineProperty(s, 'selectedChannel', { get: () => channel, set: value => { channel = value; options.watch.selectedChannel.handler.call(s); } });
    s.selectedChannel = 'pr-25';
    return s;
}
const ok = value => new Response(JSON.stringify(value));
async function load(s, value = manifest) { fetcher = async () => ok(value); await s.checkRemoteUpdate(); }
const s = state();
await load(s); assert.equal(s.lookupState, 'found/current'); assert.ok(s.upToDate);
// Editing invalidates metadata before another request is even made.
s.selectedChannel = 'deinemutter';
assert.equal(s.updateManifest, null); assert.equal(s.remoteBuild, null); assert.equal(s.lookupState, 'idle');
fetcher = async () => new Response('', { status: 404 }); await s.checkRemoteUpdate();
assert.equal(s.lookupState, 'not_found'); assert.equal(s.updateManifest, null); assert.equal(s.remoteBuild, null); assert.equal(s.upToDate, false);
for (const failure of [async () => new Response('', { status: 404 }), async () => { throw new Error('offline'); }]) {
    s.selectedChannel = 'pr-25'; await load(s); s.selectedChannel = 'deinemutter'; fetcher = failure; await s.checkRemoteUpdate();
    assert.equal(s.updateManifest, null); assert.equal(s.remoteBuild, null); assert.equal(s.manifestLoading, false);
}
s.selectedChannel = '../invalid'; let calls = 0; fetcher = async () => { ++calls; }; await s.checkRemoteUpdate(); assert.equal(s.lookupState, 'invalid'); assert.equal(calls, 0);
for (const [channel, change] of [['hems-next', { commit: '3'.repeat(40) }], ['pr-1', { commit: '4'.repeat(40), published_at: '2025-01-01T00:00:00Z' }], ['pr-25', { commit: '5'.repeat(40) }], ['pr-25', { commit: '6'.repeat(40), source_commit: '7'.repeat(40) }]]) {
    s.selectedChannel = channel; await load(s, { ...manifest, channel, ...change }); assert.equal(s.lookupState, 'found/different'); assert.equal(s.upToDate, false);
}
s.selectedChannel = 'hems-next'; await load(s); assert.equal(s.lookupState, 'invalid'); assert.equal(s.remoteBuild, null);
const legacy = { ...manifest }; delete legacy.source_commit; s.selectedChannel = 'pr-25'; await load(s, legacy); assert.equal(s.lookupState, 'found/current');
// Both late success and late rejection must leave the newer request authoritative.
for (const reject of [false, true]) {
    let release; s.selectedChannel = 'pr-25'; fetcher = () => new Promise((resolve, fail) => { release = () => reject ? fail(new Error('late error')) : resolve(ok(manifest)); });
    const first = s.checkRemoteUpdate(); s.selectedChannel = 'hems-next';
    await load(s, { ...manifest, channel: 'hems-next', commit: '8'.repeat(40) }); release(); await first;
    assert.equal(s.updateManifest.channel, 'hems-next'); assert.equal(s.lookupState, 'found/different'); assert.equal(s.manifestError, '');
}
// A response whose body finishes late is rejected too.
let body, started; const bodyStarted = new Promise(resolve => { started = resolve; }); s.selectedChannel = 'pr-25'; fetcher = async () => ({ ok: true, status: 200, json: () => new Promise(resolve => { body = resolve; started(); }) });
const first = s.checkRemoteUpdate(); await bodyStarted; s.selectedChannel = 'hems-next'; await load(s, { ...manifest, channel: 'hems-next' }); body(manifest); await first; assert.equal(s.updateManifest.channel, 'hems-next');
options.beforeUnmount.call(s); assert.equal(s.lookupState, 'idle'); assert.equal(s.updateManifest, null);
console.log('Firmware lookup transitions, pr-25 → deinemutter, legacy images, old PRs, and request races passed');
