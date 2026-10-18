<template>
    <CardElement :text="copy.title" textVariant="text-bg-primary" add-space>
        <p class="small text-body-secondary mb-3">{{ copy.hint }}</p>
        <p v-if="error" class="alert alert-warning mb-0" role="alert">{{ copy.loadFailed }} {{ error }}</p>
        <template v-else-if="status">
            <div class="row g-2 readiness-grid">
                <div class="col-sm-6 col-xl-3">
                    <div class="readiness-item rounded border p-3 h-100">
                        <div class="small text-body-secondary">{{ copy.model }}</div>
                        <div
                            class="fw-semibold mt-1"
                            :class="modelReady ? 'text-success-emphasis' : 'text-warning-emphasis'"
                        >
                            {{ modelReady ? copy.ready : copy.incomplete }}
                        </div>
                    </div>
                </div>
                <div class="col-sm-6 col-xl-3">
                    <div class="readiness-item rounded border p-3 h-100">
                        <div class="small text-body-secondary">{{ copy.tariff }}</div>
                        <div
                            class="fw-semibold mt-1"
                            :class="status.tariff_usable ? 'text-success-emphasis' : 'text-warning-emphasis'"
                        >
                            {{ status.tariff_usable ? copy.ready : copy.notUsable }}
                        </div>
                    </div>
                </div>
                <div class="col-sm-6 col-xl-3">
                    <div class="readiness-item rounded border p-3 h-100">
                        <div class="small text-body-secondary">{{ copy.soc }}</div>
                        <div
                            class="fw-semibold mt-1"
                            :class="status.storage_soc_fresh ? 'text-success-emphasis' : 'text-warning-emphasis'"
                        >
                            {{ status.storage_soc_fresh ? copy.fresh : copy.waiting }}
                        </div>
                    </div>
                </div>
                <div class="col-sm-6 col-xl-3">
                    <div class="readiness-item rounded border p-3 h-100">
                        <div class="small text-body-secondary">{{ copy.dispatch }}</div>
                        <div
                            class="fw-semibold mt-1"
                            :class="status.config.dispatch_enabled ? 'text-primary-emphasis' : 'text-body-secondary'"
                        >
                            {{ dispatchSummary }}
                        </div>
                    </div>
                </div>
            </div>

            <div v-if="missingModel.length" class="alert alert-warning mt-3 mb-0" role="status">
                <div class="fw-semibold">{{ copy.modelMissing }}</div>
                <div class="small mt-1">{{ copy.modelMissingHint }}</div>
                <ul class="small mb-0 mt-2 ps-3">
                    <li v-for="item in missingModel" :key="item">{{ item }}</li>
                </ul>
            </div>
            <div v-else-if="planningReady" class="alert alert-success mt-3 mb-0" role="status">
                <div class="fw-semibold">{{ copy.planningReady }}</div>
                <div class="small mt-1">{{ copy.planningReadyHint }}</div>
            </div>
            <div v-else class="rounded bg-body-tertiary p-3 mt-3 small" role="status">
                <div class="fw-semibold">{{ copy.waitingTitle }}</div>
                <div class="mt-1">{{ waitingSummary }}</div>
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

