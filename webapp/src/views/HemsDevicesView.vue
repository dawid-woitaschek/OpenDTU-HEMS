<template>
    <BasePage :title="'HEMS — ' + $t('menu.DeviceManager') + ' / ' + $t('profiles.Title')" :isLoading="dataLoading">
        <BootstrapAlert
            v-model="alert.show"
            dismissible
            :variant="alert.type"
            :auto-dismiss="alert.type != 'success' ? 0 : 5000"
        >
            {{ alert.message }}
        </BootstrapAlert>
        <form v-if="!dataLoading && hemsConfig.version === 1" @submit.prevent="saveConfig">
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
                <FormFooter @reload="getConfig" />
            </CardElement>
        </form>
        <div v-else-if="!dataLoading" class="alert alert-warning" role="alert">
            {{ $t('hems.UnsupportedVersion') }}
        </div>
        <div v-if="!dataLoading" class="mt-5">
            <HemsProfilePanel />
        </div>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import CardElement from '@/components/CardElement.vue';
import FormFooter from '@/components/FormFooter.vue';
import HemsProfilePanel from '@/components/HemsProfilePanel.vue';
import InputElement from '@/components/InputElement.vue';
import type { AlertResponse } from '@/types/AlertResponse';
import type { HemsConfig } from '@/types/HemsConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

export default defineComponent({
    components: { BasePage, BootstrapAlert, CardElement, FormFooter, HemsProfilePanel, InputElement },
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
