<template>
    <span class="help-hint">
        <button
            ref="trigger"
            type="button"
            class="help-trigger"
            :class="{ 'help-trigger-plain': plain }"
            :aria-label="label || $t('redesign.help')"
            :aria-describedby="visible ? id : undefined"
            :aria-expanded="visible"
            @focus="focus"
            @blur="blur"
            @pointerenter="hover"
            @pointerleave="hideSoon"
            @keydown.esc.stop.prevent="hide"
            @click.stop.prevent="toggle"
        >
            <slot name="trigger">?</slot>
        </button>
        <Teleport to="body">
            <span
                v-if="visible"
                ref="panel"
                :id="id"
                role="tooltip"
                class="help-content"
                :style="position"
                @pointerenter="cancelHide"
                @pointerleave="hideSoon"
                ><slot>{{ text }}</slot></span
            >
        </Teleport>
    </span>
</template>
<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, ref, useId } from 'vue';
defineProps<{ text?: string; label?: string; plain?: boolean }>();
const id = useId();
const trigger = ref<HTMLButtonElement>(),
    panel = ref<HTMLElement>();
const visible = ref(false),
    position = ref<Record<string, string>>({});
let pinned = false,
    focused = false,
    timer: ReturnType<typeof setTimeout> | undefined;
function cancelHide() {
    clearTimeout(timer);
}
function hide() {
    cancelHide();
    visible.value = false;
    pinned = false;
}
function hideSoon() {
    cancelHide();
    if (!pinned && !focused) timer = setTimeout(hide, 180);
}
async function show() {
    cancelHide();
    const rect = trigger.value?.getBoundingClientRect();
    if (!rect) return;
    const width = Math.min(320, window.innerWidth - 24);
    position.value = {
        width: `${width}px`,
        left: `${Math.max(12, Math.min(rect.left, window.innerWidth - width - 12))}px`,
    };
    visible.value = true;
    await nextTick();
    const height = panel.value?.getBoundingClientRect().height || 0;
    const below = rect.bottom + 8;
    const top = below + height <= window.innerHeight - 8 ? below : Math.max(8, rect.top - height - 8);
    position.value = { ...position.value, top: `${top}px` };
}
function focus() {
    focused = true;
    void show();
}
function blur() {
    focused = false;
    pinned = false;
    hideSoon();
}
function hover(event: PointerEvent) {
    if (event.pointerType === 'mouse') void show();
}
function toggle() {
    if (pinned) hide();
    else {
        pinned = true;
        void show();
    }
}
function outside(event: Event) {
    if (!trigger.value?.contains(event.target as Node) && !panel.value?.contains(event.target as Node)) hide();
}
function escape(event: KeyboardEvent) {
    if (event.key === 'Escape') hide();
}
function scroll(event: Event) {
    if (!panel.value?.contains(event.target as Node)) hide();
}
onMounted(() => {
    window.addEventListener('resize', hide);
    document.addEventListener('pointerdown', outside);
    document.addEventListener('keydown', escape);
    document.addEventListener('scroll', scroll, true);
});
onUnmounted(() => {
    cancelHide();
    window.removeEventListener('resize', hide);
    document.removeEventListener('pointerdown', outside);
    document.removeEventListener('keydown', escape);
    document.removeEventListener('scroll', scroll, true);
});
</script>
