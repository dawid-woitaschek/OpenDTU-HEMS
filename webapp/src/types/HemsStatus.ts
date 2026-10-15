export type HemsQuality = 'fresh' | 'stale' | 'unknown';

export interface HemsSignal<T> {
    quality: HemsQuality;
    value: T | null;
}

export interface HemsDeviceState {
    configured: boolean;
    enabled: boolean;
    reachable: boolean;
    has_update: boolean;
    age_ms: number | null;
}

export interface HemsPowerLimit {
    active: boolean;
    value_w: number | null;
}

export interface HemsActuationTarget {
    profile: string;
    control_mode: 'monitor' | 'hems';
    permission_granted: boolean;
    allowed: boolean;
    ready_for_active: boolean;
    blockers: string[];
    ownership_uncertain: boolean;
    command: {
        last_error: string;
        last_stage: string;
        execution_error: string;
        transport_error: string;
        planning_error: string;
        state: 'idle' | 'pending' | 'running' | 'succeeded' | 'failed' | 'cancelled' | (string & {});
        id: number | null;
        generation: number | null;
        action: string | null;
        value: number | null;
    };
    validation: { allowed: boolean; blockers: string[] };
    recovery: {
        state:
            | 'not_loaded'
            | 'idle'
            | 'armed'
            | 'recovery_pending'
            | 'checking_identity'
            | 'recovery_running'
            | 'retry_wait'
            | 'recovered'
            | 'unresolved'
            | 'operator_resolving'
            | (string & {});
        target: string;
        journal_present: boolean;
        automatic_supported: boolean;
        identity_available: boolean;
        identity_supported: boolean;
        blocked: boolean;
        reason: string;
        attempts: number;
        retry_in_ms: number;
        clear_pending: boolean;
        record_checksum: number;
        record_id: string;
        resolution_available: boolean;
        resolution_pending: boolean;
        operator_acknowledged: boolean;
        last_error: string;
        last_stage: string;
        execution_error: string;
        transport_error: string;
    };
    cleanup: {
        state: 'idle' | 'pending' | 'running' | 'retry_wait' | 'succeeded' | (string & {});
        retained_context: boolean;
        attempts: number;
        last_error: string;
        last_stage: string;
        execution_error: string;
        transport_error: string;
        modbus_exception: number;
    };
}

export interface HemsStatus {
    state: 'Disabled' | 'Monitor';
    decision: 'None';
    validation: {
        state: 'inactive' | 'active' | 'expired' | 'revoked' | (string & {});
        active: boolean;
        remaining_ms: number;
        commands_accepted: number;
    };
    external_devices: number;
    warp: {
        configured: boolean;
        enabled: boolean;
        reachable: boolean;
        valid: boolean;
        stale: boolean;
        status: 'Disabled' | 'Connected' | 'Unreachable' | 'Stale' | 'Invalid';
        error: string;
        modbus_exception: number;
        successful_polls: number;
        failed_polls: number;
        last_success_ms: number | null;
        age_ms: number | null;
        vehicle_connected: boolean | null;
        charging: boolean | null;
        power_w: number | null;
        allowed_current_a: number | null;
        session_energy_kwh: number | null;
        iec_state: number | null;
        charging_state: number | null;
        table_version: number | null;
        meter_available: boolean | null;
    };
    storage: {
        configured: boolean;
        enabled: boolean;
        profile_loaded: boolean;
        reachable: boolean;
        valid: boolean;
        stale: boolean;
        status: 'Disabled' | 'Connected' | 'Unreachable' | 'Stale' | 'Invalid';
        error: string;
        modbus_exception: number;
        successful_polls: number;
        failed_polls: number;
        last_success_ms: number | null;
        age_ms: number | null;
        profile: string;
        host: string;
        port: number;
        unit: number;
        soc_pct: number | null;
        ac_power_w: number | null;
        dc_power_w: number | null;
        battery_voltage_v: number | null;
        battery_current_a: number | null;
        temperature_c: number | null;
        charge_energy_total_kwh: number | null;
        discharge_energy_total_kwh: number | null;
        operating_state: number | null;
        fault_code: number | null;
        alarm_code: number | null;
        charging: boolean | null;
        discharging: boolean | null;
        fault_active: boolean | null;
        alarm_active: boolean | null;
    };
    normalized: {
        grid: {
            power_w: HemsSignal<number>;
        };
        pv: {
            power_w: HemsSignal<number>;
        };
        storage: {
            device: HemsDeviceState;
            soc_pct: HemsSignal<number>;
            ac_power_w: HemsSignal<number>;
            dc_power_w: HemsSignal<number>;
            battery_voltage_v: HemsSignal<number>;
            battery_current_a: HemsSignal<number>;
            temperature_c: HemsSignal<number>;
            charging: HemsSignal<boolean>;
            discharging: HemsSignal<boolean>;
            fault_active: HemsSignal<boolean>;
            alarm_active: HemsSignal<boolean>;
        };
        evse: {
            device: HemsDeviceState;
            vehicle_connected: HemsSignal<boolean>;
            charging: HemsSignal<boolean>;
            power_w: HemsSignal<number>;
            allowed_current_a: HemsSignal<number>;
        };
    };
    constraints: {
        storage: {
            charge_allowed: boolean;
            discharge_allowed: boolean;
            max_charge_power: HemsPowerLimit;
            max_discharge_power: HemsPowerLimit;
        };
    };
    actuation: {
        global_active: boolean;
        evse: HemsActuationTarget;
        storage: HemsActuationTarget;
    };
    shadow: {
        ev_protection: {
            state: 'inactive' | 'waiting' | 'would_block_storage_discharge';
            debounce_ms: number;
            condition_age_ms: number | null;
            charging_known: boolean;
            charging: boolean | null;
            would_block_storage_discharge: boolean;
        };
    };
}
