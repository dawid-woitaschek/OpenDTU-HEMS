<template>
    <BasePage :title="$t('systeminfo.SystemInfo')" :isLoading="dataLoading" :show-reload="true" @reload="getSystemInfo">
        <FirmwareInfo :systemStatus="systemDataList" v-model:allowVersionInfo="allowVersionInfo" />
        <div class="mt-5"></div>
        <HardwareInfo :systemStatus="systemDataList" />
        <div class="mt-5"></div>
        <MemoryInfo :systemStatus="systemDataList" />
        <div class="mt-5"></div>
        <MemoryDetails
            :title="$t('memorydetails.HeapDetails')"
            :total="systemDataList.heap_total"
            :used="systemDataList.heap_used"
            :min-free="systemDataList.heap_min_free"
            :max-block="systemDataList.heap_max_block"
        />
        <div class="mt-5"></div>
        <template v-if="systemDataList.psram_total > 0">
            <MemoryDetails
                :title="$t('memorydetails.PsramDetails')"
                :total="systemDataList.psram_total"
                :used="systemDataList.psram_used"
                :min-free="systemDataList.psram_min_free"
                :max-block="systemDataList.psram_max_block"
            />
            <div class="mt-5"></div>
        </template>
        <TaskDetails :taskDetails="systemDataList.task_details" />
        <div class="mt-5"></div>
        <RadioInfo :systemStatus="systemDataList" />
        <div class="mt-5"></div>
        <UartAllocations :allocations="systemDataList.uarts" />
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import FirmwareInfo from '@/components/FirmwareInfo.vue';
import HardwareInfo from '@/components/HardwareInfo.vue';
import MemoryInfo from '@/components/MemoryInfo.vue';
import MemoryDetails from '@/components/MemoryDetails.vue';
import TaskDetails from '@/components/TaskDetails.vue';
import RadioInfo from '@/components/RadioInfo.vue';
import UartAllocations from '@/components/UartAllocations.vue';
import type { SystemStatus } from '@/types/SystemStatus';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';
import {
    installedSourceCommit,
    sourceComparisonBase,
    sourceHasAdvanced,
    validManifest,
} from '@/utils/firmwareProvenance';
import type { FirmwareManifest } from '@/utils/firmwareProvenance';

export default defineComponent({
    components: {
        BasePage,
        FirmwareInfo,
        HardwareInfo,
        MemoryInfo,
        MemoryDetails,
        TaskDetails,
        RadioInfo,
        UartAllocations,
    },
    data() {
        return {
            dataLoading: true,
            systemDataList: {} as SystemStatus,
            allowVersionInfo: false,
            updateCheck: 0,
        };
    },
    created() {
        this.allowVersionInfo = (localStorage.getItem('allowVersionInfo') || '0') == '1';
        this.getSystemInfo();
    },
    methods: {
        getSystemInfo() {
            this.dataLoading = true;
            fetch('/api/system/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.systemDataList = data;
                    this.dataLoading = false;
                    this.getUpdateInfo();
                });
        },
        async getUpdateInfo() {
            const check = ++this.updateCheck;
            if (this.systemDataList.git_hash === undefined) {
                return;
            }

            // If the left char is a "g" the value is the git hash (remove the "g")
            this.systemDataList.git_is_hash = this.systemDataList.git_hash?.substring(0, 1) == 'g';
            this.systemDataList.git_hash = this.systemDataList.git_is_hash
                ? this.systemDataList.git_hash?.substring(1)
                : this.systemDataList.git_hash;

            // Handle format "v0.1-5-gabcdefh"
            if (this.systemDataList.git_hash?.lastIndexOf('-') >= 0) {
                this.systemDataList.git_hash = this.systemDataList.git_hash.substring(
                    this.systemDataList.git_hash.lastIndexOf('-') + 2
                );
                this.systemDataList.git_is_hash = true;
            }

            if (!this.allowVersionInfo) {
                return;
            }

            const installed = this.systemDataList;
            installed.update_url = '';
            try {
                // Older system/status responses omitted the full SHA; firmware/status already exposed it.
                if (!installed.git_sha) {
                    const response = await fetch('/api/firmware/status', { headers: authHeader() });
                    const firmware = await handleResponse(response, this.$emitter, this.$router);
                    Object.assign(installed, firmware);
                }
                let manifest: FirmwareManifest | undefined;
                if (!installed.source_commit && installed.update_repository && installed.update_channel) {
                    try {
                        const response = await fetch(
                            `https://raw.githubusercontent.com/${installed.update_repository}/firmware-channel-${installed.update_channel}/manifest.json`,
                            { cache: 'no-store' }
                        );
                        const value: unknown = response.ok ? await response.json() : null;
                        if (validManifest(value, installed.update_repository, installed.update_channel))
                            manifest = value;
                    } catch {
                        /* Legacy build comparison remains available if the channel is unreachable. */
                    }
                }
                const base = sourceComparisonBase(installed, manifest);
                if (!base) throw new Error(this.$t('systeminfo.VersionError'));
                // Adopt recovered source metadata only when the exact installed image matched its own channel.
                installed.source_commit = installedSourceCommit(installed, manifest);
                const repository = installed.update_repository || 'dawid-woitaschek/OpenDTU-HEMS';
                const response = await fetch(
                    `https://api.github.com/repos/${repository}/compare/${encodeURIComponent(base)}...${encodeURIComponent(installed.git_branch)}`,
                    { headers: { Accept: 'application/vnd.github+json', 'X-GitHub-Api-Version': '2026-03-10' } }
                );
                if (!response.ok) throw new Error(this.$t('systeminfo.VersionError'));
                const data = await response.json();
                if (check !== this.updateCheck || installed !== this.systemDataList || !this.allowVersionInfo) return;
                const advanced = sourceHasAdvanced(data);
                installed.update_text = this.$t(advanced ? 'systeminfo.VersionNew' : 'systeminfo.VersionOk');
                installed.update_status = advanced ? 'text-bg-warning' : 'text-bg-success';
                installed.update_url = advanced ? data.html_url : '';
            } catch (error) {
                if (check !== this.updateCheck || installed !== this.systemDataList || !this.allowVersionInfo) return;
                installed.update_text = error instanceof Error ? error.message : this.$t('systeminfo.VersionError');
                installed.update_status = 'text-bg-secondary';
            }
        },
    },
    watch: {
        allowVersionInfo(allow: boolean) {
            localStorage.setItem('allowVersionInfo', allow ? '1' : '0');
            if (allow) {
                this.getUpdateInfo();
            }
        },
    },
});
</script>
