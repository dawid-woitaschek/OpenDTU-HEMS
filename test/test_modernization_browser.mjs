// Rendered regressions using real Vue components and bounded mocked providers.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import { createHemsFixture } from './fixtures/hems.mjs';
process.chdir(new URL('..', import.meta.url).pathname);
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || '../work/browser/node_modules/playwright');
const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox'] });
const catalog = JSON.parse(readFileSync('work/profile-catalog/catalog.json', 'utf8'));
const manifest = { schema: 'opendtu-hems-update/v1', repository: 'dawid-woitaschek/OpenDTU-HEMS', channel: 'pr-25', branch: 'feature/hems-economic-dispatch', commit: '1'.repeat(40), source_commit: '2'.repeat(40), published_at: '2026-10-19T12:00:00Z', builds: { generic_esp32s3_usb: { file: 'opendtu-hems-generic_esp32s3_usb.bin', size: 100, md5: 'a'.repeat(32), sha256: 'b'.repeat(64) } } };
const baseUrl = process.env.HEMS_BASE_URL || 'http://127.0.0.1:5173';
const errors = [];
try {
    for (const width of [360, 1440]) {
        const context = await browser.newContext({ viewport: { width, height: 1000 }, locale: 'en' });
        const page = await context.newPage(); page.setDefaultTimeout(15000);
        page.on('pageerror', error => errors.push(error.message));
        await page.addInitScript(() => { localStorage.setItem('locale', 'en'); localStorage.setItem('user', JSON.stringify({ authdata: btoa('test:test') })); });
        const fixture = createHemsFixture(), installed = [], posts = [], remote = [];
        let catalogFailure = '', profileFailure = '', backendFailure = false, late;
        await page.route('https://raw.githubusercontent.com/**', async route => {
            const url = route.request().url(); remote.push(url);
            assert.equal(route.request().headers().authorization, undefined);
            if (url.includes('/firmware-channel-')) {
                const channel = url.match(/firmware-channel-([^/]+)/)[1];
                if (channel === 'deinemutter') return route.fulfill({ status: 404, body: '' });
                if (channel === 'offline') return route.abort();
                if (channel === 'pr-1') return route.fulfill({ json: { ...manifest, channel, commit: '3'.repeat(40), published_at: '2025-01-01T00:00:00Z' } });
                if (channel === 'slow') { late = route; return; }
                return route.fulfill({ json: { ...manifest, channel, commit: channel === 'pr-25' ? manifest.commit : '4'.repeat(40) } });
            }
            if (url.endsWith('/catalog.json')) return catalogFailure === 'network' ? route.abort() : route.fulfill(catalogFailure === 'malformed' ? { json: {} } : { json: catalog });
            const entry = catalog.profiles.find(p => url.endsWith('/device-profiles/' + p.path));
            assert.ok(entry, url);
            if (profileFailure === '404') return route.fulfill({ status: 404, body: '' });
            return route.fulfill({ body: profileFailure === 'integrity' ? '{}' : readFileSync('device-profiles/' + entry.path), contentType: 'application/json' });
        });
        await page.route('**/api/**', async route => {
            const url = new URL(route.request().url()), path = url.pathname;
            if (route.request().method() === 'POST') {
                const data = JSON.parse(route.request().postData()); posts.push({ path, query: url.search, data });
                assert.equal(path, '/api/hems/profiles/import');
                assert.ok(route.request().headers().authorization?.startsWith('Basic '));
                if (backendFailure) return route.fulfill({ status: 400, json: { message: 'Rejected by backend validator' } });
                const entry = catalog.profiles.find(p => p.id === data.id);
                const summary = { ...entry, source: 'repository', origin: 'Imported', content_sha256: entry.installed_sha256, capabilities: [entry.category], used_by: [], in_use: false, control_reason: 'control_available', telemetry_count: data.telemetry.length };
                const index = installed.findIndex(p => p.id === data.id);
                if (index < 0) installed.push(summary); else installed[index] = summary;
                return route.fulfill({ json: { ok: true } });
            }
            const body = path === '/api/firmware/status' ? { git_sha: manifest.commit, source_commit: manifest.source_commit, git_branch: manifest.branch, pioenv: 'generic_esp32s3_usb', update_channel: 'pr-25', update_repository: manifest.repository, ota_supported: true }
                : path === '/api/hems/config' ? fixture.config : path === '/api/hems/status' ? fixture.status
                : path === '/api/hems/profiles' ? { profiles: installed, corrupt_slots: 0 }
                : path === '/api/hems/profiles/evse-binding' ? fixture.binding
                : path.endsWith('/i18n/languages') ? [] : path.endsWith('/config') ? { enabled: false } : {};
            return route.fulfill({ json: body });
        });
        await page.goto(baseUrl + '/firmware/upgrade');
        const channel = page.locator('#firmware-update-channel');
        await page.getByText('The installed firmware is already the current image in this update channel.', { exact: true }).waitFor();
        await channel.fill('deinemutter');
        assert.equal(await page.getByRole('heading', { name: 'Available firmware', exact: true }).count(), 0);
        await page.getByRole('button', { name: 'Check again', exact: true }).first().click();
        await page.getByText('No published firmware snapshot is available for this update channel yet.', { exact: true }).waitFor();
        assert.equal(await page.getByRole('button', { name: 'Download and install firmware', exact: true }).count(), 0);
        assert.equal(await page.getByText('feature/hems-economic-dispatch', { exact: true }).count(), 1); // installed metadata only
        for (const name of ['pr-1', 'offline', '../bad']) {
            await channel.fill(name); await channel.press('Enter');
            if (name === 'pr-1') await page.getByText('A different firmware image is available for this update channel.', { exact: true }).waitFor();
            else { await page.locator('.alert-warning').waitFor(); assert.equal(await page.getByRole('button', { name: 'Download and install firmware', exact: true }).count(), 0); }
            assert.doesNotMatch(await page.locator('body').innerText(), /newer firmware|newer or different/i);
        }
        const slowRequest = page.waitForRequest(r => r.url().includes('firmware-channel-slow'));
        await channel.fill('slow'); await channel.press('Enter'); await slowRequest;
        await channel.fill('hems-next'); await channel.press('Enter');
        await page.getByText('A different firmware image is available for this update channel.', { exact: true }).waitFor();
        await late.fulfill({ json: { ...manifest, channel: 'slow' } });
        await page.waitForTimeout(100);
        assert.equal(await page.locator('code').filter({ hasText: /^slow$/ }).count(), 0);
        await page.goto(baseUrl + '/energy/integrations');
        const profilePanel = page.locator('.card').filter({ has: page.getByRole('heading', { name: 'Repository profiles', exact: true }) });
        await profilePanel.getByText(catalog.profiles[0].id, { exact: true }).waitFor();
        assert.equal(remote.filter(url => url.includes('/device-profiles/')).length, 0);
        assert.equal(remote.filter(url => url.endsWith('/catalog.json')).length, 1);
        assert.equal(await profilePanel.getByRole('heading', { name: 'Storage', exact: true }).count(), 1);
        assert.equal(await profilePanel.getByRole('heading', { name: 'EVSE', exact: true }).count(), 1);
        const entry = catalog.profiles[0];
        const card = profilePanel.locator('.border.rounded').filter({ hasText: entry.id });
        for (const failure of ['404', 'integrity', 'backend', '']) {
            profileFailure = failure; backendFailure = failure === 'backend';
            await card.getByRole('button', { name: 'Import', exact: true }).click();
            if (failure) await profilePanel.locator('.text-danger').waitFor();
            else await profilePanel.getByText('Installed · matches repository', { exact: true }).waitFor();
            if (failure) assert.equal(installed.length, 0);
        }
        assert.ok(await card.getByRole('button', { name: 'Update / Replace', exact: true }).isDisabled());
        assert.equal(remote.filter(url => url.includes('/device-profiles/')).length, 4);
        assert.equal(posts.length, 2); // backend rejection and successful import; integrity failures never POST
        assert.equal(await page.locator('#hems-storage-profile, #hems-evse-profile').count(), 0, 'Integration management contains no physical profile binding forms');
        const physical = category => category === 'storage' ? '/plant/battery' : '/plant/wallbox';
        const select = category => category === 'storage' ? '#hems-storage-profile' : '#hems-evse-profile';
        await page.goto(baseUrl + physical(entry.category));
        await page.locator(select(entry.category) + ' option').filter({ hasText: entry.id }).waitFor({ state: 'attached' });
        assert.equal(await page.locator(select(entry.category) + ' option').filter({ hasText: entry.id }).isDisabled(), false);
        await page.goto(baseUrl + '/energy/integrations');
        const other = catalog.profiles.find(p => p.category !== entry.category);
        assert.ok(other, 'Catalog has both storage and EVSE profiles');
        const otherCard = profilePanel.locator('.border.rounded').filter({ hasText: other.id });
        await otherCard.getByRole('button', { name: 'Import', exact: true }).click();
        await otherCard.getByText('Installed · matches repository', { exact: true }).waitFor();
        await page.goto(baseUrl + physical(other.category));
        await page.locator(select(other.category) + ' option').filter({ hasText: other.id }).waitFor({ state: 'attached' });
        assert.equal(await page.locator(select(other.category) + ' option').filter({ hasText: other.id }).isDisabled(), false);
        await page.goto(baseUrl + '/energy/integrations');
        installed[0].content_sha256 = '0'.repeat(64); await page.reload();
        await profilePanel.getByText('Installed · differs from repository', { exact: true }).waitFor();
        page.once('dialog', dialog => dialog.accept());
        await card.getByRole('button', { name: 'Update / Replace', exact: true }).click();
        await profilePanel.getByText('Installed · matches repository', { exact: true }).waitFor();
        assert.equal(posts.at(-1).query, '?replace=' + encodeURIComponent(entry.id));
        assert.equal(posts.filter(p => /command|config/.test(p.path)).length, 0);
        for (const failure of ['network', 'malformed']) {
            catalogFailure = failure; await page.reload();
            await profilePanel.locator('.alert-warning').waitFor();
            await profilePanel.getByText('Manual file import (fallback)', { exact: true }).click();
            assert.ok(await page.locator('#profile-json').isVisible());
            const exports = profilePanel.getByRole('button', { name: 'Export', exact: true });
            assert.equal(await exports.count(), installed.length);
            for (const button of await exports.all()) assert.ok(await button.isEnabled());
        }
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
        assert.equal(posts.filter(p => /command|control|config/.test(p.path)).length, 0);
        await context.close();
    }
    assert.deepEqual(errors, []);
    console.log('Rendered firmware stale-state/race and lazy repository import/update/failure regressions passed at 360/1440px');
} finally { await browser.close(); }
