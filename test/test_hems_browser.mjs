// Optional rendered check: start `yarn dev --host 127.0.0.1`, then run with Playwright installed.
// PLAYWRIGHT_MODULE may point to an isolated Playwright install; no firmware dependency is added.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdir } from 'node:fs/promises';
import { createHemsFixture, fixtureNow } from './fixtures/hems.mjs';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const screenshotDir = process.env.HEMS_SCREENSHOTS || 'work/screenshots';
await mkdir(screenshotDir, { recursive: true });
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const errors = [], requests = [];
try {
    for (const width of [320, 360, 768, 1440]) {
        const context = await browser.newContext({ viewport: { width, height: 1000 }, timezoneId: 'Europe/Berlin', locale: 'en', hasTouch: width <= 360 });
        const page = await context.newPage();
        page.on('pageerror', e => errors.push(e.message));
        await page.clock.install({ time: fixtureNow * 1000 });
        await page.addInitScript(() => { localStorage.setItem('locale', 'en'); });
        const fixture = createHemsFixture();
        let scenario = 'normal';
        await page.route('**/api/**', async route => {
            const url = new URL(route.request().url()), path = url.pathname;
            requests.push({ path, method: route.request().method() });
            if (scenario === 'offline' && path === '/api/hems/status') return route.abort();
            let body = path.endsWith('/i18n/languages') ? [] : path === '/api/hems/status' ? fixture.status
                : path === '/api/hems/config' ? fixture.config : path === '/api/hems/profiles/evse-binding' ? fixture.binding
                : path === '/api/hems/profiles' ? fixture.profiles : path.startsWith('/api/hems/optimizer') ? fixture.optimizer
                : path.startsWith('/api/hems/energy/tariff') ? structuredClone(fixture.tariff)
                : path.startsWith('/api/hems/energy/forecast') ? structuredClone(fixture.forecast)
                : path === '/api/system/status' ? { reset_diagnostics: { esp_reset_reason: 'ESP_RST_SW', previous_boot: { valid: true, operation: 'provider_refresh' } }, heap_min_free: 90000, task_details: [] } : {};
            if (route.request().method() === 'POST') {
                const form = await new Response(route.request().postDataBuffer(), { headers: { 'content-type': route.request().headers()['content-type'] } }).formData();
                const payload = JSON.parse(form.get('data'));
                requests.at(-1).payload = payload;
                if (path === '/api/hems/config') Object.assign(fixture.config, payload);
                if (path === '/api/hems/profiles/evse-binding') Object.assign(fixture.binding, payload);
                if (path === '/api/hems/optimizer/config') fixture.optimizer.config = payload;
                if (path === '/api/hems/energy/tariff/config') fixture.tariff.config = payload;
                if (path === '/api/hems/energy/forecast/config') fixture.forecast.config = payload;
                return route.fulfill({ json: { ok: true, type: 'success' } });
            }
            if (scenario === 'feedOffline' && path.includes('/energy/forecast')) return route.abort();
            if (scenario === 'empty' && path.includes('/energy/forecast')) {
                body.intervals = []; body.valid = false; body.fresh = false; body.current_covered = false; body.planning_usable = false; body.state = 'unavailable';
            }
            if (scenario === 'partial' && path.includes('/energy/forecast')) {
                body.intervals = body.intervals.filter(i => i.start !== fixtureNow + 1800);
                body.horizon_complete = false; body.partial_horizon = true; body.state = 'partial';
            }
            if (scenario === 'partial' && path.startsWith('/api/hems/optimizer')) {
                body = structuredClone(body); body.state = 'partial'; body.warnings = ['pv_horizon_incomplete'];
            }
            if (scenario === 'blocked' && path.startsWith('/api/hems/optimizer')) {
                body = structuredClone(body); body.state = 'not_ready'; body.blockers = ['storage_soc_not_fresh', 'tariff_not_usable']; body.schedule = [];
            }
            if (scenario === 'todayOnly' && path.includes('/energy/tariff')) {
                body.intervals = body.intervals.filter(i => i.start < Date.parse('2026-10-25T23:00:00Z') / 1000);
                body.horizon_complete = false; body.partial_horizon = true;
            }
            if (scenario === 'rateLimit' && path.includes('/energy/forecast')) {
                body.last_error = 'provider_rate_limited';
                body.manual_refresh = { allowed: false, reason: 'rate_limited', retry_after_seconds: 600 };
            }
            if (scenario === 'stale' && path.includes('/energy/forecast')) { body.fresh = false; body.state = 'stale'; body.planning_usable = false; }
            if (scenario === 'gap' && path.includes('/energy/tariff')) {
                body.intervals = body.intervals.filter(i => i.start !== fixtureNow && i.start !== fixtureNow + 900);
                body.current_covered = false; body.planning_usable = false; body.horizon_complete = false; body.state = 'missing_current_interval';
            }
            return route.fulfill({ json: body });
        });
        for (const path of ['/hems', '/hems/settings', '/hems/devices', '/hems/diagnostics']) {
            await page.goto('http://127.0.0.1:5173' + path, { timeout: 60000 });
            await page.locator('nav[aria-label="HEMS navigation"]').waitFor();
            if (path === '/hems') {
                await page.locator('.energy-chart').first().waitFor();
                assert.match(await page.locator('body').innerText(), /Proposal only · no device writes/);
            }
            await page.waitForTimeout(200);
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth), false, `${path} overflows at ${width}`);
            await page.screenshot({ path: `${screenshotDir}/${width}-${path.split('/').pop() || 'overview'}.png`, fullPage: true });
        }
        if (width === 360) {
            await page.goto('http://127.0.0.1:5173/hems/settings');
            const optimizerForm = page.locator('form').filter({ has: page.locator('#optimizer-enabled') });
            await page.locator('#optimizer-grid-charging').selectOption('false');
            await page.locator('#optimizer-economic-discharge').selectOption('false');
            await page.locator('#optimizer-target-enabled').check();
            await page.locator('#optimizer-target-soc').fill('70');
            await page.locator('#optimizer-target-deadline').fill('2026-10-26T06:00');
            await optimizerForm.getByRole('button', { name: 'Save', exact: true }).click();
            await page.getByText('Economic settings saved or calculation queued.', { exact: true }).waitFor();
            const policySave = requests.findLast(r => r.path === '/api/hems/optimizer/config');
            assert.equal(policySave.payload.policies.grid_charging_allowed, false);
            assert.equal(policySave.payload.policies.discharge_allowed, false);
            assert.equal(policySave.payload.policies.target_soc_pct, 70);
            assert.equal(policySave.payload.policies.target_deadline, Date.parse('2026-10-26T05:00:00Z') / 1000);
            await page.reload();
            await page.locator('#optimizer-target-soc').waitFor();
            assert.equal(await page.locator('#optimizer-target-soc').inputValue(), '70');
            assert.equal(await page.locator('#optimizer-target-deadline').inputValue(), '2026-10-26T06:00');

            await page.locator('#tariff-vat').fill('19');
            await page.locator('#tariff-grid_micros_per_kwh').fill('12.3456');
            await page.locator('form').filter({ has: page.locator('#tariff-vat') }).getByRole('button', { name: 'Save', exact: true }).click();
            await page.getByText('Settings saved.', { exact: true }).first().waitFor();
            assert.equal(fixture.tariff.config.pricing.vat_basis_points, 1900);
            assert.equal(fixture.tariff.config.pricing.grid_micros_per_kwh, 123456);
            await page.locator('#pv-peak-0').fill('3.25');
            await page.locator('#pv-longitude_e6-0').fill('13.400001');
            await page.locator('form').filter({ has: page.locator('#pv-name-0') }).getByRole('button', { name: 'Save', exact: true }).click();
            await page.waitForFunction(() => [...document.querySelectorAll('[role="status"]')].filter(e => e.textContent === 'Settings saved.').length === 2);
            assert.equal(fixture.forecast.config.planes[0].peak_w, 3250);
            assert.equal(fixture.forecast.config.planes[0].longitude_e6, 13400001);
            await page.locator('#hems-mode').selectOption('Monitor');
            await page.locator('form').filter({ has: page.locator('#hems-mode') }).getByRole('button', { name: 'Save', exact: true }).click();
            await page.waitForFunction(() => document.querySelector('#hems-mode').value === 'Monitor');
            assert.equal(requests.filter(r => r.path === '/api/hems/config' && r.method === 'POST').at(-1).payload.mode, 'Monitor');
            assert.equal(fixture.config.storage.control_mode, 'hems');
        }
        await page.goto('http://127.0.0.1:5173/hems');
        const tariff = page.locator('.hems-energy.card').first();
        const energyChart = tariff.locator('.energy-chart');
        const ranges = energyChart.getByRole('group', { name: 'Chart range' });
        await ranges.getByRole('button', { name: 'Today', exact: true }).waitFor();
        assert.equal(await ranges.getByRole('button', { name: 'Today', exact: true }).getAttribute('aria-pressed'), 'true');
        assert.ok(await energyChart.locator('.classification-band').count() > 0);
        assert.equal(await energyChart.locator('.classification-band').first().getAttribute('height'), '190');
        await ranges.getByRole('button', { name: 'Tomorrow', exact: true }).click();
        assert.match(await energyChart.locator('.inspection').innerText(), /Oct 26/);
        await energyChart.getByRole('button', { name: 'Next interval', exact: true }).click();
        assert.match(await energyChart.locator('.inspection').innerText(), /12:15/);
        await energyChart.getByRole('button', { name: 'Back to now', exact: true }).click();
        await tariff.locator('.next-period.period-expensive').click();
        assert.match(await energyChart.locator('.inspection').innerText(), /High/);
        await energyChart.getByRole('checkbox', { name: 'Market price', exact: true }).check();
        assert.ok(await energyChart.locator('.source-line').getAttribute('d'));
        await energyChart.getByRole('checkbox', { name: 'Market price', exact: true }).uncheck();
        await ranges.getByRole('button', { name: '2 days', exact: true }).click();
        assert.equal(await energyChart.locator('.day-line').count(), 2);
        await energyChart.getByRole('button', { name: 'Back to now', exact: true }).click();
        await tariff.screenshot({ style: '.navbar { visibility: hidden !important; }', path: `${screenshotDir}/${width}-tariff.png` });
        const chart = page.locator('.chart-surface').first();
        await chart.waitFor();
        await chart.focus();
        await page.keyboard.press('ArrowRight');
        assert.match(await page.locator('.inspection').first().innerText(), /ct\/kWh/);
        const box = await chart.boundingBox();
        if (width === 360) await page.touchscreen.tap(box.x + box.width * .6, box.y + box.height / 2);
        else await page.mouse.move(box.x + box.width * .6, box.y + box.height / 2);
        assert.match(await page.locator('.inspection').first().innerText(), /Oct/);
        if (width === 360) {
            await chart.focus();
            await page.keyboard.press('Home');
            for (let i = 0; i < 8; i++) await page.keyboard.press('ArrowRight');
            assert.match(await energyChart.locator('.inspection').innerText(), /GMT\+2/);
            for (let i = 0; i < 4; i++) await page.keyboard.press('ArrowRight');
            assert.match(await energyChart.locator('.inspection').innerText(), /GMT\+1/);
            await page.keyboard.press('End');
            assert.equal(await energyChart.getByRole('button', { name: 'Next interval', exact: true }).isDisabled(), true);
        }
        for (const theme of ['light', 'dark']) {
            await page.evaluate(theme => document.documentElement.setAttribute('data-bs-theme', theme), theme);
            const contrasts = await energyChart.evaluate(el => {
                const luminance = value => {
                    const rgb = value.match(/[\d.]+/g).slice(0, 3).map(Number).map(v => v / 255).map(v => v <= .04045 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4);
                    return rgb[0] * .2126 + rgb[1] * .7152 + rgb[2] * .0722;
                };
                return ['cheap', 'normal', 'expensive'].map(category => {
                    const probe = document.createElement('span');
                    probe.className = `price-badge period-${category}`;
                    el.append(probe);
                    const colors = getComputedStyle(probe), a = luminance(colors.color), b = luminance(colors.backgroundColor);
                    probe.remove();
                    return (Math.max(a, b) + .05) / (Math.min(a, b) + .05);
                });
            });
            assert.ok(contrasts.every(ratio => ratio >= 4.5), `${theme} badge contrast ${contrasts}`);
        }
        await page.evaluate(() => document.documentElement.setAttribute('data-bs-theme', 'dark'));
        await page.screenshot({ path: `${screenshotDir}/${width}-dark.png`, fullPage: true });
        await tariff.screenshot({ style: '.navbar { visibility: hidden !important; }', path: `${screenshotDir}/${width}-tariff-dark.png` });
        assert.ok((await energyChart.getByRole('button', { name: 'Next interval', exact: true }).boundingBox()).width >= 44);
        assert.ok((await energyChart.getByRole('button', { name: 'Next interval', exact: true }).boundingBox()).height >= 44);
        scenario = 'rateLimit';
        await page.reload();
        await page.getByText('Provider rate limit reached.', { exact: false }).waitFor();
        assert.match(await page.locator('body').innerText(), /Existing data is retained/);
        assert.match(await page.locator('body').innerText(), /Retry in 600 seconds/);
        scenario = 'stale';
        await page.reload();
        await page.getByText('Data is outdated', { exact: false }).first().waitFor();
        // A retained curve remains inspectable, but current/summary numbers are unavailable.
        const solar = page.locator('.card').filter({ hasText: 'Remaining today' });
        assert.equal(await solar.locator('.fs-2').innerText(), '—');
        scenario = 'gap';
        await page.reload();
        await page.locator('.energy-chart').first().waitFor();
        const gaps = await page.locator('.primary-line').first().getAttribute('d');
        assert.ok((gaps.match(/M/g) || []).length >= 2);
        assert.match(await page.locator('.hems-energy.card').first().innerText(), /Current interval missing/);
        assert.match(await page.locator('.hems-energy.card').first().innerText(), /Not available for planning/);
        scenario = 'empty';
        await page.reload();
        await page.getByText('No data yet', { exact: true }).first().waitFor();
        const forecast = page.locator('.hems-energy.card').nth(1);
        assert.equal(await forecast.locator('.energy-chart').count(), 0);
        assert.equal(await forecast.locator('.fs-2').innerText(), '—');
        scenario = 'partial';
        await page.reload();
        await page.getByText('Partial horizon', { exact: true }).waitFor();
        assert.match(await forecast.innerText(), /Available for planning/);
        assert.equal(await forecast.locator('.fs-2').innerText(), '—');
        await page.getByText('PV forecast does not cover the full horizon.', { exact: true }).waitFor();
        scenario = 'todayOnly';
        await page.reload();
        await page.locator('.energy-chart').first().getByRole('button', { name: 'Tomorrow', exact: true }).click();
        await page.getByText('No data available for this day yet.', { exact: false }).waitFor();
        assert.equal(await page.locator('.energy-chart').first().getByRole('button', { name: 'Next interval', exact: true }).isDisabled(), true);
        scenario = 'blocked';
        await page.reload();
        await page.getByText('Waiting for fresh battery SoC.', { exact: true }).waitFor();
        assert.equal(await page.getByRole('link', { name: 'Review data and battery settings' }).count(), 1);
        // A lost feed request must mark retained values as unverified and suppress summaries.
        scenario = 'normal';
        await page.reload();
        await forecast.locator('.energy-chart').waitFor();
        scenario = 'feedOffline';
        await page.clock.runFor(15000);
        await page.getByText('Last known data · connection lost', { exact: true }).waitFor();
        assert.equal(await forecast.locator('.fs-2').innerText(), '—');
        scenario = 'offline';
        await page.getByRole('button', { name: 'Reload' }).click();
        await page.getByText('Status connection lost').first().waitFor();
        await context.close();
    }
    assert.deepEqual(errors, []);
    assert.equal(requests.some(r => r.path === '/api/hems/command'), false, 'Viewing must not submit device commands');
    console.log('Browser passed: four HEMS routes at 320/360/768/1440px, no horizontal overflow or runtime errors, canonical tariff/PV saves and Monitor mode, dark mode, day ranges, category jumps, source overlay, DST inspection, keyboard/touch controls, accessible badge contrast, rate limits, empty/stale/partial/gap/missing-day states, optimizer blockers, lost feed/status, and zero device commands.');
} finally { await browser.close(); }