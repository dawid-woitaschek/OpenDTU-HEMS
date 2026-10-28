import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import vm from 'node:vm';
process.chdir(new URL('..', import.meta.url).pathname);
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
const { sha256 } = await import('../webapp/node_modules/@noble/hashes/sha2.js');
const { bytesToHex } = await import('../webapp/node_modules/@noble/hashes/utils.js');
let fetcher;
const module = { exports: {} };
vm.runInNewContext(ts.transpileModule(readFileSync('webapp/src/utils/profileCatalog.ts', 'utf8'), { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 } }).outputText,
    { module, exports: module.exports, require: name => name.includes('sha2') ? { sha256 } : { bytesToHex }, fetch: (...args) => fetcher(...args), TextDecoder, Uint8Array, AbortSignal, Error });
const p = module.exports;
const catalog = { version: 1, source_commit: '1'.repeat(40), profiles: ['evse/Tinkerforge-WARP3-Smart.json', 'storage/Marstek-Venus-E-3.0.json'].map(path => {
    const raw = readFileSync('device-profiles/' + path), value = JSON.parse(raw);
    return { id: value.id, category: path.split('/')[0], vendor: value.metadata.vendor, model: value.metadata.model, schema: value.schema, source_revision: value.metadata.source_revision, path, size: raw.length, sha256: createHash('sha256').update(raw).digest('hex'), installed_sha256: 'c'.repeat(64) };
}) };
const entry = catalog.profiles[0];
const bytes = readFileSync('device-profiles/' + entry.path);
assert.equal(createHash('sha256').update(bytes).digest('hex'), entry.sha256);
assert.equal(p.parseCatalog(catalog), catalog);
assert.equal(p.installedCatalogState(entry), 'missing');
assert.equal(p.installedCatalogState(entry, {}), 'unknown');
assert.equal(p.installedCatalogState(entry, { content_sha256: entry.installed_sha256 }), 'current');
assert.equal(p.installedCatalogState(entry, { content_sha256: 'a'.repeat(64) }), 'different');
let calls = [];
fetcher = async (url, options) => { calls.push(url); assert.equal(options.credentials, 'omit'); return new Response(url.endsWith('catalog.json') ? JSON.stringify(catalog) : bytes); };
const loaded = await p.fetchCatalog(); assert.equal(calls.length, 1); assert.equal(loaded.profiles.length, 2);
assert.equal(await p.fetchCatalogProfile(catalog, entry), bytes.toString()); assert.equal(calls.length, 2);
assert.ok(calls[1].includes('/' + catalog.source_commit + '/device-profiles/'));
for (const change of [{ version: 2 }, { source_commit: '../bad' }, { profiles: [...catalog.profiles, entry] }, { profiles: [{ ...entry, path: 'https://attacker/x' }] }, { profiles: [{ ...entry, size: 6145 }] }, { profiles: [{ ...entry, sha256: '' }] }]) {
    assert.throws(() => p.parseCatalog({ ...catalog, ...change }), /Malformed/);
}
for (const [response, code] of [[new Response('', { status: 404 }), 'NotFound'], [new Response('', { status: 503 }), 'Unavailable'], [new Response('{}'), 'Malformed'], [new Response('x'.repeat(256 * 1024 + 1)), 'TooLarge']]) {
    fetcher = async () => response; await assert.rejects(p.fetchCatalog(), new RegExp(code));
}
fetcher = async () => { throw new Error('offline'); }; await assert.rejects(p.fetchCatalog(), /Network/);
fetcher = async () => new Response('', { status: 404 }); await assert.rejects(p.fetchCatalogProfile(catalog, entry), /ProfileNotFound/);
fetcher = async () => new Response(bytes.toString().replace(entry.id, 'different')); await assert.rejects(p.fetchCatalogProfile(catalog, entry), /Integrity/);
fetcher = async () => new Response(bytes); await assert.rejects(p.fetchCatalogProfile(catalog, { ...entry }), /Malformed/);
fetcher = async () => new Response('null');
const malformed = { ...entry, size: 4, sha256: createHash('sha256').update('null').digest('hex') };
await assert.rejects(p.fetchCatalogProfile({ ...catalog, profiles: [malformed] }, malformed), /MalformedProfile/);
fetcher = async () => new Response('{bad');
const invalidJson = { ...entry, size: 4, sha256: createHash('sha256').update('{bad').digest('hex') };
await assert.rejects(p.fetchCatalogProfile({ ...catalog, profiles: [invalidJson] }, invalidJson), /MalformedProfile/);
for (const file of ['profileMessages', 'firmwareMessages', 'hemsTariffMessages']) {
    const messagesModule = { exports: {} };
    vm.runInNewContext(ts.transpileModule(readFileSync('webapp/src/' + file + '.ts', 'utf8'), { compilerOptions: { module: ts.ModuleKind.CommonJS } }).outputText, { module: messagesModule, exports: messagesModule.exports });
    const messages = Object.values(messagesModule.exports)[0];
    const keys = (value, prefix = '') => Object.entries(value).flatMap(([key, item]) => typeof item === 'object' ? keys(item, prefix + key + '.') : prefix + key).sort();
    for (const locale of ['de']) assert.equal(JSON.stringify(keys(messages[locale])), JSON.stringify(keys(messages.en)), file + ': ' + locale);
}
console.log('Static catalog schema, bounded lazy fetch, installed comparison, pinned revision, network and integrity tests passed');
