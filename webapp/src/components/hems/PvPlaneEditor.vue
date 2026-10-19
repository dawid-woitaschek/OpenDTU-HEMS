<template>
    <p class="small text-body-secondary">{{ $t('hemsUi.azimuthHint') }}</p>
    <div class="row g-3 mb-3">
        <div v-for="(plane, index) in planes" :key="index" class="col-12 col-xl-6">
            <fieldset class="border rounded p-3 h-100">
                <legend class="float-none w-auto fs-6 px-1">
                    {{ plane.name || $t('hemsUi.planeNumber', { number: index + 1 }) }}
                </legend>
                <div class="d-flex justify-content-between align-items-center gap-2 mb-3">
                    <label class="form-check-label"
                        ><input v-model="plane.enabled" type="checkbox" class="form-check-input me-2" />{{
                            $t('hemsUi.enabled')
                        }}</label
                    >
                    <button
                        type="button"
                        class="btn btn-outline-danger btn-sm"
                        :aria-label="$t('hemsUi.removePlane', { number: index + 1 })"
                        @click="
                            emit(
                                'update:planes',
                                planes.filter((_, i) => i !== index)
                            )
                        "
                    >
                        {{ $t('hemsUi.remove') }}
                    </button>
                </div>
                <label :for="`pv-name-${index}`" class="form-label">{{ $t('hemsUi.name') }}</label>
                <input
                    :id="`pv-name-${index}`"
                    v-model="plane.name"
                    class="form-control mb-2"
                    maxlength="32"
                    required
                />
                <div class="row g-2">
                    <div v-for="field in coordinates" :key="field.key" class="col-sm-6">
                        <label :for="`pv-${field.key}-${index}`" class="form-label"
                            >{{ $t('hemsUi.' + field.label) }} (°)</label
                        >
                        <input
                            :id="`pv-${field.key}-${index}`"
                            :value="plane[field.key] == null ? '' : plane[field.key]! / 1e6"
                            type="number"
                            class="form-control"
                            :min="-field.max"
                            :max="field.max"
                            step="0.000001"
                            required
                            @input="plane[field.key] = coordinate($event)"
                        />
                    </div>
                    <div v-for="field in angles" :key="field.key" class="col-6">
                        <label :for="`pv-${field.key}-${index}`" class="form-label"
                            >{{ $t('hemsUi.' + field.key) }} (°)</label
                        >
                        <input
                            :id="`pv-${field.key}-${index}`"
                            v-model.number="plane[field.key]"
                            type="number"
                            class="form-control"
                            :min="field.min"
                            :max="field.max"
                            step="1"
                            required
                        />
                    </div>
                    <div class="col-sm-6">
                        <label :for="`pv-peak-${index}`" class="form-label">{{ $t('hemsUi.peakPower') }} (kWp)</label>
                        <input
                            :id="`pv-peak-${index}`"
                            :value="plane.peak_w / 1000 || ''"
                            type="number"
                            class="form-control"
                            min="0.001"
                            max="1000"
                            step="0.001"
                            required
                            @input="plane.peak_w = Math.round(Number(($event.target as HTMLInputElement).value) * 1000)"
                        />
                    </div>
                </div>
                <p
                    v-if="matchesSaved(index)"
                    class="small mt-3 mb-0"
                    :class="statuses?.[index]?.last_error ? 'text-warning-emphasis' : 'text-body-secondary'"
                >
                    {{
                        !plane.enabled
                            ? $t('hemsUi.disabled')
                            : statuses?.[index]?.last_error
                              ? $t('hemsUi.' + providerMessage(statuses[index]!.last_error!))
                              : statuses?.[index]?.state === 'not_attempted'
                                ? $t('hemsUi.planeNotAttempted')
                                : statuses?.[index]?.state === 'loading'
                                  ? $t('hemsUi.loadingEnergy')
                                  : $t(statuses?.[index]?.fetched_at ? 'hemsUi.ready' : 'hemsUi.waitingData')
                    }}
                </p>
                <p v-else class="small text-body-secondary mt-3 mb-0">{{ $t('hemsUi.unsavedPlane') }}</p>
            </fieldset>
        </div>
    </div>
    <button
        type="button"
        class="btn btn-outline-secondary mb-3"
        :disabled="planes.length >= 6"
        @click="
            emit('update:planes', [
                ...planes,
                { enabled: true, name: '', latitude_e6: null, longitude_e6: null, tilt: 0, azimuth: 0, peak_w: 0 },
            ])
        "
    >
        {{ $t('hemsUi.addPlane') }} ({{ planes.length }}/6)
    </button>
</template>
<script setup lang="ts">
import type { PvPlane, PlaneStatus } from '@/types/HemsEnergy';
import { providerMessage } from '@/utils/hemsPresentation';
const props = defineProps<{
    planes: PvPlane[];
    saved: PvPlane[];
    statuses?: PlaneStatus[];
}>();
const emit = defineEmits<{ 'update:planes': [planes: PvPlane[]] }>();
const coordinates = [
    { key: 'latitude_e6', label: 'latitude', max: 90 },
    { key: 'longitude_e6', label: 'longitude', max: 180 },
] as const;
const angles = [
    { key: 'tilt', min: 0, max: 90 },
    { key: 'azimuth', min: -180, max: 180 },
] as const;
function coordinate(event: Event) {
    const value = (event.target as HTMLInputElement).value;
    return value === '' ? null : Math.round(Number(value) * 1e6);
}
function matchesSaved(index: number) {
    return JSON.stringify(props.planes[index]) === JSON.stringify(props.saved[index]);
}
</script>
