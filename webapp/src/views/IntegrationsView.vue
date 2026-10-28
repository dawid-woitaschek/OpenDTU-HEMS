<template>
    <BasePage :title="$t('redesign.integrations')">
        <HemsProfilePanel />
        <CardElement :text="$t('redesign.dataSources')" :help="$t('redesign.sourcesHelp')" add-space>
            <ul class="mb-0">
                <li>
                    <RouterLink to="/plant/power-meter">{{ $t('redesign.powerMeter') }}</RouterLink>
                </li>
                <li>
                    <RouterLink to="/plant/solar-charger">{{ $t('redesign.solarCharger') }}</RouterLink>
                </li>
                <li>
                    <RouterLink to="/plant/battery">{{ $t('redesign.battery') }}</RouterLink>
                </li>
                <li>
                    <RouterLink to="/plant/wallbox">{{ $t('redesign.wallbox') }}</RouterLink>
                </li>
                <li>
                    <RouterLink to="/energy/planning"
                        >{{ $t('hemsUi.tariff') }} / {{ $t('hemsUi.forecast') }}</RouterLink
                    >
                </li>
            </ul>
        </CardElement>
    </BasePage>
</template>
<script setup lang="ts">
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import HemsProfilePanel from '@/components/HemsProfilePanel.vue';
</script>
