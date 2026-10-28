<template>
    <span class="label-with-help">
        <label :for="forId" :class="labelClass"
            ><slot>{{ text }}</slot></label
        >
        <HelpHint v-if="help" :text="help" :label="text ? $t('redesign.helpFor', { label: text }) : undefined" />
    </span>
</template>
<script setup lang="ts">
import HelpHint from './HelpHint.vue';
defineProps<{ forId?: string; text?: string; help?: string; labelClass?: string }>();
</script>
