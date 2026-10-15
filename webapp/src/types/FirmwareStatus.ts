export interface FirmwareStatus {
    ota_supported: boolean;
    git_hash: string;
    git_sha: string;
    git_branch: string;
    pioenv: string;
    update_repository: string;
    update_channel: string;
}
