<template>
    <BasePage :title="$t('profiles.Title')">
        <HemsProfilePanel />
    </BasePage>
</template>

<script setup lang="ts">
import BasePage from '@/components/BasePage.vue';
import HemsProfilePanel from '@/components/HemsProfilePanel.vue';
</script>
