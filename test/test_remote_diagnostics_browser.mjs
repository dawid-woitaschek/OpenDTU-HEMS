// Browser boundary checks with intercepted firmware APIs; no physical validation.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { createLiveFixture } from './fixtures/live.mjs';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const baseUrl = process.env.HEMS_BASE_URL || 'http://127.0.0.1:5173';
try {
    for (const locale of ['en', 'de']) for (const width of [320, 1440]) {
        const context = await browser.newContext({ viewport: { width, height: 1000 }, locale });
        const page = await context.newPage(), errors = [], posts = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.addInitScript(locale => { localStorage.setItem('locale', locale); localStorage.setItem('user', JSON.stringify({ authdata: btoa('fixture:fixture') })); }, locale);
        await page.routeWebSocket(/\/livedata$/, () => {});
        let config = { enabled: false, endpoint: '', interval_minutes: 60, quarter_hour: false, token_set: false };
        let send = 0;
        await page.route('**/api/**', async route => {
            const path = new URL(route.request().url()).pathname;
            if (route.request().method() === 'POST') {
                assert.equal(route.request().headers().authorization, 'Basic ' + btoa('fixture:fixture'));
                if (path.endsWith('/send')) { send++; return route.fulfill({ status: 202, json: { scheduled: true } }); }
                const form = await new Response(route.request().postDataBuffer(), { headers: { 'content-type': route.request().headers()['content-type'] } }).formData();
                const payload = JSON.parse(form.get('data')); posts.push(payload);
                config = { ...config, ...payload, token_set: payload.token !== undefined ? !!payload.token : config.token_set };
                delete config.token;
                return route.fulfill({ json: { success: true } });
            }
            return route.fulfill({ json: path.endsWith('/i18n/languages') ? [] : path === '/api/livedata/status' ? createLiveFixture()
                : path === '/api/remote-diagnostics/config' ? config
                : path === '/api/remote-diagnostics/status' ? { enabled: config.enabled, token_set: config.token_set, busy: false, last_error: null, runtime_reason: config.enabled ? null : 'disabled', pending_samples: 12, dropped_samples: 0, telemetry_sequence: 1, last_acknowledged_checkpoint_sequence: 0 }
                : {} });
        });
        await page.goto(baseUrl + '/system/remote-diagnostics');
        await page.locator('#remote-interval').waitFor();
        assert.equal(await page.locator('#remote-interval').inputValue(), '60');
        assert.equal(await page.locator('#remote-enabled').isChecked(), false);
        assert.equal(await page.locator('#remote-quarter-hour').isChecked(), false);
        assert.equal(await page.locator('#remote-token').getAttribute('type'), 'password');
        assert.equal(await page.locator('form button').count(), 1);
        assert.equal(await page.locator('#main-navigation a[href="/system/remote-diagnostics"].active').count(), 1);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
        await page.locator('#remote-enabled').check();
        await page.locator('#remote-quarter-hour').check();
        await page.locator('#remote-endpoint').fill('https://collector.example.org/api/v1/telemetry');
        await page.locator('#remote-token').fill('hems_browser_secret');
        await page.locator('form button').click();
        await page.waitForFunction(() => document.querySelector('#remote-token').value === '');
        assert.equal(posts[0].token, 'hems_browser_secret');
        assert.equal(posts[0].quarter_hour, true);
        await page.getByRole('button', { name: locale === 'en' ? 'Send now' : 'Jetzt senden', exact: true }).click();
        await page.waitForFunction(() => document.querySelector('[role="status"]')?.textContent.includes('vorgemerkt') || document.querySelector('[role="status"]')?.textContent.includes('scheduled'));
        assert.equal(send, 1);
        await page.locator('form button').click();
        await page.waitForTimeout(100);
        assert.equal(posts[1].token, undefined);
        assert.equal(await page.locator('#remote-token').inputValue(), '');
        assert.ok(!(await page.locator('body').innerText()).includes('hems_browser_secret'));
        assert.deepEqual(errors, []);
        await context.close();
        console.log(`remote diagnostics browser ${locale} ${width}px passed`);
    }
} finally { await browser.close(); }
