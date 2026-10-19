// Synthetic, hardware-free API responses. Each interval is explicitly present; no live providers.
export const fixtureNow = Date.parse('2026-10-25T10:30:00Z') / 1000;
export function createHemsFixture() {
    const now = fixtureNow;
    const start = Date.parse('2026-10-24T22:00:00Z') / 1000;
    const tomorrow = Date.parse('2026-10-25T23:00:00Z') / 1000;
    const end = tomorrow + 86400;
    const signal = value => ({ quality: 'fresh', value });
    const device = { configured: true, enabled: true, reachable: true, has_update: true, age_ms: 200 };
    const telemetry = { ...device, valid: true, stale: false, status: 'Connected', error: 'None', modbus_exception: 0, successful_polls: 100, failed_polls: 0, last_success_ms: 999800 };
    const target = { profile: 'fixture', control_mode: 'hems', permission_granted: true, allowed: true, ready_for_active: true, blockers: [], ownership_uncertain: false,
        command: { write_attempted: false, readback_verified: false, completed_ms: 0, age_ms: 0, automatic: false, modbus_exception: 0, last_error: 'none', last_stage: 'idle', execution_error: 'none', transport_error: 'None', planning_error: 'none', state: 'idle', id: null, generation: null, action: null, value: null },
        profile_info: { id: 'fixture', schema_version: 2, telemetry_supported: true, control_supported: true, control_reason: 'ready', identity_metadata: true, recovery_metadata: false, release_replay_safe: false, source_revision: 'fixture', fingerprint: 1234, operations: [{ action: 'set_charge_power_w', has_input: true, min: 0, max: 2500, step: 1, unit: 'W' }] },
        cleanup: { state: 'idle', retained_context: false, attempts: 0, last_error: 'none', last_stage: 'idle', execution_error: 'none', transport_error: 'None', modbus_exception: 0 } };
    const status = { state: 'Active', uptime_ms: 1000000, decision: 'None', external_devices: 2,
        warp: { ...telemetry, vehicle_connected: true, charging: true, power_w: 3680, allowed_current_a: 16, session_energy_kwh: 12.4, iec_state: 2, charging_state: 3, table_version: 2, meter_available: true },
        storage: { ...telemetry, profile_loaded: true, profile: 'Marstek-Venus-E-3.0', host: 'battery.local', port: 502, unit: 1, soc_pct: 62, ac_power_w: 1200, dc_power_w: 1140, battery_voltage_v: 51.2, battery_current_a: 22.2, temperature_c: 27, charge_energy_total_kwh: 100, discharge_energy_total_kwh: 80, operating_state: 1, fault_code: 0, alarm_code: 0, charging: true, discharging: false, fault_active: false, alarm_active: false },
        normalized: { grid: { power_w: signal(-245) }, pv: { power_w: signal(4820) }, storage: { device, soc_pct: signal(62), ac_power_w: signal(1200), dc_power_w: signal(1140), battery_voltage_v: signal(51.2), battery_current_a: signal(22.2), temperature_c: signal(27), charging: signal(true), discharging: signal(false), fault_active: signal(false), alarm_active: signal(false) }, evse: { device, vehicle_connected: signal(true), charging: signal(true), power_w: signal(3680), allowed_current_a: signal(16) } },
        constraints: { storage: { charge_allowed: true, discharge_allowed: false, max_charge_power: { active: false, value_w: null }, max_discharge_power: { active: false, value_w: null } } },
        actuation: { global_active: true, storage_control_enabled: true, evse_control_enabled: true, storage: structuredClone(target), evse: structuredClone(target) },
        shadow: { ev_protection: { state: 'would_block_storage_discharge', debounce_ms: 120000, condition_age_ms: null, charging_known: true, charging: true, would_block_storage_discharge: true, blocking_storage_discharge: true } } };
    const config = { version: 1, enabled: true, mode: 'Active', warp: { enabled: true, host: 'charger.local', port: 502 }, storage: { enabled: true, host: 'battery.local', profile: 'Marstek-Venus-E-3.0', port: 502, unit: 1, control_mode: 'hems' } };
    const pricing = { enabled: true, vat_basis_points: 0, grid_micros_per_kwh: 80000, markup_micros_per_kwh: 20000, monthly_micros: 9990000 };
    const planes = Array.from({ length: 6 }, (_, i) => ({ enabled: i < 2, name: ['South roof', 'West roof', 'Garage', 'East roof', 'Annex', 'Garden'][i], latitude_e6: 52520000, longitude_e6: 13405000, tilt: 30, azimuth: i ? 90 : 0, peak_w: 3000 }));
    const base = { revision: 1, manual_refresh: { allowed: true, reason: null, retry_after_seconds: 0, next_allowed: now }, next_refresh: now + 3600, source_age_seconds: null, transport_age_seconds: 600, rate_remaining: 6, source: 'Deterministic fixture', state: 'ready', fresh: true, enabled: true, fetched_at: now - 600, generated_at: now - 600, horizon_end: end, future_count: 140, horizon_complete: true, valid: true, current_covered: true, planning_usable: true, partial_horizon: false, last_error: null, current: null, intervals: [], request_stage: 'idle', request: { http_status: 200, dns_state: 'resolved', tls_error: 0, payload_bytes: 5000, json_peak: 8192 }, worker_stack_free_bytes: 4096, runtime_memory: { heap_free: 150000 } };
    const tariffIntervals = Array.from({ length: (end - start) / 900 }, (_, i) => {
        const utc = start + i * 900, hour = new Date(utc * 1000).getUTCHours();
        const source = Math.round((hour >= 10 && hour < 14 ? -5 : 12 + 15 * Math.sin((hour - 7) / 24 * Math.PI * 2)) * 10000);
        return { start: utc, end: utc + 900, price_micros_per_kwh: source, import_price_micros_per_kwh: source + 100000 };
    });
    const forecastIntervals = Array.from({ length: (end - start) / 3600 }, (_, i) => {
        const utc = start + i * 3600, hour = new Date(utc * 1000).getUTCHours() + 1;
        const power_w = hour > 6 && hour < 18 ? Math.round(Math.sin((hour - 6) / 12 * Math.PI) * 6000) : 0;
        return { start: utc, end: utc + 3600, power_w, energy_wh: power_w };
    });
    const tariff = { ...base, provider: 'energy_charts', currency: 'EUR', attribution: 'Energy-Charts.info; CC BY 4.0', intervals: tariffIntervals, config: { provider: 'energy_charts', enabled: true, url: '', pricing, planes: [], refresh_seconds: 3600, max_age_seconds: 10800, horizon_seconds: 86400 } };
    const forecast = { ...base, provider: 'forecast_solar', intervals: forecastIntervals, plane_status: planes.map(() => ({ fetched_at: now - 600, last_error: null })), config: { provider: 'forecast_solar', enabled: true, url: '', pricing, planes, refresh_seconds: 7200, max_age_seconds: 10800, horizon_seconds: 86400 } };
    for (const feed of [tariff, forecast]) feed.current = feed.intervals.find(i => i.start <= now && now < i.end);
    const optimizer = { state: 'ready', shadow_only: true, device_writes: false, config: { enabled: true, soc_bounds_configured: true, effective_price: true, export_configured: false, export_micros_per_kwh: 0, horizon_seconds: 86400, capacity_wh: 5120, min_soc_pct: 15, max_soc_pct: 95, charge_permille: 950, discharge_permille: 950, max_charge_w: 2500, max_discharge_w: 2500, expected_base_load_w: 300, soc_step_pct: 5, policies: { grid_charging_allowed: true, discharge_allowed: true, target_enabled: false, target_soc_pct: 70, target_deadline: 0 } }, blockers: [], assumptions: ['constant_explicit_base_load', 'terminal_soc_at_least_initial', 'future_ev_constraints_unknown'], warnings: ['modeled_savings_not_guaranteed'], tariff_source: 'energy_charts', forecast_source: 'forecast_solar', storage_soc_fresh: true, initial_soc_pct: 62, final_soc_pct: 65, current_discharge_allowed: false, baseline_cost_nano: 4200000000, optimized_cost_nano: 3000000000, modeled_savings_nano: 1200000000, horizon_start: now, horizon_end: now + 86400, price_basis: 'effective_variable_import', currency: 'EUR', interval_count: 96, has_more: true,
        schedule: Array.from({ length: 24 }, (_, i) => ({ start: now + i * 900, end: now + (i + 1) * 900, used_price_micros_per_kwh: 50000, pv_w: 4000, assumed_load_w: 300, action: i < 3 ? 'charge' : 'hold', battery_w: i < 3 ? 1200 : 0, end_soc_pct: 62 + i * .1, grid_import_wh: 0, grid_export_wh: 625, reason: i < 3 ? 'horizon_cost_minimum_with_reserve_and_losses' : 'least_cost_no_beneficial_shift' })) };
    const profileDefaults = { origin: 'Imported', schema: 'opendtu-hems-modbus-profile/v2', source: 'fixture', source_revision: 'fixture', author: 'fixture', license: 'GPL-2.0', telemetry_count: 6, used_by: [], in_use: true, control_reason: 'ready', identity_metadata: true, recovery_metadata: false };
    optimizer.config.dispatch_enabled = false;
    optimizer.dispatch = { enabled: false, paused_manual: false, state: 'disabled', global_mode: 'Active', storage_permission: true, cleanup_pending: false, cleanup_state: 'idle' };
    status.economic_dispatch = optimizer.dispatch;
    const result = { status, config, tariff, forecast, optimizer, binding: { profile: 'Tinkerforge-WARP3-Smart', control_mode: 'hems' }, profiles: { profiles: [{ id: 'Marstek-Venus-E-3.0', vendor: 'Marstek', model: 'Venus E', capabilities: ['storage'] }, { id: 'Tinkerforge-WARP3-Smart', vendor: 'Tinkerforge', model: 'WARP3', capabilities: ['evse'] }] } };
    result.profiles.profiles = result.profiles.profiles.map(profile => ({ ...profileDefaults, ...profile }));
    return result;
}

