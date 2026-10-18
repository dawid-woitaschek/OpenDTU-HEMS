import type { EnergyFeed, EnergyInterval } from '../types/HemsEnergy';
import type { HemsSignal } from '../types/HemsStatus';

export type TariffClass = 'cheap' | 'normal' | 'expensive';
export interface ChartInterval {
    start: number;
    end: number;
    value: number | null;
    secondary?: number | null;
    category?: TariffClass;
}
// Calendar arithmetic, never +86400: local days can be 23, 24, 25 or fractional-offset hours.
export function dayBounds(now: number, offset = 0): [number, number] {
    const day = new Date(now * 1000);
    day.setHours(0, 0, 0, 0);
    day.setDate(day.getDate() + offset);
    const start = day.getTime() / 1000;
    day.setDate(day.getDate() + 1);
    return [start, day.getTime() / 1000];
}
export function splitLocalDays<T extends { start: number; end: number }>(intervals: T[]): T[] {
    return intervals.flatMap((interval) => {
        if (!Number.isFinite(interval.start) || !Number.isFinite(interval.end) || interval.end <= interval.start)
            return [];
        const pieces: T[] = [];
        let start = interval.start;
        while (start < interval.end) {
            const end = Math.min(dayBounds(start)[1], interval.end);
            pieces.push({ ...interval, start, end });
            start = end;
        }
        return pieces;
    });
}
export function finite(value: number | null | undefined): value is number {
    return typeof value === 'number' && Number.isFinite(value);
}
export function signalValue<T>(signal?: HemsSignal<T>, offline = false): T | null {
    return !offline && signal?.quality === 'fresh' ? signal.value : null;
}
export function formatPrice(
    micros: number | null | undefined,
    currency = 'EUR',
    locale?: string,
    decimals?: number
): string {
    if (!finite(micros)) return '—';
    // No effective-price calculation here: the backend supplies the authoritative signed integer.
    return `${(micros / (currency === 'EUR' ? 10000 : 1e6)).toLocaleString(locale, {
        minimumFractionDigits: 2,
        maximumFractionDigits: decimals ?? (currency === 'EUR' ? 4 : 6),
    })} ${currency === 'EUR' ? 'ct' : currency}/kWh`;
}
export function tariffBasis(feed?: EnergyFeed): 'effective' | 'source' {
    return feed?.intervals.some((i) => finite(i.import_price_micros_per_kwh)) ? 'effective' : 'source';
}
export function tariffIntervals(feed?: EnergyFeed): ChartInterval[] {
    const effective = tariffBasis(feed) === 'effective';
    return (feed?.intervals || []).map((i) => ({
        start: i.start,
        end: i.end,
        value: (effective ? i.import_price_micros_per_kwh : i.price_micros_per_kwh) ?? null,
        secondary: effective ? (i.price_micros_per_kwh ?? null) : null,
    }));
}
// Keep data availability separate from refresh failures: retained data can still be usable.
// Planning permission comes from planning_usable in the API, never from this display state.
export function feedReadiness(
    feed: EnergyFeed,
    unavailable = false
): 'disabled' | 'noData' | 'unverifiedData' | 'outdated' | 'currentGap' | 'partialData' | 'freshData' {
    if (!feed.enabled) return 'disabled';
    if (feed.valid === false || !feed.intervals.length) return 'noData';
    if (unavailable) return 'unverifiedData';
    if (!feed.fresh) return 'outdated';
    if (feed.current_covered === false || feed.state === 'missing_current_interval') return 'currentGap';
    if (feed.partial_horizon === true || !feed.horizon_complete) return 'partialData';
    return 'freshData';
}
// HEMS-relative daily classification. Inverse duration-weighted empirical CDF at .2/.8.
// Strict comparisons preserve ties; equal/zero/negative days can be entirely normal.
// This is deliberately NOT the official Energy-Charts Strompreisampel (no <=0 override).
export function classifyTariff(intervals: ChartInterval[]): ChartInterval[] {
    const pieces = splitLocalDays(intervals);
    const days = new Map<number, ChartInterval[]>();
    for (const piece of pieces) {
        if (!finite(piece.value)) continue;
        const key = dayBounds(piece.start)[0];
        const values = days.get(key) || [];
        values.push(piece);
        days.set(key, values);
    }
    const bounds = new Map<number, [number, number]>();
    for (const [key, values] of days) {
        const sorted = [...values].sort((a, b) => a.value! - b.value!);
        const duration = sorted.reduce((sum, i) => sum + i.end - i.start, 0);
        const quantile = (q: number) => {
            let covered = 0;
            for (const i of sorted) {
                covered += i.end - i.start;
                if (covered >= duration * q) return i.value!;
            }
            return sorted[sorted.length - 1]!.value!;
        };
        bounds.set(key, [quantile(0.2), quantile(0.8)]);
    }
    return pieces.map((i) => {
        const threshold = bounds.get(dayBounds(i.start)[0]);
        if (!finite(i.value) || !threshold) return i;
        return { ...i, category: i.value < threshold[0] ? 'cheap' : i.value > threshold[1] ? 'expensive' : 'normal' };
    });
}
export function summarize(intervals: ChartInterval[], start: number, end: number) {
    let cursor = start,
        weighted = 0,
        seconds = 0;
    let complete = end > start;
    for (const i of intervals) {
        const from = Math.max(start, i.start),
            until = Math.min(end, i.end);
        if (until <= from || !finite(i.value)) continue;
        if (from > cursor) complete = false;
        // Ignore duplicate/overlapping coverage; input API is ordered and non-overlapping.
        const duration = until - Math.max(cursor, from);
        if (duration <= 0) continue;
        weighted += i.value * duration;
        seconds += duration;
        cursor = until;
    }
    return {
        average: seconds ? weighted / seconds : null,
        energyWh: seconds ? weighted / 3600 : null,
        complete: complete && cursor >= end,
        seconds,
    };
}
export function forecastIntervals(intervals: EnergyInterval[]): ChartInterval[] {
    return intervals.map((i) => ({ start: i.start, end: i.end, value: i.power_w ?? null }));
}
export function nextPeriod(intervals: ChartInterval[], category: TariffClass, now: number) {
    const first = intervals.findIndex((i) => i.end > now && i.category === category && finite(i.value));
    if (first < 0) return null;
    const period = { start: Math.max(now, intervals[first]!.start), end: intervals[first]!.end };
    for (const i of intervals.slice(first + 1)) {
        if (i.start !== period.end || i.category !== category || !finite(i.value)) break;
        period.end = i.end;
    }
    return period;
}
// Stable identifiers observed in Provider.cpp, Service.cpp, Native.cpp and Transport.cpp.
export function providerMessage(state: string): string {
    if (['provider_rate_limited', 'rate_limited'].includes(state)) return 'rateLimited';
    if (['waiting_for_time', 'clock_invalid', 'clock_or_source_time_invalid'].includes(state)) return 'waitingTime';
    if (state === 'network_unavailable') return 'waitingNetwork';
    if (state === 'transport_backoff') return 'transportBackoff';
    if (state === 'provider_backoff') return 'providerBackoff';
    if (state === 'local_retry') return 'localRetry';
    if (state === 'busy') return 'requestBusy';
    if (['disabled', 'not_enabled', 'provider_disabled'].includes(state)) return 'disabled';
    if (state === 'stale') return 'outdated';
    if (['partial', 'missing_current_interval'].includes(state)) return 'partial';
    if (state === 'ready') return 'ready';
    if (state === 'unavailable') return 'waitingData';
    if (state === 'invalid_config') return 'invalidConfig';
    return 'updateFailed';
}
// Preserve gaps and missing secondary prices. Only join adjacent, known step intervals.
export function stepPath(
    intervals: ChartInterval[],
    x: (n: number) => number,
    y: (n: number) => number,
    secondary = false
) {
    let path = '',
        previous: ChartInterval | undefined;
    for (const i of intervals) {
        const value = secondary ? i.secondary : i.value;
        if (!finite(value)) {
            previous = undefined;
            continue;
        }
        const round = (n: number) => Math.round(n * 100) / 100;
        path += `${previous?.end === i.start ? 'L' : 'M'}${round(x(i.start))},${round(y(value))}H${round(x(i.end))}`;
        previous = i;
    }
    return path;
}
