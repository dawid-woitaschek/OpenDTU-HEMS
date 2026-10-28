// Exercise the production shared live-data owner and supported-locale policy without hardware.
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
import { createLiveFixture } from './fixtures/live.mjs';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
const vue = { ref: value => ({ value }), shallowRef: value => ({ value }), readonly: value => value, computed: fn => ({ get value() { return fn(); } }) };
function compile(file, imports, globals = {}) {
    const module = { exports: {} };
    vm.runInNewContext(ts.transpileModule(readFileSync(new URL('../webapp/src/' + file, import.meta.url), 'utf8'), { compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, esModuleInterop: false } }).outputText,
        { module, exports: module.exports, require: name => imports[name] || {}, ...globals });
    return module.exports;
}
let now = 1000000, tick, source = createLiveFixture(), fail = false, pending, sockets = [];
class Socket {
    constructor(url, callbacks) { this.url = url; this.callbacks = callbacks; sockets.push(this); }
    connect() { this.connected = true; }
    close() { this.connected = false; }
}
const auth = { authHeader: () => ({}), authUrl: () => '', handleResponse: async response => { if (!response.ok) throw Error('unavailable'); return response.json(); } };
const live = compile('utils/liveData.ts', { vue, './authentication': auth, './websocketService': { default: Socket } }, {
    Date: { now: () => now }, AbortController, location: { protocol: 'http:', host: 'fixture.local' },
    setInterval: callback => { tick = callback; return 1; }, clearInterval() {},
    fetch: async () => { if (pending) return pending; if (fail) throw Error('offline'); return new Response(JSON.stringify(source)); },
});
const helpers = compile('utils/inverterPresentation.ts', {});
live.startLiveData({}, {});
const consumer = live.useLiveData();
await consumer.reload();
assert.equal(consumer.connection.value, 'connecting');
assert.equal(consumer.inverters.value.length, 2);
for (let n = 0; n < 20; n++) live.useLiveData();
assert.equal(sockets.length, 1, 'Consumers do not own redundant sockets');
sockets[0].callbacks.onOpen(); await consumer.reload();
assert.equal(consumer.connection.value, 'connected');
const update = structuredClone(source.inverters[0]); update.AC[0].Power.v = 425;
sockets[0].callbacks.onMessage({ data: JSON.stringify({ inverters: [update], power_meter: { enabled: true } }) });
assert.equal(consumer.inverters.value[0].AC[0].Power.v, 425);
assert.equal(consumer.inverters.value.length, 2, 'Single-inverter updates retain other inverters');
assert.equal(consumer.data.value.power_meter.Power.v, -245, 'Partial aggregate updates retain other fields');
let finishRefresh;
pending = new Promise(resolve => { finishRefresh = resolve; });
const overlappingRefresh = consumer.reload();
const newer = structuredClone(update); newer.AC[0].Power.v = 450;
sockets[0].callbacks.onMessage({ data: JSON.stringify({ inverters: [newer], power_meter: { Power: { v: -300, u: 'W', d: 1, max: 0 } } }) });
const oldSnapshot = structuredClone(source);
oldSnapshot.inverters.push({ ...structuredClone(source.inverters[1]), serial: '100000000003', name: 'New inverter' });
finishRefresh(new Response(JSON.stringify(oldSnapshot))); pending = undefined;
await overlappingRefresh;
assert.equal(consumer.inverters.value[0].AC[0].Power.v, 450, 'HTTP refresh cannot replace a newer inverter frame');
assert.equal(consumer.data.value.power_meter.Power.v, -300, 'HTTP refresh cannot replace a newer meter frame');
assert.equal(consumer.inverters.value.length, 3, 'Full refresh still discovers added devices');
now += 61000; tick();
assert.equal(helpers.inverterState(consumer.inverters.value[0], true), 'lastKnown');
assert.equal(helpers.inverterTemperature(consumer.inverters.value[0]), 38);
sockets[0].callbacks.onClose(); assert.equal(consumer.connection.value, 'disconnected');
fail = true; await consumer.reload();
assert.equal(consumer.unavailable.value, true);
assert.equal(consumer.inverters.value[0].AC[0].Power.v, 450, 'Failure retains inspectable last-known data');
const zero = { ...consumer.inverters.value[0], reachable: false, AC: [{ Power: { v: 0 } }] };
assert.equal(helpers.inverterState(zero, false), 'unreachable', 'An unreachable zero is never current');
let release;
pending = new Promise(resolve => { release = resolve; });
const oldRequest = consumer.reload();
live.stopLiveData();
fail = false; pending = undefined; source = createLiveFixture(); source.inverters[0].AC[0].Power.v = 333;
live.startLiveData({}, {}); await consumer.reload();
release(new Response(JSON.stringify(createLiveFixture()))); await oldRequest;
assert.equal(consumer.inverters.value[0].AC[0].Power.v, 333, 'Old connection requests cannot replace a new owner snapshot');
assert.equal(sockets[0].connected, false);
live.stopLiveData(); assert.equal(sockets.at(-1).connected, false);

const saved = new Map([['locale', 'fr']]);
const supplements = {};
for (const file of ['hemsMessages', 'hemsTariffMessages', 'firmwareMessages', 'profileMessages', 'redesignMessages']) {
    supplements['./' + file] = compile(file + '.ts', {});
}
const fakeI18n = { createI18n: options => ({ global: { locale: { value: options.locale }, availableLocales: Object.keys(options.messages), mergeLocaleMessage() {} } }) };
const language = compile('i18n.ts', { ...supplements, 'vue-i18n': fakeI18n, '@intlify/unplugin-vue-i18n/messages': { default: { en: {}, de: {} } } }, {
    navigator: { language: 'fr-FR' }, document: { documentElement: {} },
    localStorage: { getItem: key => saved.get(key), setItem: (key, value) => saved.set(key, value) },
    fetch() { throw Error('The supported language set must not load remote packs'); },
});
assert.equal(saved.get('locale'), 'en');
assert.deepEqual(Array.from(language.allLocales, locale => locale.code), ['en', 'de']);
for (const unsupported of ['fr', 'fr-FR', 'es', 'unknown', '']) { language.setLocale(unsupported); assert.equal(saved.get('locale'), 'en'); }
language.setLocale('de-DE'); assert.equal(saved.get('locale'), 'de');
assert.deepEqual(readdirSync(new URL('../webapp/src/locales', import.meta.url)).sort(), ['de.json', 'en.json']);
const packDirectory = new URL('../lang/', import.meta.url);
for (const file of readdirSync(packDirectory).filter(name => name.endsWith('.lang.json'))) {
    const pack = JSON.parse(readFileSync(new URL(file, packDirectory), 'utf8'));
    assert.ok(['de', 'en'].includes(pack.meta?.code), `Unsupported language pack must not be shipped: ${file}`);
}
const messages = supplements['./redesignMessages'].redesignMessages;
assert.deepEqual(Object.keys(messages.en.redesign).sort(), Object.keys(messages.de.redesign).sort());
console.log('Shared live ownership, partial frames, stale/unreachable data, abort isolation and strict EN/DE locale fallback passed');
