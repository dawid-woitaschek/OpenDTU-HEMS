<template>
    <figure class="energy-chart hems-energy mb-0" ref="container">
        <div class="d-flex flex-wrap justify-content-between gap-2 small mb-2">
            <span class="fw-semibold"
                >{{ label }} <span class="text-body-secondary">({{ unit }})</span></span
            >
            <label v-if="intervals.some((i) => finite(i.secondary))" class="form-check-label">
                <input v-model="showSource" type="checkbox" class="form-check-input me-1" />{{
                    t('hemsUi.sourcePrice')
                }}
            </label>
        </div>
        <div class="chart-toolbar mb-2">
            <div class="chart-range" role="group" :aria-label="t('hemsUi.chartRange')">
                <button
                    v-for="range in ['today', 'tomorrow', 'twoDays'] as const"
                    :key="range"
                    type="button"
                    :aria-pressed="view === range"
                    @click="changeView(range)"
                >
                    {{ t('hemsUi.' + range) }}
                </button>
            </div>
            <div v-if="hasCategories" class="chart-legend small">
                <span class="text-body-secondary">{{ t('hemsUi.relativePrices') }}</span>
                <span v-for="category in ['cheap', 'normal', 'expensive'] as const" :key="category"
                    ><i class="legend-swatch" :class="`period-${category}`"></i>{{ t('hemsUi.' + category) }}</span
                >
            </div>
        </div>
        <svg
            ref="surface"
            :viewBox="`0 0 ${width} ${height}`"
            class="w-100 chart-surface"
            tabindex="0"
            role="img"
            :aria-label="`${label}. ${t('hemsUi.chartHelp')}`"
            @pointerdown="inspect($event, true)"
            @pointermove="inspect($event)"
            @keydown.left.prevent="move(-1)"
            @keydown.right.prevent="move(1)"
            @keydown.home.prevent="selectEdge(false)"
            @keydown.end.prevent="selectEdge(true)"
            @focus="focusCurrent"
        >
            <title>{{ label }} · {{ zone }}</title>
            <rect :x="left" :y="top" :width="width - left - right" :height="bottom - top" class="plot-background" />
            <rect
                v-for="band in bands"
                :key="`band-${band.start}`"
                :x="x(band.start)"
                :width="Math.max(0, x(band.end) - x(band.start))"
                :y="top"
                :height="bottom - top"
                class="classification-band"
                :class="`period-${band.category}`"
            />
            <g v-for="tick in yTicks" :key="tick">
                <line
                    :x1="left"
                    :x2="width - right"
                    :y1="y(tick)"
                    :y2="y(tick)"
                    class="grid-line"
                    :class="{ 'zero-line': tick === 0 }"
                />
                <text :x="left - 6" :y="y(tick) + 4" text-anchor="end">
                    {{ tick.toLocaleString(locale, { maximumFractionDigits: 6 }) }}
                </text>
            </g>
            <g v-for="day in days" :key="day">
                <line :x1="x(day)" :x2="x(day)" :y1="top" :y2="bottom" class="day-line" />
                <text :x="x(day) + 5" y="16">
                    {{ t(day === dayBounds(now)[0] ? 'hemsUi.today' : 'hemsUi.tomorrow') }} ·
                    {{ new Date(day * 1000).toLocaleDateString(locale, { day: 'numeric', month: 'short' }) }}
                </text>
            </g>
            <g v-for="tick in xTicks" :key="tick">
                <text :x="x(tick)" :y="height - 8" text-anchor="middle">{{ time(tick) }}</text>
            </g>
            <path v-if="showSource" :d="secondaryPath" class="source-line" />
            <path :d="primaryPath" class="primary-line" />
            <g v-if="now >= start && now < end">
                <line :x1="x(now)" :x2="x(now)" :y1="top" :y2="bottom" class="now-line" />
                <text :x="Math.min(width - right - 30, x(now) + 4)" :y="top + 12" class="now-label">
                    {{ t('hemsUi.now') }}
                </text>
            </g>
            <g v-if="selected">
                <rect
                    :x="x(selected.start)"
                    :width="Math.max(1, x(selected.end) - x(selected.start))"
                    :y="top"
                    :height="bottom - top"
                    class="inspection-band"
                />
                <circle
                    v-if="finite(selected.value)"
                    :cx="x((selected.start + selected.end) / 2)"
                    :cy="y(selected.value)"
                    r="4"
                    class="inspection-dot"
                />
            </g>
            <rect :x="left" :y="top" :width="width - left - right" :height="bottom - top + 10" fill="transparent" />
        </svg>
        <figcaption class="inspection rounded p-3 mt-2" aria-live="polite" aria-atomic="true">
            <template v-if="selected">
                <div class="small text-body-secondary">{{ date(selected.start) }} – {{ intervalEnd(selected) }}</div>
                <div class="d-flex align-items-center flex-wrap gap-2 mt-1">
                    <strong class="fs-5">{{ display(selected.value) }}</strong>
                    <span v-if="selected.category" class="price-badge" :class="`period-${selected.category}`">{{
                        t('hemsUi.' + selected.category)
                    }}</span>
                    <span v-if="!finite(selected.value)" class="small">{{ t('hemsUi.noInterval') }}</span>
                </div>
                <span v-if="showSource && finite(selected.secondary)" class="d-block small text-body-secondary"
                    >{{ t('hemsUi.sourcePrice') }}: {{ display(selected.secondary) }}</span
                >
            </template>
            <span v-else class="text-body-secondary">{{
                inspected ? t('hemsUi.noInterval') : t('hemsUi.chartHelp')
            }}</span>
        </figcaption>
        <div class="d-flex align-items-center justify-content-between gap-2 mt-2">
            <div class="d-flex gap-1">
                <button
                    type="button"
                    class="btn btn-outline-secondary chart-step"
                    :aria-label="t('hemsUi.previousInterval')"
                    :disabled="!canMove(-1)"
                    @click="move(-1)"
                >
                    ‹
                </button>
                <button
                    type="button"
                    class="btn btn-outline-secondary chart-step"
                    :aria-label="t('hemsUi.nextInterval')"
                    :disabled="!canMove(1)"
                    @click="move(1)"
                >
                    ›
                </button>
            </div>
            <button type="button" class="btn btn-link btn-sm" @click="resetNow">{{ t('hemsUi.backToNow') }}</button>
        </div>
        <p v-if="!values.length" class="small text-body-secondary mt-2 mb-0" role="status">
            {{ t('hemsUi.noDayData') }}
        </p>
        <p class="small text-body-secondary mt-2 mb-0">{{ zone }} · {{ t('hemsUi.chartHelp') }}</p>
    </figure>
