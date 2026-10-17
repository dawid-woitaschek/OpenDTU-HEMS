<template>
    <CardElement
        v-for="kind in domains"
        :key="kind"
        :text="t('hemsUi.' + kind)"
        textVariant="text-bg-primary"
        add-space
    >
        <p v-if="errors[kind]" class="alert alert-warning" role="alert">{{ errors[kind] }}</p>
        <p v-if="notices[kind]" class="alert alert-success" role="status">{{ notices[kind] }}</p>
        <template v-if="feeds[kind]">
            <p v-if="!feeds[kind]!.enabled" class="text-body-secondary">{{ t('hemsUi.disabled') }}</p>
            <template v-else>
                <div v-if="health(kind)" class="alert alert-warning py-2" role="status">
                    {{ health(kind) }}
                    <span v-if="feeds[kind]!.intervals.length && feeds[kind]!.last_error">{{
                        t('hemsUi.retainedData')
                    }}</span>
                    <span v-if="feeds[kind]!.manual_refresh?.retry_after_seconds">{{
                        t('hemsUi.retryIn', { seconds: feeds[kind]!.manual_refresh!.retry_after_seconds })
                    }}</span>
                </div>
                <template v-if="props.view === 'overview'">
                    <div class="row g-3 mb-3">
                        <div class="col-12 col-sm-6 col-lg-4">
                            <div class="small text-body-secondary">
                                {{ t(kind === 'tariff' ? 'hemsUi.effectiveNow' : 'hemsUi.remainingToday') }}
                            </div>
                            <div class="fs-2 fw-semibold lh-sm my-1">
                                {{
                                    kind === 'tariff'
                                        ? currentPrice('import_price_micros_per_kwh')
                                        : energySummary(0, true)
                                }}
                            </div>
                            <div class="small text-body-secondary" v-if="kind === 'tariff'">
                                {{ t('hemsUi.sourcePrice') }}: {{ currentPrice('price_micros_per_kwh') }}
                            </div>
                            <span v-if="kind === 'tariff' && currentCategory()" class="small">{{
                                t('hemsUi.' + currentCategory())
                            }}</span>
                        </div>
                        <div v-for="day in [0, 1]" :key="day" class="col-6 col-lg-4">
                            <div class="small text-body-secondary">
                                {{ t(day ? 'hemsUi.tomorrow' : 'hemsUi.today') }} ·
                                {{ t(kind === 'tariff' ? 'hemsUi.average' : 'hemsUi.forecastTotal') }}
                            </div>
                            <div class="fs-4 fw-semibold my-1">
                                {{ kind === 'tariff' ? dailyPrice(day) : energySummary(day) }}
                            </div>
                            <div v-if="!summaryComplete(kind, day)" class="small text-body-secondary">
                                {{ t(kind === 'tariff' ? 'hemsUi.coveredOnly' : 'hemsUi.incompleteForecast') }}
                            </div>
                        </div>
                    </div>
                    <div v-if="kind === 'tariff'" class="d-flex flex-wrap gap-3 small mb-3">
                        <span v-for="category in ['cheap', 'expensive'] as const" :key="category"
                            ><strong>{{ t('hemsUi.next' + category) }}:</strong> {{ periodText(category) }}</span
                        >
                    </div>
                    <EnergyChart
                        v-if="feeds[kind]!.intervals.length"
                        :intervals="chartData(kind)"
                        :now="now"
                        :label="
                            t(
                                kind === 'tariff'
                                    ? tariffBasis(feeds.tariff) === 'effective'
                                        ? 'hemsUi.effectivePrice'
                                        : 'hemsUi.sourcePrice'
                                    : 'hemsUi.aggregateForecast'
                            )
                        "
                        :unit="
                            kind === 'tariff'
                                ? feeds.tariff?.currency === 'EUR'
                                    ? 'ct/kWh'
                                    : `${feeds.tariff?.currency}/kWh`
                                : 'W'
                        "
                        :display="(value) => chartValue(kind, value)"
                    />
                    <p v-else class="text-body-secondary">{{ t('hemsUi.waitingData') }}</p>
                    <details v-if="kind === 'tariff'" class="small mt-3">
                        <summary>{{ t('hemsUi.classification') }}</summary>
                        <p class="text-body-secondary mt-2 mb-0">{{ t('hemsUi.classificationHint') }}</p>
                    </details>
                    <div
                        v-if="kind === 'forecast' && feeds[kind]!.config.planes.length"
                        class="d-flex flex-wrap gap-2 mt-3"
                    >
                        <span
                            v-for="(plane, index) in feeds[kind]!.config.planes"
                            :key="index"
                            class="border rounded px-2 py-1 small"
                            >{{ plane.name }} · {{ plane.peak_w / 1000 }} kWp ·
                            {{ plane.enabled ? t('hemsUi.enabled') : t('hemsUi.disabled') }}</span
                        >
                    </div>
                    <p
                        v-if="kind === 'tariff' && tariffBasis(feeds.tariff) === 'source'"
                        class="small text-body-secondary mt-2"
                    >
                        {{ t('hemsUi.sourceOnly') }}
                    </p>
                    <p v-if="kind === 'forecast'" class="small text-body-secondary mt-2">
                        {{ t('hemsUi.forecastHint') }}
                    </p>
                </template>
            </template>
            <p class="small text-body-secondary mt-3">
                {{ providerLabel(feeds[kind]!.provider)
                }}<span v-if="feeds[kind]!.attribution"> · {{ feeds[kind]!.attribution }}</span>
            </p>
            <form v-if="props.view === 'settings' && drafts[kind]" @submit.prevent="save(kind)" class="mt-4">
                <label class="form-check-label mb-3"
                    ><input
                        :id="`${kind}-enabled`"
                        v-model="drafts[kind]!.enabled"
                        type="checkbox"
                        class="form-check-input me-2"
                    />{{ t('hemsUi.enabled') }}</label
                >
                <label :for="`${kind}-provider`" class="form-label d-block">{{ t('hemsUi.provider') }}</label>
                <select
                    :id="`${kind}-provider`"
                    v-model="drafts[kind]!.provider"
                    class="form-select mb-3"
                    @change="selectProvider(kind)"
                >
                    <option :value="kind === 'tariff' ? 'energy_charts' : 'forecast_solar'">
                        {{ kind === 'tariff' ? 'Energy-Charts · DE-LU' : 'Forecast.Solar' }}
                    </option>
                    <option v-if="drafts[kind]!.provider.startsWith('normalized_')" :value="drafts[kind]!.provider">
                        {{ providerLabel(drafts[kind]!.provider) }}
                    </option>
                </select>
                <template v-if="kind === 'tariff'">
                    <label class="form-check-label mb-2"
                        ><input
                            id="tariff-pricing-enabled"
                            v-model="drafts[kind]!.pricing.enabled"
                            class="form-check-input me-2"
                            type="checkbox"
                        />{{ t('hemsUi.useComponents') }}</label
                    >
                    <p class="small text-body-secondary">{{ t('hemsUi.priceFormula') }}</p>
                    <div class="row g-3 mb-3">
                        <div class="col-sm-6">
                            <label for="tariff-vat" class="form-label">{{ t('hemsUi.vat') }} (%)</label>
                            <input
                                id="tariff-vat"
                                :value="toDisplay(drafts[kind]!.pricing.vat_basis_points, 100)"
                                type="number"
                                class="form-control"
                                min="0"
                                max="100"
                                step="0.01"
                                required
                                @input="
                                    drafts[kind]!.pricing.vat_basis_points = toCanonical(
                                        ($event.target as HTMLInputElement).value,
                                        100
                                    )
                                "
                            />
                        </div>
                        <div v-for="field in priceFields" :key="field.key" class="col-sm-6">
                            <label :for="`tariff-${field.key}`" class="form-label"
                                >{{ t('hemsUi.' + field.key) }} ({{ field.unit }})</label
                            >
                            <input
                                :id="`tariff-${field.key}`"
                                :value="toDisplay(drafts[kind]!.pricing[field.key], field.scale)"
                                type="number"
                                class="form-control"
                                :min="field.min"
                                :max="field.max"
                                :step="field.step"
                                required
                                @input="
                                    drafts[kind]!.pricing[field.key] = toCanonical(
                                        ($event.target as HTMLInputElement).value,
                                        field.scale
                                    )
                                "
                            />
                        </div>
                    </div>
                </template>
                <PvPlaneEditor
                    v-if="drafts[kind]!.provider === 'forecast_solar'"
                    v-model:planes="drafts[kind]!.planes"
                    :saved="feeds[kind]!.config.planes"
                    :statuses="feeds[kind]!.plane_status"
                />
                <details class="border rounded p-3 mb-3">
                    <summary>{{ t('hemsUi.advancedSettings') }}</summary>
                    <label :for="`${kind}-advanced-source`" class="form-label mt-3">{{
                        t('hemsUi.integration')
                    }}</label>
                    <select
                        :id="`${kind}-advanced-source`"
                        v-model="drafts[kind]!.provider"
                        class="form-select mb-3"
                        @change="selectProvider(kind)"
                    >
                        <option :value="kind === 'tariff' ? 'energy_charts' : 'forecast_solar'">
                            {{ kind === 'tariff' ? 'Energy-Charts · DE-LU' : 'Forecast.Solar' }}
                        </option>
                        <option value="normalized_push">{{ t('hemsUi.normalizedPush') }}</option>
                        <option value="normalized_http">{{ t('hemsUi.normalizedHttp') }}</option>
                    </select>
                    <template v-if="drafts[kind]!.provider === 'normalized_http'">
                        <label :for="`${kind}-url`" class="form-label">{{ t('hemsUi.bridgeUrl') }}</label>
                        <input
                            :id="`${kind}-url`"
                            v-model="drafts[kind]!.url"
                            type="url"
                            class="form-control mb-3"
                            maxlength="192"
                            placeholder="http://bridge.local/energy.json"
                            required
                        />
                    </template>
                    <div class="row g-3">
                        <div v-for="field in settingFields" :key="field.key" class="col-md-4">
                            <label :for="`${kind}-${field.key}`" class="form-label"
                                >{{ t('hemsUi.' + field.key) }} (s)</label
                            >
                            <input
                                :id="`${kind}-${field.key}`"
                                v-model.number="drafts[kind]![field.key]"
                                type="number"
                                class="form-control"
                                :min="field.min"
                                :max="field.max"
                                step="1"
                                required
                            />
                        </div>
                    </div>
                    <template v-if="drafts[kind]!.provider === 'normalized_push'">
                        <label :for="`${kind}-upload`" class="form-label mt-3">{{ t('hemsUi.uploadJson') }}</label>
                        <input
                            :id="`${kind}-upload`"
                            class="form-control"
                            type="file"
                            accept="application/json,.json"
                            :disabled="
                                busy[kind] || !feeds[kind]!.enabled || feeds[kind]!.provider !== 'normalized_push'
                            "
                            @change="upload(kind, $event)"
                        />
                        <a
                            class="d-block mt-2"
                            href="https://github.com/dawid-woitaschek/OpenDTU-HEMS/blob/feature/hems-real-control/docs/HEMS_Energy_Data.md"
                            target="_blank"
                            rel="noopener"
                            >{{ t('hemsUi.bridgeDocs') }}</a
                        >
                    </template>
                </details>
                <div class="d-flex flex-wrap gap-2">
                    <button class="btn btn-primary" :disabled="busy[kind]">{{ t('hemsUi.save') }}</button>
                    <button
                        type="button"
                        class="btn btn-outline-secondary"
                        :disabled="
                            busy[kind] ||
                            !feeds[kind]!.enabled ||
                            feeds[kind]!.provider === 'normalized_push' ||
                            feeds[kind]!.manual_refresh?.allowed === false
                        "
                        @click="refresh(kind)"
                    >
                        {{ t('hemsUi.refresh') }}
                    </button>
                </div>
                <p v-if="feeds[kind]!.manual_refresh?.allowed === false" class="small text-body-secondary mt-2">
                    {{ stateText(feeds[kind]!.manual_refresh!.reason || 'unavailable') }}
                    <span v-if="feeds[kind]!.manual_refresh!.retry_after_seconds">{{
                        t('hemsUi.retryIn', { seconds: feeds[kind]!.manual_refresh!.retry_after_seconds })
                    }}</span>
                </p>
            </form>
            <template v-if="props.view === 'diagnostics'">
                <dl class="row small">
                    <dt class="col-sm-4">state / last_error</dt>
                    <dd class="col-sm-8 text-break">{{ feeds[kind]!.state }} / {{ feeds[kind]!.last_error || '—' }}</dd>
                    <dt class="col-sm-4">generated_at / fetched_at</dt>
                    <dd class="col-sm-8">
                        {{ date(feeds[kind]!.generated_at) }} / {{ date(feeds[kind]!.fetched_at) }}
                    </dd>
                    <dt class="col-sm-4">source_age / transport_age (s)</dt>
                    <dd class="col-sm-8">
                        {{ feeds[kind]!.source_age_seconds ?? '—' }} / {{ feeds[kind]!.transport_age_seconds }}
                    </dd>
                    <dt class="col-sm-4">next_refresh</dt>
                    <dd class="col-sm-8">{{ date(feeds[kind]!.next_refresh) }}</dd>
                </dl>
                <details>
                    <summary>{{ t('hemsUi.providerDiagnostics') }}</summary>
                    <pre class="diagnostic-json mt-3">{{ JSON.stringify(feeds[kind], null, 2) }}</pre>
                </details>
            </template>
        </template>
    </CardElement>
