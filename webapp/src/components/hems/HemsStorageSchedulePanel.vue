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
        <p class="small text-body-secondary mb-3">{{ t('hemsUi.dispatchHint') }}</p>

        <p v-if="error" class="alert alert-warning" role="alert">{{ copy.loadFailed }} {{ error }}</p>
        <template v-if="plan && !error">
            <div
                v-if="plan.dispatch"
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
                        <section class="schedule-summary h-100 rounded border p-3" aria-labelledby="hems-current-action">
                            <div id="hems-current-action" class="small text-body-secondary">{{ copy.now }}</div>
                            <div class="d-flex align-items-baseline gap-2 flex-wrap mt-1">
                                <strong class="fs-4">{{ actionLabel(currentStep?.action) }}</strong>
                                <span class="fs-5">{{ powerLabel(currentStep) }}</span>
                            </div>
                            <div v-if="currentStep" class="small text-body-secondary mt-2">
                                {{ timeRange(currentStep.start, currentStep.end) }} ·
                                {{ formatPrice(currentStep.used_price_micros_per_kwh, plan.currency, locale) }} ·
                                {{ copy.soc }} {{ soc(currentStep.end_soc_pct) }}
                            </div>
                            <div v-if="currentStep?.reason" class="small mt-2">
                                <span class="text-body-secondary">{{ copy.reason }}:</span>
                                {{ reasonLabel(currentStep.reason) }}
                            </div>
                        </section>
                    </div>
                    <div class="col-md-6">
                        <section class="schedule-summary h-100 rounded border p-3" aria-labelledby="hems-next-action">
                            <div id="hems-next-action" class="small text-body-secondary">{{ copy.nextChange }}</div>
                            <template v-if="nextChange">
                                <div class="d-flex align-items-baseline gap-2 flex-wrap mt-1">
                                    <strong class="fs-4">{{ actionLabel(nextChange.action) }}</strong>
                                    <span class="fs-5">{{ powerLabel(nextChange) }}</span>
                                </div>
                                <div class="small text-body-secondary mt-2">
                                    {{ date(nextChange.start) }} ·
                                    {{ formatPrice(nextChange.used_price_micros_per_kwh, plan.currency, locale) }}
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

                <section class="rounded border p-3 mb-3" aria-labelledby="hems-schedule-timeline">
                    <div class="d-flex flex-wrap align-items-end justify-content-between gap-2 mb-2">
                        <div>
                            <h2 id="hems-schedule-timeline" class="h6 mb-1">{{ copy.timeline }}</h2>
                            <div class="small text-body-secondary">
                                {{ timeRange(windowStart, windowEnd) }} · {{ horizonLabel }}
                            </div>
                        </div>
                        <div class="d-flex flex-wrap gap-3 small" aria-label="Schedule legend">
                            <span class="legend-item"><span class="legend-swatch action-charge"></span>{{ copy.charge }}</span>
                            <span class="legend-item"><span class="legend-swatch action-hold"></span>{{ copy.hold }}</span>
                            <span class="legend-item"><span class="legend-swatch action-discharge"></span>{{ copy.discharge }}</span>
                        </div>
                    </div>
                    <div
                        class="schedule-timeline"
                        role="img"
                        :aria-label="copy.timelineAria"
                    >
                        <div
                            v-for="segment in segments"
                            :key="`${segment.start}-${segment.action}`"
                            class="schedule-segment"
                            :class="actionClass(segment.action)"
                            :style="{ flexGrow: String(segment.end - segment.start) }"
                            :title="segmentTitle(segment)"
                        >
                            <span class="visually-hidden">{{ segmentTitle(segment) }}</span>
                        </div>
                    </div>
                    <div class="d-flex justify-content-between small text-body-secondary mt-1">
                        <span>{{ shortTime(windowStart) }}</span>
                        <span>{{ shortTime(windowEnd) }}</span>
                    </div>
                </section>

                <section v-if="socPath" class="rounded border p-3 mb-3" aria-labelledby="hems-soc-plan">
                    <div class="d-flex flex-wrap align-items-end justify-content-between gap-2 mb-2">
                        <div>
                            <h2 id="hems-soc-plan" class="h6 mb-1">{{ copy.socPlan }}</h2>
                            <div class="small text-body-secondary">
                                {{ soc(plan.initial_soc_pct) }} → {{ soc(plan.final_soc_pct) }}
                            </div>
                        </div>
                        <div class="small text-body-secondary">
                            {{ copy.reserve }} {{ plan.config.min_soc_pct }}% · {{ copy.maximum }} {{ plan.config.max_soc_pct }}%
                        </div>
                    </div>
                    <svg class="soc-chart" viewBox="0 0 1000 140" preserveAspectRatio="none" role="img" :aria-label="copy.socPlan">
                        <line x1="0" x2="1000" :y1="socY(plan.config.min_soc_pct)" :y2="socY(plan.config.min_soc_pct)" class="reserve-line" />
                        <path :d="socPath" class="soc-line" />
                    </svg>
                    <div class="d-flex justify-content-between small text-body-secondary">
                        <span>{{ soc(socRange[0]) }}</span>
                        <span>{{ copy.plannedRange }}</span>
                        <span>{{ soc(socRange[1]) }}</span>
                    </div>
                </section>

                <section class="rounded bg-body-tertiary p-3 mb-3 small" aria-labelledby="hems-storage-strategy">
                    <div class="d-flex flex-wrap align-items-center justify-content-between gap-2">
                        <strong id="hems-storage-strategy">{{ copy.strategy }}</strong>
                        <RouterLink to="/hems/settings">{{ copy.editStrategy }}</RouterLink>
                    </div>
                    <div class="row g-2 mt-1">
                        <div class="col-sm-6 col-lg-3">
                            <span class="text-body-secondary">{{ copy.reserve }}</span><br />
                            <strong>{{ plan.config.min_soc_pct }}%</strong>
                        </div>
                        <div class="col-sm-6 col-lg-3">
                            <span class="text-body-secondary">{{ copy.gridCharging }}</span><br />
                            <strong>{{ yesNo(plan.config.policies?.grid_charging_allowed !== false) }}</strong>
                        </div>
                        <div class="col-sm-6 col-lg-3">
                            <span class="text-body-secondary">{{ copy.discharge }}</span><br />
                            <strong>{{ yesNo(plan.config.policies?.discharge_allowed !== false) }}</strong>
                        </div>
                        <div class="col-sm-6 col-lg-3">
                            <span class="text-body-secondary">{{ copy.target }}</span><br />
                            <strong>{{ targetLabel }}</strong>
                        </div>
                    </div>
                </section>

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
                <RouterLink to="/hems/settings">{{ copy.settings }}</RouterLink>
                <RouterLink to="/hems/diagnostics">{{ t('hemsUi.fullSchedule') }}</RouterLink>
            </div>
        </template>
    </CardElement>
