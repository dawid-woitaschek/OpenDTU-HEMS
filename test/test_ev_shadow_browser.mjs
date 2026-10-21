// Browser/API presentation only. Native tests independently execute production planning/control.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { createHemsFixture } from './fixtures/hems.mjs';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const errors = [];
try {
    for (const width of [320, 768, 1440]) {
        const fixture = createHemsFixture(), posts = [];
        const config = { enabled: false, deadline: 1792891800, required_wh: 5000, min_current_a: 6, max_current_a: 16,
            max_price_enabled: false, max_price_micros_per_kwh: 250000, pv_preferred: true, nominal_voltage: 0, phases: 0 };
        fixture.optimizer.config.ev_shadow = config;
        fixture.optimizer.ev_shadow = { config, enabled: false, shadow_only: true, device_writes: false,
            reason: 'disabled', coverage_reason: null, stale_inputs: false, plan_revision: 5, input_revision: 2,
            storage_plan_revision: 5, current_action: 'wait', current_a: 0, next_change: null, next_action: 'wait', completion: null,
            required_wh: 5000, planned_wh: 0, deliverable_wh: 0, shortfall_wh: 5000, cost_nano: 0, currency: 'EUR',
            storage_interaction: 'storage_schedule_preserved', storage_conflict: false, pv_included: false, has_more: false, schedule: [] };
        const context = await browser.newContext({ viewport: { width, height: 1000 }, timezoneId: 'Europe/Berlin' });
        const page = await context.newPage(); page.on('pageerror', e => errors.push(e.message));
        await page.addInitScript(() => localStorage.setItem('locale', 'en'));
        await page.route('**/api/**', async route => {
            const request = route.request(), path = new URL(request.url()).pathname;
            if (request.method() === 'POST') {
                const form = await new Response(request.postDataBuffer(), { headers: { 'content-type': request.headers()['content-type'] } }).formData();
                const data = JSON.parse(form.get('data')); posts.push({ path, data });
                assert.equal(path, '/api/hems/optimizer/config'); assert.deepEqual(Object.keys(data), ['ev_shadow']);
                assert.equal(data.ev_shadow.deadline, Date.parse('2026-10-25T01:30:00Z') / 1000);
                fixture.optimizer.ev_shadow.config = fixture.optimizer.config.ev_shadow = data.ev_shadow;
                fixture.optimizer.ev_shadow.enabled = true; fixture.optimizer.ev_shadow.reason = 'planned';
                return route.fulfill({ json: { ok: true, type: 'success' } });
            }
            const body = path.endsWith('/i18n/languages') ? [] : path === '/api/hems/status' ? fixture.status
                : path === '/api/hems/config' ? fixture.config : path === '/api/hems/profiles' ? fixture.profiles
                : path === '/api/hems/profiles/evse-binding' ? fixture.binding : path.startsWith('/api/hems/optimizer') ? fixture.optimizer
                : path.startsWith('/api/hems/energy/tariff') ? fixture.tariff : path.startsWith('/api/hems/energy/forecast') ? fixture.forecast : {};
            return route.fulfill({ json: body });
        });
        await page.goto('http://127.0.0.1:5173/hems/settings');
        const form = page.locator('form').filter({ has: page.locator('#ev-deadline') });
        await form.waitFor(); await form.getByText('Enable EV shadow planning', { exact: true }).click();
        await page.locator('#ev-deadline').fill('2026-10-25T02:30');
        await page.locator('#ev-offset').waitFor();
        await page.locator('#ev-offset').selectOption(String(Date.parse('2026-10-25T01:30:00Z') / 1000));
        await form.locator('summary').click(); await page.locator('#ev-voltage').fill('230'); await page.locator('#ev-phases').selectOption('3');
        await form.getByRole('button', { name: 'Save', exact: true }).click();
        await page.getByText('EV planning settings saved.', { exact: true }).waitFor(); assert.equal(posts.length, 1);
        fixture.optimizer.ev_shadow.planned_wh = 4000; fixture.optimizer.ev_shadow.shortfall_wh = 1000;
        fixture.optimizer.ev_shadow.deliverable_wh = 4000; fixture.optimizer.ev_shadow.cost_nano = 123456789;
        fixture.optimizer.ev_shadow.reason = 'energy_shortfall'; fixture.optimizer.ev_shadow.storage_conflict = true;
        fixture.optimizer.ev_shadow.storage_interaction = 'storage_discharge_slots_excluded';
        fixture.optimizer.ev_shadow.schedule = [{ start: 1792891800, end: 1792892700, charge_end: 1792892500, current_a: 6, energy_wh: 1000 }];
        await page.goto('http://127.0.0.1:5173/hems');
        await page.getByText('Planning only. This feature never controls the wallbox.', { exact: true }).waitFor();
        await page.getByText('Storage-discharge intervals excluded from EV charging', { exact: true }).waitFor();
        assert.match(await page.locator('body').innerText(), /The covered, permitted slots cannot supply all required energy/);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, `EV UI overflow at ${width}`);
        await page.goto('http://127.0.0.1:5173/hems/diagnostics');
        await page.getByText('Economic EV Charging · shadow plan', { exact: true }).waitFor();
        assert.equal(posts.length, 1, 'Overview/Diagnostics must send no commands');
        await context.close();
    }
    assert.deepEqual(errors, []);
    console.log('EV browser settings/UTC-offset intent save, shadow Overview/Diagnostics, shortfall/storage conflict and 320/768/1440px passed');
} finally { await browser.close(); }
