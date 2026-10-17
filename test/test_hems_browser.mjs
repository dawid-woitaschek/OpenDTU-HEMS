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
    for (const width of [360, 768, 1440]) {
        const context = await browser.newContext({ viewport: { width, height: 1000 }, timezoneId: 'Europe/Berlin', locale: 'en', hasTouch: width === 360 });
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
                if (path === '/api/hems/energy/tariff/config') fixture.tariff.config = payload;
                if (path === '/api/hems/energy/forecast/config') fixture.forecast.config = payload;
                return route.fulfill({ json: { ok: true, type: 'success' } });
            }
            if (scenario === 'rateLimit' && path.includes('/energy/forecast')) {
                body.last_error = 'provider_rate_limited';
                body.manual_refresh = { allowed: false, reason: 'rate_limited', retry_after_seconds: 600 };
            }
            if (scenario === 'stale' && path.includes('/energy/forecast')) { body.fresh = false; body.state = 'stale'; }
            if (scenario === 'gap' && path.includes('/energy/tariff')) {
                body.intervals = body.intervals.filter(i => i.start !== fixtureNow && i.start !== fixtureNow + 900);
            }
            return route.fulfill({ json: body });
        });
        for (const path of ['/hems', '/hems/settings', '/hems/devices', '/hems/diagnostics']) {
            await page.goto('http://127.0.0.1:5173' + path);
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
        const chart = page.locator('.chart-surface').first();
        await chart.waitFor();
        await chart.focus();
        await page.keyboard.press('ArrowRight');
        assert.match(await page.locator('.inspection').first().innerText(), /ct\/kWh/);
        const box = await chart.boundingBox();
        if (width === 360) await page.touchscreen.tap(box.x + box.width * .6, box.y + box.height / 2);
        else await page.mouse.move(box.x + box.width * .6, box.y + box.height / 2);
        assert.match(await page.locator('.inspection').first().innerText(), /Oct/);
        await page.evaluate(() => document.documentElement.setAttribute('data-bs-theme', 'dark'));
        await page.screenshot({ path: `${screenshotDir}/${width}-dark.png`, fullPage: true });
        scenario = 'rateLimit';
        await page.reload();
        await page.getByText('Provider rate limit reached.', { exact: false }).waitFor();
        assert.match(await page.locator('body').innerText(), /Existing data is retained/);
        assert.match(await page.locator('body').innerText(), /Retry in 600 seconds/);
        scenario = 'stale';
        await page.reload();
        await page.getByText('Data is outdated', { exact: false }).waitFor();
        // A retained curve remains inspectable, but current/summary numbers are unavailable.
        const solar = page.locator('.card').filter({ hasText: 'Remaining today' });
        assert.equal(await solar.locator('.fs-2').innerText(), '—');
        scenario = 'gap';
        await page.reload();
        await page.locator('.energy-chart').first().waitFor();
        const gaps = await page.locator('.primary-line').first().getAttribute('d');
        assert.ok((gaps.match(/M/g) || []).length >= 2);
        scenario = 'offline';
        await page.getByRole('button', { name: 'Reload' }).click();
        await page.getByText('Status connection lost').first().waitFor();
        await context.close();
    }
    assert.deepEqual(errors, []);
    assert.equal(requests.some(r => r.path === '/api/hems/command'), false, 'Viewing must not submit device commands');
    console.log('Browser passed: four HEMS routes at 360/768/1440px, no horizontal overflow or runtime errors, canonical tariff/PV saves and Monitor mode, dark mode, keyboard/touch inspection, rate limits, stale data, gaps, lost status, and zero device commands.');
} finally { await browser.close(); }
