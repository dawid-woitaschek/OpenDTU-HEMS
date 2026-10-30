// Exercise the actual component methods, response handling and write DTO.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
let fetcher;
function compile(source, imports = {}) {
    const module = { exports: {} };
    vm.runInNewContext(ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 } }).outputText,
        { module, exports: module.exports, require: key => imports[key] || {}, fetch: (...args) => fetcher(...args), Headers, FormData, localStorage: { getItem: () => null, removeItem() {} } });
    return module.exports;
}
const read = path => readFileSync(new URL(path, import.meta.url), 'utf8');
const dto = compile(read('../webapp/src/types/RemoteDiagnostics.ts'));
const auth = compile(read('../webapp/src/utils/authentication.ts'));
const componentText = read('../webapp/src/views/RemoteDiagnosticsView.vue');
const component = compile(componentText.match(/<script lang="ts">([\s\S]*?)<\/script>/)[1], {
    vue: { defineComponent: options => options }, '@/utils/authentication': auth, '@/types/RemoteDiagnostics': dto,
}).default;
const translations = JSON.parse(read('../webapp/src/locales/en.json'));
const redirects = [];
const state = { ...component.data(), $t: key => key.split('.').reduce((value, part) => value?.[part], translations) || key,
    $emitter: { emit() {} }, $router: { push: route => redirects.push(route), currentRoute: { value: { fullPath: '/system/remote-diagnostics' } } } };
for (const [name, method] of Object.entries(component.methods)) state[name] = method.bind(state);
assert.equal(state.config.interval_minutes, 60); assert.equal(state.config.quarter_hour, false); assert.equal(state.config.enabled, false);
assert.equal(state.token, '');
const config = { enabled: true, endpoint: 'https://collector.example.org/api/v1/telemetry', interval_minutes: 60, token_set: true };
let posted, sends = 0;
fetcher = async (url, options) => {
    assert.ok(options.headers instanceof Headers);
    assert.equal(options.headers.get('X-Requested-With'), 'XMLHttpRequest');
    if (url.endsWith('/config') && options.method === 'POST') {
        posted = JSON.parse(options.body.get('data'));
        return new Response(JSON.stringify({ success: true }));
    }
    if (url.endsWith('/send')) { sends++; assert.equal(options.method, 'POST'); return new Response('{"scheduled":true}', { status: 202 }); }
    return new Response(JSON.stringify(url.endsWith('/config') ? config : { enabled: true, token_set: true, pending_samples: 12 }));
};
await state.loadConfiguration(); assert.equal(state.config.token_set, true); assert.equal(state.token, '');
state.config.quarter_hour = true;
await state.save(); assert.equal(posted.quarter_hour, true); assert.equal(posted.token, undefined); assert.equal(posted.token_set, undefined);
state.token = 'hems_replacement'; await state.save(); assert.equal(posted.token, 'hems_replacement'); assert.equal(state.token, '');
state.config.enabled = false; state.clearToken = true; state.token = 'ignored'; await state.save(); assert.equal(posted.token, ''); assert.equal(state.clearToken, false);
await state.sendNow(); assert.equal(sends, 1);
state.status = { ...state.status, pending_transmission_age_seconds: 3599, expired_transmission_count: 2, last_hems_trigger: ['quarter_hour', 'plan_revision'] };
const rows = component.computed.statusRows.call(state);
assert.equal(rows.find(row => row.key === 'pending_transmission_age_seconds').value, 3599);
assert.equal(rows.find(row => row.key === 'last_hems_trigger').value, 'quarter_hour, plan_revision'); assert.equal(state.sending, false); assert.equal(state.status.pending_samples, 12);
fetcher = async () => new Response('{"success":false}', { status: 400 });
state.token = 'keep_until_saved'; await state.save(); assert.equal(state.failed, true); assert.equal(state.token, 'keep_until_saved'); assert.equal(state.saving, false);
fetcher = async () => new Response('{}', { status: 401 }); await state.sendNow(); assert.equal(redirects.at(-1).path, '/login');
for (const lang of ['en', 'de']) {
    const messages = JSON.parse(read(`../webapp/src/locales/${lang}.json`)).remoteDiagnostics;
    for (const key of Object.keys(translations.remoteDiagnostics)) assert.ok(messages[key], `${lang}:${key}`);
}
const navigation = read('../webapp/src/utils/hemsNavigation.ts');
assert.ok(navigation.slice(navigation.indexOf("label: 'system'")).includes("path: '/system/remote-diagnostics'"));
const api = read('../src/remote/Api.cpp'); assert.equal((api.match(/WebApi.checkCredentials\(request\)/g) || []).length, 4);
assert.ok(read('../src/WebApi_file.cpp').includes('requestFile.indexOf("remote-diagnostics")'));
assert.ok(!read('../src/remote/Service.cpp').includes('localhost'));
console.log('remote diagnostics UI configuration/redaction/status/manual/auth/DE/EN passed');
