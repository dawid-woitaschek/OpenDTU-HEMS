// Runs the real Admin setup and shared response handler with deterministic fetches.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
function compile(source, imports = {}) {
    const module = { exports: {} };
    vm.runInNewContext(ts.transpileModule(source, {
        compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
    }).outputText, { module, exports: module.exports, require: name => imports[name] || {},
        fetch: (...args) => fetcher(...args), Headers, FormData, localStorage: { getItem: () => null, removeItem() {} } });
    return module.exports;
}
let fetcher;
const translations = JSON.parse(readFileSync(new URL('../webapp/src/locales/en.json', import.meta.url), 'utf8'));
const useI18n = () => ({ locale: { value: 'en' }, t: (key, args = {}) => {
    const value = key.split('.').reduce((o, part) => o?.[part], translations) || key;
    return value.replace(/\{(\w+)\}/g, (_, name) => args[name]);
} });

const auth = compile(readFileSync(new URL('../webapp/src/utils/authentication.ts', import.meta.url), 'utf8'));
const component = readFileSync(new URL('../webapp/src/views/HemsAdminView.vue', import.meta.url), 'utf8');
const source = component.match(/<script setup lang="ts">([\s\S]*?)<\/script>/)[1];
const redirects = [];
const router = { push: value => redirects.push(value), currentRoute: { value: { fullPath: '/hems/settings' } } };
const state = compile(source + '\nexport { save, error, notice, warning, busy, revision };', {
    vue: { ref: value => ({ value }), onMounted() {}, getCurrentInstance: () => ({ appContext: { config: {
        globalProperties: { $emitter: { emit() {} } },
    } } }) },
    'vue-i18n': { useI18n },
    'vue-router': { useRouter: () => router }, '@/utils/authentication': auth,
});
const response = (data, status = 200) => new Response(JSON.stringify(data), { status });
let calls;
async function save(post, reload = () => response({})) {
    calls = [];
    fetcher = async (path, options) => {
        calls.push({ path, method: options.method });
        return options.method === 'POST' ? post() : reload();
    };
    await state.save('/api/hems/profiles/evse-binding', { control_mode: 'monitor' });
    assert.equal(state.busy.value, false);
}
await save(() => response({ ok: true, type: 'warning' })); // old endpoint success shape
assert.equal(state.error.value, '');
assert.equal(state.notice.value, 'Settings saved.');
assert.equal(calls.length, 3);
await save(() => response({ ok: true, type: 'success' }));
assert.equal(state.error.value, '');
await save(() => response({ ok: true, type: 'warning', warning: 'Release failed; permission saved.' }));
assert.match(state.warning.value, /Release failed/);
assert.equal(state.notice.value, 'Settings saved.');
await save(() => response({ type: 'success' }), () => { throw new Error('Offline'); });
assert.equal(state.error.value, '');
assert.equal(state.notice.value, 'Settings saved.');
assert.match(state.warning.value, /reloading failed: Offline/);
await save(() => response({ message: 'Write failed!', type: 'warning' }));
assert.equal(state.error.value, 'Write failed!');
assert.equal(state.notice.value, '');
assert.equal(calls.length, 1);
await save(() => response({ message: 'Invalid profile' }, 409));
assert.equal(state.error.value, 'Invalid profile');
assert.equal(calls.length, 1);
await save(() => { throw new Error('Network unavailable'); });
assert.equal(state.error.value, 'Network unavailable');
await save(() => response({}, 401));
assert.equal(redirects.at(-1).path, '/login');
console.log('Admin persisted save, warning, reload, POST failure and authentication tests passed');
