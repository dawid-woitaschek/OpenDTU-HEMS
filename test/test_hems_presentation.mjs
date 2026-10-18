// The production helpers execute in explicit browser time zones, without hardware or live providers.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(new URL('../webapp/package.json', import.meta.url));
const ts = require('typescript');
const code = ts.transpileModule(readFileSync(new URL('../webapp/src/utils/hemsPresentation.ts', import.meta.url), 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 },
}).outputText;
const h = await import('data:text/javascript;base64,' + Buffer.from(code).toString('base64'));
const policyCode = ts.transpileModule(readFileSync(new URL('../webapp/src/utils/hemsPolicy.ts', import.meta.url), 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 },
}).outputText;
const policy = await import('data:text/javascript;base64,' + Buffer.from(policyCode).toString('base64'));
const scheduleCode = ts.transpileModule(readFileSync(new URL('../webapp/src/utils/hemsSchedule.ts', import.meta.url), 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 },
}).outputText;
const schedule = await import('data:text/javascript;base64,' + Buffer.from(scheduleCode).toString('base64'));
const epoch = text => new Date(text).getTime() / 1000;
const originalZone = process.env.TZ;
let checks = 0;
for (const [zone, date, dayHours] of [
    ['UTC', '2026-03-29T12:00:00Z', 24],
    ['Europe/Berlin', '2026-03-29T12:00:00Z', 23],
    ['Europe/Berlin', '2026-10-25T12:00:00Z', 25],
    ['America/New_York', '2026-03-08T12:00:00Z', 23],
    ['America/New_York', '2026-11-01T12:00:00Z', 25],
    ['Asia/Kathmandu', '2026-10-18T12:00:00Z', 24],
    ['Australia/Lord_Howe', '2026-10-04T12:00:00Z', 23.5],
]) {
    process.env.TZ = zone;
    const now = epoch(date), [start, end] = h.dayBounds(now);
    assert.equal(policy.utcDeadline(policy.localDeadline(now)), now); // UTC persistence survives browser timezone changes.
    assert.equal(policy.utcDeadline('2026-02-31T06:00'), 0);
    assert.equal(policy.utcDeadline(''), 0);
    assert.equal(policy.utcDeadline('2107-01-01T06:00'), 0);
    assert.equal((end - start) / 3600, dayHours, zone);
    assert.equal(h.dayBounds(now, 1)[0], end);
    assert.equal(new Date(start * 1000).getHours(), 0);
    const split = h.splitLocalDays([{ start: start - 900, end: end + 900, value: 10 }]);
    assert.equal(split.length, 3);
    assert.deepEqual(split.map(i => i.end - i.start), [900, dayHours * 3600, 900]);
    const constant = [{ start, end, value: 1000 }, { start: end, end: h.dayBounds(now, 1)[1], value: 2000 }];
    assert.equal(h.summarize(constant, start, end).energyWh, 1000 * dayHours);
    assert.equal(h.summarize(constant, now, end).energyWh, 1000 * (end - now) / 3600);
    assert.equal(h.summarize(constant, end, h.dayBounds(now, 1)[1]).average, 2000);
    const intervals = Array.from({ length: 10 }, (_, n) => ({ start: start + n * 900, end: start + (n + 1) * 900, value: n }));
    const classified = h.classifyTariff(intervals);
    assert.deepEqual(classified.map(i => i.category), [...Array(3).fill('cheap'), ...Array(3).fill('normal'), ...Array(4).fill('expensive')]);
    assert.deepEqual(classified.map(i => i.value), intervals.map(i => i.value)); // display does not mutate economics
    assert.ok(intervals.every(i => !('category' in i)));
    checks++;
}
process.env.TZ = 'Europe/Berlin';
assert.equal(policy.utcDeadline('2026-03-29T02:30'), 0); // Nonexistent local DST time.
assert.equal(policy.utcDeadline('2026-10-25T06:00'), epoch('2026-10-25T05:00:00Z'));
const [start, end] = h.dayBounds(epoch('2026-10-25T12:00:00Z'));
// Equal-price and nearly flat days must not invent actionable-looking low/high distinctions.
for (const value of [-1234, 0, 1234]) {
    assert.ok(h.classifyTariff(Array.from({ length: 8 }, (_, n) => ({ start: start + n * 900, end: start + (n + 1) * 900, value }))).every(i => i.category === 'normal'));
}
assert.equal(h.classifyTariff([{ start, end, value: 0 }])[0].category, 'normal');
const nearlyFlat = Array.from({ length: 12 }, (_, n) => ({ start: start + n * 900, end: start + (n + 1) * 900, value: 350000 + n * 180 }));
assert.ok(h.classifyTariff(nearlyFlat).every(i => i.category === 'normal'));
const negative = h.classifyTariff(Array.from({ length: 10 }, (_, n) => ({ start: start + n * 900, end: start + (n + 1) * 900, value: n - 10 })));
assert.equal(negative[9].category, 'expensive'); // negative can still be relatively high within the day
// Duration, not sample count, determines the robust daily price range.
const unequal = [{ start, end: start + 3600, value: 1 }, { start: start + 3600, end: start + 4500, value: 2 }];
assert.deepEqual(h.classifyTariff(unequal).map(i => i.category), ['cheap', 'expensive']);
const nextDayEnd = h.dayBounds(end, 0)[1];
const crossMidnight = h.classifyTariff([{ start: end - 900, end: end + 900, value: 1 }, { start: end + 900, end: nextDayEnd, value: 9 }]);
assert.equal(crossMidnight.length, 3);
assert.equal(crossMidnight[0].category, 'normal');
assert.equal(crossMidnight[1].category, 'normal');
// Regression from the live DE-LU shape: 35.16 and 35.69 ct/kWh belong to the same low-price level,
// rather than making one isolated quarter-hour "cheap" because it barely crossed a rank quantile.
const realisticPrices = [344886, 351645, 356929, 356572, 360868, 380000, 400000, 420000, 440000, 464100, 480000, 497337];
const realistic = h.classifyTariff(realisticPrices.map((value, n) => ({ start: start + n * 900, end: start + (n + 1) * 900, value })));
assert.equal(realistic[1].category, 'cheap');
assert.equal(realistic[2].category, 'cheap');
assert.ok(h.nextPeriod(realistic, 'cheap', start).end - start >= 6 * 900);
// Gaps and null prices remain gaps in the primary and secondary step paths and period summaries.
const gaps = [{ start, end: start + 900, value: -10, secondary: 0, category: 'cheap' }, { start: start + 1800, end: start + 2700, value: 20, secondary: null, category: 'cheap' }];
assert.equal(h.summarize(gaps, start, start + 2700).complete, false);
assert.equal(h.summarize(gaps, start, start + 2700).average, 5);
assert.equal(h.summarize([], start, end).average, null);
assert.equal(h.summarize([{ start, end, value: null }], start, end).energyWh, null);
assert.equal((h.stepPath(gaps, x => x - start, y => y).match(/M/g) || []).length, 2);
assert.equal((h.stepPath(gaps, x => x - start, y => y, true).match(/M/g) || []).length, 1);
assert.equal(h.stepPath([{ start, end: start + 900, value: 1 }, { start: start + 900, end: start + 1800, value: 2 }], x => x - start, y => y), 'M0,1H900L900,2H1800');
assert.deepEqual(h.nextPeriod(gaps, 'cheap', start + 100), { start: start + 100, end: start + 900 });
assert.equal(h.nextPeriod(gaps, 'expensive', start), null);
const mixed = { intervals: [{ start, end: start + 900, price_micros_per_kwh: -50000, import_price_micros_per_kwh: 123456 }, { start: start + 900, end: start + 1800, price_micros_per_kwh: 0 }] };
assert.equal(h.tariffBasis(mixed), 'effective');
assert.equal(h.tariffIntervals(mixed)[0].value, 123456); // authoritative backend value, never recomputed
assert.equal(h.tariffIntervals(mixed)[1].value, null); // no mixed effective/source curve
assert.equal(h.summarize(h.tariffIntervals(mixed), start, start + 1800).complete, false);
assert.equal(h.tariffBasis({ intervals: [{ start, end, price_micros_per_kwh: 0 }] }), 'source');
assert.equal(h.tariffIntervals({ intervals: [{ start, end, price_micros_per_kwh: 0 }] })[0].value, 0);
assert.equal(h.formatPrice(-1234, 'EUR', 'en'), '-0.1234 ct/kWh');
assert.equal(h.formatPrice(0, 'EUR', 'en'), '0.00 ct/kWh');
assert.equal(h.formatPrice(123456, 'EUR', 'de'), '12,3456 ct/kWh');
assert.equal(h.formatPrice(123456, 'CHF', 'en'), '0.123456 CHF/kWh');
for (const v of [null, undefined, NaN, Infinity]) assert.equal(h.formatPrice(v), '—');
assert.equal(h.signalValue({ quality: 'fresh', value: 0 }), 0);
assert.equal(h.signalValue({ quality: 'fresh', value: false }), false);
assert.equal(h.signalValue({ quality: 'stale', value: 100 }), null);
assert.equal(h.signalValue({ quality: 'unknown', value: 100 }), null);
assert.equal(h.signalValue({ quality: 'fresh', value: 100 }, true), null);
for (const [identifier, expected] of Object.entries({ provider_rate_limited: 'rateLimited', rate_limited: 'rateLimited', network_unavailable: 'waitingNetwork', clock_invalid: 'waitingTime', waiting_for_time: 'waitingTime', transport_backoff: 'transportBackoff', provider_backoff: 'providerBackoff', stale: 'outdated', missing_current_interval: 'partial', future_unknown_error: 'updateFailed' })) assert.equal(h.providerMessage(identifier), expected);
// Refresh status is independent of availability; retrying does not invalidate retained data.
const feed = { enabled: true, fresh: true, valid: true, current_covered: true, planning_usable: true, horizon_complete: true, intervals: [{ start, end, power_w: 0 }] };
assert.equal(h.feedReadiness(feed), 'freshData');
assert.equal(h.feedReadiness(feed, true), 'unverifiedData');
assert.equal(h.feedReadiness({ ...feed, enabled: false }), 'disabled');
assert.equal(h.feedReadiness({ ...feed, intervals: [] }), 'noData');
assert.equal(h.feedReadiness({ ...feed, valid: false }), 'noData');
assert.equal(h.feedReadiness({ ...feed, fresh: false }), 'outdated');
assert.equal(h.feedReadiness({ ...feed, current_covered: false }), 'currentGap');
assert.equal(h.feedReadiness({ ...feed, horizon_complete: false, partial_horizon: true }), 'partialData');
assert.equal(h.feedReadiness({ ...feed, last_error: 'provider_rate_limited', manual_refresh: { allowed: false, retry_after_seconds: 600 } }), 'freshData');
assert.equal(h.feedReadiness({ ...feed, fresh: false, last_error: 'provider_rate_limited' }), 'outdated');
// Storage schedule presentation merges only true command-equivalent slots, keeps power changes, exposes gaps,
// and never labels a future slot as the current action.
const scheduleStart = start + 10000;
const scheduleSteps = [
    { start: scheduleStart, end: scheduleStart + 900, used_price_micros_per_kwh: 300000, action: 'charge', reason: 'low_price', battery_w: 1000, end_soc_pct: 51 },
    { start: scheduleStart + 900, end: scheduleStart + 1800, used_price_micros_per_kwh: 310000, action: 'charge', reason: 'low_price', battery_w: 1000, end_soc_pct: 52 },
    { start: scheduleStart + 1800, end: scheduleStart + 2700, used_price_micros_per_kwh: 320000, action: 'charge', reason: 'low_price', battery_w: 1500, end_soc_pct: 54 },
    { start: scheduleStart + 3600, end: scheduleStart + 4500, used_price_micros_per_kwh: 400000, action: 'hold', reason: 'no_shift', battery_w: 0, end_soc_pct: 54 },
    { start: scheduleStart + 4500, end: scheduleStart + 5400, used_price_micros_per_kwh: 410000, action: 'stop', reason: 'no_shift', battery_w: 0, end_soc_pct: 54 },
];
const blocks = schedule.operationBlocks(scheduleSteps);
assert.equal(blocks.length, 3);
assert.deepEqual(blocks.map(block => [block.action, block.battery_w, block.slots]), [['charge', 1000, 2], ['charge', 1500, 1], ['hold', 0, 2]]);
assert.deepEqual([blocks[0].min_price_micros_per_kwh, blocks[0].max_price_micros_per_kwh], [300000, 310000]);
assert.equal(schedule.currentScheduleStep(scheduleSteps, scheduleStart - 1), undefined);
assert.equal(schedule.currentScheduleStep(scheduleSteps, scheduleStart + 1000).battery_w, 1000);
assert.equal(schedule.nextOperationBlock(blocks, scheduleStart + 1000).battery_w, 1500);
const timeline = schedule.timelineParts(scheduleSteps, scheduleStart, scheduleStart + 5400);
assert.equal(timeline.filter(part => part.gap).length, 1);
assert.deepEqual(timeline.find(part => part.gap), { start: scheduleStart + 2700, end: scheduleStart + 3600, action: 'gap', battery_w: 0, gap: true });
assert.equal(schedule.socAt(scheduleSteps, 50, scheduleStart, scheduleStart), 50);
assert.equal(schedule.socAt(scheduleSteps, 50, scheduleStart, scheduleStart + 1800), 52);
assert.equal(schedule.socAt(scheduleSteps, 50, scheduleStart, scheduleStart + 5400), 54);
// Supported locales must have the same product keys and interpolation parameters.
const messages = ['en', 'de', 'fr'].map(l => JSON.parse(readFileSync(new URL(`../webapp/src/locales/${l}.json`, import.meta.url), 'utf8')).hemsUi);
const paths = (o, prefix = '') => Object.entries(o).flatMap(([key, value]) => typeof value === 'object' ? paths(value, prefix + key + '.') : [prefix + key]).sort();
for (const locale of messages.slice(1)) assert.deepEqual(paths(locale), paths(messages[0]));
for (const key of paths(messages[0])) {
    const value = (message) => key.split('.').reduce((o, part) => o[part], message);
    const placeholders = message => [...value(message).matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort();
    for (const locale of messages.slice(1)) assert.deepEqual(placeholders(locale), placeholders(messages[0]), key);
}
if (originalZone === undefined) delete process.env.TZ; else process.env.TZ = originalZone;
console.log(`HEMS presentation passed: ${checks} timezone/day cases, robust price levels/flat days/duration/signs, schedule blocks/gaps/SoC, exact prices, forecast summaries, freshness, provider errors and locale parity`);
