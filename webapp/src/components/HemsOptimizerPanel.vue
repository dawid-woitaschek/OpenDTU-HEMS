<template>
    <CardElement text="Economic Shadow Optimizer" textVariant="text-bg-primary" add-space>
        <p class="alert alert-info">
            <strong>SHADOW PLAN · NO DEVICE WRITES</strong><br />Calculates a proposed schedule only. Modeled savings
            depend on the explicit load and battery assumptions.
        </p>
        <p v-if="error" class="alert alert-warning" role="alert">{{ error }}</p>
        <p v-if="notice" class="alert alert-success" role="status">{{ notice }}</p>
        <template v-if="plan">
            <h3 class="h5">{{ label(plan.state) }}</h3>
            <ul v-if="plan.blockers.length">
                <li v-for="blocker in plan.blockers" :key="blocker">{{ label(blocker) }}</li>
            </ul>
            <dl class="row">
                <dt class="col-sm-4">Tariff / PV</dt>
                <dd class="col-sm-8">
                    {{ plan.tariff_source || 'Unavailable' }} / {{ plan.forecast_source || 'Excluded' }}
                </dd>
                <dt class="col-sm-4">Storage SoC</dt>
                <dd class="col-sm-8">
                    {{ plan.storage_soc_fresh ? 'Fresh' : 'Unavailable or stale' }} · {{ soc(plan.initial_soc_pct) }} →
                    {{ soc(plan.final_soc_pct) }}
                </dd>
                <dt class="col-sm-4">EV Protection now</dt>
                <dd class="col-sm-8">
                    {{
                        plan.current_discharge_allowed === false
                            ? 'Discharge blocked in current interval'
                            : 'No immediate discharge restriction'
                    }}
                    · Future EV state unknown
                </dd>
                <dt class="col-sm-4">Baseline modeled cost</dt>
                <dd class="col-sm-8">{{ money(plan.baseline_cost_nano) }}</dd>
                <dt class="col-sm-4">Optimized modeled cost</dt>
                <dd class="col-sm-8">{{ money(plan.optimized_cost_nano) }}</dd>
                <dt class="col-sm-4">Modeled savings</dt>
                <dd class="col-sm-8">{{ money(plan.modeled_savings_nano) }}</dd>
                <dt class="col-sm-4">Horizon</dt>
                <dd class="col-sm-8">
                    {{ date(plan.horizon_start) }} – {{ date(plan.horizon_end) }} · {{ label(plan.price_basis || '') }}
                </dd>
            </dl>
            <details v-if="plan.assumptions?.length">
                <summary>Assumptions and warnings</summary>
                <ul>
                    <li v-for="item in [...plan.assumptions, ...(plan.warnings || [])]" :key="item">
                        {{ label(item) }}
                    </li>
                </ul>
            </details>
            <form v-if="draft" class="mt-3" @submit.prevent="save">
                <div class="form-check mb-3">
                    <input
                        id="optimizer-enabled"
                        v-model="draft.enabled"
                        type="checkbox"
                        class="form-check-input"
                    /><label for="optimizer-enabled" class="form-check-label">Enable shadow calculations</label>
                </div>
                <div class="form-check mb-3">
                    <input
                        id="optimizer-soc-confirm"
                        v-model="draft.soc_bounds_configured"
                        type="checkbox"
                        class="form-check-input"
                    /><label for="optimizer-soc-confirm" class="form-check-label"
                        >I have explicitly configured the minimum and maximum SoC below</label
                    >
                </div>
                <div class="row">
                    <div v-for="field in fields" :key="field.key" class="col-sm-6 mb-2">
                        <label :for="`optimizer-${field.key}`" class="form-label">{{ field.label }}</label>
                        <input
                            :id="`optimizer-${field.key}`"
                            v-model.number="draft[field.key]"
                            type="number"
                            class="form-control"
                            :min="field.min"
                            :max="field.max"
                            step="1"
                            required
                        />
                    </div>
                </div>
                <p class="form-text">
                    Efficiency uses per mille: 950 = 95%. Zero capacity, efficiency or power limits leave the optimizer
                    Not ready. Base load is a constant assumption, not a household forecast.
                </p>
                <label for="optimizer-price-basis" class="form-label">Price basis</label>
                <select id="optimizer-price-basis" v-model="draft.effective_price" class="form-select mb-2">
                    <option :value="true">Effective variable import price (requires configured components)</option>
                    <option :value="false">Source / market price only</option>
                </select>
                <div class="form-check mb-2">
                    <input
                        id="optimizer-export"
                        v-model="draft.export_configured"
                        type="checkbox"
                        class="form-check-input"
                    /><label for="optimizer-export" class="form-check-label">Use explicit export compensation</label>
                </div>
                <label for="optimizer-export-price" class="form-label">Export value (currency/kWh)</label
                ><input
                    id="optimizer-export-price"
                    :value="draft.export_micros_per_kwh / 1e6"
                    type="number"
                    class="form-control mb-3"
                    min="-2147"
                    max="2147"
                    step="0.000001"
                    :disabled="!draft.export_configured"
                    @input="
                        draft.export_micros_per_kwh = Math.round(
                            Number(($event.target as HTMLInputElement).value) * 1e6
                        )
                    "
                />
                <button class="btn btn-primary me-2" :disabled="busy">Save shadow settings</button
                ><button
                    type="button"
                    class="btn btn-secondary"
                    :disabled="busy || !plan.config.enabled"
                    @click="recalculate"
                >
                    Recalculate
                </button>
            </form>
            <div v-if="plan.schedule.length" class="table-responsive mt-3">
                <table class="table table-sm">
                    <caption>
                        Proposed schedule ·
                        {{
                            zone
                        }}
                        ·
                        {{
                            plan.interval_count
                        }}
                        intervals.
                        {{
                            plan.config.expected_base_load_w
                        }}
                        W assumed base load. Battery + charging / − discharging.
                    </caption>
                    <thead>
                        <tr>
                            <th>Time</th>
                            <th>Price / kWh</th>
                            <th>PV / load W</th>
                            <th>Battery action</th>
                            <th>Power W</th>
                            <th>End SoC</th>
                            <th>Grid import / export Wh</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="step in plan.schedule" :key="step.start">
                            <td>{{ date(step.start) }}</td>
                            <td>{{ (step.used_price_micros_per_kwh / 1e6).toFixed(4) }}</td>
                            <td>{{ step.pv_w }} / {{ step.assumed_load_w }}</td>
                            <td>{{ label(step.action) }}</td>
                            <td>{{ step.battery_w }}</td>
                            <td>{{ step.end_soc_pct.toFixed(1) }}%</td>
                            <td>{{ step.grid_import_wh.toFixed(1) }} / {{ step.grid_export_wh.toFixed(1) }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div v-if="plan.schedule.length" class="mt-2">
                <button
                    class="btn btn-outline-secondary me-2"
                    :disabled="offset === 0"
                    @click="
                        offset = Math.max(0, offset - 24);
                        load();
                    "
                >
                    Previous intervals</button
                ><button
                    class="btn btn-outline-secondary"
                    :disabled="!plan.has_more"
                    @click="
                        offset += 24;
                        load();
                    "
                >
                    Next intervals
                </button>
            </div>
        </template>
    </CardElement>
</template>
<script setup lang="ts">
import { getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
type Config = {
    soc_bounds_configured: boolean;
    enabled: boolean;
    effective_price: boolean;
    export_configured: boolean;
    export_micros_per_kwh: number;
    horizon_seconds: number;
    capacity_wh: number;
    min_soc_pct: number;
    max_soc_pct: number;
    charge_permille: number;
    discharge_permille: number;
    max_charge_w: number;
    max_discharge_w: number;
    expected_base_load_w: number;
    soc_step_pct: number;
};
type Step = {
    start: number;
    end: number;
    used_price_micros_per_kwh: number;
    pv_w: number;
    assumed_load_w: number;
    action: string;
    battery_w: number;
    end_soc_pct: number;
    grid_import_wh: number;
    grid_export_wh: number;
};
type Plan = {
    config: Config;
    state: string;
    blockers: string[];
    schedule: Step[];
    assumptions?: string[];
    warnings?: string[];
    tariff_source?: string;
    forecast_source?: string;
    storage_soc_fresh?: boolean;
    initial_soc_pct?: number;
    final_soc_pct?: number;
    current_discharge_allowed?: boolean;
    baseline_cost_nano?: number;
    optimized_cost_nano?: number;
    modeled_savings_nano?: number;
    horizon_start?: number;
    horizon_end?: number;
    price_basis?: string;
    currency?: string;
    interval_count?: number;
    schedule_offset?: number;
    has_more?: boolean;
};
const fields = [
    { key: 'capacity_wh', label: 'Battery capacity Wh', min: 0, max: 1000000 },
    { key: 'min_soc_pct', label: 'Minimum SoC %', min: 0, max: 99 },
    { key: 'max_soc_pct', label: 'Maximum SoC %', min: 1, max: 100 },
    { key: 'charge_permille', label: 'Charge efficiency ‰', min: 0, max: 1000 },
    { key: 'discharge_permille', label: 'Discharge efficiency ‰', min: 0, max: 1000 },
    { key: 'max_charge_w', label: 'Maximum AC charge W', min: 0, max: 100000 },
    { key: 'max_discharge_w', label: 'Maximum AC discharge W', min: 0, max: 100000 },
    { key: 'expected_base_load_w', label: 'Assumed constant base load W', min: 0, max: 100000 },
    { key: 'horizon_seconds', label: 'Planning horizon seconds (up to 48 hours)', min: 900, max: 172800 },
    { key: 'soc_step_pct', label: 'SoC planning step %', min: 2, max: 10 },
] as const;
const offset = ref(0);
const plan = ref<Plan>();
const draft = ref<Config>();
const error = ref(''),
    notice = ref(''),
    busy = ref(false);
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
const label = (s: string) => s.replaceAll('_', ' ');
const date = (s?: number) => (s ? new Date(s * 1000).toLocaleString(undefined, { timeZoneName: 'short' }) : 'No plan');
const soc = (value?: number) => (value == null ? 'Unknown' : `${value.toFixed(1)}%`);
const money = (value?: number) =>
    value == null ? 'Unavailable' : `${(value / 1e9).toFixed(4)} ${plan.value?.currency || ''}`;
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false;
async function api(suffix = '', data?: unknown) {
    const body = new FormData();
    if (data !== undefined) body.append('data', JSON.stringify(data));
    return fetch(`/api/hems/optimizer${suffix}`, {
        headers: authHeader(),
        ...(data !== undefined ? { method: 'POST', body } : {}),
    }).then((r) => handleResponse(r, emitter, router, true));
}
async function load(reset = false) {
    try {
        const result = (await api(`?all=1&offset=${offset.value}`)) as Plan;
        if (disposed) return;
        plan.value = result;
        if (!draft.value || reset) draft.value = structuredClone(result.config);
        error.value = '';
    } catch (e) {
        error.value = String(e);
    }
}
async function perform(suffix: string, data: unknown, reset = false) {
    busy.value = true;
    notice.value = '';
    try {
        await api(suffix, data);
        notice.value = 'Saved or queued. Shadow calculation only.';
        await load(reset);
    } catch (e) {
        error.value = String(e);
    } finally {
        busy.value = false;
    }
}
const save = () => perform('/config', draft.value, true);
const recalculate = () => perform('/recalculate', {});
onMounted(() => {
    void load();
    timer = setInterval(() => {
        if (!busy.value) void load();
    }, 15000);
});
onUnmounted(() => {
    disposed = true;
    if (timer) clearInterval(timer);
});
</script>
