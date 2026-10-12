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
        <form v-if="!dataLoading && hemsConfig.version === 1" @submit.prevent="saveConfig">
            <CardElement :text="$t('hems.General')" textVariant="text-bg-primary">
                <InputElement :label="$t('hems.Enabled')" v-model="hemsConfig.enabled" type="checkbox" wide />
                <div class="row mb-3">
                    <span class="col-sm-4">{{ $t('hems.Mode') }}</span>
                    <span class="col-sm-8">{{ $t(hemsConfig.enabled ? 'hems.Monitor' : 'hems.Disabled') }}</span>
                </div>
                <div class="alert alert-secondary" role="alert">{{ $t('hems.MonitorHint') }}</div>
            </CardElement>
            <CardElement :text="$t('hems.Warp')" textVariant="text-bg-primary">
                <div class="alert alert-info" role="alert">{{ $t('hems.ReadOnlyHint') }}</div>
                <InputElement :label="$t('hems.WarpEnabled')" v-model="hemsConfig.warp.enabled" type="checkbox" wide />
                <template v-if="hemsConfig.warp.enabled">
                    <InputElement
                        :label="$t('hems.Host')"
                        v-model="hemsConfig.warp.host"
                        type="text"
                        maxlength="253"
                        wide
                    />
                    <InputElement
                        :label="$t('hems.Port')"
                        v-model="hemsConfig.warp.port"
                        type="number"
                        min="1"
                        max="65535"
                        step="1"
                        wide
                    />
                    <div class="alert alert-secondary" role="alert">{{ $t('hems.WarpSetupHint') }}</div>
                </template>
            </CardElement>
            <FormFooter @reload="getConfig" />
        </form>
        <div v-else-if="!dataLoading" class="alert alert-warning" role="alert">
            {{ $t('hems.UnsupportedVersion') }}
        </div>
        <HemsProfilePanel v-if="!dataLoading" />
    </BasePage>
</template>

<script lang="ts">
import HemsProfilePanel from '@/components/HemsProfilePanel.vue';
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import CardElement from '@/components/CardElement.vue';
import FormFooter from '@/components/FormFooter.vue';
import InputElement from '@/components/InputElement.vue';
import type { AlertResponse } from '@/types/AlertResponse';
import type { HemsConfig } from '@/types/HemsConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

export default defineComponent({
    components: { HemsProfilePanel, BasePage, BootstrapAlert, CardElement, FormFooter, InputElement },
    data() {
        return {
            dataLoading: true,
            hemsConfig: {} as HemsConfig,
            alert: {} as AlertResponse,
        };
    },
    created() {
        this.getConfig();
    },
    methods: {
        getConfig() {
            this.dataLoading = true;
            fetch('/api/hems/config', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: HemsConfig) => {
                    this.hemsConfig = data;
                    this.dataLoading = false;
                });
        },
        saveConfig() {
            const formData = new FormData();
            formData.append('data', JSON.stringify(this.hemsConfig));
            fetch('/api/hems/config', { method: 'POST', headers: authHeader(), body: formData })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: AlertResponse) => {
                    this.alert = data;
                    this.alert.message = this.$t('apiresponse.' + data.code);
                    this.alert.show = true;
                });
        },
    },
});
</script>
