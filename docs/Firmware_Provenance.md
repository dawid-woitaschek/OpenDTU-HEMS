# Firmware update provenance

Rolling channels publish `firmware-channel-<channel>/manifest.json` with schema
`opendtu-hems-update/v1`. The additive provenance fields keep v1 compatible:

| Field | Identity |
| --- | --- |
| `commit` | Exact image/build commit, matching compiled `git_sha`. For a PR this is the workflow's synthetic merge (`github.sha`). Do not replace it with the source SHA. |
| `source_commit` | Full source SHA: `github.event.pull_request.head.sha` for PR builds. |
| `tested_merge_commit` | Full commit actually tested/built: `github.sha`. |
| `branch` | Source branch, including the PR head branch. |
| `repository`, `channel` | Repository and published update channel (`pr-25`, `hems`, etc.). |
| `builds` | Environment-keyed OTA filename, byte size, MD5 and SHA256. |
| `published_at` | UTC snapshot publication time. |

On non-PR builds, all three commit fields equal the build's full HEAD SHA.
Local builds resolve HEAD when GitHub variables are absent. Firmware exposes
`source_commit` and `tested_merge_commit` alongside `git_sha` through both
`/api/firmware/status` and `/api/system/status`. These are small compiled strings;
no provider payloads or persistent JSON buffers are added.

Firmware Update compares the complete installed `git_sha` with manifest `commit`,
plus repository/channel/branch/environment. A rebuild with a different tested
merge can therefore offer a different image even when its source is unchanged.
The browser still validates the selected image's size and hashes before upload.

The opt-in System Information source comparison instead uses the compiled
`source_commit`. For older firmware without this field, a manifest's source SHA
can be recovered only when its exact build SHA, repository, channel, branch and
environment match the installed image. A newer/different channel's source SHA
must never be adopted as installed provenance. GitHub's full-SHA comparison
`ahead_by` determines whether the source branch advanced; display abbreviations
are never used for identity comparison.

Legacy manifests containing only `commit` remain valid: image comparison keeps
using `commit`, and source comparison falls back to the full installed build SHA
when no source provenance can be recovered. Legacy source identity is shown as
unknown rather than labeling an unverified synthetic SHA as the branch source.
When an old channel has already moved beyond an old firmware image and both lack
source provenance, that original source cannot be reconstructed from the current
manifest. GitHub access failures remain explicit version-check errors.

Validation:

```sh
python3 test/test_firmware_channels.py
python3 test/test_firmware_provenance.py
node test/test_firmware_provenance.mjs
```

The Python suite executes the production manifest writer and compiled constant
generator for PR and non-PR events. The frontend suite executes shared identity
logic and the real System Information check with deterministic channel/GitHub
responses, including legacy builds and an advancing source branch.
