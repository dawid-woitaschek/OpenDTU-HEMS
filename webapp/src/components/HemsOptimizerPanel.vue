<template>
    <CardElement :text="t('hemsUi.optimizer')" textVariant="text-bg-primary" add-space>
        <div class="d-flex flex-wrap gap-2 align-items-center mb-2">
            <span class="badge text-bg-secondary">SHADOW</span><strong>{{ t('hemsUi.shadowOnly') }}</strong>
        </div>
        <p class="small text-body-secondary">{{ t('hemsUi.shadowHint') }}</p>
        <p v-if="error" class="alert alert-warning" role="alert">{{ t('hemsUi.loadFailed') }} {{ error }}</p>
        <p v-if="notice" class="alert alert-success" role="status">{{ notice }}</p>
        <template v-if="plan">
            <h2 class="h5">{{ error ? t('hemsUi.unavailable') : label(plan.state) }}</h2>
            <ul v-if="plan.blockers.length" class="small">
                <li v-for="blocker in plan.blockers" :key="blocker">{{ label(blocker) }}</li>
            </ul>
            <template v-if="hasResult">
                <div class="row g-3 mb-3">
                    <div class="col-sm-6">
                        <div class="small text-body-secondary">{{ t('hemsUi.modeledSavings') }}</div>
                        <div class="fs-4 fw-semibold">{{ money(plan.modeled_savings_nano) }}</div>
                    </div>
                    <div class="col-sm-6">
                        <div class="small text-body-secondary">{{ t('hemsUi.modeledSoc') }}</div>
                        <div class="fs-4">{{ soc(plan.initial_soc_pct) }} → {{ soc(plan.final_soc_pct) }}</div>
                    </div>
                </div>
                <p class="small text-body-secondary">
                    {{ date(plan.horizon_start) }} – {{ date(plan.horizon_end) }} · {{ label(plan.price_basis || '') }}
                </p>
                <div v-if="props.view === 'overview'" class="row g-2">
                    <div v-for="step in upcoming" :key="step.start" class="col-md-4">
                        <div class="border rounded p-3 h-100">
                            <div class="small text-body-secondary">{{ date(step.start) }} – {{ date(step.end) }}</div>
                            <div class="fw-semibold my-1">
                                {{ label(step.action) }} · {{ Math.abs(step.battery_w).toLocaleString(locale) }} W
                            </div>
                            <div class="small">
                                {{ formatPrice(step.used_price_micros_per_kwh, plan.currency, locale) }} ·
                                {{ soc(step.end_soc_pct) }}
                            </div>
                            <div v-if="step.reason" class="small text-body-secondary mt-1">
                                {{ label(step.reason) }}
                            </div>
                        </div>
                    </div>
                </div>
                <details class="mt-3 small">
                    <summary>{{ t('hemsUi.assumptions') }}</summary>
                    <ul class="mt-2">
                        <li v-for="item in [...(plan.assumptions || []), ...(plan.warnings || [])]" :key="item">
                            {{ label(item) }}
                        </li>
                    </ul>
                    <p>
                        {{ t('hemsUi.baselineCost') }}: {{ money(plan.baseline_cost_nano) }} ·
                        {{ t('hemsUi.optimizedCost') }}: {{ money(plan.optimized_cost_nano) }}
                    </p>
                </details>
            </template>
            <form v-if="draft && props.view === 'settings'" class="mt-4" @submit.prevent="save">
                <label class="form-check-label d-block mb-3"
                    ><input
                        id="optimizer-enabled"
                        v-model="draft.enabled"
                        type="checkbox"
                        class="form-check-input me-2"
                    />{{ t('hemsUi.enableShadow') }}</label
                >
                <label class="form-check-label d-block mb-3"
                    ><input
                        id="optimizer-soc-confirm"
                        v-model="draft.soc_bounds_configured"
                        type="checkbox"
                        class="form-check-input me-2"
                    />{{ t('hemsUi.confirmSoc') }}</label
                >
                <div class="row g-3 mb-3">
                    <div v-for="field in fields.filter((f) => !f.advanced)" :key="field.key" class="col-sm-6">
                        <label :for="`optimizer-${field.key}`" class="form-label"
                            >{{ t('hemsUi.' + field.label) }} ({{ field.unit }})</label
                        >
                        <input
                            :id="`optimizer-${field.key}`"
                            :value="draft[field.key] / field.scale"
                            type="number"
                            class="form-control"
                            :min="field.min"
                            :max="field.max"
                            :step="field.step"
                            required
                            @input="
                                draft[field.key] = Math.round(
                                    Number(($event.target as HTMLInputElement).value) * field.scale
                                )
                            "
                        />
                    </div>
                </div>
                <p class="form-text">{{ t('hemsUi.optimizerConfigHint') }}</p>
                <label for="optimizer-price-basis" class="form-label">{{ t('hemsUi.priceBasis') }}</label>
                <select id="optimizer-price-basis" v-model="draft.effective_price" class="form-select mb-3">
                    <option :value="true">{{ t('hemsUi.effectivePrice') }}</option>
                    <option :value="false">{{ t('hemsUi.sourcePrice') }}</option>
                </select>
                <label class="form-check-label d-block mb-2"
                    ><input
                        id="optimizer-export"
                        v-model="draft.export_configured"
                        type="checkbox"
                        class="form-check-input me-2"
                    />{{ t('hemsUi.useExport') }}</label
                >
                <label for="optimizer-export-price" class="form-label">{{ t('hemsUi.exportValue') }} (ct/kWh)</label>
                <input
                    id="optimizer-export-price"
                    :value="draft.export_micros_per_kwh / 10000"
                    type="number"
                    class="form-control mb-3"
                    min="-214748.3648"
                    max="214748.3647"
                    step="0.0001"
                    :disabled="!draft.export_configured"
                    required
                    @input="
                        draft.export_micros_per_kwh = Math.round(
                            Number(($event.target as HTMLInputElement).value) * 10000
                        )
                    "
                />
                <details class="border rounded p-3 mb-3">
                    <summary>{{ t('hemsUi.advancedSettings') }}</summary>
                    <div class="row g-3 mt-1">
                        <div v-for="field in fields.filter((f) => f.advanced)" :key="field.key" class="col-sm-6">
                            <label :for="`optimizer-${field.key}`" class="form-label"
                                >{{ t('hemsUi.' + field.label) }} ({{ field.unit }})</label
                            ><input
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
                </details>
                <div class="d-flex flex-wrap gap-2">
                    <button class="btn btn-primary" :disabled="busy">{{ t('hemsUi.save') }}</button
                    ><button
                        type="button"
                        class="btn btn-outline-secondary"
                        :disabled="busy || !plan.config.enabled"
                        @click="recalculate"
                    >
                        {{ t('hemsUi.recalculate') }}
                    </button>
                </div>
            </form>
            <template v-if="props.view === 'diagnostics'">
                <details class="mt-3">
                    <summary>{{ t('hemsUi.optimizerDiagnostics') }}</summary>
                    <pre class="diagnostic-json mt-3">{{ JSON.stringify(plan, null, 2) }}</pre>
                </details>
                <div v-if="plan.schedule.length" class="table-responsive mt-3">
                    <table class="table table-sm">
                        <caption>
                            {{
                                t('hemsUi.proposedSchedule')
                            }}
                            ·
                            {{
                                zone
                            }}
                            ·
                            {{
                                plan.interval_count
                            }}
                            {{
                                t('hemsUi.intervals')
                            }}
                        </caption>
                        <thead>
                            <tr>
                                <th>{{ t('hemsUi.time') }}</th>
                                <th>{{ t('hemsUi.priceBasis') }}</th>
                                <th>PV / load W</th>
                                <th>{{ t('hemsUi.proposal') }}</th>
                                <th>W</th>
                                <th>SoC</th>
                                <th>Grid import / export Wh</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="step in plan.schedule" :key="step.start">
                                <td>{{ date(step.start) }}</td>
                                <td>{{ formatPrice(step.used_price_micros_per_kwh, plan.currency, locale) }}</td>
                                <td>{{ step.pv_w }} / {{ step.assumed_load_w }}</td>
                                <td>{{ label(step.action) }}</td>
                                <td>{{ step.battery_w }}</td>
                                <td>{{ soc(step.end_soc_pct) }}</td>
                                <td>{{ step.grid_import_wh.toFixed(1) }} / {{ step.grid_export_wh.toFixed(1) }}</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
                <div class="d-flex flex-wrap gap-2 mt-2" v-if="plan.schedule.length">
                    <button
                        class="btn btn-outline-secondary"
                        :disabled="offset === 0"
                        @click="
                            offset = Math.max(0, offset - 24);
                            load();
                        "
                    >
                        {{ t('hemsUi.previous') }}</button
                    ><button
                        class="btn btn-outline-secondary"
                        :disabled="!plan.has_more"
                        @click="
                            offset += 24;
                            load();
                        "
                    >
                        {{ t('hemsUi.next') }}
                    </button>
                </div>
            </template>
        </template>
    </CardElement>
