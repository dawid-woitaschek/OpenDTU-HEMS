export interface FirmwareStatus {
    ota_supported: boolean;
    git_hash: string;
    git_sha: string;
    source_commit?: string;
    tested_merge_commit?: string;
    git_branch: string;
    pioenv: string;
    update_repository: string;
    update_channel: string;
}
