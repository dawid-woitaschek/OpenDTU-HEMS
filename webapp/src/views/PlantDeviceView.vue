<template>
    <BasePage :title="$t('redesign.' + device)">
        <section v-if="device === 'powerMeter'" class="card mb-4">
            <div class="card-body">
                <h2 class="h6">{{ $t('hems.GridPower') }}</h2>
                <strong class="fs-3 overview-value">{{ livePower }}</strong>
            </div>
        </section>
        <template v-if="device === 'battery'">
            <BatteryView />
            <HemsDevicesView device="battery" embedded />
            <details class="border rounded p-3 mt-4">
                <summary>{{ $t('redesign.batteryTelemetry') }}</summary>
                <BatteryAdminView embedded />
            </details>
            <details class="border rounded p-3 mt-3">
                <summary>{{ $t('menu.AcChargerSettings') }}</summary>
                <GridChargerView /><GridChargerAdminView embedded />
            </details>
        </template>
        <template v-else-if="device === 'solarCharger'">
            <SolarChargerView />
            <details class="border rounded p-3 mt-4">
                <summary>{{ $t('redesign.deviceConfiguration') }}</summary>
                <SolarChargerAdminView embedded />
            </details>
            <RouterLink to="/plant/inverters" class="btn btn-outline-primary mt-3">{{
                $t('redesign.inverters')
            }}</RouterLink>
        </template>
        <PowerMeterAdminView v-else embedded />
    </BasePage>
</template>
<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { usePolledResource } from '@/utils/usePolledResource';
import type { HemsStatus } from '@/types/HemsStatus';
import { signalValue } from '@/utils/hemsPresentation';
import PowerMeterAdminView from './PowerMeterAdminView.vue';
import BasePage from '@/components/BasePage.vue';
import BatteryView from '@/components/BatteryView.vue';
import SolarChargerView from '@/components/SolarChargerView.vue';
import GridChargerView from '@/components/GridChargerView.vue';
import BatteryAdminView from './BatteryAdminView.vue';
import SolarChargerAdminView from './SolarChargerAdminView.vue';
import GridChargerAdminView from './GridChargerAdminView.vue';
import HemsDevicesView from './HemsDevicesView.vue';
const props = defineProps<{ device: 'solarCharger' | 'battery' | 'powerMeter' }>();
const { locale } = useI18n();
const { data, unavailable } = usePolledResource<HemsStatus>('/api/hems/status', () => props.device === 'powerMeter');
const livePower = computed(() => {
    const signal = data.value?.normalized.grid.power_w;
    const value = signalValue(signal, unavailable.value);
    return value === null ? '—' : `${value.toLocaleString(locale.value, { maximumFractionDigits: 1 })} W`;
});
</script>
