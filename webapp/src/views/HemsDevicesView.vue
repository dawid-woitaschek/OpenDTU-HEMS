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
        <template v-if="!dataLoading && !loadFailed && hemsConfig.version === 1">
            <form @submit.prevent="saveWarp">
                <CardElement :text="$t('hems.Warp')" textVariant="text-bg-primary">
                    <InputElement
                        :label="$t('hems.WarpEnabled')"
                        v-model="hemsConfig.warp.enabled"
                        type="checkbox"
                        wide
                    />
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
                        <div class="row mb-3">
                            <label for="hems-evse-profile" class="col-sm-4 col-form-label">{{
                                $t('storage.Profile')
                            }}</label>
                            <div class="col-sm-8">
                                <select id="hems-evse-profile" class="form-select" v-model="evseBinding.profile">
                                    <option value="" disabled>
                                        {{
                                            evseProfiles.length
                                                ? $t('hems.EvseSelectProfile')
                                                : $t('hems.EvseNoProfiles')
                                        }}
                                    </option>
                                    <option v-if="evseProfileUnavailable" :value="evseBinding.profile" disabled>
                                        ⚠ {{ evseBinding.profile }}
                                    </option>
                                    <option v-for="profile in evseProfiles" :key="profile.id" :value="profile.id">
                                        {{ profile.id }} — {{ profile.vendor }} / {{ profile.model }}
                                    </option>
                                </select>
                                <div v-if="evseProfileUnavailable" class="alert alert-warning mt-2 mb-0" role="alert">
                                    {{ $t('hems.EvseProfileUnavailable') }}
                                </div>
                                <div
                                    v-else-if="!evseProfiles.length"
                                    class="alert alert-warning mt-2 mb-0"
                                    role="alert"
                                >
                                    {{ $t('hems.EvseNoProfilesHint') }}
                                </div>
                            </div>
                        </div>
                        <div class="row mb-3">
                            <label for="hems-evse-control-mode" class="col-sm-4 col-form-label">{{
                                $t('hems.ControlMode')
                            }}</label>
                            <div class="col-sm-8">
                                <select
                                    id="hems-evse-control-mode"
                                    class="form-select"
                                    v-model="evseBinding.control_mode"
                                >
                                    <option value="monitor">{{ $t('hems.ControlMonitorOnly') }}</option>
                                    <option value="hems">{{ $t('hems.ControlHems') }}</option>
                                </select>
                                <div class="form-text">{{ $t('hems.ControlModeHint') }}</div>
                            </div>
                        </div>
                        <div class="alert alert-secondary" role="alert">{{ $t('hems.WarpSetupHint') }}</div>
                    </template>
                    <FormFooter @reload="getConfig" />
                </CardElement>
            </form>

            <form @submit.prevent="saveStorage">
                <CardElement :text="$t('storage.Title')" textVariant="text-bg-primary" add-space>
                    <InputElement
                        :label="$t('storage.Enabled')"
                        v-model="hemsConfig.storage.enabled"
                        type="checkbox"
                        wide
                    />
                    <template v-if="hemsConfig.storage.enabled">
                        <InputElement
                            :label="$t('hems.Host')"
                            v-model="hemsConfig.storage.host"
                            type="text"
                            maxlength="253"
                            wide
                        />
                        <div class="row mb-3">
                            <label for="hems-storage-profile" class="col-sm-4 col-form-label">{{
                                $t('storage.Profile')
                            }}</label>
                            <div class="col-sm-8">
                                <select
                                    id="hems-storage-profile"
                                    class="form-select"
                                    v-model="hemsConfig.storage.profile"
                                >
                                    <option value="" disabled>
                                        {{
                                            storageProfiles.length
                                                ? $t('storage.SelectProfile')
                                                : $t('storage.NoProfiles')
                                        }}
                                    </option>
                                    <option
                                        v-if="storageProfileUnavailable"
                                        :value="hemsConfig.storage.profile"
                                        disabled
                                    >
                                        ⚠ {{ $t('profiles.MissingStorageProfile', { id: hemsConfig.storage.profile }) }}
                                    </option>
                                    <option v-for="profile in storageProfiles" :key="profile.id" :value="profile.id">
                                        {{ profile.id }} — {{ profile.vendor }} / {{ profile.model }}
                                    </option>
                                </select>
                                <div
                                    v-if="storageProfileUnavailable"
                                    class="alert alert-warning mt-2 mb-0"
                                    role="alert"
                                >
                                    {{ $t('profiles.MissingStorageProfile', { id: hemsConfig.storage.profile }) }}
                                </div>
                                <div
                                    v-else-if="!storageProfiles.length"
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
                        <div class="row mb-3">
                            <label for="hems-storage-control-mode" class="col-sm-4 col-form-label">{{
                                $t('hems.ControlMode')
                            }}</label>
                            <div class="col-sm-8">
                                <select
                                    id="hems-storage-control-mode"
                                    class="form-select"
                                    v-model="hemsConfig.storage.control_mode"
                                >
                                    <option value="monitor">{{ $t('hems.ControlMonitorOnly') }}</option>
                                    <option value="hems">{{ $t('hems.ControlHems') }}</option>
                                </select>
                                <div class="form-text">{{ $t('hems.ControlModeHint') }}</div>
                            </div>
                        </div>
                        <div class="alert alert-secondary" role="alert">{{ $t('storage.SetupHint') }}</div>
                    </template>
                    <FormFooter @reload="getConfig" />
                </CardElement>
            </form>
        </template>
        <div v-else-if="!dataLoading && !loadFailed" class="alert alert-warning" role="alert">
            {{ $t('hems.UnsupportedVersion') }}
        </div>
        <div v-if="!dataLoading && !loadFailed" class="mt-5">
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
import type { HemsConfig, HemsDeviceControlMode } from '@/types/HemsConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

