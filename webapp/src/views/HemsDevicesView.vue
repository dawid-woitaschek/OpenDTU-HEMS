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

            <CardElement :text="$t('storage.Title')" textVariant="text-bg-primary" add-space>
                <div class="alert alert-info" role="alert">{{ $t('hems.ReadOnlyHint') }}</div>
                <InputElement :label="$t('storage.Enabled')" v-model="hemsConfig.storage.enabled" type="checkbox" wide />
                <template v-if="hemsConfig.storage.enabled">
                    <InputElement
                        :label="$t('hems.Host')"
                        v-model="hemsConfig.storage.host"
                        type="text"
                        maxlength="253"
                        wide
                    />
                    <div class="row mb-3">
                        <label for="hems-storage-profile" class="col-sm-4 col-form-label">{{ $t('storage.Profile') }}</label>
                        <div class="col-sm-8">
                            <select
                                id="hems-storage-profile"
                                class="form-select"
                                v-model="hemsConfig.storage.profile"
                            >
                                <option value="" disabled>
                                    {{ profiles.length ? $t('storage.SelectProfile') : $t('storage.NoProfiles') }}
                                </option>
                                <option
                                    v-if="storageProfileUnavailable"
                                    :value="hemsConfig.storage.profile"
                                    disabled
                                >
                                    ⚠ {{ $t('profiles.MissingStorageProfile', { id: hemsConfig.storage.profile }) }}
                                </option>
                                <option v-for="profile in profiles" :key="profile.id" :value="profile.id">
                                    {{ profile.id }} — {{ profile.vendor }} / {{ profile.model }}
                                </option>
                            </select>
                            <div v-if="storageProfileUnavailable" class="alert alert-warning mt-2 mb-0" role="alert">
                                {{ $t('profiles.MissingStorageProfile', { id: hemsConfig.storage.profile }) }}
                            </div>
                            <div
                                v-else-if="!profiles.length"
                                class="alert alert-warning mt-2 mb-0"
                                role="alert"
                            >
                                {{ $t('storage.NoProfilesHint') }}
                            </div>
                        </div>
                    </div>
                    <InputElement
                        :label="$t('storage.Port')"
                        v-model="hemsConfig.storage.port"
                        type="number"
                        min="0"
                        max="65535"
                        step="1"
                        wide
                    />
                    <InputElement
                        :label="$t('storage.UnitId')"
                        v-model="hemsConfig.storage.unit"
                        type="number"
                        min="0"
                        max="247"
                        step="1"
                        wide
                    />
                    <div class="alert alert-secondary" role="alert">{{ $t('storage.SetupHint') }}</div>
                </template>
                <FormFooter @reload="getConfig" />
            </CardElement>
        </form>
        <div v-else-if="!dataLoading" class="alert alert-warning" role="alert">
            {{ $t('hems.UnsupportedVersion') }}
        </div>
        <div v-if="!dataLoading" class="mt-5">
            <HemsProfilePanel @profiles-changed="refreshProfiles" />
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

type ProfileSummary = {
    id: string;
    vendor: string;
    model: string;
    capabilities: string[];
};

export default defineComponent({
    components: { BasePage, BootstrapAlert, CardElement, FormFooter, HemsProfilePanel, InputElement },
    data() {
        return {
            dataLoading: true,
            hemsConfig: {} as HemsConfig,
            profiles: [] as ProfileSummary[],
            alert: {} as AlertResponse,
        };
    },
    computed: {
        storageProfileUnavailable(): boolean {
            const profile = this.hemsConfig.storage?.profile;
            return !!profile && !this.profiles.some((candidate) => candidate.id === profile);
        },
    },
    created() {
        this.getConfig();
    },
    methods: {
        applyProfiles(profileData: { profiles: ProfileSummary[] }) {
            this.profiles = profileData.profiles.filter(
                (profile) => profile.id !== 'example.synthetic' && profile.capabilities?.includes('storage')
            );
        },
        refreshProfiles() {
            fetch('/api/hems/profiles', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((profileData) => this.applyProfiles(profileData));
        },
        getConfig() {
            this.dataLoading = true;
            Promise.all([
                fetch('/api/hems/config', { headers: authHeader() }).then((response) =>
                    handleResponse(response, this.$emitter, this.$router)
                ),
                fetch('/api/hems/profiles', { headers: authHeader() }).then((response) =>
                    handleResponse(response, this.$emitter, this.$router)
                ),
            ]).then(([config, profileData]) => {
                this.hemsConfig = config as HemsConfig;
                this.applyProfiles(profileData as { profiles: ProfileSummary[] });
                this.dataLoading = false;
            });
        },
        showValidationError(message: string) {
            this.alert = { show: true, type: 'danger', code: 0, message };
        },
        saveConfig() {
            if (this.hemsConfig.storage.enabled) {
                if (!this.hemsConfig.storage.profile) {
                    this.showValidationError(this.$t('storage.ProfileRequired'));
                    return;
                }
                if (this.storageProfileUnavailable) {
                    this.showValidationError(this.$t('storage.ProfileUnavailable'));
                    return;
                }
            } else {
                this.hemsConfig.storage.profile = '';
            }

            const formData = new FormData();
            formData.append('data', JSON.stringify(this.hemsConfig));
            fetch('/api/hems/config', { method: 'POST', headers: authHeader(), body: formData })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: AlertResponse) => {
                    this.alert = data;
                    this.alert.message = data.type === 'success' ? this.$t('apiresponse.' + data.code) : data.message;
                    this.alert.show = true;
                    this.getConfig();
                });
        },
    },
});
</script>
