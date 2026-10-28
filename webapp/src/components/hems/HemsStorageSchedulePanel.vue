<template>
    <CardElement :text="copy.title" textVariant="text-bg-primary" add-space>
        <div class="d-flex flex-wrap align-items-center justify-content-between gap-2 mb-2">
            <div class="d-flex flex-wrap align-items-center gap-2">
                <span class="badge text-bg-secondary">{{ t('hemsUi.planning') }}</span>
                <strong>{{ t(plan?.dispatch?.enabled ? 'hemsUi.dispatchEnabled' : 'hemsUi.shadowOnly') }}</strong>
            </div>
            <span v-if="plan?.dispatch && !error" class="small text-body-secondary">
                {{ dispatchLabel(plan.dispatch.state) }}
            </span>
        </div>
        <HelpHint :text="t('hemsUi.dispatchHint')" :label="t('hemsUi.planning')" />

        <p v-if="error" class="alert alert-warning" role="alert">{{ copy.loadFailed }} {{ error }}</p>
        <template v-if="plan && !error">
            <div
                v-if="plan.dispatch && (plan.dispatch.enabled || plan.dispatch.paused_manual)"
                class="dispatch-strip rounded border p-3 mb-3 d-flex flex-wrap align-items-center justify-content-between gap-2"
                role="status"
            >
                <div>
                    <div class="fw-semibold">{{ dispatchLabel(plan.dispatch.state) }}</div>
                    <div class="small text-body-secondary mt-1">
                        {{ t('hemsUi.globalMode') }}: {{ t('hems.' + plan.dispatch.global_mode) }} ·
                        {{ t('hemsUi.storagePermission') }}:
                        {{ t(plan.dispatch.storage_permission ? 'hemsUi.hemsControl' : 'hemsUi.monitorOnly') }}
                    </div>
                </div>
                <button
                    v-if="plan.dispatch.paused_manual"
                    type="button"
                    class="btn btn-outline-primary"
                    :disabled="busy || !plan.config.enabled || !plan.config.dispatch_enabled"
                    @click="resume"
                >
                    {{ t('hemsUi.resumeEconomic') }}
                </button>
            </div>

            <div v-if="!hasPlan" class="rounded border p-3 mb-3">
                <div class="fw-semibold">{{ copy.noPlan }}</div>
                <div class="small text-body-secondary mt-1">{{ stateLabel }}</div>
                <ul v-if="plan.blockers?.length" class="small mb-0 mt-2 ps-3">
                    <li v-for="blocker in plan.blockers" :key="blocker">{{ reasonLabel(blocker) }}</li>
                </ul>
            </div>

            <template v-else>
                <div class="row g-3 mb-3">
                    <div class="col-md-6">
                        <section
                            class="schedule-summary h-100 rounded border p-3"
                            aria-labelledby="hems-current-action"
                        >
                            <div id="hems-current-action" class="small text-body-secondary">{{ copy.now }}</div>
                            <template v-if="currentStep">
                                <div class="d-flex align-items-baseline gap-2 flex-wrap mt-1">
                                    <strong class="fs-4">{{ actionLabel(currentStep.action) }}</strong>
                                    <span class="fs-5">{{ powerLabel(currentStep.battery_w) }}</span>
                                </div>
                                <div class="small text-body-secondary mt-2">
                                    {{ timeRange(currentStep.start, currentStep.end) }} ·
                                    {{ formatPrice(currentStep.used_price_micros_per_kwh, plan.currency, locale) }} ·
                                    {{ copy.soc }} {{ soc(currentStep.end_soc_pct) }}
                                </div>
                                <div v-if="currentStep.reason" class="small mt-2">
                                    <span class="text-body-secondary">{{ copy.reason }}:</span>
                                    {{ reasonLabel(currentStep.reason) }}
                                </div>
                            </template>
                            <div v-else class="fw-semibold mt-1">{{ copy.noCurrent }}</div>
                        </section>
                    </div>
                    <div class="col-md-6">
                        <section class="schedule-summary h-100 rounded border p-3" aria-labelledby="hems-next-action">
                            <div id="hems-next-action" class="small text-body-secondary">{{ copy.nextChange }}</div>
                            <template v-if="nextChange">
                                <div class="d-flex align-items-baseline gap-2 flex-wrap mt-1">
                                    <strong class="fs-4">{{ actionLabel(nextChange.action) }}</strong>
                                    <span class="fs-5">{{ powerLabel(nextChange.battery_w) }}</span>
                                </div>
                                <div class="small text-body-secondary mt-2">
                                    {{ date(nextChange.start) }} · {{ blockPrice(nextChange) }}
                                </div>
                                <div v-if="nextChange.reason" class="small mt-2">
                                    <span class="text-body-secondary">{{ copy.reason }}:</span>
                                    {{ reasonLabel(nextChange.reason) }}
                                </div>
                            </template>
                            <div v-else class="fw-semibold mt-1">{{ copy.noChange }}</div>
                        </section>
                    </div>
                </div>

                <details class="border rounded p-3 mb-3">
                    <summary>{{ t('redesign.planDetails') }}</summary>
                    <div class="mt-3">
                        <section class="rounded border p-3 mb-3" aria-labelledby="hems-schedule-timeline">
                            <div class="d-flex flex-wrap align-items-end justify-content-between gap-2 mb-2">
                                <div>
                                    <h2 id="hems-schedule-timeline" class="h6 mb-1">{{ copy.timeline }}</h2>
                                    <div class="small text-body-secondary">
                                        {{ timeRange(windowStart, windowEnd) }} · {{ horizonLabel }}
                                    </div>
                                </div>
                                <div class="d-flex flex-wrap gap-3 small" aria-label="Schedule legend">
                                    <span class="legend-item"
                                        ><span class="legend-swatch action-charge"></span>{{ copy.charge }}</span
                                    >
                                    <span class="legend-item"
                                        ><span class="legend-swatch action-hold"></span>{{ copy.hold }}</span
                                    >
                                    <span class="legend-item"
                                        ><span class="legend-swatch action-discharge"></span>{{ copy.discharge }}</span
                                    >
                                    <span class="legend-item"
                                        ><span class="legend-swatch action-gap"></span>{{ copy.gap }}</span
                                    >
                                </div>
                            </div>
                            <div class="timeline-wrap">
                                <div class="schedule-timeline" role="img" :aria-label="copy.timelineAria">
                                    <div
                                        v-for="part in parts"
                                        :key="`${part.start}-${part.action}-${part.battery_w}`"
                                        class="schedule-segment"
                                        :class="part.gap ? 'action-gap' : actionClass(part.action)"
                                        :style="{ flexGrow: String(part.end - part.start) }"
                                        :title="partTitle(part)"
                                    >
                                        <span class="visually-hidden">{{ partTitle(part) }}</span>
                                    </div>
                                </div>
                                <span
                                    v-if="nowPosition != null"
                                    class="now-marker"
                                    :style="{ left: `${nowPosition}%` }"
                                    aria-hidden="true"
                                ></span>
                            </div>
                            <div class="d-flex justify-content-between small text-body-secondary mt-1">
                                <span>{{ shortDateTime(windowStart) }}</span>
                                <span>{{ shortDateTime(windowMiddle) }}</span>
                                <span>{{ shortDateTime(windowEnd) }}</span>
                            </div>
                        </section>

                        <section class="rounded border p-3 mb-3" aria-labelledby="hems-operation-plan">
                            <div class="d-flex flex-wrap align-items-end justify-content-between gap-2 mb-2">
                                <div>
                                    <h2 id="hems-operation-plan" class="h6 mb-1">{{ copy.operationPlan }}</h2>
                                    <div class="small text-body-secondary">{{ copy.operationPlanHint }}</div>
                                </div>
                                <RouterLink to="/energy/diagnostics" class="small">{{
                                    t('hemsUi.fullSchedule')
                                }}</RouterLink>
                            </div>
                            <div class="table-responsive">
                                <table class="table table-sm align-middle mb-0 operation-table">
                                    <thead>
                                        <tr class="small text-body-secondary">
                                            <th scope="col">{{ copy.period }}</th>
                                            <th scope="col">{{ copy.action }}</th>
                                            <th scope="col">{{ copy.power }}</th>
                                            <th scope="col">{{ copy.soc }}</th>
                                            <th scope="col">{{ copy.price }}</th>
                                            <th scope="col">{{ copy.reason }}</th>
                                        </tr>
                                    </thead>
                                    <tbody>
                                        <tr
                                            v-for="block in operationRows"
                                            :key="`${block.start}-${block.action}-${block.battery_w}`"
                                        >
                                            <td class="text-nowrap">{{ timeRange(block.start, block.end) }}</td>
                                            <td>
                                                <span class="operation-dot" :class="actionClass(block.action)"></span>
                                                {{ actionLabel(block.action) }}
                                            </td>
                                            <td class="text-nowrap">{{ powerLabel(block.battery_w) }}</td>
                                            <td class="text-nowrap">{{ soc(block.end_soc_pct) }}</td>
                                            <td class="text-nowrap">{{ blockPrice(block) }}</td>
                                            <td class="small text-body-secondary">
                                                {{ reasonLabel(block.reason || '') }}
                                            </td>
                                        </tr>
                                    </tbody>
                                </table>
                            </div>
                            <div
                                v-if="futureBlocks.length > operationRows.length"
                                class="small text-body-secondary mt-2"
                            >
                                {{
                                    copy.moreBlocks.replace(
                                        '{count}',
                                        String(futureBlocks.length - operationRows.length)
                                    )
                                }}
                            </div>
                        </section>

                        <section v-if="socPath" class="rounded border p-3 mb-3" aria-labelledby="hems-soc-plan">
                            <div class="d-flex flex-wrap align-items-end justify-content-between gap-2 mb-2">
                                <div>
                                    <h2 id="hems-soc-plan" class="h6 mb-1">{{ copy.socPlan }}</h2>
                                    <div class="small text-body-secondary">
                                        {{ soc(startSoc) }} → {{ soc(visibleEndSoc) }}
                                    </div>
                                </div>
                                <div class="small text-body-secondary">
                                    {{ copy.reserve }} {{ plan.config.min_soc_pct }}% · {{ copy.maximum }}
                                    {{ plan.config.max_soc_pct }}%
                                </div>
                            </div>
                            <svg
                                class="soc-chart"
                                viewBox="0 0 1000 140"
                                preserveAspectRatio="none"
                                role="img"
                                :aria-label="copy.socPlan"
                            >
                                <line
                                    x1="0"
                                    x2="1000"
                                    :y1="socY(plan.config.min_soc_pct)"
                                    :y2="socY(plan.config.min_soc_pct)"
                                    class="reserve-line"
                                />
                                <path :d="socPath" class="soc-line" />
                            </svg>
                            <div class="d-flex justify-content-between small text-body-secondary">
                                <span>{{ soc(socRange[0]) }}</span>
                                <span>{{ copy.plannedRange }}</span>
                                <span>{{ soc(socRange[1]) }}</span>
                            </div>
                        </section>

                        <section
                            class="rounded bg-body-tertiary p-3 mb-3 small"
                            aria-labelledby="hems-storage-strategy"
                        >
                            <div class="d-flex flex-wrap align-items-center justify-content-between gap-2">
                                <strong id="hems-storage-strategy">{{ copy.strategy }}</strong>
                                <RouterLink to="/energy/planning">{{ copy.editStrategy }}</RouterLink>
                            </div>
                            <div class="row g-2 mt-1">
                                <div class="col-sm-6 col-lg-3">
                                    <span class="text-body-secondary">{{ copy.reserve }}</span
                                    ><br />
                                    <strong>{{ plan.config.min_soc_pct }}%</strong>
                                </div>
                                <div class="col-sm-6 col-lg-3">
                                    <span class="text-body-secondary">{{ copy.gridCharging }}</span
                                    ><br />
                                    <strong>{{ yesNo(plan.config.policies?.grid_charging_allowed !== false) }}</strong>
                                </div>
                                <div class="col-sm-6 col-lg-3">
                                    <span class="text-body-secondary">{{ copy.discharge }}</span
                                    ><br />
                                    <strong>{{ yesNo(plan.config.policies?.discharge_allowed !== false) }}</strong>
                                </div>
                                <div class="col-sm-6 col-lg-3">
                                    <span class="text-body-secondary">{{ copy.target }}</span
                                    ><br />
                                    <strong>{{ targetLabel }}</strong>
                                </div>
                            </div>
                        </section>

                        <EnergyManagementSummary :plan="plan" />
                    </div>
                </details>
                <div class="row g-3">
                    <div class="col-sm-6">
                        <div class="small text-body-secondary">{{ t('hemsUi.modeledSavings') }}</div>
                        <div class="fs-5 fw-semibold">{{ money(plan.modeled_savings_nano) }}</div>
                    </div>
                    <div class="col-sm-6">
                        <div class="small text-body-secondary">{{ copy.coverage }}</div>
                        <div class="fw-semibold">{{ coverageLabel }}</div>
                    </div>
                </div>

                <div
                    v-if="plan.state === 'partial' || plan.warnings?.length"
                    class="rounded bg-body-tertiary p-3 mt-3 small"
                    role="status"
                >
                    <strong>{{ t('hemsUi.planLimits') }}</strong>
                    <ul v-if="plan.warnings?.length" class="mb-0 mt-1 ps-3">
                        <li v-for="warning in plan.warnings" :key="warning">{{ reasonLabel(warning) }}</li>
                    </ul>
                </div>
            </template>

            <div class="d-flex flex-wrap gap-3 mt-3 small">
                <RouterLink to="/energy/planning">{{ copy.settings }}</RouterLink>
                <RouterLink to="/energy/diagnostics">{{ t('hemsUi.fullSchedule') }}</RouterLink>
            </div>
        </template>
    </CardElement>
