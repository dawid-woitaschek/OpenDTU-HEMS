<template>
    <BasePage :title="$t('redesign.integrations')">
        <HemsNav />
        <HemsProfilePanel />
        <CardElement :text="$t('redesign.dataSources')" add-space>
            <div class="d-flex align-items-center gap-2 mb-3">
                {{ $t('redesign.normalizedSources') }}
                <HelpHint :text="$t('redesign.sourcesHelp')" />
            </div>
            <ul class="mb-0">
                <li>
                    <RouterLink to="/plant/power-meter">{{ $t('redesign.powerMeter') }}</RouterLink>
                </li>
                <li>
                    <RouterLink to="/plant/solar">{{ $t('redesign.solar') }}</RouterLink>
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
import HelpHint from '@/components/HelpHint.vue';
import HemsNav from '@/components/hems/HemsNav.vue';
import HemsProfilePanel from '@/components/HemsProfilePanel.vue';
</script>