</template>

<script setup lang="ts">
import { computed, getCurrentInstance, onMounted, onUnmounted, reactive, ref } from 'vue';
import { useRouter } from 'vue-router';
import CardElement from '@/components/CardElement.vue';
import { authHeader, handleResponse } from '@/utils/authentication';
import { priceFields, toDisplay, toCanonical } from '@/utils/tariffUnits';
import { useI18n } from 'vue-i18n';
import EnergyChart from '@/components/hems/EnergyChart.vue';
import PvPlaneEditor from '@/components/hems/PvPlaneEditor.vue';
import type {
    EnergyKind as Kind,
    EnergySettings as Settings,
    EnergyFeed as Feed,
    EnergyView,
} from '@/types/HemsEnergy';
import {
    dayBounds,
    summarize,
    tariffIntervals,
    tariffBasis,
    classifyTariff,
    forecastIntervals,
    nextPeriod,
    formatPrice,
    providerMessage,
    type TariffClass,
} from '@/utils/hemsPresentation';
const props = defineProps<{ view?: EnergyView }>();
const emit = defineEmits<{ health: [issue: boolean] }>();
const { t, locale } = useI18n();
const now = ref(Date.now() / 1000);
function selectProvider(kind: Kind) {
    const draft = drafts[kind];
    if (!draft) return;
    draft.url = '';
    if (draft.provider === 'forecast_solar') {
        draft.refresh_seconds = 7200;
        draft.max_age_seconds = 10800;
    }
    if (draft.provider === 'energy_charts') draft.refresh_seconds = 3600;
}
function providerLabel(source: string) {
    return (
        (
            {
                energy_charts: 'Energy-Charts · DE-LU',
                forecast_solar: 'Forecast.Solar',
                normalized_http: t('hemsUi.normalizedHttp'),
                normalized_push: t('hemsUi.normalizedPush'),
            } as Record<string, string>
        )[source] || source
    );
}
function summary(kind: Kind, day: number, remaining = false) {
    const [start, end] = dayBounds(now.value, day);
    const data = kind === 'tariff' ? tariffIntervals(feeds.tariff) : forecastIntervals(feeds.forecast?.intervals || []);
    return summarize(data, remaining ? Math.max(start, now.value) : start, end);
}
function summaryComplete(kind: Kind, day: number) {
    return summary(kind, day).complete;
}
function usable(kind: Kind) {
    return feeds[kind]?.enabled && feeds[kind]?.fresh && !errors[kind];
}
function energySummary(day: number, remaining = false) {
    const result = summary('forecast', day, remaining);
    return usable('forecast') && result.complete && result.energyWh !== null
        ? `${(result.energyWh / 1000).toLocaleString(locale.value, { maximumFractionDigits: 2 })} kWh`
        : '—';
}
function dailyPrice(day: number) {
    return usable('tariff')
        ? formatPrice(summary('tariff', day).average, feeds.tariff?.currency, locale.value, 2)
        : '—';
}
function currentPrice(key: 'price_micros_per_kwh' | 'import_price_micros_per_kwh') {
    const interval = feeds.tariff?.intervals.find((i) => i.start <= now.value && now.value < i.end);
    return usable('tariff') ? price(interval?.[key], feeds.tariff?.currency) : '—';
}
function currentCategory() {
    return usable('tariff')
        ? classified.value.find((i) => i.start <= now.value && now.value < i.end)?.category || ''
        : '';
}
const classified = computed(() => classifyTariff(tariffIntervals(feeds.tariff)));
function chartData(kind: Kind) {
    if (kind === 'forecast') return forecastIntervals(feeds.forecast?.intervals || []);
    const scale = feeds.tariff?.currency === 'EUR' ? 10000 : 1e6;
    return classified.value.map((i) => ({
        ...i,
        value: i.value == null ? null : i.value / scale,
        secondary: i.secondary == null ? null : i.secondary / scale,
    }));
}
function chartValue(kind: Kind, value: number | null | undefined) {
    if (value == null) return '—';
    if (kind === 'forecast') return `${value.toLocaleString(locale.value, { maximumFractionDigits: 0 })} W`;
    return price(value * (feeds.tariff?.currency === 'EUR' ? 10000 : 1e6), feeds.tariff?.currency);
}
function periodText(category: TariffClass) {
    const period = usable('tariff') ? nextPeriod(classified.value, category, now.value) : null;
    return period ? `${date(period.start)} – ${date(period.end)}` : t('hemsUi.noneInData');
}
function health(kind: Kind) {
    const feed = feeds[kind];
    if (!feed?.enabled) return '';
    if (feed.last_error) return stateText(feed.last_error);
    return ['ready', 'partial'].includes(feed.state) ? '' : stateText(feed.state);
}
const domains: Kind[] = ['tariff', 'forecast'];
const feeds = reactive<Partial<Record<Kind, Feed>>>({});
const drafts = reactive<Partial<Record<Kind, Settings>>>({});
const errors = reactive({ tariff: '', forecast: '' });
const notices = reactive({ tariff: '', forecast: '' });
const busy = reactive({ tariff: false, forecast: false });
const settingFields = [
    { key: 'refresh_seconds', label: 'Refresh interval (native minimum applies)', min: 60, max: 86400 },
    {
        key: 'max_age_seconds',
        label: 'Maximum PV source age (tariff validity uses interval coverage)',
        min: 60,
        max: 604800,
    },
    { key: 'horizon_seconds', label: 'Requested future horizon', min: 900, max: 604800 },
] as const;
const emitter = getCurrentInstance()!.appContext.config.globalProperties.$emitter;
const router = useRouter();
let timer: ReturnType<typeof setInterval> | undefined;
let disposed = false;
const date = (utc: number | null) =>
    utc
        ? new Date(utc * 1000).toLocaleString(locale.value, {
              month: 'short',
              day: 'numeric',
              hour: '2-digit',
              minute: '2-digit',
              timeZoneName: 'short',
          })
        : '—';