</template>

<script setup lang="ts">
import { computed, getCurrentInstance, onMounted, onUnmounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { formatPrice } from '@/utils/hemsPresentation';
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

type Step = {
    start: number;
    end: number;
    used_price_micros_per_kwh: number;
    action: string;
    reason?: string;
    battery_w: number;
    end_soc_pct: number;
};

type Plan = {
    dispatch?: EconomicDispatch;
    config: Config;
    state: string;
    blockers: string[];
    warnings?: string[];
    schedule: Step[];
    initial_soc_pct?: number;
    final_soc_pct?: number;
    modeled_savings_nano?: number;
    horizon_start?: number;
    horizon_end?: number;
    currency?: string;
    interval_count?: number;
    has_more?: boolean;
    schedule_offset?: number;
    plan_revision?: number;
};

type Segment = Step & { duration: number };

type Copy = {
    title: string;
    loadFailed: string;
    noPlan: string;
    now: string;
    nextChange: string;
    noChange: string;
    reason: string;
    soc: string;
    timeline: string;
    timelineAria: string;
    charge: string;
    hold: string;
    discharge: string;
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
        now: 'Now',
        nextChange: 'Next change',
        noChange: 'No further action change in the loaded horizon',
        reason: 'Reason',
        soc: 'SoC',
        timeline: 'Next storage actions',
        timelineAria: 'Storage schedule timeline with charge, hold and discharge intervals',
        charge: 'Charge',
        hold: 'Hold',
        discharge: 'Discharge',
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
        now: 'Jetzt',
        nextChange: 'Nächste Änderung',
        noChange: 'Keine weitere Aktionsänderung im geladenen Planungshorizont',
        reason: 'Grund',
        soc: 'SoC',
        timeline: 'Nächste Speicheraktionen',
        timelineAria: 'Speicherfahrplan mit Lade-, Halte- und Entladeintervallen',
        charge: 'Laden',
        hold: 'Halten',
        discharge: 'Entladen',
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
        now: 'Maintenant',
        nextChange: 'Prochain changement',
        noChange: 'Aucun autre changement d’action dans l’horizon chargé',
        reason: 'Raison',
        soc: 'SoC',
        timeline: 'Prochaines actions du stockage',
        timelineAria: 'Chronologie du stockage avec charge, maintien et décharge',
        charge: 'Charge',
        hold: 'Maintien',
        discharge: 'Décharge',
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
const reasonLabel = (value: string) => label(value);
const dispatchLabel = (value: string) =>
    te('hemsUi.dispatchStates.' + value)
        ? t('hemsUi.dispatchStates.' + value)
        : t('hemsUi.dispatchStates.plan_unavailable');
const stateLabel = computed(() => label(plan.value?.state || 'not_ready'));
const hasPlan = computed(
    () => !!plan.value && ['ready', 'partial'].includes(plan.value.state) && plan.value.schedule.some((step) => step.end > now.value)
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
            if (first.plan_revision != null && next.plan_revision != null && first.plan_revision !== next.plan_revision) break;
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
        error.value = reason && typeof reason === 'object' && 'message' in reason ? String(reason.message) : String(reason);
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
        error.value = reason && typeof reason === 'object' && 'message' in reason ? String(reason.message) : String(reason);
    } finally {
        busy.value = false;
    }
}

const currentStep = computed(() => {
    const schedule = plan.value?.schedule || [];
    return schedule.find((step) => step.start <= now.value && now.value < step.end) || schedule.find((step) => step.start > now.value);
});

const nextChange = computed(() => {
    const schedule = plan.value?.schedule || [];
    const current = currentStep.value;
    if (!current) return undefined;
    const index = schedule.findIndex((step) => step.start === current.start);
    for (let i = Math.max(0, index + 1); i < schedule.length; i++) {
        if (schedule[i]!.action !== current.action) return schedule[i];
    }
    return undefined;
});

const windowStart = computed(() => currentStep.value?.start || plan.value?.schedule.find((step) => step.end > now.value)?.start || now.value);
const windowEnd = computed(() => {
    const scheduleEnd = plan.value?.schedule.at(-1)?.end || windowStart.value;
    const planEnd = plan.value?.horizon_end || scheduleEnd;
    return Math.min(windowStart.value + 86400, scheduleEnd, planEnd);
});

const visibleSteps = computed(() =>
    (plan.value?.schedule || []).filter((step) => step.end > windowStart.value && step.start < windowEnd.value)
);

const segments = computed<Segment[]>(() => {
    const result: Segment[] = [];
    for (const source of visibleSteps.value) {
        const step = {
            ...source,
            start: Math.max(source.start, windowStart.value),
            end: Math.min(source.end, windowEnd.value),
            duration: Math.max(0, Math.min(source.end, windowEnd.value) - Math.max(source.start, windowStart.value)),
        };
        if (!step.duration) continue;
        const previous = result.at(-1);
        if (previous && previous.end === step.start && previous.action === step.action) {
            previous.end = step.end;
            previous.duration += step.duration;
            previous.end_soc_pct = step.end_soc_pct;
            previous.battery_w = step.battery_w;
            previous.reason = step.reason || previous.reason;
        } else result.push(step);
    }
    return result;
});

const horizonLabel = computed(() => `${((windowEnd.value - windowStart.value) / 3600).toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${copy.value.hours}`);
const coverageLabel = computed(() => {
    if (!visibleSteps.value.length) return '—';
    const hours = (visibleSteps.value.at(-1)!.end - visibleSteps.value[0]!.start) / 3600;
    const suffix = plan.value?.has_more && (plan.value?.schedule.length || 0) >= 96 ? ' +' : '';
    return `${hours.toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${copy.value.hours}${suffix}`;
});

const socRange = computed<[number, number]>(() => {
    const values = [plan.value?.initial_soc_pct, ...visibleSteps.value.map((step) => step.end_soc_pct)].filter(
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
    const span = Math.max(1, high - low);
    return 130 - ((Math.min(high, Math.max(low, value)) - low) / span) * 120;
}

const socPath = computed(() => {
    if (!visibleSteps.value.length || windowEnd.value <= windowStart.value) return '';
    const points: Array<[number, number]> = [];
    const initial = plan.value?.initial_soc_pct;
    if (Number.isFinite(initial)) points.push([windowStart.value, initial!]);
    for (const step of visibleSteps.value) {
        if (Number.isFinite(step.end_soc_pct)) points.push([Math.min(step.end, windowEnd.value), step.end_soc_pct]);
    }
    if (points.length < 2) return '';
    const span = windowEnd.value - windowStart.value;
    return points
        .map(([time, value], index) => `${index ? 'L' : 'M'}${((time - windowStart.value) / span) * 1000},${socY(value)}`)
        .join(' ');
});

const targetLabel = computed(() => {
    const policy = plan.value?.config.policies;
    if (!policy?.target_enabled) return copy.value.none;
    return `${policy.target_soc_pct}% ${copy.value.until} ${date(policy.target_deadline)}`;
});

function actionLabel(action?: string) {
    if (!action) return '—';
    if (te('hemsUi.optimizerStates.' + action)) return t('hemsUi.optimizerStates.' + action);
    if (action === 'charge') return copy.value.charge;
    if (action === 'discharge') return copy.value.discharge;
    if (action === 'hold' || action === 'stop') return copy.value.hold;
    return action.replaceAll('_', ' ');
}

function actionClass(action: string) {
    if (action === 'charge') return 'action-charge';
    if (action === 'discharge') return 'action-discharge';
    return 'action-hold';
}

function powerLabel(step?: Step) {
    if (!step) return '—';
    if (!step.battery_w) return '0 W';
    return `${Math.abs(step.battery_w).toLocaleString(locale.value)} W`;
}

function segmentTitle(segment: Segment) {
    return `${actionLabel(segment.action)} · ${powerLabel(segment)} · ${timeRange(segment.start, segment.end)} · ${copy.value.soc} ${soc(segment.end_soc_pct)}`;
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

function shortTime(epoch: number) {
    return new Date(epoch * 1000).toLocaleTimeString(locale.value, { hour: '2-digit', minute: '2-digit' });
}

function timeRange(start: number, end: number) {
    const sameDay = new Date(start * 1000).toDateString() === new Date(end * 1000).toDateString();
    if (sameDay) return `${shortTime(start)}–${shortTime(end)}`;
    return `${date(start)} – ${date(end)}`;
}

function soc(value?: number) {
    return value == null || !Number.isFinite(value) ? '—' : `${value.toLocaleString(locale.value, { maximumFractionDigits: 1 })}%`;
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
    transition: filter 0.15s ease;
}

.schedule-segment + .schedule-segment {
    border-left: 1px solid color-mix(in srgb, var(--bs-body-color) 18%, transparent);
}

.schedule-segment:hover,
.schedule-segment:focus-visible {
    filter: brightness(0.92);
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

.legend-item {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
}

.legend-swatch {
    width: 0.8rem;
    height: 0.8rem;
    border: 1px solid var(--bs-border-color);
    border-radius: 0.2rem;
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
