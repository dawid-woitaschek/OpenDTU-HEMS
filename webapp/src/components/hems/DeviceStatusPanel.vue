<template>
    <section class="card mb-4" :aria-label="$t('hems.Status')">
        <div class="card-body">
            <h2 class="h6">{{ $t('hems.Status') }}</h2>
            <p v-if="unavailable" class="text-warning-emphasis mb-0" role="alert">{{ $t('hems.StatusUnavailable') }}</p>
            <template v-else-if="current">
                <div class="d-flex flex-wrap gap-3">
                    <strong>{{ $t('hems.' + current.status) }}</strong>
                    <template v-if="device === 'battery' && data?.storage">
                        <span>{{ $t('hems.StorageSoc') }}: {{ number(data.storage.soc_pct, '%') }}</span>
                        <span>{{ $t('storage.AcPower') }}: {{ number(data.storage.ac_power_w, 'W') }}</span>
                        <span>{{ $t('storage.Temperature') }}: {{ number(data.storage.temperature_c, '°C') }}</span>
                    </template>
                    <template v-else-if="data?.warp">
                        <span
                            >{{ $t('hems.EvseVehicle') }}:
                            {{
                                $t(
                                    'hems.' +
                                        (data.warp.vehicle_connected === null
                                            ? 'Unknown'
                                            : data.warp.vehicle_connected
                                              ? 'Yes'
                                              : 'No')
                                )
                            }}</span
                        >
                        <span>{{ $t('hemsUi.charging') }}: {{ number(data.warp.power_w, 'W') }}</span>
                    </template>
                    <span
                        >{{ $t('hems.DataAge') }}:
                        {{ number(current.age_ms === null ? null : current.age_ms / 1000, 's') }}</span
                    >
                </div>
                <div v-if="current.error && current.error.toLowerCase() !== 'none'" class="text-warning-emphasis mt-2">
                    {{ $t('hems.Errors.' + current.error) }}
                </div>
                <div
                    v-if="device === 'battery' && (data?.storage.fault_active || data?.storage.alarm_active)"
                    class="alert alert-warning mt-2 mb-0"
                >
                    {{ $t(data.storage.fault_active ? 'hemsUi.deviceFault' : 'hemsUi.deviceAlarm') }}
                </div>
            </template>
            <span v-else>{{ $t('base.Loading') }}</span>
        </div>
    </section>
</template>
<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { usePolledResource } from '@/utils/usePolledResource';
import type { HemsStatus } from '@/types/HemsStatus';
const props = defineProps<{ device: string }>();
const { locale } = useI18n();
const { data, unavailable } = usePolledResource<HemsStatus>('/api/hems/status');
const current = computed(() => (props.device === 'battery' ? data.value?.storage : data.value?.warp));
const number = (value: number | null, unit: string) =>
    value === null ? '—' : `${value.toLocaleString(locale.value, { maximumFractionDigits: 1 })} ${unit}`;
</script>
