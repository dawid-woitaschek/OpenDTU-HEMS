<template>
    <div :class="['card', table ? 'card-table' : '', addSpace ? 'mt-5' : '']">
        <div :class="['card-header', textVariant]">
            <span class="label-with-help">{{ text }}<HelpHint v-if="help" :text="help" :label="text" /></span>
        </div>
        <div :class="['card-body', 'card-text', centerContent ? 'text-center' : '', flexChildren ? 'd-flex' : '']">
            <slot />
        </div>
    </div>
</template>

<script lang="ts">
import HelpHint from './HelpHint.vue';
import { defineComponent } from 'vue';

export default defineComponent({
    components: { HelpHint },
    props: {
        help: String,
        text: String,
        textVariant: String,
        table: Boolean,
        addSpace: Boolean,
        centerContent: Boolean,
        flexChildren: Boolean,
    },
});
</script>
