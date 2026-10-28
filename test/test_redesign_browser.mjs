// Redesign navigation and real component flows against intercepted APIs only.
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { mkdir } from "node:fs/promises";
import { createHemsFixture, fixtureNow } from "./fixtures/hems.mjs";
const require = createRequire(import.meta.url);
const baseUrl = process.env.HEMS_BASE_URL || 'http://127.0.0.1:5173';
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || "playwright");
const browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || "/usr/bin/chromium",
    headless: true,
    args: ["--no-sandbox"],
});
const screenshotDir =
    process.env.HEMS_SCREENSHOTS || "work/redesign-screenshots";
await mkdir(screenshotDir, { recursive: true });
const errors = [];
const value = (v, u = "W") => ({ v, u, d: 1, max: v });
const live = {
    inverters: ["East", "West"].map((name, index) => ({
        name,
        serial: String(100000000001 + index),
        order: index,
        data_age: 1,
        data_age_ms: 1000,
        poll_enabled: true,
        reachable: true,
        producing: true,
        limit_relative: 75 - index * 25,
        limit_absolute: 600 - index * 200,
        events: 0,
        AC: [{ Power: value(400), Voltage: value(230, "V") }],
        DC: [],
        INV: [],
        radio_stats: {
            tx_request: 10,
            tx_re_request: 0,
            rx_success: 10,
            rx_fail_nothing: 0,
            rx_fail_partial: 0,
            rx_fail_corrupt: 0,
            rssi: -60,
        },
    })),
    total: {
        Power: value(800),
        YieldDay: value(4, "kWh"),
        YieldTotal: value(200, "kWh"),
    },
    hints: {
        time_sync: false,
        default_password: false,
        radio_problem: false,
        pin_mapping_issue: false,
    },
    solarcharger: { enabled: false },
    battery: { enabled: false },
    gridcharger: { enabled: false },
    power_meter: { enabled: true, Power: value(-245) },
};
try {
    for (const locale of ["en", "de"])
        for (const width of [320, 768, 1440]) {
            const fixture = createHemsFixture(),
                posts = [];
            let offline = false;
            const context = await browser.newContext({
                viewport: { width, height: 1000 },
                locale,
                timezoneId: "Europe/Berlin",
            });
            const page = await context.newPage();
            page.on("pageerror", (e) => errors.push(e.message));
            await page.clock.install({ time: fixtureNow * 1000 });
            await page.addInitScript((locale) => {
                localStorage.setItem("locale", locale);
                localStorage.setItem(
                    "user",
                    JSON.stringify({ authdata: btoa("fixture:fixture") }),
                );
            }, locale);
            await page.routeWebSocket(
                /\/(livedata|batterylivedata|solarchargerlivedata|gridchargerlivedata)$/,
                () => {},
            );
            await page.route("https://raw.githubusercontent.com/**", (route) =>
                route.fulfill({ status: 404, body: "" }),
            );
            await page.route("**/api/**", async (route) => {
                const path = new URL(route.request().url()).pathname;
                if (
                    offline &&
                    ["/api/hems/status", "/api/livedata/status"].includes(path)
                )
                    return route.abort();
                if (route.request().method() === "POST") {
                    const form = await new Response(
                        route.request().postDataBuffer(),
                        {
                            headers: {
                                "content-type": route.request().headers()[
                                    "content-type"
                                ],
                            },
                        },
                    ).formData();
                    const payload = JSON.parse(form.get("data"));
                    posts.push({ path, payload });
                    if (path === "/api/hems/optimizer/config")
                        fixture.optimizer.config = payload;
                    return route.fulfill({
                        json: { ok: true, type: "success" },
                    });
                }
                const body = path.endsWith("/i18n/languages")
                    ? []
                    : path === "/api/hems/status"
                      ? fixture.status
                      : path === "/api/livedata/status"
                        ? live
                        : path === "/api/hems/config"
                          ? fixture.config
                          : path === "/api/hems/profiles"
                            ? fixture.profiles
                            : path === "/api/hems/profiles/evse-binding"
                              ? fixture.binding
                              : path.startsWith("/api/hems/optimizer")
                                ? fixture.optimizer
                                : path.startsWith("/api/hems/energy/tariff")
                                  ? fixture.tariff
                                  : path.startsWith("/api/hems/energy/forecast")
                                    ? fixture.forecast
                                    : path === "/api/system/status"
                                      ? {
                                            reset_diagnostics: {},
                                            task_details: [],
                                        }
                                      : path.endsWith("/config")
                                        ? { enabled: false }
                                        : path ===
                                            "/api/solarchargerlivedata/status"
                                          ? {
                                                dpl: {
                                                    PLSTATE: 2,
                                                    PLLIMIT: 1000,
                                                },
                                                solarcharger: {
                                                    full_update: true,
                                                    instances: {},
                                                },
                                            }
                                          : {};
                return route.fulfill({ json: body });
            });
            for (const path of [
                "/",
                "/live",
                "/plant/inverters",
                "/plant/solar",
                "/plant/battery",
                "/plant/wallbox",
                "/plant/power-meter",
                "/energy/planning",
                "/energy/integrations",
                "/energy/diagnostics",
            ]) {
                await page.goto("http://127.0.0.1:5173" + path);
                await page.locator("h1").waitFor();
                await page.waitForTimeout(150);
                assert.equal(
                    await page.locator('[role="main"]').count(),
                    1,
                    `${path} has one page landmark`,
                );
                assert.equal(
                    await page.evaluate(
                        () => document.documentElement.scrollWidth > innerWidth,
                    ),
                    false,
                    `${locale} ${path} overflows at ${width}`,
                );
                assert.doesNotMatch(
                    await page.locator("body").innerText(),
                    /redesign\.[a-zA-Z]+/,
                );
                if (path === "/") {
                    const help = page.locator("#overview-pv .help-trigger");
                    await help.focus();
                    const tooltip = page.getByRole("tooltip");
                    await tooltip.waitFor();
                    await tooltip
                        .getByText(locale === "de" ? "1.000 W" : "1,000 W", {
                            exact: true,
                        })
                        .waitFor();
                    assert.match(await tooltip.innerText(), /East: 75.*600 W/s);
                    assert.match(await tooltip.innerText(), /West: 50.*400 W/s);
                    const box = await tooltip.boundingBox();
                    assert.ok(
                        box.x >= 0 && box.x + box.width <= width,
                        "Tooltip fits viewport",
                    );
                    await page.keyboard.press("Escape");
                    assert.equal(await tooltip.count(), 0);
                    await page.locator("h1").click();
                    await page.screenshot({
                        path: `${screenshotDir}/${locale}-${width}-overview.png`,
                        fullPage: true,
                    });
                }
                if (path === "/plant/battery") {
                    assert.equal(
                        await page.locator("#hems-evse-profile").count(),
                        0,
                    );
                    assert.equal(
                        await page.locator("#hems-storage-profile").count(),
                        1,
                    );
                }
                if (path === "/plant/wallbox") {
                    assert.equal(
                        await page.locator("#hems-storage-profile").count(),
                        0,
                    );
                    assert.equal(
                        await page.locator("#hems-evse-profile").count(),
                        1,
                    );
                }
                if (path === "/energy/integrations") {
                    assert.equal(
                        await page
                            .locator(
                                "#hems-storage-profile, #hems-evse-profile",
                            )
                            .count(),
                        0,
                    );
                }
                if (path === "/energy/planning") {
                    await page
                        .locator("#optimizer-grid-charging")
                        .selectOption("false");
                    await page.locator("#optimizer-target-enabled").check();
                    await page.locator("#optimizer-target-soc").fill("70");
                    await page
                        .locator("#optimizer-target-deadline")
                        .fill("2026-10-26T07:00");
                    await page
                        .locator("form")
                        .filter({ has: page.locator("#optimizer-enabled") })
                        .locator("button.btn-primary")
                        .click();
                    await page.waitForTimeout(100);
                    const saved = posts.findLast(
                        (p) => p.path === "/api/hems/optimizer/config",
                    );
                    assert.equal(
                        saved.payload.policies.grid_charging_allowed,
                        false,
                    );
                    assert.equal(saved.payload.policies.target_soc_pct, 70);
                    assert.equal(
                        saved.payload.policies.target_deadline,
                        Date.parse("2026-10-26T06:00:00Z") / 1000,
                    );
                    assert.equal(
                        saved.payload.dispatch_enabled,
                        false,
                        "UI changes never implicitly enable execution",
                    );
                    await page.screenshot({
                        path: `${screenshotDir}/${locale}-${width}-planning.png`,
                        fullPage: true,
                    });
                }
            }
            for (const [oldPath, canonical] of [
                ["/hems", "/"],
                ["/info/hems", "/"],
                ["/hems/devices", "/energy/integrations"],
                ["/settings/battery", "/plant/battery"],
            ]) {
                await page.goto("http://127.0.0.1:5173" + oldPath);
                await page.waitForURL("**" + canonical);
            }
            offline = true;
            await page.goto("http://127.0.0.1:5173/");
            await page.locator(".alert-warning").first().waitFor();
            assert.equal(
                await page.locator("#overview-pv").count(),
                0,
                "No retained live values on a failed initial status request",
            );
            assert.equal(
                posts.some((p) => p.path === "/api/hems/command"),
                false,
                "Browsing and planning never submit device commands",
            );
            await context.close();
        }
    assert.deepEqual(errors, []);
    console.log(
        "Redesign: en/de routes, 320/768/1440px, device separation, keyboard limits tooltip, bookmark redirects, unchanged save payloads and offline behavior passed",
    );
} finally {
    await browser.close();
}
