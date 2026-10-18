<template>
    <CardElement :text="$t('firmwareinfo.FirmwareInformation')" textVariant="text-bg-primary" table>
        <div class="table-responsive">
            <table class="table table-hover table-condensed">
                <tbody>
                    <tr>
                        <th>{{ $t('firmwareinfo.Hostname') }}</th>
                        <td>{{ systemStatus.hostname }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.SdkVersion') }}</th>
                        <td>{{ systemStatus.sdkversion }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.ConfigVersion') }}</th>
                        <td>{{ systemStatus.config_version }}-OnBattery-{{ systemStatus.config_version_onbattery }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.FirmwareVersion') }}</th>
                        <td>
                            <a
                                :href="versionInfoUrl"
                                target="_blank"
                                v-tooltip
                                :title="$t('firmwareinfo.FirmwareVersionHint')"
                            >
                                {{ systemStatus.git_hash }}
                            </a>
                        </td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.FirmwareBranch') }}</th>
                        <td>{{ systemStatus.git_branch }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareupgrade.SourceCommit') }}</th>
                        <td>
                            <code :title="systemStatus.source_commit">{{
                                systemStatus.source_commit?.slice(0, 12) || '—'
                            }}</code>
                        </td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareupgrade.TestedMergeCommit') }}</th>
                        <td>
                            <code :title="systemStatus.tested_merge_commit || systemStatus.git_sha">{{
                                (systemStatus.tested_merge_commit || systemStatus.git_sha)?.slice(0, 12) || '—'
                            }}</code>
                        </td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareupgrade.BuildCommit') }}</th>
                        <td>
                            <code :title="systemStatus.git_sha">{{ systemStatus.git_sha?.slice(0, 12) || '—' }}</code>
                        </td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.PioEnv') }}</th>
                        <td>{{ systemStatus.pioenv }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.FirmwareUpdate') }}</th>
                        <td>
                            <div class="form-check form-check-inline form-switch">
                                <input
                                    v-model="modelAllowVersionInfo"
                                    class="form-check-input"
                                    type="checkbox"
                                    role="switch"
                                    v-tooltip
                                    :title="$t('firmwareinfo.FrmwareUpdateAllow')"
                                />
                                <label class="form-check-label">
                                    <a
                                        v-if="modelAllowVersionInfo && systemStatus.update_url"
                                        :href="systemStatus.update_url"
                                        target="_blank"
                                        v-tooltip
                                        :title="$t('firmwareinfo.FirmwareUpdateHint')"
                                    >
                                        <span class="badge" :class="systemStatus.update_status">
                                            {{ systemStatus.update_text }}
                                        </span>
                                    </a>
                                    <span
                                        v-else-if="modelAllowVersionInfo"
                                        class="badge"
                                        :class="systemStatus.update_status"
                                    >
                                        {{ systemStatus.update_text }}
                                    </span>
                                </label>
                            </div>
                        </td>
                    </tr>
                    <tr v-if="systemStatus.reset_diagnostics">
                        <th>ESP-IDF reset reason</th>
                        <td>
                            {{ systemStatus.reset_diagnostics.esp_reset_reason }} ({{
                                systemStatus.reset_diagnostics.esp_reset_reason_code
                            }})
                        </td>
                    </tr>
                    <tr v-if="systemStatus.reset_diagnostics">
                        <th>Raw RTC reset reasons</th>
                        <td>
                            CPU 0: {{ systemStatus.resetreason_0_raw }} · CPU 1:
                            {{ systemStatus.resetreason_1_raw ?? 'Unavailable' }}
                        </td>
                    </tr>
                    <tr v-if="systemStatus.reset_diagnostics">
                        <th>Previous boot</th>
                        <td>
                            <template v-if="systemStatus.reset_diagnostics.previous_boot.valid">
                                {{
                                    systemStatus.reset_diagnostics.previous_boot.active
                                        ? 'Active operation at last checkpoint'
                                        : 'Last operation completed'
                                }}: {{ systemStatus.reset_diagnostics.previous_boot.component }} /
                                {{ systemStatus.reset_diagnostics.previous_boot.operation }} /
                                {{ systemStatus.reset_diagnostics.previous_boot.phase }} · index
                                {{ systemStatus.reset_diagnostics.previous_boot.sub_index }} ·
                                {{ systemStatus.reset_diagnostics.previous_boot.uptime_ms }} ms
                                <br />
                                Intentional restart marker:
                                {{
                                    systemStatus.reset_diagnostics.previous_boot.intentional_restart ? 'Set' : 'Not set'
                                }}
                                · {{ systemStatus.reset_diagnostics.previous_boot.restart_source }}
                            </template>
                            <span v-else>No valid retained checkpoint</span>
                            <details>
                                <summary>Reset diagnostic record</summary>
                                <pre>{{ JSON.stringify(systemStatus.reset_diagnostics, null, 2) }}</pre>
                            </details>
                        </td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.ResetReason0') }}</th>
                        <td>{{ systemStatus.resetreason_0 }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.ResetReason1') }}</th>
                        <td>{{ systemStatus.resetreason_1 }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.ConfigSaveCount') }}</th>
                        <td>{{ $n(systemStatus.cfgsavecount, 'decimal') }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.RuntimeSaveCount') }}</th>
                        <td>{{ systemStatus.runtime_savecount }}</td>
                    </tr>
                    <tr>
                        <th>{{ $t('firmwareinfo.Uptime') }}</th>
                        <td>
                            {{ $t('firmwareinfo.UptimeValue', timeInHours(systemStatus.uptime)) }}
                        </td>
                    </tr>
                </tbody>
            </table>
        </div>
    </CardElement>
</template>

<script lang="ts">
import CardElement from '@/components/CardElement.vue';
import type { SystemStatus } from '@/types/SystemStatus';
import { timestampToString } from '@/utils';
import { defineComponent, type PropType } from 'vue';

export default defineComponent({
    components: {
        CardElement,
    },
    props: {
        systemStatus: { type: Object as PropType<SystemStatus>, required: true },
        allowVersionInfo: Boolean,
    },
    computed: {
        modelAllowVersionInfo: {
            get(): boolean {
                return !!this.allowVersionInfo;
            },
            set(value: boolean) {
                this.$emit('update:allowVersionInfo', value);
            },
        },
        timeInHours() {
            return (value: number) => {
                const [count, time] = timestampToString(this.$i18n.locale, value, true);
                return { count, time };
            };
        },
        versionInfoUrl(): string {
            if (this.systemStatus.git_is_hash) {
                return 'https://github.com/dawid-woitaschek/OpenDTU-HEMS/commits/' + this.systemStatus.git_hash;
            }
            return 'https://github.com/dawid-woitaschek/OpenDTU-HEMS/releases/tag/' + this.systemStatus.git_hash;
        },
    },
});
</script>