</template>
<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { chartScale, dayBounds, finite, stepPath, type ChartInterval } from '@/utils/hemsPresentation';
import './energy.css';
const props = defineProps<{
    intervals: ChartInterval[];
    now: number;
    label: string;
    unit: string;
    display: (value: number | null | undefined) => string;
    focusAt?: { utc: number };
    includeZero?: boolean;
}>();
const { t, locale } = useI18n();
const container = ref<HTMLElement>();
const surface = ref<SVGSVGElement>();
const view = ref<'today' | 'tomorrow' | 'twoDays'>('today');
const width = ref(600),
    height = 250,
    left = 48,
    right = 14,
    top = 30,
    bottom = 220;
const showSource = ref(false),
    inspected = ref<number | null>(null);
const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
const start = computed(() => dayBounds(props.now, view.value === 'tomorrow' ? 1 : 0)[0]);
const end = computed(() => dayBounds(props.now, view.value === 'today' ? 0 : 1)[1]);
const days = computed(() => (view.value === 'twoDays' ? [start.value, dayBounds(props.now, 1)[0]] : [start.value]));
const hasCategories = computed(() => props.intervals.some((i) => i.category));
const visible = computed(() =>
    props.intervals
        .filter((i) => i.end > start.value && i.start < end.value)
        .map((i) => ({ ...i, start: Math.max(start.value, i.start), end: Math.min(end.value, i.end) }))
);
// Merge adjacent categories so the plot has continuous, quiet bands rather than interval seams.
const bands = computed(() => {
    const result: ChartInterval[] = [];
    for (const interval of visible.value) {
        if (!interval.category || interval.category === 'normal' || !finite(interval.value)) continue;
        const previous = result[result.length - 1];
        if (previous?.end === interval.start && previous.category === interval.category) previous.end = interval.end;
        else result.push({ ...interval });
    }
    return result;
});
const values = computed(() =>
    visible.value.flatMap((i) => [i.value, ...(showSource.value ? [i.secondary] : [])]).filter(finite)
);
const scale = computed(() => chartScale(values.value, props.includeZero));
const x = (utc: number) => left + ((utc - start.value) / (end.value - start.value)) * (width.value - left - right);
const y = (value: number) =>
    bottom - ((value - scale.value.min) / (scale.value.max - scale.value.min)) * (bottom - top);