type ProfileSummary = {
    id: string;
    vendor: string;
    model: string;
    capabilities: string[];
};

type EvseBinding = {
    profile: string;
    control_mode: HemsDeviceControlMode;
};

export default defineComponent({
    components: { BasePage, BootstrapAlert, CardElement, FormFooter, HemsProfilePanel, InputElement },
    data() {
        return {
            dataLoading: true,
            loadFailed: false,
            hemsConfig: {} as HemsConfig,
            evseBinding: { profile: '', control_mode: 'monitor' } as EvseBinding,
            storageProfiles: [] as ProfileSummary[],
            evseProfiles: [] as ProfileSummary[],
            alert: {} as AlertResponse,
        };
    },
    computed: {
        storageProfileUnavailable(): boolean {
            const profile = this.hemsConfig.storage?.profile;
            return !!profile && !this.storageProfiles.some((candidate) => candidate.id === profile);
        },
        evseProfileUnavailable(): boolean {
            const profile = this.evseBinding.profile;
            return !!profile && !this.evseProfiles.some((candidate) => candidate.id === profile);
        },
    },
    created() {
        this.getConfig();
    },
    methods: {
        showError(message: string) {
            this.alert = { show: true, type: 'danger', code: 0, message };
        },
        validHost(host: string): boolean {
            if (!host || host.length > 253 || host.trim() !== host) return false;

            if (/^[0-9.]+$/.test(host) && host.includes('.')) {
                const parts = host.split('.');
                return (
                    parts.length === 4 &&
                    parts.every(
                        (part) =>
                            /^\d{1,3}$/.test(part) && Number.parseInt(part, 10) >= 0 && Number.parseInt(part, 10) <= 255
                    )
                );
            }

            return host
                .split('.')
                .every(
                    (label) =>
                        label.length >= 1 &&
                        label.length <= 63 &&
                        /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(label)
                );
        },
        validInteger(value: unknown, minimum: number, maximum: number): boolean {
            const number = typeof value === 'number' ? value : Number(value);
            return Number.isInteger(number) && number >= minimum && number <= maximum;
        },
        applyProfiles(profileData: { profiles: ProfileSummary[] }) {
            this.storageProfiles = profileData.profiles.filter((profile) => profile.capabilities?.includes('storage'));
            this.evseProfiles = profileData.profiles.filter((profile) => profile.capabilities?.includes('evse'));
        },
        refreshProfiles() {
            fetch('/api/hems/profiles', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((profileData) => this.applyProfiles(profileData))
                .catch(() => this.showError(this.$t('hems.ConfigLoadFailed')));
        },
        getConfig() {
            this.dataLoading = true;
            this.loadFailed = false;
            Promise.all([
                fetch('/api/hems/config', { headers: authHeader() }).then((response) =>
                    handleResponse(response, this.$emitter, this.$router)
                ),
                fetch('/api/hems/profiles', { headers: authHeader() }).then((response) =>
                    handleResponse(response, this.$emitter, this.$router)
                ),
                fetch('/api/hems/profiles/evse-binding', { headers: authHeader() }).then((response) =>
                    handleResponse(response, this.$emitter, this.$router)
                ),
            ])
                .then(([config, profileData, evseBinding]) => {
                    const typedConfig = config as HemsConfig;
                    if (!typedConfig.storage.control_mode) typedConfig.storage.control_mode = 'monitor';
                    this.hemsConfig = typedConfig;
                    this.applyProfiles(profileData as { profiles: ProfileSummary[] });
                    const typedBinding = evseBinding as EvseBinding;
                    if (!typedBinding.control_mode) typedBinding.control_mode = 'monitor';
                    this.evseBinding = typedBinding;
                    this.dataLoading = false;
                })
                .catch(() => {
                    this.loadFailed = true;
                    this.dataLoading = false;
                    this.showError(this.$t('hems.ConfigLoadFailed'));
                });
        },
        async postConfig(payload: object): Promise<boolean> {
            try {
                const formData = new FormData();
                formData.append('data', JSON.stringify(payload));
                const data = (await fetch('/api/hems/config', {
                    method: 'POST',
                    headers: authHeader(),
                    body: formData,
                }).then((response) => handleResponse(response, this.$emitter, this.$router))) as AlertResponse;
                this.alert = data;
                this.alert.message = data.type === 'success' ? this.$t('apiresponse.' + data.code) : data.message;
                this.alert.show = true;
                return data.type === 'success';
            } catch {
                this.showError(this.$t('hems.ConfigSaveFailed'));
                return false;
            }
        },
        async postEvseBinding(): Promise<boolean> {
            try {
                const formData = new FormData();
                formData.append(
                    'data',
                    JSON.stringify({ profile: this.evseBinding.profile, control_mode: this.evseBinding.control_mode })
                );
                await fetch('/api/hems/profiles/evse-binding', {
                    method: 'POST',
                    headers: authHeader(),
                    body: formData,
                }).then((response) => handleResponse(response, this.$emitter, this.$router));
                return true;
            } catch (error) {
                this.showError(error instanceof Error ? error.message : this.$t('hems.ConfigSaveFailed'));
                return false;
            }
        },
        async saveWarp() {
            if (this.hemsConfig.warp.enabled) {
                if (!this.validHost(this.hemsConfig.warp.host)) {
                    this.showError(this.$t('hems.InvalidWallboxHost'));
                    return;
                }
                if (!this.validInteger(this.hemsConfig.warp.port, 1, 65535)) {
                    this.showError(this.$t('hems.InvalidWallboxPort'));
                    return;
                }
                if (!this.evseBinding.profile) {
                    this.showError(this.$t('hems.EvseProfileRequired'));
                    return;
                }
                if (this.evseProfileUnavailable) {
                    this.showError(this.$t('hems.EvseProfileUnavailable'));
                    return;
                }
            } else {
                // A disabled EVSE owns no profile, exactly like disabled storage.
                this.evseBinding.profile = '';
                this.evseBinding.control_mode = 'monitor';
            }

            const warp = this.hemsConfig.warp.enabled ? this.hemsConfig.warp : { enabled: false, host: '', port: 502 };
            if (!(await this.postConfig({ version: this.hemsConfig.version, warp }))) return;
            if (!(await this.postEvseBinding())) return;
            this.getConfig();
        },
        async saveStorage() {
            if (this.hemsConfig.storage.enabled) {
                if (!this.validHost(this.hemsConfig.storage.host)) {
                    this.showError(this.$t('storage.InvalidHost'));
                    return;
                }
                if (!this.hemsConfig.storage.profile) {
                    this.showError(this.$t('storage.ProfileRequired'));
                    return;
                }
                if (this.storageProfileUnavailable) {
                    this.showError(this.$t('storage.ProfileUnavailable'));
                    return;
                }
                if (!this.validInteger(this.hemsConfig.storage.port, 0, 65535)) {
                    this.showError(this.$t('storage.InvalidPort'));
                    return;
                }
                if (!this.validInteger(this.hemsConfig.storage.unit, 0, 247)) {
                    this.showError(this.$t('storage.InvalidUnit'));
                    return;
                }
            } else {
                this.hemsConfig.storage.profile = '';
                this.hemsConfig.storage.control_mode = 'monitor';
            }

            if (
                await this.postConfig({
                    version: this.hemsConfig.version,
                    storage: this.hemsConfig.storage,
                })
            ) {
                this.getConfig();
            }
        },
    },
});
</script>
