<template>
    <CardElement
        :text="t('hemsUi.optimizer')"
        :help="t('redesign.optimizerHelp')"
        textVariant="text-bg-primary"
        add-space
    >
        <div class="d-flex flex-wrap gap-2 align-items-center mb-2">
            <span class="badge text-bg-secondary">{{ t('hemsUi.planning') }}</span>
            <strong>{{ t(plan?.dispatch?.enabled ? 'hemsUi.dispatchEnabled' : 'hemsUi.shadowOnly') }}</strong>
        </div>
        <div v-if="plan?.dispatch && !error" class="border rounded p-3 mb-3" role="status">
            <div class="fw-semibold">{{ dispatchLabel(plan.dispatch.state) }}</div>
            <div class="small text-body-secondary mt-1">
                {{ t('hemsUi.globalMode') }}: {{ t('hems.' + plan.dispatch.global_mode) }} ·
                {{ t('hemsUi.storagePermission') }}:
                {{ t(plan.dispatch.storage_permission ? 'hemsUi.hemsControl' : 'hemsUi.monitorOnly') }}
            </div>
            <p
                v-if="plan.dispatch.cleanup_pending || plan.dispatch.cleanup_state === 'failed'"
                class="small text-warning-emphasis mt-2 mb-0"
            >
                {{ t(plan.dispatch.cleanup_state === 'failed' ? 'hemsUi.cleanupFailed' : 'hemsUi.cleanupPending') }}
            </p>
            <button
                v-if="plan.dispatch.paused_manual"
                type="button"
                class="btn btn-outline-primary mt-2"
                :disabled="busy || !plan.config.enabled || !plan.config.dispatch_enabled"
                @click="resume"
            >
                {{ t('hemsUi.resumeEconomic') }}
            </button>
        </div>
        <p v-if="error" class="alert alert-warning" role="alert">{{ t('hemsUi.loadFailed') }} {{ error }}</p>
        <p v-if="notice" class="alert alert-success" role="status">{{ notice }}</p>
        <template v-if="plan">
            <div class="border rounded p-3 mb-3 small" v-if="!error && props.view !== 'settings'">
                <strong>{{ t('hemsUi.storagePolicies') }}</strong>
                <div>
                    {{ t('hemsUi.batteryReserve') }}: {{ plan.config.min_soc_pct }}% · {{ t('hemsUi.maxDesiredSoc') }}:
                    {{ plan.config.max_soc_pct }}%
                </div>
                <div>
                    {{ t('hemsUi.gridCharging') }}:
                    {{
                        t(plan.config.policies?.grid_charging_allowed === false ? 'hemsUi.forbidden' : 'hemsUi.allowed')
                    }}
                </div>
                <div>
                    {{ t('hemsUi.economicDischarge') }}:
                    {{ t(plan.config.policies?.discharge_allowed === false ? 'hemsUi.forbidden' : 'hemsUi.allowed') }}
                </div>
                <div v-if="plan.config.policies?.target_enabled">
                    {{
                        t('hemsUi.targetSummary', {
                            soc: plan.config.policies.target_soc_pct,
                            deadline: date(plan.config.policies.target_deadline),
                        })
                    }}
                    <span v-if="plan.target_state && hasResult"> · {{ label(plan.target_state) }}</span>
                </div>
                <div v-if="plan.current_charge_limit_w != null">
                    {{ t('hemsUi.currentChargeLimit') }}: {{ plan.current_charge_limit_w }} W
                </div>
                <div v-if="plan.current_discharge_limit_w != null">
                    {{ t('hemsUi.currentDischargeLimit') }}: {{ plan.current_discharge_limit_w }} W
                </div>
                <ul v-if="plan.policy_restrictions?.length" class="mb-0 mt-2 ps-3">
                    <li v-for="item in plan.policy_restrictions" :key="item">{{ label(item) }}</li>
                </ul>
            </div>
            <h2 class="h5">{{ error ? t('hemsUi.unavailable') : label(plan.state) }}</h2>
            <ul v-if="plan.blockers.length" class="small">
                <li v-for="blocker in plan.blockers" :key="blocker">{{ label(blocker) }}</li>
            </ul>
            <RouterLink
                v-if="plan.blockers.length && props.view === 'overview'"
                to="/energy/planning"
                class="small d-inline-block mb-3"
                >{{ t('hemsUi.reviewPrerequisites') }}</RouterLink
            >
            <template v-if="hasResult && props.view !== 'settings'">
                <div class="row g-3 mb-3">
                    <div class="col-sm-6">
                        <div class="small text-body-secondary">{{ t('hemsUi.modeledSavings') }}</div>
                        <div class="fs-4 fw-semibold">{{ money(plan.modeled_savings_nano) }}</div>
                        <div class="small text-body-secondary">{{ t('hemsUi.savingsHorizon') }}</div>
                    </div>
                    <div class="col-sm-6">
                        <div class="small text-body-secondary">{{ t('hemsUi.modeledSoc') }}</div>
                        <div class="fs-4">{{ soc(plan.initial_soc_pct) }} → {{ soc(plan.final_soc_pct) }}</div>
                    </div>
                </div>
                <EnergyManagementSummary :plan="plan" />
                <p class="small text-body-secondary">
                    {{ date(plan.horizon_start) }} – {{ date(plan.horizon_end) }} · {{ label(plan.price_basis || '') }}
                </p>
                <div
                    v-if="plan.state === 'partial' || plan.warnings?.length"
                    class="rounded bg-body-tertiary p-3 mb-3 small"
                    role="status"
                >
                    <strong>{{ t('hemsUi.planLimits') }}</strong>
                    <p v-if="plan.state === 'partial'" class="mt-1 mb-1">{{ t('hemsUi.partialPlanHint') }}</p>
                    <ul v-if="plan.warnings?.length" class="mb-0 mt-1 ps-3">
                        <li v-for="warning in plan.warnings" :key="warning">{{ label(warning) }}</li>
                    </ul>
                </div>
                <p v-if="props.view === 'overview'" class="small fw-semibold">{{ t('hemsUi.upcomingProposals') }}</p>
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
                        <li v-for="item in plan.assumptions || []" :key="item">
                            {{ label(item) }}
                        </li>
                    </ul>
                    <p>
                        {{ t('hemsUi.baselineCost') }}: {{ money(plan.baseline_cost_nano) }} ·
                        {{ t('hemsUi.optimizedCost') }}: {{ money(plan.optimized_cost_nano) }}
                    </p>
                </details>
                <RouterLink
                    v-if="props.view === 'overview'"
                    to="/energy/diagnostics"
                    class="small d-inline-block mt-3"
                    >{{ t('hemsUi.fullSchedule') }}</RouterLink
                >
            </template>
            <form v-if="draft && props.view === 'settings'" class="mt-4" @submit.prevent="save">
                <div class="form-check mb-3">
                    <input
                        id="optimizer-enabled"
                        v-model="draft.enabled"
                        type="checkbox"
                        class="form-check-input me-2"
                    /><FieldLabel
                        for-id="optimizer-enabled"
                        :text="t('hemsUi.enableShadow')"
                        :help="t('redesign.shadowHelp')"
                        label-class="form-check-label"
                    />
                </div>
                <div class="form-check mb-3">
                    <input
                        id="optimizer-soc-confirm"
                        v-model="draft.soc_bounds_configured"
                        type="checkbox"
                        class="form-check-input me-2"
                    /><FieldLabel
                        for-id="optimizer-soc-confirm"
                        :text="t('hemsUi.confirmSoc')"
                        :help="t('redesign.socConfirmHelp')"
                        label-class="form-check-label"
                    />
                </div>

                <label for="optimizer-expected_base_load_w" class="form-label">{{ t('hemsUi.baseLoad') }} (W)</label>
                <input
                    id="optimizer-expected_base_load_w"
                    v-model.number="draft.expected_base_load_w"
                    type="number"
                    min="0"
                    max="100000"
                    step="1"
                    class="form-control mb-3"
                    required
                />
                <fieldset class="border rounded p-3 mb-3" v-if="draft.policies">
                    <legend class="float-none w-auto fs-6 px-1">
                        {{ t('hemsUi.storagePolicies') }}
                    </legend>
                    <div class="row g-3">
                        <div class="col-sm-6">
                            <FieldLabel
                                for-id="optimizer-min_soc_pct"
                                :text="t('hemsUi.batteryReserve') + ' (%)'"
                                :help="t('redesign.reserveHelp')"
                                label-class="form-label"
                            />
                            <input
                                id="optimizer-min_soc_pct"
                                v-model.number="draft.min_soc_pct"
                                type="number"
                                class="form-control"
                                min="0"
                                :max="Math.min(99, draft.max_soc_pct)"
                                step="1"
                                required
                            />
                        </div>
                        <div class="col-sm-6">
                            <FieldLabel
                                for-id="optimizer-max_soc_pct"
                                :text="t('hemsUi.maxDesiredSoc') + ' (%)'"
                                :help="t('redesign.maxSocHelp')"
                                label-class="form-label"
                            />
                            <input
                                id="optimizer-max_soc_pct"
                                v-model.number="draft.max_soc_pct"
                                type="number"
                                class="form-control"
                                :min="Math.max(1, draft.min_soc_pct)"
                                max="100"
                                step="1"
                                required
                            />
                        </div>
                        <div class="col-sm-6">
                            <FieldLabel
                                for-id="optimizer-grid-charging"
                                :text="t('hemsUi.gridCharging') + ''"
                                :help="t('redesign.gridChargeHelp')"
                                label-class="form-label"
                            />
                            <select
                                id="optimizer-grid-charging"
                                v-model="draft.policies.grid_charging_allowed"
                                class="form-select"
                            >
                                <option :value="true">{{ t('hemsUi.allowed') }}</option>
                                <option :value="false">{{ t('hemsUi.forbidden') }}</option>
                            </select>
                        </div>
                        <div class="col-sm-6">
                            <FieldLabel
                                for-id="optimizer-economic-discharge"
                                :text="t('hemsUi.economicDischarge') + ''"
                                :help="t('redesign.dischargeHelp')"
                                label-class="form-label"
                            />
                            <select
                                id="optimizer-economic-discharge"
                                v-model="draft.policies.discharge_allowed"
                                class="form-select"
                            >
                                <option :value="true">{{ t('hemsUi.allowed') }}</option>
                                <option :value="false">{{ t('hemsUi.forbidden') }}</option>
                            </select>
                        </div>
                    </div>
                    <div class="form-check mb-3">
                        <input
                            id="optimizer-target-enabled"
                            v-model="draft.policies.target_enabled"
                            type="checkbox"
                            class="form-check-input me-2"
                        /><FieldLabel
                            for-id="optimizer-target-enabled"
                            :text="t('hemsUi.targetCharge')"
                            :help="t('redesign.targetHelp')"
                            label-class="form-check-label"
                        />
                    </div>
                    <div v-if="draft.policies.target_enabled" class="row g-3">
                        <div class="col-sm-4">
                            <label for="optimizer-target-soc" class="form-label"
                                >{{ t('hemsUi.targetAtLeast') }} (%)</label
                            >
                            <input
                                id="optimizer-target-soc"
                                v-model.number="draft.policies.target_soc_pct"
                                type="number"
                                class="form-control"
                                :min="draft.min_soc_pct"
                                :max="draft.max_soc_pct"
                                step="1"
                                required
                            />
                        </div>
                        <div class="col-sm-8">
                            <label for="optimizer-target-deadline" class="form-label"
                                >{{ t('hemsUi.targetBy') }} · {{ zone }}</label
                            >
                            <input
                                id="optimizer-target-deadline"
                                :value="localDeadline(draft.policies.target_deadline)"
                                type="datetime-local"
                                class="form-control"
                                required
                                @input="setDeadline"
                            />
                        </div>
                    </div>
                </fieldset>

                <div class="border rounded p-3 mb-3">
                    <div class="form-check mb-3">
                        <input
                            id="optimizer-dispatch-enabled"
                            v-model="draft.dispatch_enabled"
                            type="checkbox"
                            class="form-check-input me-2"
                        /><FieldLabel
                            for-id="optimizer-dispatch-enabled"
                            :text="t('hemsUi.enableDispatch')"
                            :help="t('redesign.dispatchHelp')"
                            label-class="form-check-label"
                        />
                    </div>
                </div>

                <details class="border rounded p-3 mb-3">
                    <summary>{{ t('hemsUi.energyManagement') }}</summary>
                    <EnergyManagementSettings
                        class="mt-3"
                        v-model="draft.energy_management"
                        @reference="applyReference"
                    />
                </details>
                <details class="border rounded p-3 mb-3">
                    <summary>
                        {{ t('redesign.advancedOptimizer') }}
                    </summary>
                    <div class="row g-3 mt-1">
                        <div v-for="field in advancedFields" :key="field.key" class="col-sm-6">
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
                    <hr />
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
                    <label for="optimizer-export-price" class="form-label"
                        >{{ t('hemsUi.exportValue') }} (ct/kWh)</label
                    >
                    <input
                        id="optimizer-export-price"
                        :value="draft.export_micros_per_kwh / 10000"
                        type="number"
                        class="form-control"
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
import FieldLabel from '@/components/FieldLabel.vue';
import EnergyManagementSummary from '@/components/hems/EnergyManagementSummary.vue';
import EnergyManagementSettings from '@/components/hems/EnergyManagementSettings.vue';
import { defaultEnergyManagement, type EnergyManagement } from '@/utils/hemsEnergyManagement';
import { computed, getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { useI18n } from 'vue-i18n';
import type { EnergyView } from '@/types/HemsEnergy';
import type { EconomicDispatch } from '@/types/HemsStatus';
import { formatPrice } from '@/utils/hemsPresentation';
import { localDeadline, utcDeadline } from '@/utils/hemsPolicy';
const props = defineProps<{ view?: EnergyView }>();
const { t, te, locale } = useI18n();
const now = ref(Date.now() / 1000);
type StoragePolicies = {
    grid_charging_allowed: boolean;
    discharge_allowed: boolean;
    target_enabled: boolean;
    target_soc_pct: number;
    target_deadline: number;
};
type Config = {
    soc_bounds_configured: boolean;
    enabled: boolean;
    dispatch_enabled: boolean;
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
    policies: StoragePolicies;
    energy_management: EnergyManagement;
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
    dispatch?: EconomicDispatch & {
        coordination_state?: string;
        export_enforcement_available?: boolean;
        coordinated_battery_w?: number;
        planned_battery_w?: number;
        last_power_w?: number;
    };
    load_forecast_source?: string;
    load_learned_slots?: number;
    load_total_slots?: number;
    cost_breakdown?: {
        import_nano: number;
        export_nano: number;
        degradation_nano: number;
        buffer_nano: number;
        terminal_inventory_change_nano: number;
        curtailed_wh: number;
        wear_source: number;
    };
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
    current_charge_limit_w?: number | null;
    current_discharge_limit_w?: number | null;
    target_state?: string;
    policy_restrictions?: string[];
};
const advancedFields = [
    { key: 'capacity_wh', label: 'capacity', unit: 'kWh', min: 0, max: 1000, scale: 1000, step: 0.001 },
    { key: 'charge_permille', label: 'chargeEfficiency', unit: '%', min: 0, max: 100, scale: 10, step: 0.1 },
    { key: 'discharge_permille', label: 'dischargeEfficiency', unit: '%', min: 0, max: 100, scale: 10, step: 0.1 },
    { key: 'max_charge_w', label: 'maxCharge', unit: 'W', min: 0, max: 100000, scale: 1, step: 1 },
    { key: 'max_discharge_w', label: 'maxDischarge', unit: 'W', min: 0, max: 100000, scale: 1, step: 1 },
    { key: 'horizon_seconds', label: 'planHorizon', unit: 's', min: 900, max: 172800, scale: 1, step: 1 },
    { key: 'soc_step_pct', label: 'socStep', unit: '%', min: 2, max: 10, scale: 1, step: 1 },
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
const dispatchLabel = (s: string) =>
    te('hemsUi.dispatchStates.' + s) ? t('hemsUi.dispatchStates.' + s) : t('hemsUi.dispatchStates.plan_unavailable');
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
        if (!draft.value || reset) {
            draft.value = structuredClone(result.config);
            draft.value.dispatch_enabled ??= false;
            draft.value.energy_management ||= defaultEnergyManagement();
            draft.value.policies ||= {
                grid_charging_allowed: true,
                discharge_allowed: true,
                target_enabled: false,
                target_soc_pct: 70,
                target_deadline: 0,
            };
        }
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
        notice.value = t('hemsUi.economicSaved');
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
function setDeadline(event: Event) {
    const input = event.target as HTMLInputElement;
    const epoch = utcDeadline(input.value);
    input.setCustomValidity(epoch ? '' : t('hemsUi.invalidDeadline'));
    if (draft.value) draft.value.policies.target_deadline = epoch;
}
function applyReference() {
    if (!draft.value) return;
    draft.value.policies.grid_charging_allowed = true;
    draft.value.policies.discharge_allowed = true;
    draft.value.policies.target_enabled = false;
    draft.value.soc_step_pct = 2;
}
const save = () => perform('/config', draft.value, true);
const recalculate = () => perform('/recalculate', {});
const resume = () => perform('/resume', {});
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
