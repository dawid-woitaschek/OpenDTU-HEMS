<template>
    <span class="help-hint">
        <button
            ref="trigger"
            type="button"
            class="help-trigger"
            :aria-label="label || $t('redesign.help')"
            :aria-describedby="visible ? id : undefined"
            @focus="
                focused = true;
                show();
            "
            @blur="
                focused = false;
                hideSoon();
            "
            @mouseenter="show"
            @mouseleave="hideSoon"
            @keydown.esc.stop.prevent="hide"
            @click.stop.prevent="show"
        >
            ?
        </button>
        <Teleport to="body">
            <span
                v-if="visible"
                :id="id"
                role="tooltip"
                class="help-content"
                :style="position"
                @mouseenter="cancelHide"
                @mouseleave="hideSoon"
            >
                <slot>{{ text }}</slot>
            </span>
        </Teleport>
    </span>
</template>
<script setup lang="ts">
import { onMounted, onUnmounted, ref, useId } from 'vue';
defineProps<{ text?: string; label?: string }>();
const id = useId();
const trigger = ref<HTMLButtonElement>();
const visible = ref(false),
    focused = ref(false);
const position = ref<Record<string, string>>({});
let timer: ReturnType<typeof setTimeout> | undefined;
function cancelHide() {
    clearTimeout(timer);
}
function hide() {
    cancelHide();
    visible.value = false;
}
function hideSoon() {
    cancelHide();
    if (!focused.value) timer = setTimeout(hide, 150);
}
function show() {
    cancelHide();
    const rect = trigger.value?.getBoundingClientRect();
    if (!rect) return;
    const width = Math.min(288, window.innerWidth - 24);
    position.value = {
        width: `${width}px`,
        left: `${Math.max(12, Math.min(rect.left, window.innerWidth - width - 12))}px`,
        ...(window.innerHeight - rect.bottom < 240
            ? { bottom: `${window.innerHeight - rect.top + 8}px` }
            : { top: `${rect.bottom + 8}px` }),
    };
    visible.value = true;
}
function escape(event: KeyboardEvent) {
    if (event.key === 'Escape') hide();
}
onMounted(() => {
    window.addEventListener('resize', hide);
    window.addEventListener('keydown', escape);
});
onUnmounted(() => {
    cancelHide();
    window.removeEventListener('resize', hide);
    window.removeEventListener('keydown', escape);
});
</script>
