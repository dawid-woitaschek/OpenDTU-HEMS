<template>
    <BasePage :title="$t('firmwareupgrade.FirmwareUpgrade')">
        <div class="position-relative" v-if="loading">
            <div class="position-absolute top-50 start-50 translate-middle">
                <div class="spinner-border" role="status">
                    <span class="visually-hidden">{{ $t('firmwareupgrade.Loading') }}</span>
                </div>
            </div>
        </div>

        <CardElement
            :text="$t('firmwareupgrade.OtaError')"
            textVariant="text-bg-danger"
            center-content
            v-if="!loading && !uploading && !remoteUpdating && OTAError != ''"
        >
            <p class="h1 mb-2">
                <BIconExclamationCircleFill />
            </p>
            {{ OTAError }}
            <div class="mt-3 d-flex gap-3 justify-content-center">
                <button class="btn btn-light" @click="clear">
                    <BIconArrowLeft /> {{ $t('firmwareupgrade.Back') }}
                </button>
                <button class="btn btn-primary" @click="retryOTA" :disabled="file === null">
                    <BIconArrowRepeat /> {{ $t('firmwareupgrade.Retry') }}
                </button>
            </div>
        </CardElement>

        <CardElement
            :text="$t('firmwareupgrade.OtaStatus')"
            textVariant="text-bg-success"
            center-content
            v-else-if="!loading && !uploading && !remoteUpdating && OTASuccess"
        >
            <BIconCheckCircle class="fs-1" />&nbsp;{{ $t('firmwareupgrade.OtaSuccess') }}
        </CardElement>

        <CardElement
            :text="$t('firmwareupgrade.OtaError')"
            textVariant="text-bg-warning"
            v-else-if="!loading && firmwareStatus.ota_supported === false"
        >
            {{ $t('firmwareupgrade.NoOtaSupport') }}
        </CardElement>

        <template v-else-if="!loading">
            <CardElement
                v-if="!uploading"
                :text="$t('firmwareupgrade.RepositoryUpdate')"
                textVariant="text-bg-primary"
            >
                <div class="text-start">
                    <h6>{{ $t('firmwareupgrade.InstalledFirmware') }}</h6>
                    <dl class="row mb-0">
                        <dt class="col-sm-4">{{ $t('firmwareupgrade.Branch') }}</dt>
                        <dd class="col-sm-8"><code>{{ firmwareStatus.git_branch }}</code></dd>
                        <dt class="col-sm-4">{{ $t('firmwareupgrade.Commit') }}</dt>
                        <dd class="col-sm-8"><code>{{ shortSha(firmwareStatus.git_sha) }}</code></dd>
                        <dt class="col-sm-4">{{ $t('firmwareupgrade.BuildTarget') }}</dt>
                        <dd class="col-sm-8"><code>{{ firmwareStatus.pioenv }}</code></dd>
                        <dt class="col-sm-4">{{ $t('firmwareupgrade.UpdateChannel') }}</dt>
                        <dd class="col-sm-8">
                            <code>{{ firmwareStatus.update_channel || '—' }}</code>
                        </dd>
                    </dl>

                    <hr />

                    <div v-if="!firmwareStatus.update_channel" class="alert alert-warning mb-0" role="alert">
                        {{ $t('firmwareupgrade.NoUpdateChannel') }}
                    </div>

                    <div v-else-if="manifestLoading" class="text-center py-3">
                        <div class="spinner-border spinner-border-sm" role="status"></div>
                        <span class="ms-2">{{ $t('firmwareupgrade.Loading') }}</span>
                    </div>

                    <div v-else-if="manifestError" class="alert alert-warning mb-0" role="alert">
                        <div>{{ manifestError }}</div>
                        <button class="btn btn-sm btn-outline-dark mt-2" @click="checkRemoteUpdate">
                            <BIconArrowRepeat /> {{ $t('firmwareupgrade.RetryCheck') }}
                        </button>
                    </div>

                    <template v-else-if="updateManifest && remoteBuild">
                        <h6>{{ $t('firmwareupgrade.AvailableFirmware') }}</h6>
                        <dl class="row mb-3">
                            <dt class="col-sm-4">{{ $t('firmwareupgrade.Branch') }}</dt>
                            <dd class="col-sm-8"><code>{{ updateManifest.branch }}</code></dd>
                            <dt class="col-sm-4">{{ $t('firmwareupgrade.Commit') }}</dt>
                            <dd class="col-sm-8"><code>{{ shortSha(updateManifest.commit) }}</code></dd>
                            <dt class="col-sm-4">{{ $t('firmwareupgrade.BuildTarget') }}</dt>
                            <dd class="col-sm-8">
                                <code>{{ firmwareStatus.pioenv }}</code>
                                <span class="text-muted ms-2">({{ formatSize(remoteBuild.size) }})</span>
                            </dd>
                            <dt class="col-sm-4">{{ $t('firmwareupgrade.Published') }}</dt>
                            <dd class="col-sm-8">{{ formatDate(updateManifest.published_at) }}</dd>
                        </dl>

                        <div v-if="upToDate" class="alert alert-success mb-0" role="alert">
                            {{ $t('firmwareupgrade.FirmwareCurrent') }}
                        </div>
                        <div v-else class="alert alert-info mb-3" role="alert">
                            {{ $t('firmwareupgrade.FirmwareAvailable') }}
                        </div>

                        <div v-if="!upToDate" class="text-center">
                            <button class="btn btn-primary" @click="installRemote" :disabled="remoteUpdating">
                                <span
                                    v-if="remoteUpdating"
                                    class="spinner-border spinner-border-sm me-2"
                                    role="status"
                                ></span>
                                {{ $t('firmwareupgrade.InstallLatest') }}
                            </button>
                            <div v-if="remoteUpdating && remoteStatus" class="small text-muted mt-2">
                                {{ remoteStatus }}
                            </div>
                        </div>
                    </template>
                </div>
            </CardElement>

            <CardElement
                v-else
                :text="$t('firmwareupgrade.UploadProgress')"
                textVariant="text-bg-primary"
                center-content
            >
                <div class="progress">
                    <div
                        class="progress-bar"
                        role="progressbar"
                        :style="{ width: progress + '%' }"
                        v-bind:aria-valuenow="progress"
                        aria-valuemin="0"
                        aria-valuemax="100"
                    >
                        {{ progress }}%
                    </div>
                </div>
            </CardElement>

            <details v-if="!uploading && !remoteUpdating" class="mt-4">
                <summary class="btn btn-outline-secondary">
                    {{ $t('firmwareupgrade.ManualInstall') }}
                </summary>
                <div class="mt-3">
                    <CardElement
                        :text="$t('firmwareupgrade.FirmwareUpload')"
                        textVariant="text-bg-secondary"
                        center-content
                    >
                        <p class="text-muted text-start">{{ $t('firmwareupgrade.ManualInstallHint') }}</p>
                        <div class="form-group">
                            <input
                                class="form-control"
                                type="file"
                                ref="file"
                                accept=".bin,.bin.gz"
                                @change="selectFirmware"
                            />
                        </div>
                        <div class="mt-3" v-if="file !== null">
                            <div class="mb-2">{{ file.name }}</div>
                            <button class="btn btn-primary" @click="uploadOTA">
                                {{ $t('firmwareupgrade.FirmwareUpload') }}
                            </button>
                        </div>
                    </CardElement>
                </div>
            </details>
        </template>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import type { FirmwareStatus } from '@/types/FirmwareStatus';