const yTicks = computed(() => scale.value.ticks);
const xTicks = computed(() => {
    const result: number[] = [];
    const step = view.value === 'twoDays' ? (width.value < 480 ? 12 : 6) : width.value < 480 ? 6 : 3;
    for (const day of days.value) {
        // Build wall-clock ticks per day; epoch positions remain DST-safe.
        for (let hour = step; hour < 24; hour += step) {
            const d = new Date(day * 1000);
            d.setHours(hour);
            result.push(d.getTime() / 1000);
        }
    }
    return [...new Set(result)];
});
const primaryPath = computed(() => stepPath(visible.value, x, y));
const secondaryPath = computed(() => stepPath(visible.value, x, y, true));
const selected = computed(() => {
    const utc = inspected.value ?? props.now;
    return visible.value.find((i) => i.start <= utc && utc < i.end);
});
const time = (utc: number) =>
    new Date(utc * 1000).toLocaleTimeString(locale.value, { hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
const date = (utc: number) =>
    new Date(utc * 1000).toLocaleString(locale.value, {
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        timeZoneName: 'shortOffset',
    });
function intervalEnd(interval: ChartInterval) {
    return dayBounds(interval.start)[0] !== dayBounds(interval.end)[0]
        ? date(interval.end)
        : new Date(interval.end * 1000).toLocaleTimeString(locale.value, {
              hour: '2-digit',
              minute: '2-digit',
              timeZoneName: 'shortOffset',
          });
}
function inspect(event: PointerEvent, down = false) {
    if (event.pointerType !== 'mouse' && !down && !event.buttons) return;
    const svg = event.currentTarget as SVGSVGElement;
    if (down) svg.setPointerCapture(event.pointerId);
    const bounds = svg.getBoundingClientRect();
    const position = ((event.clientX - bounds.left) / bounds.width) * width.value;
    if (position < left || position > width.value - right) return;
    inspected.value = start.value + ((position - left) / (width.value - left - right)) * (end.value - start.value);
}
function focusCurrent() {
    if (!selected.value) inspected.value = visible.value[0]?.start ?? null;
}
function canMove(direction: number) {
    if (!visible.value.length) return false;
    if (!selected.value) return true;
    const next = visible.value.indexOf(selected.value) + direction;
    return next >= 0 && next < visible.value.length;
}
function move(direction: number) {
    if (!canMove(direction)) return;
    const index = selected.value ? visible.value.indexOf(selected.value) : direction > 0 ? -1 : visible.value.length;
    inspected.value = visible.value[index + direction]?.start ?? null;
}
function selectEdge(last: boolean) {
    inspected.value = visible.value[last ? visible.value.length - 1 : 0]?.start ?? null;
}
function changeView(next: typeof view.value) {
    view.value = next;
    inspected.value = next === 'tomorrow' ? dayBounds(props.now, 1)[0] : null;
}
function resetNow() {
    changeView('today');
}
watch(() => dayBounds(props.now)[0], resetNow);
watch(
    () => props.focusAt,
    (target) => {
        if (!target) return;
        view.value = target.utc < dayBounds(props.now, 1)[0] ? 'today' : 'tomorrow';
        inspected.value = target.utc;
        surface.value?.focus();
    }
);
let observer: ResizeObserver | undefined;
let resizeFrame = 0;
onMounted(() => {
    observer = new ResizeObserver((entries) => {
        const next = Math.max(240, Math.round(entries[0]?.contentRect.width || 600));
        if (next !== width.value) {
            cancelAnimationFrame(resizeFrame);
            resizeFrame = requestAnimationFrame(() => {
                width.value = next;
            });
        }
    });
    if (container.value) observer.observe(container.value);
});
onUnmounted(() => {
    observer?.disconnect();
    cancelAnimationFrame(resizeFrame);
});
</script>
<style scoped>
.energy-chart {
    min-width: 0;
}
.chart-surface {
    display: block;
    touch-action: pan-y;
    color: var(--bs-body-color);
}
.chart-surface:focus-visible {
    outline: 2px solid var(--bs-primary);
    outline-offset: 2px;
}
svg text {
    fill: var(--bs-secondary-color);
    font-size: 11px;
}
.grid-line {
    stroke: var(--bs-border-color);
    stroke-width: 0.5;
}
.zero-line {
    stroke: var(--bs-secondary-color);
    stroke-width: 1;
}
.day-line {
    stroke: var(--bs-border-color);
    stroke-dasharray: 3 3;
}
.primary-line {
    fill: none;
    stroke: var(--energy-line);
    stroke-width: 2.5;
    stroke-linejoin: round;
}
.source-line {
    fill: none;
    stroke: var(--bs-secondary-color);
    stroke-width: 1.2;
    stroke-dasharray: 4 3;
}
.now-line {
    stroke: var(--bs-body-color);
    stroke-dasharray: 4 3;
}
.now-label {
    fill: var(--bs-body-color);
}
.inspection-band {
    fill: var(--energy-line);
    opacity: 0.08;
}
.inspection-dot {
    fill: var(--energy-line);
}
.inspection {
    min-height: 4.5rem;
    overflow-wrap: anywhere;
}
.plot-background {
    fill: var(--energy-plot);
}
.classification-band {
    fill: var(--period-color);
    opacity: 0.11;
}
.chart-toolbar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
}
.chart-range {
    display: flex;
    padding: 3px;
    border-radius: 0.65rem;
    background: var(--bs-tertiary-bg);
}
.chart-range button {
    border: 0;
    border-radius: 0.5rem;
    background: transparent;
    color: var(--bs-secondary-color);
    padding: 0.5rem 0.75rem;
    min-height: 44px;
    font-size: 0.875rem;
}
.chart-range button[aria-pressed='true'] {
    background: var(--bs-body-bg);
    color: var(--bs-body-color);
    box-shadow: 0 1px 4px #0002;
    font-weight: 600;
}
.chart-legend {
    display: flex;
    flex-wrap: wrap;
    gap: 0.35rem 0.75rem;
}
.chart-legend > .text-body-secondary {
    flex-basis: 100%;
}
.legend-swatch {
    display: inline-block;
    width: 0.55rem;
    height: 0.55rem;
    border-radius: 50%;
    margin-right: 0.35rem;
    background: var(--period-color);
}
.inspection {
    background: var(--bs-tertiary-bg);
}
.chart-step {
    width: 44px;
    height: 44px;
    font-size: 1.35rem;
    line-height: 1;
}
</style>