type OptimizerConfig = {
    enabled: boolean;
    dispatch_enabled: boolean;
    soc_bounds_configured: boolean;
    capacity_wh: number;
    min_soc_pct: number;
    max_soc_pct: number;
    charge_permille: number;
    discharge_permille: number;
    max_charge_w: number;
    max_discharge_w: number;
};
type OptimizerStatus = {
    state: string;
    blockers?: string[];
    tariff_usable?: boolean;
    storage_soc_fresh?: boolean;
    config: OptimizerConfig;
    dispatch?: { state?: string; paused_manual?: boolean };
};
type Copy = {
    title: string;
    hint: string;
    loadFailed: string;
    model: string;
    tariff: string;
    soc: string;
    dispatch: string;
    ready: string;
    incomplete: string;
    notUsable: string;
    fresh: string;
    waiting: string;
    dispatchOff: string;
    dispatchPaused: string;
    dispatchOn: string;
    modelMissing: string;
    modelMissingHint: string;
    confirmBounds: string;
    capacity: string;
    efficiencies: string;
    powerLimits: string;
    socBounds: string;
    planningReady: string;
    planningReadyHint: string;
    waitingTitle: string;
    waitingTariff: string;
    waitingSoc: string;
    optimizerOff: string;
    waitingGeneric: string;
};
const texts: Record<string, Copy> = {
    en: {
        title: 'Planning readiness',
        hint: 'A compact check of the inputs required for a useful storage plan. Strategy stays simple; technical battery-model values remain under Advanced settings below.',
        loadFailed: 'Planning readiness could not be loaded.',
        model: 'Battery model',
        tariff: 'Tariff',
        soc: 'Storage SoC',
        dispatch: 'Economic dispatch',
        ready: 'Ready',
        incomplete: 'Incomplete',
        notUsable: 'Not usable for planning',
        fresh: 'Fresh',
        waiting: 'Waiting for telemetry',
        dispatchOff: 'Off · Shadow only',
        dispatchPaused: 'On · paused manually',
        dispatchOn: 'On',
        modelMissing: 'Battery model is incomplete',
        modelMissingHint:
            'Complete these technical values under Advanced settings in the optimizer before judging its schedule:',
        confirmBounds: 'Confirm the configured SoC bounds',
        capacity: 'Usable battery capacity',
        efficiencies: 'Charge and discharge efficiency',
        powerLimits: 'Maximum charge and discharge power',
        socBounds: 'Reserve must be below the maximum desired SoC',
        planningReady: 'Planning inputs are ready',
        planningReadyHint:
            'The optimizer can calculate from the current tariff and fresh storage SoC. Economic writes still depend on the separate dispatch opt-in and runtime gates.',
        waitingTitle: 'Waiting for runtime input',
        waitingTariff: 'The tariff is currently not usable for planning.',
        waitingSoc: 'A fresh storage SoC is still missing.',
        optimizerOff: 'Shadow optimization is disabled.',
        waitingGeneric: 'The optimizer is waiting for another current prerequisite.',
    },
    de: {
        title: 'Planungsbereitschaft',
        hint: 'Kompakter Check der Eingaben, die für einen brauchbaren Speicherfahrplan nötig sind. Die Strategie bleibt einfach; technische Batteriemodell-Werte liegen unten unter Erweiterte Einstellungen.',
        loadFailed: 'Die Planungsbereitschaft konnte nicht geladen werden.',
        model: 'Batteriemodell',
        tariff: 'Tarif',
        soc: 'Speicher-SoC',
        dispatch: 'Economic Dispatch',
        ready: 'Bereit',
        incomplete: 'Unvollständig',
        notUsable: 'Nicht planbar',
        fresh: 'Aktuell',
        waiting: 'Wartet auf Telemetrie',
        dispatchOff: 'Aus · nur Shadow',
        dispatchPaused: 'An · manuell pausiert',
        dispatchOn: 'An',
        modelMissing: 'Batteriemodell ist unvollständig',
        modelMissingHint:
            'Diese technischen Werte unter Erweiterte Einstellungen im Optimierer vervollständigen, bevor der Fahrplan bewertet wird:',
        confirmBounds: 'Konfigurierte SoC-Grenzen bestätigen',
        capacity: 'Nutzbare Batteriekapazität',
        efficiencies: 'Lade- und Entladewirkungsgrad',
        powerLimits: 'Maximale Lade- und Entladeleistung',
        socBounds: 'Reserve muss unter dem maximal gewünschten SoC liegen',
        planningReady: 'Planungseingaben sind bereit',
        planningReadyHint:
            'Der Optimierer kann mit dem aktuellen Tarif und einem frischen Speicher-SoC rechnen. Wirtschaftliche Schreibzugriffe hängen weiterhin vom separaten Dispatch-Opt-in und den Runtime-Gates ab.',
        waitingTitle: 'Wartet auf Runtime-Eingabe',
        waitingTariff: 'Der Tarif ist aktuell nicht für die Planung nutzbar.',
        waitingSoc: 'Ein frischer Speicher-SoC fehlt noch.',
        optimizerOff: 'Die Shadow-Optimierung ist deaktiviert.',
        waitingGeneric: 'Der Optimierer wartet noch auf eine aktuelle Voraussetzung.',
    },
    fr: {
        title: 'Disponibilité de la planification',
        hint: 'Contrôle compact des entrées requises pour un plan de stockage utile. La stratégie reste simple ; les valeurs techniques du modèle de batterie restent dans les réglages avancés ci-dessous.',
        loadFailed: 'La disponibilité de la planification n’a pas pu être chargée.',
        model: 'Modèle de batterie',
        tariff: 'Tarif',
        soc: 'SoC du stockage',
        dispatch: 'Dispatch économique',
        ready: 'Prêt',
        incomplete: 'Incomplet',
        notUsable: 'Non utilisable pour la planification',
        fresh: 'Actuel',
        waiting: 'En attente de télémétrie',
        dispatchOff: 'Désactivé · Shadow uniquement',
        dispatchPaused: 'Activé · pause manuelle',
        dispatchOn: 'Activé',
        modelMissing: 'Le modèle de batterie est incomplet',
        modelMissingHint:
            'Complétez ces valeurs techniques dans les réglages avancés de l’optimiseur avant d’évaluer son plan :',
        confirmBounds: 'Confirmer les limites de SoC configurées',
        capacity: 'Capacité utile de la batterie',
        efficiencies: 'Rendement de charge et de décharge',
        powerLimits: 'Puissance maximale de charge et de décharge',
        socBounds: 'La réserve doit être inférieure au SoC maximal souhaité',
        planningReady: 'Les entrées de planification sont prêtes',
        planningReadyHint:
            'L’optimiseur peut calculer avec le tarif actuel et un SoC de stockage récent. Les écritures économiques dépendent toujours de l’activation séparée du dispatch et des contrôles d’exécution.',
        waitingTitle: 'En attente d’une entrée d’exécution',
        waitingTariff: 'Le tarif n’est actuellement pas utilisable pour la planification.',
        waitingSoc: 'Un SoC de stockage récent manque encore.',
        optimizerOff: 'L’optimisation Shadow est désactivée.',
        waitingGeneric: 'L’optimiseur attend encore une condition actuelle.',
    },
};

