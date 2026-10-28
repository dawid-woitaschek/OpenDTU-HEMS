// Human-facing IA, help, locale and live-state regressions; every API is intercepted.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdir } from 'node:fs/promises';
import { createHemsFixture, fixtureNow } from './fixtures/hems.mjs';
import { createLiveFixture, inverterUiResponse } from './fixtures/live.mjs';
import { readFileSync } from 'node:fs';
import { systemUiResponse } from './fixtures/system-ui.mjs';
const require = createRequire(import.meta.url);
const baseUrl = process.env.HEMS_BASE_URL || 'http://127.0.0.1:5173';
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const screenshots = process.env.HEMS_SCREENSHOTS || 'work/redesign-screenshots';
await mkdir(screenshots, { recursive: true });
const errors = [];
async function assertHelpPlacement(page) {
    assert.deepEqual(await page.locator('.help-hint').evaluateAll(hints => hints.filter(h => {
        if (h.querySelector('.help-trigger-plain')) return false;
        return h.closest('label') || !h.closest('.label-with-help, h2, h3, legend, summary, .card-header');
    }).map(h => h.outerHTML)), [], 'Every help button is attached to a label or heading, outside control labels');
}
async function currentMeasurements(card) {
    return (await card.locator('dl dd').allTextContents()).slice(0, 3).map(text => text.trim().replace(/\s+/g, ' '));
}
async function assertMeasurementsUnavailable(card) {
    assert.deepEqual(await currentMeasurements(card), ['—', '—', '—'], 'Unverified output, limit and temperature must not look like current numeric measurements');
}
async function assertHeaderBranding(page) {
    const brand = page.locator('header a.navbar-brand');
    const logo = brand.locator('img.brand-logo');
    assert.match(await logo.getAttribute('src'), /^data:image\/svg\+xml[;,]/, 'Logo travels inside embedded app.js; firmware has no arbitrary asset route');
    await logo.evaluate(img => img.decode());
    assert.equal(await brand.getAttribute('href'), '/');
    assert.equal(await brand.innerText(), 'OpenDTU-HEMS');
    assert.equal(await brand.locator('svg').count(), 0, 'The house icon is replaced');
    assert.equal(await logo.getAttribute('alt'), '', 'Adjacent wordmark identifies the product');
    assert.equal(await brand.getByRole('img').count(), 0, 'Decorative logo does not duplicate the accessible name');
    assert.equal(await logo.evaluate(img => img.naturalWidth === 1254 && img.naturalHeight === 1254 && getComputedStyle(img).objectFit === 'contain'), true);
    const bounds = await logo.boundingBox(), text = await brand.locator(':scope > span').boundingBox();
    const connection = await page.locator('.live-connection-dot').boundingBox();
    assert.ok(Math.abs(bounds.width - bounds.height) < 1 && bounds.height === 36, 'Logo preserves its square proportions at header size');
    assert.ok(bounds.x + bounds.width <= text.x && connection.x >= text.x + text.width, 'Logo, wordmark and connection indicator keep their order');
}
try {
    for (const locale of (process.env.REDESIGN_LOCALES?.split(',') || ['en', 'de'])) for (const width of (process.env.REDESIGN_WIDTHS?.split(',').map(Number) || [320, 768, 1440])) {
        const fixture = createHemsFixture(), live = createLiveFixture(), posts = [], sockets = [], requests = [];
        const home = JSON.parse(readFileSync(new URL(`../webapp/src/locales/${locale}.json`, import.meta.url))).home;
        // Make unrelated devices genuinely enabled, so the old catch-all would mount them.
        live.solarcharger = { enabled: true, power: { v: 3210.12, u: 'W', d: 2 } };
        live.battery = { enabled: true, soc: { v: 78.91, u: '%', d: 2 } };
        live.gridcharger = { enabled: true, Power: { v: 1234.56, u: 'W', d: 2 } };
        live.inverters[0].DC = [{ name: { v: 0, u: 'PV input', d: 0 }, Power: { v: 123, u: 'W', d: 0 } }];
        const deviceSockets = [];
        let offline = false, closeConnections = false, unauthorized = false;
        const context = await browser.newContext({ viewport: { width, height: 1000 }, locale, timezoneId: 'Europe/Berlin', hasTouch: width === 320 });
        const page = await context.newPage();
        page.on('pageerror', error => { const failure = { path: new URL(page.url()).pathname, message: error.message }; errors.push(failure); console.error(failure); });
        await page.clock.install({ time: fixtureNow * 1000 });
        await page.addInitScript(locale => { if (!localStorage.getItem('locale')) localStorage.setItem('locale', locale); localStorage.setItem('user', JSON.stringify({ authdata: btoa('fixture:fixture') })); }, locale);
        await page.routeWebSocket(/\/livedata$/, socket => { sockets.push(socket); if (closeConnections) socket.close(); });
        for (const channel of ['batterylivedata', 'solarchargerlivedata', 'gridchargerlivedata', 'console']) {
            await page.routeWebSocket(new RegExp('/' + channel + '$'), () => { deviceSockets.push(channel); });
        }
        await page.route('https://raw.githubusercontent.com/**', route => route.fulfill({ status: 404, body: '' }));
        await page.route('**/api/**', async route => {
            const path = new URL(route.request().url()).pathname; requests.push(path);
            if (unauthorized && path === '/api/livedata/status') return route.fulfill({ status: 401, json: {} });
            if (offline && ['/api/hems/status', '/api/livedata/status'].includes(path)) return route.abort();
            if (route.request().method() === 'POST') {
                const form = await new Response(route.request().postDataBuffer(), { headers: { 'content-type': route.request().headers()['content-type'] } }).formData();
                const payload = JSON.parse(form.get('data')); posts.push({ path, payload });
                if (path === '/api/hems/optimizer/config') fixture.optimizer.config = payload;
                return route.fulfill({ json: { ok: true, type: 'success' } });
            }
            const body = path.endsWith('/i18n/languages') ? [{ code: 'fr', name: 'Français' }, { code: 'es', name: 'Español' }]
                : path === '/api/hems/status' ? fixture.status : path === '/api/livedata/status' ? live
                : path === '/api/hems/config' ? fixture.config : path === '/api/hems/profiles' ? fixture.profiles
                : path === '/api/hems/profiles/evse-binding' ? fixture.binding
                : path.startsWith('/api/hems/optimizer') ? fixture.optimizer
                : path.startsWith('/api/hems/energy/tariff') ? fixture.tariff
                : path.startsWith('/api/hems/energy/forecast') ? fixture.forecast
                : inverterUiResponse(path) ?? systemUiResponse(path) ?? (path.endsWith('/config') ? { enabled: false } : {});
            return route.fulfill({ json: body });
        });
        const primary = ['/', '/plant/inverters/live', '/plant/inverters', '/plant/inverters/configuration', '/plant/solar-charger', '/plant/battery', '/plant/wallbox', '/plant/power-meter', '/energy/planning', '/energy/power-control', '/energy/integrations', '/energy/diagnostics'];
        const system = ['/settings/network', '/info/network', '/settings/mqtt', '/info/mqtt', '/settings/ntp', '/info/ntp', '/settings/dtu', '/settings/device', '/settings/security', '/settings/logging', '/info/console', '/settings/config', '/firmware/upgrade', '/info/system', '/maintenance/reboot', '/about'];
        for (const path of [...primary, ...system]) {
            const beforeRequests = requests.length, beforeDeviceSockets = deviceSockets.length;
            await page.goto(baseUrl + path);
            await page.locator('h1').waitFor();
            await page.waitForTimeout(100);
            assert.equal(await page.locator('[role="main"]').count(), 1, `${path} has one page landmark`);
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, `${locale} ${path} overflows at ${width}`);
            assert.doesNotMatch(await page.locator('body').innerText(), /redesign\.[a-zA-Z]+/);
            assert.deepEqual(await page.locator('header select option').evaluateAll(options => options.map(o => o.value)), ['en', 'de']);
            assert.equal(await page.locator('h1 .onlineMarker, h1 .offlineMarker').count(), 0);
            assert.equal(await page.locator('nav.nav-pills').count(), 0);
            await assertHelpPlacement(page);
            assert.equal(await page.locator('#main-navigation .navigation-group').first().locator('a').count(), 1, 'Start contains only Overview');
            assert.equal(await page.locator('#main-navigation a[href="/live"]').count(), 0);
            if (!['/', '/about'].includes(path)) assert.ok(await page.locator('#main-navigation .active').count() > 0, `${path}: sidebar locates the page`);
            if (path === '/') {
                await assertHeaderBranding(page);
                for (const theme of ['dark', 'light']) {
                    await page.locator('#bd-theme').click();
                    await page.locator(`[data-bs-theme-value="${theme}"]`).click();
                    await assertHeaderBranding(page);
                    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, `Header fits in ${theme} mode`);
                    await page.locator('header').screenshot({ path: `${screenshots}/${locale}-${width}-header-${theme}.png` });
                }
                assert.equal(await page.locator('nav a[href^="#overview-"]').count(), 0);
                assert.equal(await page.getByText(locale === 'en' ? 'Import price now' : 'Bezugspreis jetzt', { exact: true }).count(), 1);
                assert.equal(await page.locator('.energy-drilldown[open]').count(), 0);
                const help = page.locator('#overview-pv .help-trigger');
                await help.focus();
                await page.getByRole('tooltip').getByText(locale === 'de' ? '1.000 W' : '1,000 W', { exact: true }).waitFor();
                assert.match(await page.getByRole('tooltip').innerText(), /East: 75.*600 W/s);
                assert.match(await page.getByRole('tooltip').innerText(), /West: 50.*400 W/s);
                const box = await page.getByRole('tooltip').boundingBox();
                assert.ok(box.x >= 0 && box.x + box.width <= width && box.y >= 0);
                await page.keyboard.press('Escape'); assert.equal(await page.getByRole('tooltip').count(), 0);
                if (width === 320) { await help.tap(); await page.getByRole('tooltip').waitFor(); await help.tap(); }
                else { await help.click(); await page.getByRole('tooltip').waitFor(); await help.click(); }
                assert.equal(await page.getByRole('tooltip').count(), 0, 'Second click/tap closes');
                await help.click(); await page.locator('h1').click(); assert.equal(await page.getByRole('tooltip').count(), 0, 'Outside click closes');
                await page.screenshot({ path: `${screenshots}/${locale}-${width}-overview.png`, fullPage: true });
            }
            if (path === '/plant/inverters') {
                assert.equal(await page.locator('.inverter-summary').count(), 2);
                assert.equal(await page.locator('#v-pills-tab, #gridProfileView, .radio-stats').count(), 0, 'Overview does not duplicate technical Live Data');
                assert.equal(await page.locator('#main-navigation .nav-parent').filter({ hasText: locale === 'de' ? 'Wechselrichter' : 'Inverters' }).locator('..').getAttribute('class'), 'active-parent');
                await page.screenshot({ path: `${screenshots}/${locale}-${width}-inverters.png`, fullPage: true });
                assert.equal(await page.locator('[role="main"] a[href="/live"]').count(), 0);
                assert.ok(await page.locator('[role="main"] a[href^="/plant/inverters/live"]').count() >= 3);
            }
            if (path === '/plant/inverters/live') {
                assert.equal(await page.locator('.nav-children a.active').getAttribute('href'), path);
                assert.equal(await page.locator('.active-parent > a.nav-parent[href="/plant/inverters"]').count(), 1);
                assert.equal(await page.locator('#v-pills-tab button').count(), 2);
                const panel = page.locator('#v-pills-100000000001');
                await panel.waitFor({ state: 'visible' });
                await panel.getByText('PV input', { exact: true }).waitFor();
                assert.match(await panel.innerText(), /600 W/);
                assert.equal(await page.locator('[role="main"] #v-pills-tabContent').count(), 1, 'No appended device live views');
                assert.equal(await page.locator('.inverter-summary').count(), 0);
                assert.doesNotMatch(await page.locator('[role="main"]').innerText(), /3[,.]?210[,.]12|78[,.]91|1[,.]?234[,.]56/, 'No cross-device summary telemetry');
                assert.deepEqual(requests.slice(beforeRequests).filter(p => /\/api\/(battery|solarcharger|gridcharger)livedata\//.test(p)), [], 'Unrelated live views must not mount/fetch');
                assert.equal(deviceSockets.length, beforeDeviceSockets, 'No device-specific live views open sockets');
                const action = key => panel.locator(`button[title=${JSON.stringify(home[key])}], button[data-bs-original-title=${JSON.stringify(home[key])}]`);
                for (const [key, id, endpoint] of [['ShowInverterInfo', 'devInfoView', '/api/devinfo/status'], ['ShowGridProfile', 'gridProfileView', '/api/gridprofile/status'], ['ShowEventlog', 'eventView', '/api/eventlog/status']]) {
                    const requested = page.waitForRequest(r => new URL(r.url()).pathname === endpoint && new URL(r.url()).searchParams.get('inv') === '100000000001');
                    await action(key).click(); await requested;
                    const modal = page.locator('#' + id); await modal.waitFor({ state: 'visible' });
                    await modal.locator('.modal-body .spinner-border').waitFor({ state: 'hidden' });
                    await modal.locator('.btn-close').click(); await modal.waitFor({ state: 'hidden' });
                }
                await action('ShowSetInverterLimit').click();
                await page.locator('#inputTargetLimit').fill('55');
                await page.locator('#limitSettingView').getByRole('button', { name: home.SetNonPersistent, exact: true }).click();
                await page.locator('#limitSettingView').waitFor({ state: 'hidden' });
                assert.deepEqual(posts.findLast(p => p.path === '/api/limit/config').payload, { serial: '100000000001', limit_value: 55, limit_type: 1 });
                await action('TurnOnOff').click();
                await page.locator('#powerSettingView').getByRole('button', { name: home.TurnOff, exact: true }).click();
                await page.locator('#powerSettingView').waitFor({ state: 'hidden' });
                assert.deepEqual(posts.findLast(p => p.path === '/api/power/config').payload, { serial: '100000000001', power: false });
            }
            if (path === '/plant/inverters/configuration') assert.equal(await page.locator('.nav-children a.active').getAttribute('href'), path);
            if (path === '/plant/solar-charger') assert.equal(await page.locator('.overview-value').count(), 0, 'No aggregate PV value on solar-charger page');
            if (path === '/plant/battery') { assert.equal(await page.locator('#hems-evse-profile').count(), 0); assert.equal(await page.locator('#hems-storage-profile').count(), 1); }
            if (path === '/plant/wallbox') { assert.equal(await page.locator('#hems-storage-profile').count(), 0); assert.equal(await page.locator('#hems-evse-profile').count(), 1); }
            if (path === '/energy/integrations') assert.equal(await page.locator('#hems-storage-profile, #hems-evse-profile').count(), 0);
            if (path === '/energy/planning') {
                assert.ok(await page.locator('.pv-plane:not([open])').count() > 0, 'Disabled PV planes keep unused fields collapsed');
                await page.locator('.pv-plane:not([open]) > summary').first().click();
                await assertHelpPlacement(page);
                await page.locator('.pv-plane[open] > summary').filter({ hasText: locale === 'de' ? 'Deaktiviert' : 'Disabled' }).first().click();
                await page.locator('#optimizer-grid-charging').selectOption('false');
                await page.locator('#optimizer-target-enabled').check();
                await page.locator('#optimizer-target-soc').fill('70');
                await page.locator('#optimizer-target-deadline').fill('2026-10-26T07:00');
                await page.locator('form').filter({ has: page.locator('#optimizer-enabled') }).locator('button.btn-primary').click();
                await page.waitForTimeout(100);
                const saved = posts.findLast(p => p.path === '/api/hems/optimizer/config');
                assert.equal(saved.payload.policies.grid_charging_allowed, false);
                assert.equal(saved.payload.policies.target_soc_pct, 70);
                assert.equal(saved.payload.policies.target_deadline, Date.parse('2026-10-26T06:00:00Z') / 1000);
                assert.equal(saved.payload.dispatch_enabled, false);
                await page.evaluate(() => window.scrollTo(0, 0));
                await page.screenshot({ path: `${screenshots}/${locale}-${width}-planning.png`, fullPage: true });
            }
        }
        assert.deepEqual(errors, [], 'Primary route audit has no runtime errors');
        await page.goto(baseUrl + '/live?inverter=100000000002');
        await page.waitForURL('**/plant/inverters/live?inverter=100000000002');
        await page.locator('#v-pills-100000000002.active').waitFor({ state: 'visible' });
        assert.equal(await page.locator('.nav-children a.active').getAttribute('href'), '/plant/inverters/live');
        // SPA navigation shares exactly one socket and merges inverter frames without losing peers.
        await page.goto(baseUrl + '/plant/inverters');
        await page.locator('.live-connection-dot[data-state="connected"]').waitFor({ state: 'attached' });
        const initialSockets = sockets.length;
        if (width < 992) await page.locator('[aria-controls="main-navigation"]').click();
        await page.locator('#main-navigation .nav-children a[href="/plant/inverters/live"]').click();
        await page.locator('#v-pills-100000000001.active').waitFor({ state: 'visible' });
        assert.equal(await page.locator('.nav-children a.active').getAttribute('href'), '/plant/inverters/live');
        assert.equal(sockets.length, initialSockets, 'Inverter technical Live Data consumes the app-owned socket');
        if (width < 992) await page.locator('[aria-controls="main-navigation"]').click();
        await page.locator('#main-navigation a[href="/plant/inverters/configuration"]').click();
        assert.equal(await page.locator('.nav-children a.active').getAttribute('href'), '/plant/inverters/configuration');
        if (width < 992) await page.locator('[aria-controls="main-navigation"]').click();
        await page.locator('#main-navigation .nav-parent[href="/plant/inverters"]').click();
        assert.equal(sockets.length, initialSockets, 'Routed subpages do not open more live sockets');
        const east = structuredClone(live.inverters[0]); east.AC[0].Power.v = 425;
        sockets.at(-1).send(JSON.stringify({ inverters: [east] }));
        await page.locator('[data-inverter="100000000001"]').getByText('425 W', { exact: true }).waitFor();
        assert.equal(await page.locator('.inverter-summary').count(), 2);
        const eastCard = page.locator('[data-inverter="100000000001"]');
        east.AC[0].Power.v = 0; east.INV[0].Temperature.v = 0; east.limit_relative = 0; east.limit_absolute = 0;
        sockets.at(-1).send(JSON.stringify({ inverters: [east] }));
        await eastCard.getByText('0 °C', { exact: true }).waitFor();
        assert.deepEqual(await currentMeasurements(eastCard), ['0 W', '0 % · 0 W', '0 °C'], 'Verified, fresh zero values remain valid measurements');
        east.reachable = false;
        sockets.at(-1).send(JSON.stringify({ inverters: [east] }));
        await eastCard.getByText(locale === 'de' ? 'Nicht erreichbar' : 'Unreachable', { exact: true }).waitFor();
        await assertMeasurementsUnavailable(eastCard);
        assert.match(await eastCard.innerText(), locale === 'de' ? /Messwerte sind nicht bestätigt/ : /measurements cannot be verified/);
        // Nonzero retained readings are also unavailable when stale or polling is disabled.
        Object.assign(east, structuredClone(live.inverters[0])); east.data_age_ms = 61000;
        sockets.at(-1).send(JSON.stringify({ inverters: [east] }));
        await eastCard.getByText(locale === 'de' ? 'Zuletzt bekannte Daten' : 'Last-known data', { exact: true }).waitFor();
        await assertMeasurementsUnavailable(eastCard);
        east.poll_enabled = false;
        sockets.at(-1).send(JSON.stringify({ inverters: [east] }));
        await eastCard.getByText(locale === 'de' ? 'Abfrage deaktiviert' : 'Polling disabled', { exact: true }).waitFor();
        await assertMeasurementsUnavailable(eastCard);
        Object.assign(east, structuredClone(live.inverters[0]));
        sockets.at(-1).send(JSON.stringify({ inverters: [east] }));
        await eastCard.getByText('400 W', { exact: true }).waitFor();
        closeConnections = true; sockets.at(-1).close();
        await page.locator('.live-connection-dot[data-state="disconnected"]').waitFor({ state: 'attached' });
        await assertMeasurementsUnavailable(eastCard);
        await assertMeasurementsUnavailable(page.locator('[data-inverter="100000000002"]'));
        for (const [oldPath, canonical] of [['/live', '/plant/inverters/live'], ['/hems', '/'], ['/info/hems', '/'], ['/hems/devices', '/energy/integrations'], ['/settings/battery', '/plant/battery'], ['/plant/solar', '/plant/solar-charger'], ['/settings/solarcharger', '/plant/solar-charger'], ['/settings/powerlimiter', '/energy/power-control'], ['/plant/inverters/power-limit', '/energy/power-control']]) {
            await page.goto(baseUrl + oldPath); await page.waitForURL('**' + canonical);
        }
        await page.evaluate(() => localStorage.setItem('locale', 'fr'));
        await page.goto(baseUrl + '/');
        assert.equal(await page.evaluate(() => localStorage.getItem('locale')), 'en');
        assert.deepEqual(await page.locator('header select option').evaluateAll(options => options.map(o => o.value)), ['en', 'de']);
        assert.equal(requests.includes('/api/i18n/languages'), false, 'Remote language packs cannot extend the supported UI set');
        offline = true; await page.goto(baseUrl + '/'); await page.locator('.alert-warning').first().waitFor();
        assert.equal(await page.locator('#overview-pv').count(), 0);
        assert.equal(posts.some(p => p.path === '/api/hems/command'), false);
        offline = false; unauthorized = true; requests.length = 0;
        await page.goto(baseUrl + '/login');
        await page.waitForTimeout(600);
        assert.ok(requests.filter(p => p === '/api/livedata/status').length <= 2, '401/logout does not restart an unauthenticated connection in a loop');
        await context.close();
        console.log(`Redesign UX audit passed: ${locale}, ${width}px`);
    }
    assert.deepEqual(errors, []);
    console.log('Redesign IA, one live socket, EN/DE-only locales, tooltip placement/touch/outside/Escape, current versus last-known data and planning payloads passed');
} finally { await browser.close(); }
