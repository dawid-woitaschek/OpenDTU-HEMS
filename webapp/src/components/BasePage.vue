<template>
    <div
        :class="{ 'container-xxl': !embedded && !isWideScreen, 'container-fluid': !embedded && isWideScreen }"
        :role="embedded ? undefined : 'main'"
    >
        <div v-if="!embedded" class="page-header">
            <div class="row mb-3">
                <div :class="'align-content-center ' + (showReload ? 'col-10' : 'col-12')">
                    <h1 class="mb-0">
                        {{ title }}
                    </h1>
                </div>
                <div class="col-2 align-content-center" v-if="showReload">
                    <button
                        type="button"
                        class="float-end btn btn-outline-primary fs-5"
                        @click="$emit('reload')"
                        v-tooltip
                        :title="$t('base.Reload')"
                    >
                        <BIconArrowClockwise />
                    </button>
                </div>
            </div>
        </div>

        <div class="text-center" v-if="isLoading">
            <div class="spinner-border" role="status">
                <span class="visually-hidden">{{ $t('base.Loading') }}</span>
            </div>
        </div>

        <template v-if="!isLoading">
            <slot />
        </template>
    </div>
</template>

<script lang="ts">
import { defineComponent } from 'vue';
import { BIconArrowClockwise } from 'bootstrap-icons-vue';
import PullToRefresh from 'pulltorefreshjs';

export default defineComponent({
    components: {
        BIconArrowClockwise,
    },
    props: {
        embedded: Boolean,
        title: { type: String, required: true },
        isLoading: { type: Boolean, required: false, default: false },
        isWideScreen: { type: Boolean, required: false, default: false },
        showReload: { type: Boolean, required: false, default: false },
    },
    mounted() {
        if (this.embedded) return;
        PullToRefresh.init({
            mainElement: 'body', // above which element?
            instructionsPullToRefresh: this.$t('base.Pull'),
            instructionsReleaseToRefresh: this.$t('base.Release'),
            instructionsRefreshing: this.$t('base.Refreshing'),
            onRefresh: () => {
                this.$emit('reload');
            },
        });
    },
    unmounted() {
        if (!this.embedded) PullToRefresh.destroyAll();
    },
});
</script>

<style scoped>
.ptr--text {
    color: var(--bs-primary-text-emphasis) !important;
}
.ptr--icon {
    color: var(--bs-primary-text-emphasis) !important;
}
</style>