const { locale } = useI18n();
const copy = computed(() => texts[locale.value.split('-')[0] || 'en'] ?? texts.en!);
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
const status = ref<OptimizerStatus>();
const error = ref('');
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false;
let generation = 0;

const missingModel = computed(() => {
    const config = status.value?.config;
    if (!config) return [];
    const result: string[] = [];
    if (!config.soc_bounds_configured) result.push(copy.value.confirmBounds);
    if (!(config.capacity_wh > 0)) result.push(copy.value.capacity);
    if (!(config.charge_permille > 0 && config.discharge_permille > 0)) result.push(copy.value.efficiencies);
    if (!(config.max_charge_w > 0 && config.max_discharge_w > 0)) result.push(copy.value.powerLimits);
    if (!(config.min_soc_pct < config.max_soc_pct)) result.push(copy.value.socBounds);
    return result;
});
const modelReady = computed(() => !!status.value && missingModel.value.length === 0);
const planningReady = computed(
    () =>
        !!status.value &&
        status.value.config.enabled &&
        modelReady.value &&
        status.value.tariff_usable === true &&
        status.value.storage_soc_fresh === true &&
        ['ready', 'partial'].includes(status.value.state)
);
const dispatchSummary = computed(() => {
    if (!status.value?.config.dispatch_enabled) return copy.value.dispatchOff;
    if (status.value.dispatch?.paused_manual) return copy.value.dispatchPaused;
    return copy.value.dispatchOn;
});
const waitingSummary = computed(() => {
    if (!status.value) return copy.value.waitingGeneric;
    if (!status.value.config.enabled) return copy.value.optimizerOff;
    if (!status.value.tariff_usable) return copy.value.waitingTariff;
    if (!status.value.storage_soc_fresh) return copy.value.waitingSoc;
    return copy.value.waitingGeneric;
});

async function load() {
    const current = ++generation;
    try {
        const response = await fetch('/api/hems/optimizer?all=1&offset=0', { headers: authHeader() });
        const result = (await handleResponse(response, emitter, router, true)) as OptimizerStatus;
        if (disposed || current !== generation) return;
        status.value = result;
        error.value = '';
    } catch (reason) {
        if (disposed || current !== generation) return;
        error.value =
            reason && typeof reason === 'object' && 'message' in reason ? String(reason.message) : String(reason);
    }
}

onMounted(() => {
    void load();
    timer = setInterval(() => void load(), 15000);
});
onUnmounted(() => {
    disposed = true;
    generation++;
    if (timer) clearInterval(timer);
});
</script>

<style scoped>
.readiness-item {
    background: var(--bs-body-bg);
}
</style>
