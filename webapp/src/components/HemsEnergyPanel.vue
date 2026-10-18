<template>
    <CardElement
        v-for="kind in domains"
        :key="kind"
        :text="t('hemsUi.' + kind)"
        :textVariant="props.view === 'overview' ? 'energy-heading' : 'text-bg-primary'"
        class="hems-energy"
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
                </div>
                <template v-if="props.view === 'overview'">
                    <div class="d-flex flex-wrap align-items-center gap-2 mb-3" role="status">
                        <span
                            class="feed-state"
                            :class="{
                                'feed-warning': !['freshData', 'partialData', 'coveredData'].includes(
                                    feedReadiness(feeds[kind]!, unavailable[kind], kind)
                                ),
                            }"
                            >{{ t('hemsUi.' + feedReadiness(feeds[kind]!, unavailable[kind], kind)) }}</span
                        >
                        <span v-if="feeds[kind]!.planning_usable !== undefined" class="small text-body-secondary">{{
                            t(
                                feeds[kind]!.planning_usable && !unavailable[kind]
                                    ? 'hemsUi.planningUsable'
                                    : 'hemsUi.planningUnavailable'
                            )
                        }}</span>
                    </div>
                    <div class="energy-summary mb-3">
                        <div class="energy-metric energy-metric-main">
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
                            <span
                                v-if="kind === 'tariff' && currentCategory()"
                                class="price-badge mt-1"
                                :class="`period-${currentCategory()}`"
                                >{{ t('hemsUi.' + currentCategory()) }}</span
                            >
                        </div>
                        <div v-for="day in [0, 1]" :key="day" class="energy-metric">
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
                    <div v-if="kind === 'tariff'" class="next-periods mb-3">
                        <button
                            v-for="category in ['cheap', 'expensive'] as const"
                            :key="category"
                            type="button"
                            class="next-period"
                            :class="`period-${category}`"
                            :disabled="!periodInspectable(category)"
                            @click="inspectedPeriod = { utc: period(category)!.start }"
                        >
                            <span class="price-badge">{{ t('hemsUi.next' + category) }}</span>
                            <span class="d-block mt-2 fw-medium"
                                >{{ periodText(category)
                                }}<span v-if="periodInspectable(category)" aria-hidden="true"> →</span></span
                            >
                        </button>
                    </div>
                    <EnergyChart
                        v-if="feeds[kind]!.intervals.length"
                        :intervals="chartData(kind)"
                        :now="now"
                        :include-zero="kind === 'forecast'"
                        :focus-at="kind === 'tariff' ? inspectedPeriod : undefined"
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
                    <div v-else class="energy-empty p-4 rounded text-center">
                        <strong class="d-block mb-1">{{ t('hemsUi.noData') }}</strong>
                        <span class="small text-body-secondary">{{ t('hemsUi.emptyEnergyHint') }}</span>
                    </div>
                    <details
                        v-if="kind === 'forecast' && feeds[kind]!.config.planes.length"
                        class="energy-details mt-3"
                    >
                        <summary>
                            {{ t('hemsUi.pvPlanes') }} ·
                            {{ feeds[kind]!.config.planes.filter((p) => p.enabled).length }}/{{
                                feeds[kind]!.config.planes.length
                            }}
                            {{ t('hemsUi.enabled') }}
                        </summary>
                        <div class="plane-summaries mt-3">
                            <span
                                v-for="(plane, index) in feeds[kind]!.config.planes"
                                :key="index"
                                class="plane-summary small"
                                ><strong>{{ plane.name }}</strong
                                ><span class="text-body-secondary"
                                    >{{ (plane.peak_w / 1000).toLocaleString(locale) }} kWp ·
                                    {{ plane.enabled ? t('hemsUi.enabled') : t('hemsUi.disabled') }}</span
                                ></span
                            >
                        </div>
                        <RouterLink to="/hems/settings" class="small d-inline-block mt-2">{{
                            t('hemsUi.editPlanes')
                        }}</RouterLink>
                    </details>
                    <p
                        v-if="kind === 'tariff' && tariffBasis(feeds.tariff) === 'source'"
                        class="small text-body-secondary mt-2"
                    >
                        {{ t('hemsUi.sourceOnly') }}
                    </p>
                    <p v-if="kind === 'forecast'" class="small text-body-secondary mt-2">
                        {{ t('hemsUi.forecastHint') }}
                    </p>
                    <div class="feed-update mt-3 pt-3">
                        <div class="small text-body-secondary">
                            <div>{{ t('hemsUi.receivedAt') }}: {{ date(feeds[kind]!.fetched_at) }}</div>
                            <div v-if="feeds[kind]!.manual_refresh?.retry_after_seconds" class="mt-1" role="status">
                                {{ t('hemsUi.retryIn', { seconds: feeds[kind]!.manual_refresh!.retry_after_seconds }) }}
                                {{ t('hemsUi.autoRetry') }}
                            </div>
                        </div>
                        <button
                            type="button"
                            class="btn btn-outline-secondary btn-sm"
                            :disabled="
                                busy[kind] ||
                                feeds[kind]!.provider === 'normalized_push' ||
                                feeds[kind]!.manual_refresh?.allowed === false
                            "
                            @click="refresh(kind)"
                        >
                            {{ t('hemsUi.refresh') }}
                        </button>
                    </div>
                    <details class="energy-details mt-3">
                        <summary>{{ t('hemsUi.energyDetails') }}</summary>
                        <dl class="small mt-3 mb-2">
                            <dt>{{ t('hemsUi.sourceUpdated') }}</dt>
                            <dd>
                                {{
                                    feeds[kind]!.generated_at && feeds[kind]!.source_age_seconds !== null
                                        ? date(feeds[kind]!.generated_at)
                                        : t('hemsUi.sourceTimeUnknown')
                                }}
                            </dd>
                            <dt>{{ t('hemsUi.sourceFreshness') }}</dt>
                            <dd>{{ t('hemsUi.' + feedFreshness(feeds[kind]!, 'source')) }}</dd>
                            <dt>{{ t('hemsUi.transportFreshness') }}</dt>
                            <dd>{{ t('hemsUi.' + feedFreshness(feeds[kind]!, 'transport')) }}</dd>
                            <dt>{{ t('hemsUi.coverageUntil') }}</dt>
                            <dd>{{ date(feeds[kind]!.horizon_end) }}</dd>
                            <dt>{{ t('hemsUi.nextUpdate') }}</dt>
                            <dd>{{ date(feeds[kind]!.next_refresh) }}</dd>
                            <template v-if="kind === 'tariff' && feeds[kind]!.config.pricing.enabled">
                                <dt>{{ t('hemsUi.monthly_micros') }}</dt>
                                <dd>{{ monthlyFee() }}</dd>
                            </template>
                        </dl>
                        <p v-if="kind === 'tariff'" class="small text-body-secondary">
                            {{ t('hemsUi.classificationHint') }}
                        </p>
                        <RouterLink to="/hems/diagnostics" class="small">{{
                            t('hemsUi.providerDiagnosticsLink')
                        }}</RouterLink>
                    </details>
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
                    <dt class="col-sm-4">{{ t('hemsUi.sourceFreshness') }}</dt>
                    <dd class="col-sm-8">{{ t('hemsUi.' + feedFreshness(feeds[kind]!, 'source')) }}</dd>
                    <dt class="col-sm-4">{{ t('hemsUi.transportFreshness') }}</dt>
                    <dd class="col-sm-8">{{ t('hemsUi.' + feedFreshness(feeds[kind]!, 'transport')) }}</dd>
                    <dt class="col-sm-4">next_refresh</dt>
                    <dd class="col-sm-8">{{ date(feeds[kind]!.next_refresh) }}</dd>
                </dl>
                <details>
                    <summary>{{ t('hemsUi.providerDiagnostics') }}</summary>
                    <pre class="diagnostic-json mt-3">{{ JSON.stringify(feeds[kind], null, 2) }}</pre>
                </details>
            </template>
        </template>
        <p v-else-if="!errors[kind]" class="text-body-secondary" role="status">{{ t('hemsUi.loadingEnergy') }}</p>
        <RouterLink
            v-if="props.view === 'overview' && feeds[kind]?.enabled === false"
            to="/hems/settings"
            class="small"
            >{{ t('hemsUi.configureEnergy') }}</RouterLink
        >
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
    feedReadiness,
    feedFreshness,
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
const inspectedPeriod = ref<{ utc: number }>();
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
    return feeds[kind]?.enabled && feeds[kind]?.fresh && !unavailable[kind];
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
    return usable('tariff') ? formatPrice(interval?.[key], feeds.tariff?.currency, locale.value, 2) : '—';
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
function period(category: TariffClass) {
    return usable('tariff') ? nextPeriod(classified.value, category, now.value) : null;
}
function periodInspectable(category: TariffClass) {
    const next = period(category);
    return !!next && next.start < dayBounds(now.value, 1)[1];
}
function periodText(category: TariffClass) {
    if (!usable('tariff')) return t('hemsUi.unavailable');
    const next = period(category);
    if (!next) return t('hemsUi.noneInData');
    const short = (utc: number) =>
        new Date(utc * 1000).toLocaleString(locale.value, { weekday: 'short', hour: '2-digit', minute: '2-digit' });
    return `${short(next.start)} – ${short(next.end)}`;
}
function monthlyFee() {
    const amount = (feeds.tariff?.config.pricing.monthly_micros || 0) / 1e6;
    return `${amount.toLocaleString(locale.value, { minimumFractionDigits: 2, maximumFractionDigits: 2 })} ${feeds.tariff?.currency || 'EUR'}/${t('hemsUi.month')}`;
}
function health(kind: Kind) {
    const feed = feeds[kind];
    if (!feed?.enabled) return '';
    if (feed.last_error) return stateText(feed.last_error);
    if (props.view === 'overview' && ['noData', 'outdated', 'currentGap'].includes(feedReadiness(feed))) return '';
    return ['ready', 'partial'].includes(feed.state) ? '' : stateText(feed.state);
}
const domains: Kind[] = ['tariff', 'forecast'];
const feeds = reactive<Partial<Record<Kind, Feed>>>({});
const drafts = reactive<Partial<Record<Kind, Settings>>>({});
const errors = reactive({ tariff: '', forecast: '' });
// Action rejection (busy/quota/backoff) does not invalidate a successfully observed feed.
const unavailable = reactive({ tariff: false, forecast: false });
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
            unavailable[kind] = false;
            if (!drafts[kind] || reset) drafts[kind] = structuredClone(feed.config);
            errors[kind] = '';
        } catch (error) {
            unavailable[kind] = true;
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
.hems-energy :deep(.energy-heading) {
    background: var(--bs-tertiary-bg);
    font-weight: 600;
    padding-block: 1rem;
}
.energy-summary {
    display: grid;
    grid-template-columns: 1.4fr 1fr 1fr;
    gap: 1rem;
}
.energy-metric {
    min-width: 0;
    padding: 1rem;
    border-radius: 0.75rem;
    background: var(--bs-tertiary-bg);
}
.energy-metric-main {
    background: var(--energy-plot);
}
.next-periods {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.75rem;
}
.next-period {
    text-align: start;
    color: var(--bs-body-color);
    background: var(--bs-body-bg);
    border: 1px solid var(--bs-border-color);
    border-inline-start: 3px solid var(--period-color);
    border-radius: 0.65rem;
    padding: 0.85rem;
    font-size: 0.875rem;
}
.next-period:disabled {
    cursor: default;
}
.next-period:not(:disabled):hover {
    background: var(--bs-tertiary-bg);
}
.feed-state {
    background: var(--bs-tertiary-bg);
    border-radius: 2rem;
    padding: 0.25rem 0.6rem;
    font-size: 0.8rem;
    font-weight: 600;
}
.feed-warning {
    color: var(--bs-warning-text-emphasis);
    background: var(--bs-warning-bg-subtle);
}
.feed-update {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    border-top: 1px solid var(--bs-border-color);
}
.feed-update button {
    flex-shrink: 0;
    min-height: 44px;
}
.energy-details summary {
    font-size: 0.875rem;
    padding-block: 0.5rem;
}
.energy-details dd {
    overflow-wrap: anywhere;
    color: var(--bs-secondary-color);
}
.energy-empty {
    background: var(--bs-tertiary-bg);
}
.plane-summaries {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 0.5rem;
}
.plane-summary {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    border-radius: 0.5rem;
    background: var(--bs-tertiary-bg);
    padding: 0.65rem;
    overflow-wrap: anywhere;
}
@media (max-width: 575px) {
    .energy-summary {
        grid-template-columns: 1fr 1fr;
        gap: 0.5rem;
    }
    .energy-metric {
        padding: 0.75rem;
    }
    .energy-metric-main {
        grid-column: 1 / -1;
    }
    .next-periods {
        grid-template-columns: 1fr;
    }
    .feed-update {
        flex-wrap: wrap;
    }
}

.diagnostic-json {
    max-height: 36rem;
    overflow: auto;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    font-size: 0.8rem;
}
</style>
