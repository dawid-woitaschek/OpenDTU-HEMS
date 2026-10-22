import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
const source = readFileSync(new URL('../webapp/src/utils/hemsEnergyManagement.ts', import.meta.url), 'utf8');
const compiled = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 } }).outputText;
const { defaultEnergyManagement, referencePreset } = await import('data:text/javascript;base64,' + Buffer.from(compiled).toString('base64'));
const defaults = defaultEnergyManagement();
assert.equal(defaults.enabled, false);
assert.equal(defaults.automatic_load, false);
assert.equal(defaults.battery_export, false);
assert.equal(defaults.wear_source, 0);
const preset = referencePreset(defaults);
assert.equal(preset.enabled, false);
assert.equal(preset.pv_ac_limit_w, 0); // Never invent a hardware rating.
assert.equal(preset.automatic_load, true);
assert.equal(preset.ev_protection, true);
assert.equal(preset.battery_export, false);
assert.equal(preset.preference, 1);
assert.equal(defaults.automatic_load, false); // No mutation of the saved object.
const settings = readFileSync(new URL('../webapp/src/components/hems/EnergyManagementSettings.vue', import.meta.url), 'utf8');
const summary = readFileSync(new URL('../webapp/src/components/hems/EnergyManagementSummary.vue', import.meta.url), 'utf8');
const keys = [...(settings + summary).matchAll(/hemsUi\.([a-zA-Z_]+)/g)].map(m => m[1]).filter(k => k !== 'optimizerStates');
for (const locale of ['en', 'de', 'fr']) {
    const ui = JSON.parse(readFileSync(new URL(`../webapp/src/locales/${locale}.json`, import.meta.url), 'utf8')).hemsUi;
    for (const key of keys) assert.equal(typeof ui[key], 'string', `${locale}: ${key}`);
    for (const key of ['pv_export', 'battery_export', 'ev_protection', 'degradation_micros', 'buffer_micros', 'terminal_hours', 'terminal_price_micros', 'hysteresis_micros']) assert.equal(typeof ui[key], 'string', `${locale}: ${key}`);
}
console.log('Energy settings: explicit opt-in, reference goals without ratings/control grants, and en/de/fr labels passed');
