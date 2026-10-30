export interface RemoteDiagnosticsConfig {
    enabled: boolean;
    endpoint: string;
    interval_minutes: number;
    quarter_hour: boolean;
    token_set: boolean;
}
export interface RemoteDiagnosticsStatus {
    enabled: boolean;
    token_set: boolean;
    busy: boolean;
    send_requested: boolean;
    last_attempt_uptime_ms: number | null;
    last_success_uptime_ms: number | null;
    last_http_result: number;
    last_error: string | null;
    runtime_reason: string | null;
    next_upload_in_seconds: number | null;
    telemetry_sequence: number;
    retry_pending: boolean;
    last_acknowledged_checkpoint_sequence: number;
    oldest_available_sequence: number;
    newest_sequence: number;
    pending_samples: number;
    dropped_samples: number;
    pending_allocation_failures: number;
    dropped_allocation_failures: number;
    spool_available: boolean;
    spool_capacity: number;
    quarter_hour: boolean;
    pending_transmission_age_seconds: number | null;
    expired_transmission_count: number;
    dropped_telemetry_count: number;
    coalesced_hems_trigger_count: number;
    last_hems_capture_utc: number | null;
    last_hems_capture_uptime_ms: number;
    last_hems_trigger: string[];
    pending_hems_trigger: string[];
    last_optimizer_plan_revision_observed: number;
    expired_samples: number;
    expired_allocation_failures: number;
    payload_bytes: number;
    payload_max_bytes: number;
}
export function remoteDiagnosticsWrite(config: RemoteDiagnosticsConfig, token: string, clear: boolean) {
    const result: {
        enabled: boolean;
        endpoint: string;
        interval_minutes: number;
        quarter_hour: boolean;
        token?: string;
    } = {
        enabled: config.enabled,
        endpoint: config.endpoint,
        interval_minutes: config.interval_minutes,
        quarter_hour: config.quarter_hour,
    };
    if (clear) result.token = '';
    else if (token) result.token = token;
    return result;
}
