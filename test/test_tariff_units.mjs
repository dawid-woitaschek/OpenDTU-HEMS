// Exercise the same conversion helpers and save/reload path used by the tariff form.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import vm from 'node:vm';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
let fetcher;
const translations = JSON.parse(readFileSync(new URL('../webapp/src/locales/en.json', import.meta.url), 'utf8'));
const useI18n = () => ({ locale: { value: 'en' }, t: (key, args = {}) => {
    const value = key.split('.').reduce((o, part) => o?.[part], translations) || key;
    return value.replace(/\{(\w+)\}/g, (_, name) => args[name]);
} });

function compile(source, imports = {}) {
    const module = { exports: {} };
    vm.runInNewContext(ts.transpileModule(source, {
        compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
    }).outputText, { module, exports: module.exports, require: name => imports[name] || {},
        structuredClone, FormData, Intl, defineProps: () => ({ view: 'settings' }), defineEmits: () => () => {}, setInterval, clearInterval, fetch: (...args) => fetcher(...args) });
    return module.exports;
}
const units = compile(readFileSync(new URL('../webapp/src/utils/tariffUnits.ts', import.meta.url), 'utf8'));
assert.equal(units.toDisplay(1900, 100), 19);
assert.equal(units.toCanonical('19', 100), 1900);
for (const field of units.priceFields) {
    for (const canonical of [0, 1, 123456, 2147483647, ...(field.min < 0 ? [-1, -123456, -2147483648] : [4294967295])]) {
        assert.equal(units.toCanonical(units.toDisplay(canonical, field.scale), field.scale), canonical);
    }
}
assert.equal(units.toCanonical('12.34', 10000), 123400);
assert.equal(units.toCanonical('-0.1234', 10000), -1234);
assert.equal(units.toCanonical('9.99', 1e6), 9990000);
const component = readFileSync(new URL('../webapp/src/components/HemsEnergyPanel.vue', import.meta.url), 'utf8');
assert.ok(component.includes("{{ field.unit }}") && !component.includes("(currency"));
assert.match(component, /toDisplay\(drafts\[kind\]!\.pricing\[field.key\], field.scale\)/);
assert.ok(/toCanonical\(\s*\(\$event.target as HTMLInputElement\).value,\s*field.scale\s*\)/.test(component));
let persisted;
const original = { enabled: true, provider: 'energy_charts', url: '', planes: [], pricing: {
    enabled: true, vat_basis_points: 1900, grid_micros_per_kwh: 123400,
    markup_micros_per_kwh: 1234, monthly_micros: 9990000,
}, refresh_seconds: 3600, max_age_seconds: 3600, horizon_seconds: 86400 };
const state = compile(component.match(/<script setup lang="ts">([\s\S]*?)<\/script>/)[1] + '\nexport { drafts, feeds, usable, unavailable, save, load, refresh, errors };', {
    vue: { ref: value => ({ value }), computed: fn => ({ get value() { return fn(); } }), reactive: value => value, onMounted() {}, onUnmounted() {}, getCurrentInstance: () => ({ appContext: { config: { globalProperties: {} } } }) },
    'vue-i18n': { useI18n },
    'vue-router': { useRouter: () => ({}) }, '@/utils/tariffUnits': units,
    '@/utils/hemsPresentation': compile(readFileSync(new URL('../webapp/src/utils/hemsPresentation.ts', import.meta.url), 'utf8')),
    '@/utils/authentication': { authHeader: () => ({}), handleResponse: x => x },
});
fetcher = async (path, options) => {
    if (options.method === 'POST') {
        persisted = JSON.parse(options.body.get('data'));
        return {};
    }
    return { revision: 1, config: persisted, intervals: [] };
};
state.drafts.tariff = structuredClone(original);
for (const field of units.priceFields) {
    state.drafts.tariff.pricing[field.key] = units.toCanonical(units.toDisplay(original.pricing[field.key], field.scale), field.scale);
}
await state.save('tariff');
assert.deepEqual(persisted, original);
assert.deepEqual(state.drafts.tariff, original);
await state.save('tariff'); // Saving reloaded canonical values must not convert them twice.
assert.deepEqual(persisted, original);
console.log('Tariff percent, ct/kWh and EUR/month conversions and API save/reload preserve canonical integer values');

state.feeds.tariff = { enabled: true, fresh: true, planning_usable: true };
for (const [reason, expected] of [['busy', 'queued or running'], ['network_unavailable', 'Waiting for network connectivity'], ['clock_invalid', 'synchronized system time'], ['transport_backoff', 'Waiting before the next provider request'], ['provider_backoff', 'Provider requested a retry delay'], ['rate_limited', 'rate limit'], ['not_enabled', 'Disabled'], ['invalid_config', 'configuration']]) {
    fetcher = async () => new Response(JSON.stringify({ ok: false, reason, retry_after_seconds: 15 }), { status: 409 });
    await state.refresh('tariff');
    assert.ok(state.errors.tariff.includes(expected));
    assert.ok(state.errors.tariff.includes('Retry in 15 seconds'));
    assert.equal(state.usable('tariff'), true); // a rejected action does not make retained accepted data unavailable
    assert.equal(state.unavailable.tariff, false);
}
fetcher = async () => { throw new Error('API offline'); };
await state.load('tariff');
assert.equal(state.unavailable.tariff, true);
assert.equal(state.usable('tariff'), false);
console.log('Manual refresh rejection preserves data availability; API failure is separately unverified');