</template>
<script setup lang="ts">
import { computed, getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { useI18n } from 'vue-i18n';
import type { EnergyView } from '@/types/HemsEnergy';
import { formatPrice } from '@/utils/hemsPresentation';
const props = defineProps<{ view?: EnergyView }>();
const { t, te, locale } = useI18n();
const now = ref(Date.now() / 1000);
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
    reason?: string;
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
    {
        key: 'capacity_wh',
        label: 'capacity',
        unit: 'kWh',
        min: 0,
        max: 1000,
        scale: 1000,
        step: 0.001,
        advanced: false,
    },
    { key: 'min_soc_pct', label: 'minSoc', unit: '%', min: 0, max: 99, scale: 1, step: 1, advanced: false },
    { key: 'max_soc_pct', label: 'maxSoc', unit: '%', min: 1, max: 100, scale: 1, step: 1, advanced: false },
    {
        key: 'charge_permille',
        label: 'chargeEfficiency',
        unit: '%',
        min: 0,
        max: 100,
        scale: 10,
        step: 0.1,
        advanced: false,
    },
    {
        key: 'discharge_permille',
        label: 'dischargeEfficiency',
        unit: '%',
        min: 0,
        max: 100,
        scale: 10,
        step: 0.1,
        advanced: false,
    },
    { key: 'max_charge_w', label: 'maxCharge', unit: 'W', min: 0, max: 100000, scale: 1, step: 1, advanced: false },
    {
        key: 'max_discharge_w',
        label: 'maxDischarge',
        unit: 'W',
        min: 0,
        max: 100000,
        scale: 1,
        step: 1,
        advanced: false,
    },
    {
        key: 'expected_base_load_w',
        label: 'baseLoad',
        unit: 'W',
        min: 0,
        max: 100000,
        scale: 1,
        step: 1,
        advanced: false,
    },
    {
        key: 'horizon_seconds',
        label: 'planHorizon',
        unit: 's',
        min: 900,
        max: 172800,
        scale: 1,
        step: 1,
        advanced: true,
    },
    { key: 'soc_step_pct', label: 'socStep', unit: '%', min: 2, max: 10, scale: 1, step: 1, advanced: true },
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
const label = (s: string) =>
    te('hemsUi.optimizerStates.' + s) ? t('hemsUi.optimizerStates.' + s) : t('hemsUi.optimizerUnavailable');
const date = (s?: number) =>
    s
        ? new Date(s * 1000).toLocaleString(locale.value, {
              month: 'short',
              day: 'numeric',
              hour: '2-digit',
              minute: '2-digit',
              timeZoneName: 'short',
          })
        : '—';
const soc = (value?: number) => (value == null ? 'Unknown' : `${value.toFixed(1)}%`);
const money = (value?: number) =>
    value == null
        ? '—'
        : `${(value / 1e9).toLocaleString(locale.value, { maximumFractionDigits: 4 })} ${plan.value?.currency || ''}`;
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
let loadGeneration = 0;
async function load(reset = false) {
    const generation = ++loadGeneration;
    try {
        now.value = Date.now() / 1000;
        const result = (await api(`?all=1&offset=${offset.value}`)) as Plan;
        if (disposed || generation !== loadGeneration) return;
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
        notice.value = t('hemsUi.shadowSaved');
        await load(reset);
    } catch (e) {
        error.value = String(e);
    } finally {
        busy.value = false;
    }
}
const upcoming = computed(() =>
    error.value ? [] : (plan.value?.schedule || []).filter((step) => step.end > now.value).slice(0, 3)
);
const hasResult = computed(() => !error.value && ['ready', 'partial'].includes(plan.value?.state || ''));
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
<style scoped>
.diagnostic-json {
    max-height: 36rem;
    overflow: auto;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    font-size: 0.8rem;
}
</style>
