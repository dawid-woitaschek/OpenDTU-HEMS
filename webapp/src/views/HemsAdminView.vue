<template>
    <BasePage :title="$t('hems.Settings')" :isLoading="dataLoading">
        <BootstrapAlert
            v-model="alert.show"
            dismissible
            :variant="alert.type"
            :auto-dismiss="alert.type != 'success' ? 0 : 5000"
        >
            {{ alert.message }}
        </BootstrapAlert>
        <form v-if="!dataLoading && !loadFailed && hemsConfig.version === 1" @submit.prevent="saveConfig">
            <CardElement :text="$t('hems.General')" textVariant="text-bg-primary">
                <InputElement :label="$t('hems.Enabled')" v-model="hemsConfig.enabled" type="checkbox" wide />
                <div class="row mb-3">
                    <span class="col-sm-4">{{ $t('hems.Mode') }}</span>
                    <span class="col-sm-8">{{ $t(hemsConfig.enabled ? 'hems.Monitor' : 'hems.Disabled') }}</span>
                </div>
                <div class="alert alert-secondary" role="alert">{{ $t('hems.MonitorHint') }}</div>
            </CardElement>
            <FormFooter @reload="getConfig" />
        </form>
        <div v-else-if="!dataLoading && !loadFailed" class="alert alert-warning" role="alert">
            {{ $t('hems.UnsupportedVersion') }}
        </div>
        <HemsValidationPanel v-if="!dataLoading && !loadFailed" />
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import CardElement from '@/components/CardElement.vue';
import HemsValidationPanel from '@/components/HemsValidationPanel.vue';
import FormFooter from '@/components/FormFooter.vue';
import InputElement from '@/components/InputElement.vue';
import type { AlertResponse } from '@/types/AlertResponse';
import type { HemsConfig } from '@/types/HemsConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

export default defineComponent({
    components: { BasePage, BootstrapAlert, CardElement, FormFooter, InputElement, HemsValidationPanel },
    data() {
        return {
            dataLoading: true,
            loadFailed: false,
            hemsConfig: {} as HemsConfig,
            alert: {} as AlertResponse,
        };
    },
    created() {
        this.getConfig();
    },
    methods: {
        showError(message: string) {
            this.alert = { show: true, type: 'danger', code: 0, message };
        },
        getConfig() {
            this.dataLoading = true;
            this.loadFailed = false;
            fetch('/api/hems/config', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: HemsConfig) => {
                    this.hemsConfig = data;
                    this.dataLoading = false;
                })
                .catch(() => {
                    this.loadFailed = true;
                    this.dataLoading = false;
                    this.showError(this.$t('hems.ConfigLoadFailed'));
                });
        },
        saveConfig() {
            const formData = new FormData();
            formData.append(
                'data',
                JSON.stringify({
                    version: this.hemsConfig.version,
                    enabled: this.hemsConfig.enabled,
                })
            );
            fetch('/api/hems/config', { method: 'POST', headers: authHeader(), body: formData })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: AlertResponse) => {
                    this.alert = data;
                    this.alert.message = data.type === 'success' ? this.$t('apiresponse.' + data.code) : data.message;
                    this.alert.show = true;
                })
                .catch(() => this.showError(this.$t('hems.ConfigSaveFailed')));
        },
    },
});
</script>
