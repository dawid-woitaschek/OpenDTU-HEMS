import type { Inverter, InverterStatistics } from '@/types/LiveDataStatus';

// Presentation-only: do not label cached or old telemetry as a current measurement.
export function inverterState(inv: Inverter, verified: boolean) {
    if (!inv.poll_enabled) return 'disabled';
    if (!inv.reachable) return 'unreachable';
    if (!verified || !Number.isFinite(inv.data_age_ms) || inv.data_age_ms > 60000) return 'lastKnown';
    return 'reachable';
}
export function inverterTemperature(inv: Inverter): number | null {
    for (const channels of [inv.INV, inv.AC]) {
        for (const value of Object.values(channels || {}) as InverterStatistics[]) {
            if (Number.isFinite(value.Temperature?.v)) return value.Temperature!.v;
        }
    }
    return null;
}
