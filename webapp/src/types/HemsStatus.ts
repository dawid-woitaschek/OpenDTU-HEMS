export interface HemsStatus {
    state: 'Disabled' | 'Monitor';
    decision: 'None';
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
}