import CardElement from '@/components/CardElement.vue';
import { authHeader, isLoggedIn, handleResponse } from '@/utils/authentication';
import { BIconArrowLeft, BIconArrowRepeat, BIconCheckCircle, BIconExclamationCircleFill } from 'bootstrap-icons-vue';
import SparkMD5 from 'spark-md5';
import { defineComponent } from 'vue';
import { waitRestart } from '@/utils/waitRestart';

type FirmwareBuild = {
    file: string;
    size: number;
    md5: string;
    sha256: string;
};

type FirmwareManifest = {
    schema: string;
    repository: string;
    channel: string;
    branch: string;
    commit: string;
    published_at: string;
    builds: Record<string, FirmwareBuild>;
};

const MANIFEST_SCHEMA = 'opendtu-hems-update/v1';

export default defineComponent({
    components: {
        BasePage,
        CardElement,
        BIconArrowLeft,
        BIconArrowRepeat,
        BIconCheckCircle,
        BIconExclamationCircleFill,
    },
    data() {
        return {
            loading: true,
            uploading: false,
            progress: 0,
            OTAError: '',
            OTASuccess: false,
            file: null as File | null,
            firmwareStatus: {} as FirmwareStatus,
            selectedChannel: '',
            updateManifest: null as FirmwareManifest | null,
            remoteBuild: null as FirmwareBuild | null,
            manifestLoading: false,
            manifestError: '',
            remoteUpdating: false,
            remoteStatus: '',
        };
    },
    computed: {
        upToDate(): boolean {
            if (!this.updateManifest || !this.firmwareStatus.git_sha) return false;
            return (
                this.updateManifest.commit.toLowerCase() === this.firmwareStatus.git_sha.toLowerCase() &&
                this.updateManifest.branch === this.firmwareStatus.git_branch &&
                this.selectedChannel === this.firmwareStatus.update_channel
            );
        },
    },
    methods: {
        shortSha(value: string): string {
            if (!value) return '—';
            return value.length > 12 ? value.slice(0, 12) : value;
        },
        formatSize(bytes: number): string {
            return `${(bytes / 1024 / 1024).toFixed(2)} MiB`;
        },
        formatDate(value: string): string {
            const date = new Date(value);
            return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
        },
        fileMD5(blob: Blob): Promise<string> {
            return new Promise((resolve, reject) => {
                const chunkSize = 2097152;
                const chunks = Math.ceil(blob.size / chunkSize);
                const spark = new SparkMD5.ArrayBuffer();
                const fileReader = new FileReader();
                let currentChunk = 0;

                fileReader.onload = () => {
                    spark.append(fileReader.result as ArrayBuffer);
                    currentChunk += 1;
                    if (currentChunk < chunks) {
                        loadNext();
                    } else {
                        resolve(spark.end());
                    }
                };
                fileReader.onerror = (event) => reject(event);

                const loadNext = () => {
                    const start = currentChunk * chunkSize;
                    const end = Math.min(start + chunkSize, blob.size);
                    fileReader.readAsArrayBuffer(blob.slice(start, end));
                };
                loadNext();
            });
        },
        async fileSHA256(blob: Blob): Promise<string | null> {
            if (!window.isSecureContext || !window.crypto?.subtle) return null;
            const digest = await window.crypto.subtle.digest('SHA-256', await blob.arrayBuffer());
            return Array.from(new Uint8Array(digest))
                .map((byte) => byte.toString(16).padStart(2, '0'))
                .join('');
        },
        isFactoryFirmwareFile(filename: string): boolean {
            const lowerFilename = filename.toLowerCase();
            return lowerFilename.endsWith('.factory.bin') || lowerFilename.endsWith('.factory.bin.gz');
        },
        isValidFirmwareFile(filename: string): boolean {
            const validExtensions = ['.bin', '.bin.gz'];
            const lowerFilename = filename.toLowerCase();
            return !this.isFactoryFirmwareFile(filename) && validExtensions.some((ext) => lowerFilename.endsWith(ext));
        },
        setInvalidFileError() {
            this.OTAError = this.$t('firmwareupgrade.InvalidFileType', {
                validTypes: '.bin, .bin.gz (excluding .factory.bin / .factory.bin.gz)',
            }) as string;
        },
        selectFirmware(event: Event) {
            this.OTAError = '';
            this.OTASuccess = false;
            const target = event.target as HTMLInputElement;
            this.file = target.files !== null && target.files[0] ? target.files[0] : null;

            if (this.file !== null && !this.isValidFirmwareFile(this.file.name)) {
                this.setInvalidFileError();
            }
        },
        uploadFirmware(file: File, knownMd5?: string) {
            if (!this.isValidFirmwareFile(file.name)) {
                this.setInvalidFileError();
                return;
            }

            this.uploading = true;
            const formData = new FormData();
            const request = new XMLHttpRequest();
            request.addEventListener('load', () => {
                if (request.status === 200) {
                    this.OTASuccess = true;
                    waitRestart(this.$router);
                } else {
                    this.OTAError = request.responseText || `[HTTP ERROR] ${request.status} ${request.statusText}`;
                }
                this.uploading = false;
                this.progress = 0;
            });
            request.addEventListener('error', () => {
                this.OTAError = `[HTTP ERROR] ${request.status} ${request.statusText}`;
                this.uploading = false;
                this.progress = 0;
            });
            request.upload.addEventListener('progress', (event) => {
                if (event.lengthComputable) this.progress = Math.trunc((event.loaded / event.total) * 100);
            });
            request.withCredentials = true;

            const md5Promise = knownMd5 ? Promise.resolve(knownMd5) : this.fileMD5(file);
            md5Promise
                .then((md5) => {
                    formData.append('MD5', md5);
                    formData.append('firmware', file, file.name);
                    request.open('post', '/api/firmware/update');
                    authHeader().forEach((value, key) => request.setRequestHeader(key, value));
                    request.send(formData);
                })
                .catch(() => {
                    this.OTAError = 'Unknown error while preparing the firmware upload.';
                    this.uploading = false;
                    this.progress = 0;
                });
        },
        uploadOTA() {
            if (this.file === null) return;
            this.uploadFirmware(this.file);
        },
        retryOTA() {
            this.OTAError = '';
            this.OTASuccess = false;
            this.uploadOTA();
        },
        clear() {
            this.OTAError = '';
            this.OTASuccess = false;
            this.file = null;
            const input = this.$refs.file as HTMLInputElement | undefined;
            if (input !== undefined) input.value = '';
        },
        manifestUrl(): string {
            const branch = `firmware-channel-${this.selectedChannel}`;
            return `https://raw.githubusercontent.com/${this.firmwareStatus.update_repository}/${branch}/manifest.json`;
        },
        firmwareUrl(filename: string): string {
            const branch = `firmware-channel-${this.selectedChannel}`;
            return `https://raw.githubusercontent.com/${this.firmwareStatus.update_repository}/${branch}/${filename}`;
        },
        validManifest(value: unknown): value is FirmwareManifest {
            if (!value || typeof value !== 'object') return false;
            const manifest = value as Partial<FirmwareManifest>;
            return (
                manifest.schema === MANIFEST_SCHEMA &&
                manifest.repository === this.firmwareStatus.update_repository &&
                manifest.channel === this.selectedChannel &&
                typeof manifest.branch === 'string' &&
                typeof manifest.commit === 'string' &&
                /^[0-9a-f]{40}$/i.test(manifest.commit) &&
                typeof manifest.published_at === 'string' &&
                !!manifest.builds &&
                typeof manifest.builds === 'object'
            );
        },
        validBuild(value: FirmwareBuild | undefined): value is FirmwareBuild {
            if (!value) return false;
            const expectedName = `opendtu-hems-${this.firmwareStatus.pioenv}.bin`;
            return (
                value.file === expectedName &&
                Number.isInteger(value.size) &&
                value.size > 0 &&
                /^[0-9a-f]{32}$/i.test(value.md5) &&
                /^[0-9a-f]{64}$/i.test(value.sha256) &&
                !this.isFactoryFirmwareFile(value.file)
            );
        },
        async checkRemoteUpdate() {
            this.updateManifest = null;
            this.remoteBuild = null;
            this.manifestError = '';
            if (!this.selectedChannel) return;

            this.manifestLoading = true;
            try {
                const response = await fetch(this.manifestUrl(), { cache: 'no-store' });
                if (!response.ok) {
                    if (response.status === 404) throw new Error(this.$t('firmwareupgrade.ChannelNotPublished') as string);
                    throw new Error(this.$t('firmwareupgrade.UpdateCheckFailed') as string);
                }
                const manifest = (await response.json()) as unknown;
                if (!this.validManifest(manifest)) {
                    throw new Error(this.$t('firmwareupgrade.ManifestInvalid') as string);
                }
                const build = manifest.builds[this.firmwareStatus.pioenv];
                if (!this.validBuild(build)) {
                    throw new Error(this.$t('firmwareupgrade.BuildTargetMissing') as string);
                }
                this.updateManifest = manifest;
                this.remoteBuild = build;
            } catch (error) {
                this.manifestError =
                    error instanceof Error ? error.message : (this.$t('firmwareupgrade.UpdateCheckFailed') as string);
            } finally {
                this.manifestLoading = false;
            }
        },
        async installRemote() {
            if (!this.remoteBuild) return;

            this.OTAError = '';
            this.OTASuccess = false;
            this.remoteUpdating = true;
            try {
                this.remoteStatus = this.$t('firmwareupgrade.DownloadingFirmware') as string;
                const response = await fetch(this.firmwareUrl(this.remoteBuild.file), { cache: 'no-store' });
                if (!response.ok) throw new Error(this.$t('firmwareupgrade.RemoteDownloadFailed') as string);
                const blob = await response.blob();
                if (blob.size !== this.remoteBuild.size) {
                    throw new Error(this.$t('firmwareupgrade.IntegrityCheckFailed') as string);
                }

                this.remoteStatus = this.$t('firmwareupgrade.VerifyingFirmware') as string;
                const md5 = await this.fileMD5(blob);
                if (md5.toLowerCase() !== this.remoteBuild.md5.toLowerCase()) {
                    throw new Error(this.$t('firmwareupgrade.IntegrityCheckFailed') as string);
                }
                const sha256 = await this.fileSHA256(blob);
                if (sha256 !== null && sha256.toLowerCase() !== this.remoteBuild.sha256.toLowerCase()) {
                    throw new Error(this.$t('firmwareupgrade.IntegrityCheckFailed') as string);
                }

                const file = new File([blob], this.remoteBuild.file, { type: 'application/octet-stream' });
                this.remoteUpdating = false;
                this.remoteStatus = '';
                this.uploadFirmware(file, md5);
            } catch (error) {
                this.remoteUpdating = false;
                this.remoteStatus = '';
                this.OTAError =
                    error instanceof Error ? error.message : (this.$t('firmwareupgrade.RemoteDownloadFailed') as string);
            }
        },
        getFirmwareStatus() {
            fetch('/api/firmware/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data: FirmwareStatus) => {
                    this.firmwareStatus = data;
                    this.selectedChannel = data.update_channel || '';
                    this.loading = false;
                    if (data.ota_supported && this.selectedChannel) this.checkRemoteUpdate();
                })
                .catch(() => {
                    this.loading = false;
                    this.OTAError = this.$t('firmwareupgrade.UpdateCheckFailed') as string;
                });
        },
    },
    mounted() {
        if (!isLoggedIn()) {
            this.$router.push({
                path: '/login',
                query: { returnUrl: this.$router.currentRoute.value.fullPath },
            });
            return;
        }
        this.getFirmwareStatus();
    },
});
</script>
