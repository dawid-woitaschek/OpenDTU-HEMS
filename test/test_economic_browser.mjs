// Hardware-free product flow; the browser can never reach actual providers/devices.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { createHemsFixture } from './fixtures/hems.mjs';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const errors = [];
try {
    for (const width of [320, 360, 768, 1440]) {
        const fixture = createHemsFixture(), posts = [];
        fixture.status.state = fixture.optimizer.dispatch.global_mode = 'Monitor';
        const context = await browser.newContext({ viewport: { width, height: 1000 } });
        const page = await context.newPage(); page.on('pageerror', e => errors.push(e.message));
        await page.addInitScript(() => localStorage.setItem('locale', 'en'));
        await page.route('**/api/**', async route => {
            const request = route.request(), path = new URL(request.url()).pathname;
            if (request.method() === 'POST') {
                const form = await new Response(request.postDataBuffer(), { headers: { 'content-type': request.headers()['content-type'] } }).formData();
                const data = JSON.parse(form.get('data')); posts.push({ path, data });
                if (path === '/api/hems/optimizer/config') {
                    fixture.optimizer.config = data; fixture.optimizer.dispatch.enabled = data.dispatch_enabled;
                    fixture.optimizer.dispatch.state = data.dispatch_enabled ? 'waiting_active' : 'disabled';
                }
                if (path === '/api/hems/command') {
                    assert.equal(data.target, 'storage'); fixture.optimizer.dispatch.paused_manual = true;
                    fixture.optimizer.dispatch.state = 'paused_manual';
                }
                if (path === '/api/hems/optimizer/resume') {
                    fixture.optimizer.dispatch.paused_manual = false; fixture.optimizer.dispatch.state = 'waiting_active';
                }
                return route.fulfill({ json: { ok: true, type: 'success', accepted: true, command_id: 3 } });
            }
            const body = path.endsWith('/i18n/languages') ? [] : path === '/api/hems/status' ? fixture.status
                : path === '/api/hems/config' ? fixture.config : path === '/api/hems/profiles' ? fixture.profiles
                : path === '/api/hems/profiles/evse-binding' ? fixture.binding : path.startsWith('/api/hems/optimizer') ? fixture.optimizer
                : path.startsWith('/api/hems/energy/tariff') ? fixture.tariff : path.startsWith('/api/hems/energy/forecast') ? fixture.forecast : {};
            return route.fulfill({ json: body });
        });
        await page.goto('http://127.0.0.1:5173/hems/settings');
        await page.locator('#optimizer-dispatch-enabled').waitFor();
        assert.equal(await page.locator('#optimizer-dispatch-enabled').isChecked(), false);
        assert.match(await page.locator('body').innerText(), /Shadow only · dispatch disabled/);
        assert.equal(await page.locator('#energy-management-enabled').isChecked(), false);
        await page.getByRole('button', { name: 'Apply household reference goals', exact: true }).click();
        assert.equal(await page.locator('#energy-management-enabled').isChecked(), false, 'Preset does not activate control');
        assert.equal(await page.locator('#energy-load-source').inputValue(), 'true');
        assert.equal(await page.locator('#energy-battery_export').isChecked(), false);
        assert.equal(await page.locator('#energy-ev_protection').isChecked(), true);
        const energySettings = page.locator('fieldset').filter({ has: page.locator('#energy-management-enabled') });
        await energySettings.locator('details > summary').click();
        await page.locator('#energy-pv_ac_limit_w').fill('3000');
        await page.locator('#energy-net-export').fill('800');
        await page.locator('#energy-management-enabled').check();
        await page.locator('#optimizer-dispatch-enabled').check();
        await page.locator('form').filter({ has: page.locator('#optimizer-enabled') }).getByRole('button', { name: 'Save', exact: true }).click();
        await page.getByText('Waiting for Active HEMS mode', { exact: true }).first().waitFor();
        const saved = posts.find(p => p.path.endsWith('/optimizer/config')).data;
        assert.equal(saved.dispatch_enabled, true);
        assert.equal(saved.energy_management.enabled, true);
        assert.equal(saved.energy_management.max_export_w, 800);
        assert.equal(saved.energy_management.automatic_load, true);
        assert.equal(saved.energy_management.battery_export, false);
        assert.equal(saved.policies.target_enabled, false);
        assert.equal(posts.some(p => p.path === '/api/hems/command'), false, 'Opt-in in Monitor sends no device command');
        fixture.optimizer.dispatch.global_mode = fixture.status.state = 'Active';
        fixture.optimizer.dispatch.state = 'waiting_permission'; fixture.optimizer.dispatch.storage_permission = false;
        await page.reload(); await page.getByText('Waiting for Storage HEMS Control permission', { exact: true }).first().waitFor();
        fixture.optimizer.dispatch.storage_permission = true; fixture.optimizer.dispatch.state = 'blocked_protection';
        await page.reload(); await page.getByText('Blocked by EV Protection', { exact: true }).first().waitFor();
        fixture.optimizer.dispatch.state = 'charging';
        await page.goto('http://127.0.0.1:5173/hems/devices');
        const manualDetails = page.locator('details').filter({ has: page.getByText('Manual controls', { exact: true }) }).first();
        if (await manualDetails.count()) await manualDetails.locator('summary').first().click();
        // The devices view defers mounting the manual panel until its disclosure opens.
        const input = page.locator('#storage-set_charge_power_w');
        if (!(await input.count())) {
            await page.getByText('Manual controls', { exact: true }).first().click();
        }
        await input.waitFor(); await input.fill('500');
        await input.locator('..').getByRole('button', { name: 'Apply', exact: true }).click();
        await page.getByText('Economic automation paused by manual control', { exact: false }).first().waitFor();
        await page.goto('http://127.0.0.1:5173/hems');
        await page.getByRole('button', { name: 'Resume economic automation', exact: true }).waitFor();
        await page.reload(); await page.getByRole('button', { name: 'Resume economic automation', exact: true }).click();
        await page.getByText('Waiting for Active HEMS mode', { exact: true }).first().waitFor();
        assert.equal(posts.filter(p => p.path === '/api/hems/command').length, 1, 'Only the explicitly requested manual command is sent');
        assert.equal(posts.filter(p => p.path.endsWith('/optimizer/resume')).length, 1);
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, `dispatch UI overflow at ${width}`);
        await context.close();
    }
    assert.deepEqual(errors, []);
    console.log('Economic browser opt-in, Monitor isolation, blocked states, manual pause, durable presentation, explicit resume and 320/360/768/1440px layouts passed');
} finally { await browser.close(); }