</template>

<script setup lang="ts">
import HelpHint from '@/components/HelpHint.vue';
import { computed, getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import EnergyManagementSummary from '@/components/hems/EnergyManagementSummary.vue';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { formatPrice } from '@/utils/hemsPresentation';
import {
    currentScheduleStep,
    nextOperationBlock,
    operationBlocks,
    socAt,
    timelineParts,
    type StorageOperationBlock,
    type StorageScheduleStep,
    type StorageTimelinePart,
} from '@/utils/hemsSchedule';
import type { EconomicDispatch } from '@/types/HemsStatus';

type StoragePolicies = {
    grid_charging_allowed: boolean;
    discharge_allowed: boolean;
    target_enabled: boolean;
    target_soc_pct: number;
    target_deadline: number;
};

type Config = {
    enabled: boolean;
    dispatch_enabled: boolean;
    min_soc_pct: number;
    max_soc_pct: number;
    policies?: StoragePolicies;
};

type Plan = {
    dispatch?: EconomicDispatch;
    config: Config;
    state: string;
    blockers: string[];
    warnings?: string[];
    schedule: StorageScheduleStep[];
    initial_soc_pct?: number;
    final_soc_pct?: number;
    modeled_savings_nano?: number;
    horizon_start?: number;
    horizon_end?: number;
    currency?: string;
    interval_count?: number;
    has_more?: boolean;
    plan_revision?: number;
};

type Copy = {
    title: string;
    loadFailed: string;
    noPlan: string;
    noCurrent: string;
    now: string;
    nextChange: string;
    noChange: string;
    reason: string;
    action: string;
    power: string;
    price: string;
    period: string;
    soc: string;
    timeline: string;
    timelineAria: string;
    charge: string;
    hold: string;
    discharge: string;
    gap: string;
    operationPlan: string;
    operationPlanHint: string;
    moreBlocks: string;
    socPlan: string;
    reserve: string;
    maximum: string;
    plannedRange: string;
    strategy: string;
    editStrategy: string;
    gridCharging: string;
    target: string;
    none: string;
    until: string;
    coverage: string;
    settings: string;
    allowed: string;
    blocked: string;
    hours: string;
};

const texts: Record<string, Copy> = {
    en: {
        title: 'Storage schedule',
        loadFailed: 'Storage schedule could not be loaded.',
        noPlan: 'No usable storage schedule is available.',
        noCurrent: 'No active plan interval right now',
        now: 'Now',
        nextChange: 'Next command change',
        noChange: 'No further command change in the loaded horizon',
        reason: 'Reason',
        action: 'Action',
        power: 'Power',
        price: 'Price',
        period: 'Period',
        soc: 'SoC',
        timeline: 'Next storage actions',
        timelineAria: 'Storage schedule timeline with charge, hold, discharge and unavailable intervals',
        charge: 'Charge',
        hold: 'Hold',
        discharge: 'Discharge',
        gap: 'No plan data',
        operationPlan: 'Planned storage operation',
        operationPlanHint:
            'Generated by the optimizer from exact prices and constraints; these are not fixed user rules.',
        moreBlocks: '{count} more generated blocks are available in diagnostics.',
        socPlan: 'Expected state of charge',
        reserve: 'Reserve',
        maximum: 'Maximum',
        plannedRange: 'planned SoC range',
        strategy: 'Storage strategy',
        editStrategy: 'Edit strategy',
        gridCharging: 'Grid charging',
        target: 'Target',
        none: 'None',
        until: 'by',
        coverage: 'Loaded plan coverage',
        settings: 'Economic settings',
        allowed: 'Allowed',
        blocked: 'Blocked',
        hours: 'h',
    },
    de: {
        title: 'Speicherfahrplan',
        loadFailed: 'Der Speicherfahrplan konnte nicht geladen werden.',
        noPlan: 'Es ist derzeit kein nutzbarer Speicherfahrplan verfügbar.',
        noCurrent: 'Aktuell ist kein Fahrplanintervall aktiv',
        now: 'Jetzt',
        nextChange: 'Nächste Befehlsänderung',
        noChange: 'Keine weitere Befehlsänderung im geladenen Planungshorizont',
        reason: 'Grund',
        action: 'Aktion',
        power: 'Leistung',
        price: 'Preis',
        period: 'Zeitraum',
        soc: 'SoC',
        timeline: 'Nächste Speicheraktionen',
        timelineAria: 'Speicherfahrplan mit Lade-, Halte-, Entlade- und nicht verfügbaren Intervallen',
        charge: 'Laden',
        hold: 'Halten',
        discharge: 'Entladen',
        gap: 'Keine Plandaten',
        operationPlan: 'Geplanter Speicherbetrieb',
        operationPlanHint:
            'Vom Optimierer aus exakten Preisen und Begrenzungen erzeugt; keine fest hinterlegten Benutzerregeln.',
        moreBlocks: '{count} weitere erzeugte Blöcke sind in der Diagnose verfügbar.',
        socPlan: 'Erwarteter Ladezustand',
        reserve: 'Reserve',
        maximum: 'Maximum',
        plannedRange: 'geplanter SoC-Bereich',
        strategy: 'Speicherstrategie',
        editStrategy: 'Strategie bearbeiten',
        gridCharging: 'Netzladen',
        target: 'Ziel',
        none: 'Keins',
        until: 'bis',
        coverage: 'Geladener Planungshorizont',
        settings: 'Wirtschaftlichkeits-Einstellungen',
        allowed: 'Erlaubt',
        blocked: 'Gesperrt',
        hours: 'h',
    },
    fr: {
        title: 'Planification du stockage',
        loadFailed: 'La planification du stockage n’a pas pu être chargée.',
        noPlan: 'Aucune planification de stockage exploitable n’est disponible.',
        noCurrent: 'Aucun intervalle de plan n’est actif actuellement',
        now: 'Maintenant',
        nextChange: 'Prochain changement de commande',
        noChange: 'Aucun autre changement de commande dans l’horizon chargé',
        reason: 'Raison',
        action: 'Action',
        power: 'Puissance',
        price: 'Prix',
        period: 'Période',
        soc: 'SoC',
        timeline: 'Prochaines actions du stockage',
        timelineAria: 'Chronologie du stockage avec charge, maintien, décharge et intervalles indisponibles',
        charge: 'Charge',
        hold: 'Maintien',
        discharge: 'Décharge',
        gap: 'Aucune donnée de plan',
        operationPlan: 'Fonctionnement planifié du stockage',
        operationPlanHint:
            'Généré par l’optimiseur à partir des prix exacts et des contraintes ; il ne s’agit pas de règles utilisateur fixes.',
        moreBlocks: '{count} blocs générés supplémentaires sont disponibles dans les diagnostics.',
        socPlan: 'État de charge attendu',
        reserve: 'Réserve',
        maximum: 'Maximum',
        plannedRange: 'plage de SoC planifiée',
        strategy: 'Stratégie de stockage',
        editStrategy: 'Modifier la stratégie',
        gridCharging: 'Charge depuis le réseau',
        target: 'Objectif',
        none: 'Aucun',
        until: 'avant',
        coverage: 'Horizon de planification chargé',
        settings: 'Réglages économiques',
        allowed: 'Autorisé',
        blocked: 'Bloqué',
        hours: 'h',
    },
};

const { t, te, locale } = useI18n();
const copy = computed(() => texts[locale.value.split('-')[0] || 'en'] ?? texts.en!);
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const plan = ref<Plan>();
const error = ref('');
const busy = ref(false);
const fetching = ref(false);
const now = ref(Date.now() / 1000);
let disposed = false;
let pollTimer: ReturnType<typeof setTimeout> | undefined;
let clockTimer: ReturnType<typeof setInterval> | undefined;
let generation = 0;

const label = (value: string) =>
    te('hemsUi.optimizerStates.' + value) ? t('hemsUi.optimizerStates.' + value) : value.replaceAll('_', ' ');
const reasonLabel = (value: string) => (value ? label(value) : '—');
const dispatchLabel = (value: string) =>
    te('hemsUi.dispatchStates.' + value)
        ? t('hemsUi.dispatchStates.' + value)
        : t('hemsUi.dispatchStates.plan_unavailable');
const stateLabel = computed(() => label(plan.value?.state || 'not_ready'));
const hasPlan = computed(
    () =>
        !!plan.value &&
        ['ready', 'partial'].includes(plan.value.state) &&
        plan.value.schedule.some((step) => step.end > now.value)
);

async function getPage(offset: number): Promise<Plan> {
    const response = await fetch(`/api/hems/optimizer?all=1&offset=${offset}`, { headers: authHeader() });
    return handleResponse(response, emitter, router, true) as Promise<Plan>;
}
async function load() {
    if (fetching.value) return;
    fetching.value = true;
    const loadGeneration = ++generation;
    try {
        const first = await getPage(0);
        if (disposed || loadGeneration !== generation) return;
        const schedule = [...(first.schedule || [])];
        const seen = new Set(schedule.map((step) => step.start));
        let page = first;
        let offset = 24;
        while (page.has_more && schedule.length < 96) {
            const next = await getPage(offset);
            if (disposed || loadGeneration !== generation) return;
            if (first.plan_revision != null && next.plan_revision != null && first.plan_revision !== next.plan_revision)
                break;
            let added = 0;
            for (const step of next.schedule || []) {
                if (seen.has(step.start)) continue;
                seen.add(step.start);
                schedule.push(step);
                added++;
            }
            if (!added) break;
            page = next;
            offset += 24;
        }
        schedule.sort((a, b) => a.start - b.start);
        plan.value = { ...first, schedule };
        error.value = '';
    } catch (reason) {
        error.value =
            reason && typeof reason === 'object' && 'message' in reason ? String(reason.message) : String(reason);
    } finally {
        fetching.value = false;
    }
}
async function resume() {
    busy.value = true;
    error.value = '';
    try {
        const body = new FormData();
        body.append('data', '{}');
        const response = await fetch('/api/hems/optimizer/resume', { method: 'POST', headers: authHeader(), body });
        await handleResponse(response, emitter, router, true);
        await load();
    } catch (reason) {
        error.value =
            reason && typeof reason === 'object' && 'message' in reason ? String(reason.message) : String(reason);
    } finally {
        busy.value = false;
    }
}

const currentStep = computed(() => currentScheduleStep(plan.value?.schedule || [], now.value));
const allBlocks = computed(() => operationBlocks(plan.value?.schedule || []));
const futureBlocks = computed(() => allBlocks.value.filter((block) => block.end > now.value));
const operationRows = computed(() => futureBlocks.value.slice(0, 8));
const nextChange = computed(() => nextOperationBlock(allBlocks.value, now.value));
const firstFutureStep = computed(() => plan.value?.schedule.find((step) => step.start > now.value));
const windowStart = computed(() => currentStep.value?.start ?? firstFutureStep.value?.start ?? now.value);
const windowEnd = computed(() => {
    const scheduleEnd = plan.value?.schedule.at(-1)?.end || windowStart.value;
    const planEnd = plan.value?.horizon_end || scheduleEnd;
    return Math.min(windowStart.value + 86400, scheduleEnd, planEnd);
});
const windowMiddle = computed(() => windowStart.value + (windowEnd.value - windowStart.value) / 2);
const visibleSteps = computed(() =>
    (plan.value?.schedule || []).filter((step) => step.end > windowStart.value && step.start < windowEnd.value)
);
const parts = computed(() => timelineParts(plan.value?.schedule || [], windowStart.value, windowEnd.value));
const nowPosition = computed(() => {
    if (!(windowEnd.value > windowStart.value) || now.value < windowStart.value || now.value > windowEnd.value)
        return null;
    return ((now.value - windowStart.value) / (windowEnd.value - windowStart.value)) * 100;
});
const horizonLabel = computed(
    () =>
        `${((windowEnd.value - windowStart.value) / 3600).toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${copy.value.hours}`
);
const coverageLabel = computed(() => {
    if (!visibleSteps.value.length) return '—';
    const hours = (visibleSteps.value.at(-1)!.end - visibleSteps.value[0]!.start) / 3600;
    return `${hours.toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${copy.value.hours}`;
});
const startSoc = computed(() =>
    socAt(plan.value?.schedule || [], plan.value?.initial_soc_pct, plan.value?.horizon_start, windowStart.value)
);
const visibleEndSoc = computed(() => visibleSteps.value.at(-1)?.end_soc_pct ?? startSoc.value);
const socRange = computed<[number, number]>(() => {
    const values = [startSoc.value, ...visibleSteps.value.map((step) => step.end_soc_pct)].filter(
        (value): value is number => Number.isFinite(value)
    );
    if (!values.length) return [0, 100];
    let low = Math.max(0, Math.floor(Math.min(...values) - 2));
    let high = Math.min(100, Math.ceil(Math.max(...values) + 2));
    if (high - low < 10) {
        const midpoint = (low + high) / 2;
        low = Math.max(0, Math.floor(midpoint - 5));
        high = Math.min(100, Math.ceil(midpoint + 5));
        if (high - low < 10) low = Math.max(0, high - 10);
    }
    return [low, high];
});
function socY(value: number) {
    const [low, high] = socRange.value;
    return 130 - ((Math.min(high, Math.max(low, value)) - low) / Math.max(1, high - low)) * 120;
}
const socPath = computed(() => {
    if (!visibleSteps.value.length || windowEnd.value <= windowStart.value || !Number.isFinite(startSoc.value))
        return '';
    const span = windowEnd.value - windowStart.value;
    const points: Array<[number, number]> = [[windowStart.value, startSoc.value!]];
    for (const step of visibleSteps.value)
        if (Number.isFinite(step.end_soc_pct)) points.push([Math.min(step.end, windowEnd.value), step.end_soc_pct]);
    if (points.length < 2) return '';
    return points
        .map(
            ([time, value], index) => `${index ? 'L' : 'M'}${((time - windowStart.value) / span) * 1000},${socY(value)}`
        )
        .join(' ');
});
const targetLabel = computed(() => {
    const policy = plan.value?.config.policies;
    return !policy?.target_enabled
        ? copy.value.none
        : `${policy.target_soc_pct}% ${copy.value.until} ${date(policy.target_deadline)}`;
});

function actionLabel(action?: string) {
    if (!action) return '—';
    if (action === 'charge') return copy.value.charge;
    if (action === 'discharge') return copy.value.discharge;
    if (action === 'hold' || action === 'stop') return copy.value.hold;
    return te('hemsUi.optimizerStates.' + action) ? t('hemsUi.optimizerStates.' + action) : action.replaceAll('_', ' ');
}
function actionClass(action: string) {
    if (action === 'charge') return 'action-charge';
    if (action === 'discharge') return 'action-discharge';
    return 'action-hold';
}
function powerLabel(value: number) {
    return `${Math.abs(value || 0).toLocaleString(locale.value)} W`;
}
function blockPrice(block: StorageOperationBlock) {
    const low = formatPrice(block.min_price_micros_per_kwh, plan.value?.currency, locale.value);
    return block.min_price_micros_per_kwh === block.max_price_micros_per_kwh
        ? low
        : `${low} – ${formatPrice(block.max_price_micros_per_kwh, plan.value?.currency, locale.value)}`;
}
function partTitle(part: StorageTimelinePart) {
    return part.gap
        ? `${copy.value.gap} · ${timeRange(part.start, part.end)}`
        : `${actionLabel(part.action)} · ${powerLabel(part.battery_w)} · ${timeRange(part.start, part.end)}`;
}
function date(epoch?: number) {
    return epoch
        ? new Date(epoch * 1000).toLocaleString(locale.value, {
              month: 'short',
              day: 'numeric',
              hour: '2-digit',
              minute: '2-digit',
          })
        : '—';
}
function shortDateTime(epoch: number) {
    return new Date(epoch * 1000).toLocaleString(locale.value, {
        weekday: 'short',
        hour: '2-digit',
        minute: '2-digit',
    });
}
function shortTime(epoch: number) {
    return new Date(epoch * 1000).toLocaleTimeString(locale.value, { hour: '2-digit', minute: '2-digit' });
}
function timeRange(start: number, end: number) {
    const sameDay = new Date(start * 1000).toDateString() === new Date((end - 1) * 1000).toDateString();
    return sameDay ? `${shortTime(start)}–${shortTime(end)}` : `${date(start)} – ${date(end)}`;
}
function soc(value?: number) {
    return value == null || !Number.isFinite(value)
        ? '—'
        : `${value.toLocaleString(locale.value, { maximumFractionDigits: 1 })}%`;
}
function money(value?: number) {
    return value == null || !Number.isFinite(value)
        ? '—'
        : `${(value / 1e9).toLocaleString(locale.value, { maximumFractionDigits: 4 })} ${plan.value?.currency || ''}`;
}
function yesNo(value: boolean) {
    return value ? copy.value.allowed : copy.value.blocked;
}

async function poll() {
    await load();
    if (!disposed) pollTimer = setTimeout(poll, 30000);
}
onMounted(() => {
    now.value = Date.now() / 1000;
    clockTimer = setInterval(() => (now.value = Date.now() / 1000), 5000);
    void poll();
});
onUnmounted(() => {
    disposed = true;
    generation++;
    if (pollTimer) clearTimeout(pollTimer);
    if (clockTimer) clearInterval(clockTimer);
});
</script>

<style scoped>
.dispatch-strip,
.schedule-summary {
    background: var(--bs-body-bg);
}
.timeline-wrap {
    position: relative;
}
.schedule-timeline {
    display: flex;
    min-height: 3.25rem;
    overflow: hidden;
    border: 1px solid var(--bs-border-color);
    border-radius: var(--bs-border-radius);
    background: var(--bs-tertiary-bg);
}
.schedule-segment {
    flex-basis: 0;
    min-width: 2px;
}
.schedule-segment + .schedule-segment {
    border-left: 1px solid color-mix(in srgb, var(--bs-body-color) 18%, transparent);
}
.action-charge {
    background: var(--bs-success-bg-subtle);
}
.action-hold {
    background: var(--bs-secondary-bg-subtle);
}
.action-discharge {
    background: var(--bs-warning-bg-subtle);
}
.action-gap {
    background: repeating-linear-gradient(
        135deg,
        var(--bs-tertiary-bg),
        var(--bs-tertiary-bg) 6px,
        color-mix(in srgb, var(--bs-secondary-color) 18%, transparent) 6px,
        color-mix(in srgb, var(--bs-secondary-color) 18%, transparent) 12px
    );
}
.now-marker {
    position: absolute;
    top: -0.2rem;
    bottom: -0.2rem;
    width: 2px;
    transform: translateX(-1px);
    background: var(--bs-primary);
    pointer-events: none;
}
.legend-item {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
}
.legend-swatch,
.operation-dot {
    width: 0.8rem;
    height: 0.8rem;
    border: 1px solid var(--bs-border-color);
    border-radius: 0.2rem;
}
.operation-dot {
    display: inline-block;
    margin-right: 0.35rem;
    vertical-align: -0.05rem;
}
.operation-table th,
.operation-table td {
    padding-inline: 0.55rem;
}
.soc-chart {
    display: block;
    width: 100%;
    height: 8rem;
    color: var(--bs-primary);
    background: var(--bs-tertiary-bg);
    border-radius: var(--bs-border-radius);
}
.soc-line {
    fill: none;
    stroke: currentColor;
    stroke-width: 4;
    vector-effect: non-scaling-stroke;
}
.reserve-line {
    stroke: var(--bs-warning);
    stroke-width: 2;
    stroke-dasharray: 8 8;
    vector-effect: non-scaling-stroke;
}
@media (max-width: 575.98px) {
    .schedule-timeline {
        min-height: 2.75rem;
    }
    .soc-chart {
        height: 6.5rem;
    }
}
</style>
