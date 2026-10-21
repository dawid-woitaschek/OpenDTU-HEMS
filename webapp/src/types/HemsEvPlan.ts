export interface EvShadowConfig {
    enabled: boolean;
    deadline: number;
    required_wh: number;
    min_current_a: number;
    max_current_a: number;
    max_price_enabled: boolean;
    max_price_micros_per_kwh: number;
    pv_preferred: boolean;
    nominal_voltage: number;
    phases: number;
}
export interface EvShadowStep {
    start: number;
    end: number;
    charge_end: number;
    current_a: number;
    action: string;
    energy_wh: number;
    storage_w: number;
    residual_pv_w: number;
    cost_nano: number;
}
export interface EvShadowPlan {
    config: EvShadowConfig;
    enabled: boolean;
    shadow_only: true;
    device_writes: false;
    reason: string;
    coverage_reason: string | null;
    stale_inputs: boolean;
    plan_revision: number;
    input_revision: number;
    storage_plan_revision: number;
    current_action: string;
    current_a: number;
    next_change: number | null;
    next_action: string;
    completion: number | null;
    required_wh: number;
    planned_wh: number | null;
    deliverable_wh: number | null;
    shortfall_wh: number | null;
    cost_nano: number | null;
    currency: string;
    storage_interaction: string;
    storage_conflict: boolean;
    pv_included: boolean;
    has_more: boolean;
    schedule: EvShadowStep[];
}