function price(micros?: number | null, currency?: string) {
    return formatPrice(micros, currency, locale.value);
}
function stateText(state: string) {
    return t('hemsUi.' + providerMessage(state));
}
function message(error: unknown) {
    return error && typeof error === 'object' && 'message' in error
        ? String(error.message)
        : String(error || t('hemsUi.requestFailed'));
}
async function api(kind: Kind, suffix = '', data?: unknown) {
    const body = new FormData();
    if (data !== undefined) body.append('data', JSON.stringify(data));
    return fetch(`/api/hems/energy/${kind}${suffix}`, {
        headers: authHeader(),
        ...(data !== undefined ? { method: 'POST', body } : {}),
    }).then(async (response) => {
        if (suffix === '/refresh' && response.status === 409) {
            const result = await response.json();
            throw new Error(
                `${stateText(result.reason || result.message || 'unavailable')} ${result.retry_after_seconds ? t('hemsUi.retryIn', { seconds: result.retry_after_seconds }) : ''}`
            );
        }
        return handleResponse(response, emitter, router, true);
    });
}
const loading: Partial<Record<Kind, Promise<void>>> = {};
async function load(kind: Kind, reset = false) {
    if (loading[kind]) {
        await loading[kind];
        if (!reset) return;
    }
    const request = async () => {
        try {
            let feed = (await api(kind)) as Feed;
            const previous = feeds[kind];
            if (!previous || reset || previous.revision !== feed.revision) feed = (await api(kind, '?all=1')) as Feed;
            else feed.intervals = previous.intervals;
            if (disposed) return;
            feeds[kind] = feed;
            if (!drafts[kind] || reset) drafts[kind] = structuredClone(feed.config);
            errors[kind] = '';
        } catch (error) {
            errors[kind] = `${t('hemsUi.loadFailed')} ${message(error)}`;
        } finally {
            emit(
                'health',
                domains.some(
                    (k) =>
                        !!errors[k] ||
                        (!!feeds[k]?.enabled &&
                            (!!feeds[k]?.last_error ||
                                !feeds[k]?.fresh ||
                                feeds[k]?.state === 'missing_current_interval'))
                )
            );
        }
    };
    loading[kind] = request();
    try {
        await loading[kind];
    } finally {
        delete loading[kind];
    }
}
async function perform(kind: Kind, suffix: string, data: unknown, success: string, reset = false) {
    busy[kind] = true;
    errors[kind] = '';
    notices[kind] = '';
    try {
        const result = await api(kind, suffix, data);
        if (result?.ok === false) throw new Error(result.message || result.reason || t('hemsUi.requestFailed'));
        notices[kind] = success;
        await load(kind, reset);
    } catch (error) {
        errors[kind] = message(error);
    } finally {
        busy[kind] = false;
    }
}
function save(kind: Kind) {
    return perform(kind, '/config', drafts[kind], t('hemsUi.saved'), true);
}
function refresh(kind: Kind) {
    return perform(kind, '/refresh', {}, t('hemsUi.refreshQueued'));
}
async function upload(kind: Kind, event: Event) {
    const input = event.target as HTMLInputElement,
        file = input.files?.[0];
    if (!file) return;
    try {
        if (file.size > 40000) throw new Error(t('hemsUi.fileTooLarge'));
        await perform(kind, '', JSON.parse(await file.text()), t('hemsUi.dataAccepted'));
    } catch (error) {
        errors[kind] = message(error);
    }
    input.value = '';
}
onMounted(() => {
    domains.forEach((kind) => load(kind));
    timer = setInterval(
        () =>
            domains.forEach((kind) => {
                now.value = Date.now() / 1000;
                if (!busy[kind]) void load(kind);
            }),
        15000
    );
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
