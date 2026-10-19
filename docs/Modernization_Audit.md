# Modernization audit

Audit date: 2026-10-19. Base: `integration/hems-next` at
`82a06f3e61028b5d47a6d369adc7bd9f72b020ca`.

**This audit does not close the Energy-Charts migration.** Current official
provider documentation/live APIs were denied by the environment proxy (CONNECT
403). The requested network update was not present in the observed active
configuration. No official replacement contract, timestamp semantics, quota or
licensing terms are invented. Retaining an old interface merely because it is
supported is explicitly rejected. The unresolved provider migration must be
completed before treating this draft as the full modernization mission.

Classification: A CURRENT, B OUTDATED—MIGRATE, C BLOCKED FROM MIGRATION,
D UNCERTAIN, E NOT VERSIONED. "Current" below applies only to the stated
interface and checked official evidence, not every function in a dependency.

## External APIs

| Area | Current interface at start | Newest suitable interface / official source | Status | Action |
|------|---------------------------|--------------------------------------------|--------|--------|
| Energy-Charts | Legacy `/price?bzn=DE-LU&start=<unix>&end=<unix>`; `unix_seconds`, `price`, `unit` | [Official OpenAPI](https://api.energy-charts.info/openapi.json), v2 suitability and exact contract not retrievable | D; mandatory migration outstanding | No guessed v2 parser and no legacy fallback. Removed the documentation's "still supported" justification. Strict existing parser/retention tests remain. |
| Forecast.Solar | Public unversioned `/estimate/lat/lon/tilt/azimuth/kWp?time=utc&full=1`; `watt_hours_period` | [Official Swagger](https://api.forecast.solar/swagger.yaml), [estimate docs](https://doc.forecast.solar/api:estimate), blocked | D | No unverified API-version or payload change. Fixed independently reproducible diagnostics bug; expanded deterministic parameter/spacing/plane tests. |
| GitHub REST source comparison | Unversioned request defaults to old calendar version | `2026-03-10`: [official version table](https://github.com/github/docs/blob/main/data/tables/rest-api-versions.yml), [official OpenAPI](https://github.com/github/rest-api-description/blob/main/descriptions/api.github.com/api.github.com.2026-03-10.json) | B → migrated | Explicit version and JSON Accept headers. Verified compare response still contains `ahead_by`, `behind_by`, `status`, `html_url`; test asserts headers. |
| GitHub static firmware/profile assets | Raw anonymous HTTPS | [raw repository files](https://github.com/dawid-woitaschek/OpenDTU-HEMS) | E | Static catalog publication; no Contents API, account or token for browsing. Profile fetch pins immutable source SHA. |
| GitHub Actions | checkout/setup-node/setup-python v7; cache v6; upload v7; download v8; github-script v9 | Official vendor tags: checkout 7.0.1, setup-node 7.1.0, setup-python 7.0.0, cache 6.1.0, upload 7.0.2, download 8.0.2, github-script 9.0.0 | A for these checked actions | Existing rolling major refs already resolve to current stable releases. Other third-party actions remain D below. |
| PlatformIO platform | espressif32 6.10.0, Arduino 2.0.17 / bundled IDF 4.4 | [6.13.0 package contract](https://github.com/platformio/platform-espressif32/blob/v6.13.0/platform.json) is newest compatible 6.x; [8.0.0](https://github.com/platformio/platform-espressif32/blob/v8.0.0/platform.json) uses Arduino 3.3.12 | B → 6.13.0; C for 8.0.0 | Upgrade platform while retaining Arduino 2.0.17. Concrete Arduino-3/IDF-5 blocker below. Target remains ESP32-S3/8 MB/no PSRAM. |
| PlatformIO Core / esptool Python | Core 6.1.19 locally; unpinned CI provisioning | Core 6.2.0, platform-packaged esptool 4.11.0; [official Core package](https://pypi.org/project/platformio/), [esptool](https://github.com/espressif/esptool/tree/v4.11.0) | B → migrated | Shared build requirements pin the current Core and matching packaged tool; fix missing `intelhex` dependency and import precedence. esptool 5.x is not substituted into a platform that packages 4.11. |
| Native networking/MQTT/display/scheduler libraries | See version table below | Official vendor release tags and package manifests, checked 2026-10-19 | B → migrated | Upgrade compatible stable libraries and validate target build. No application control semantics/register changes. |
| MQTT wire protocol | MQTT 3.1.1 through espMqttClient | MQTT 5 needs a client that implements level 5; [current vendor constants](https://github.com/bertmelis/espMqttClient/blob/v1.7.3/src/Packets/Constants.h) still emit protocol level 4 | C | Latest client installed. Changing the integer would produce invalid packets; MQTT-5 properties, parser/session semantics and broker matrix require a separate client migration. |
| WARP Modbus | Profile v2; documented register table, functions 3/4/6/16 | [Current Tinkerforge implementation](https://github.com/Tinkerforge/esp32-firmware/blob/master/software/src/modules/modbus_tcp/modbus_tcp.cpp) still exposes referenced IEC/charging/current registers 1000/1002/1010 | E for these register contracts | No invented mapping migration. Firmware/register-table extensions are device capabilities, not a web API-version switch. Physical WARP3 validation remains mandatory. |
| Marstek Modbus | Curated firmware-specific V148/Micro-V119 profile | No authoritative current vendor contract retrievable/established | D | Preserve mapping and safety boundaries. Reverse-engineered references are not promoted to vendor authority. |
| OpenDTU / OnBattery inherited interfaces | Own REST/WebSocket endpoints, Hoymiles radio, CAN/BMS, VE.Direct, SML and configurable meter adapters | No single interchangeable newer external API | E for own endpoints; D for unchecked vendor contract revisions | Do not relabel local `/api` paths or radio revisions as an obsolete REST version. Hardware-facing changes deferred to vendor/device evidence. |
| Browser APIs | Fetch, AbortSignal timeout, typed arrays, File/Blob; SHA-256 must work on local HTTP | Standards-based APIs; no meaningful REST version | E | Small current `@noble/hashes` 2.4.0 implementation verifies profiles when Web Crypto is unavailable on local HTTP. Keep clipboard/manual file fallbacks for insecure local contexts. |
| Node / web tooling | Node 24, older Vue/Vite/lint/type tooling | [Node distribution index](https://nodejs.org/dist/index.json): stable 26.12.0; current official package metadata and vendor repositories below | B → migrated; C for TS7 | CI and local build use Node 26; current dependency releases installed. Migrate `__dirname`/CommonJS optional Vite config loading to native ESM. |
| Yarn / package-manager setup | Classic 1.22.22 and bundled Corepack assumption | Stable Yarn 4.18.1; [official Yarn source](https://github.com/yarnpkg/berry), [Corepack distribution policy](https://github.com/nodejs/corepack#readme) | B → migrated | Keep node_modules linking, immutable installs, quote npm-run-all glob for the new shell, explicitly provision Corepack 0.36.0 and share cached Node/Yarn setup across CI. Node >=25 no longer bundles Corepack. |
| JSON schemas | Own profile v1/v2, normalized energy v1, firmware manifest v1 | Repository-owned bounded contracts, not JSON Schema draft identifiers | E | v2 catalog entries remain backend validated; preserve v1 read-only profile and legacy firmware provenance compatibility. No unrelated `$schema` draft migration found. |

### Native dependency migrations

Official release source for each row is the linked vendor repository/tag; a newer
stable tag was actually checked rather than inferred from a version-shaped URL.

| Dependency | Before | Selected release | Migration implication |
|------------|--------|------------------|-----------------------|
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer/tree/v3.12.1) | 3.10.3 | 3.12.1 | Vendor manifest requires AsyncTCP ^3.5.0. |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP/tree/v3.5.0) | 3.4.10 | 3.5.0 | Matching maintained networking interface. |
| [espMqttClient](https://github.com/bertmelis/espMqttClient/tree/v1.7.3) | 1.7.2 | 1.7.3 | Same MQTT 3.1.1 application contract; vendor manifest supports AsyncTCP >=3.4. |
| [RF24](https://github.com/nRF24/RF24/tree/v1.6.2) | 1.5.0 | 1.6.2 | Compile existing radio integration; physical RF smoke test still needed. |
| [U8g2 Arduino distribution](https://github.com/olikraus/U8g2_Arduino/tree/2.37.1) | registry 2.36.18 | tag 2.37.1 | Registry is behind vendor release. Use official Arduino repository tag; its stale library.json labels itself 2.36.19, so build output includes that version plus tag commit bb6ebd1. |
| [TaskScheduler](https://github.com/arkhipenko/TaskScheduler/tree/v4.1.0) | 4.0.4 | 4.1.0 | Existing scheduling signatures preserved; native timing tests cover HEMS behavior. |
| [MycilaNTP](https://github.com/mathieucarbou/MycilaNTP/tree/v6.0.6) | 6.0.5 | 6.0.6 | Preserve synchronized-UTC preflight. |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson/tree/v7.4.3) | 7.4.3 | 7.4.3 | A: newest stable tag checked; same parser used for catalog generation and firmware imports. |
| [sunset](https://github.com/buelowp/sunset/tree/1.1.7) / [EspSoftwareSerial](https://github.com/plerup/espsoftwareserial/tree/8.2.0) | 1.1.7 / ^8.2.0 | unchanged | A for checked stable package releases. |

### Web tooling migrations

Versions come from current package metadata linked to the vendor source; builds,
lint, type checks and regression suites validate the selected graph. TypeScript 7
was attempted and rejected for the concrete Vue compiler incompatibility below.

| Package | Before | Selected | Vendor source |
|---------|--------|----------|---------------|
| @intlify/unplugin-vue-i18n | 11.0.7 | 11.2.7 | [Source](https://github.com/intlify/bundle-tools/blob/main/packages/unplugin-vue-i18n/README.md) |
| @tsconfig/node26 | 24.0.4 | 26.0.1 | [Source](https://github.com/tsconfig/bases#readme) |
| @types/bootstrap | 5.2.10 | 5.2.11 | [Source](https://github.com/DefinitelyTyped/DefinitelyTyped/tree/master/types/bootstrap) |
| @types/node | 25.5.0 | 26.6.5 | [Source](https://github.com/DefinitelyTyped/DefinitelyTyped/tree/master/types/node) |
| @vitejs/plugin-vue | 6.0.5 | 6.0.10 | [Source](https://github.com/vitejs/vite-plugin-vue/tree/main/packages/plugin-vue#readme) |
| @vue/eslint-config-typescript | 14.7.0 | 14.9.0 | [Source](https://github.com/vuejs/eslint-config-typescript#readme) |
| eslint | 10.1.0 | 10.13.0 | [Source](https://eslint.org) |
| eslint-plugin-oxlint | 1.50.0 | 1.89.0 | [Source](https://github.com/oxc-project/eslint-plugin-oxlint) |
| eslint-plugin-vue | 10.8.0 | 10.11.1 | [Source](https://eslint.vuejs.org) |
| oxlint | 1.50.0 | 1.89.0 | [Source](https://oxc.rs/docs/guide/usage/linter) |
| prettier | 3.8.1 | 3.9.10 | [Source](https://prettier.io) |
| sass | 1.98.0 | 1.105.2 | [Source](https://github.com/sass/dart-sass) |
| terser | 5.46.1 | 5.51.2 | [Source](https://terser.org) |
| typescript | 5.9.3 | 6.0.3 | [Source](https://www.typescriptlang.org/) |
| vite | 8.0.3 | 8.3.4 | [Source](https://vite.dev) |
| vite-plugin-css-injected-by-js | 4.0.1 | 5.0.2 | [Source](https://github.com/marco-prontera/vite-plugin-css-injected-by-js#readme) |
| vue | 3.5.31 | 3.5.44 | [Source](https://vuejs.org/) |
| vue-i18n | 11.3.0 | 11.4.16 | [Source](https://github.com/intlify/vue-i18n/tree/master/packages/vue-i18n#readme) |
| vue-router | 5.0.4 | 5.4.0 | [Source](https://router.vuejs.org) |
| vue-tsc | 3.2.6 | 3.3.13 | [Source](https://github.com/vuejs/language-tools#readme) |

### Concrete migration blockers and uncertain findings

| Area | Older/current interface | Newer candidate / official evidence | Actual blocker or uncertainty | Decision/action |
|------|-------------------------|-------------------------------------|-------------------------------|-----------------|
| Arduino / ESP-IDF | Arduino 2.0.17, bundled IDF 4.4.x | Arduino 3.3.12 / newer IDF; [official migration guide](https://github.com/espressif/arduino-esp32/blob/3.3.12/docs/en/migration_guides/2.x_to_3.0.rst), [IDF SPI-Ethernet migration](https://github.com/espressif/esp-idf/blob/v5.0/docs/en/migration-guides/release-5.x/networking.rst) | Current W5500 code passes an externally allocated shared SPI device to ETH_W5500_DEFAULT_CONFIG and esp_eth_mac_new_w5500. New driver allocates SPI internally and changes constructor/config signatures; private Arduino netif registration and mbedTLS _ret APIs also require porting. This is a real compatibility blocker affecting transport/hardware ownership, not "still supported". | C: latest compatible platform 6.13.0 now; separate network-driver port and Ethernet/Wi-Fi/TLS/resource tests required. |
| TypeScript | 6.0.3 after upgrade from 5.9.3 | 7.0.2; [vue-tsc 3.3.13 source](https://github.com/vuejs/language-tools/blob/v3.3.13/packages/tsc/index.ts) | Actual production build with TS7 fails ERR_PACKAGE_PATH_NOT_EXPORTED for typescript/lib/tsc; current vue-tsc resolves that JS compiler path. | C: 6.0.3 is newest compatible stable compiler; build passes. Retest when Vue supports the TS7 interface. |
| Transitive vue-i18n | Latest unplugin-vue-i18n 11.2.7 pulls vue-i18n-extensions with vue-i18n 10 | [Official plugin package](https://github.com/intlify/bundle-tools/tree/main/packages/unplugin-vue-i18n) | Current vendor dependency graph still pulls unsupported v10; top-level runtime is current v11. Overriding an internal compiler dependency requires upstream compatibility evidence. | C/D: expose inherited transitive baggage; no unsafe resolution override. |
| Other third-party Actions | stale@v11, lock-threads@v6, release-changelog-builder@v6.4.0, action-gh-release@v3, pinned dynamic-badges SHA | Vendor releases not comprehensively verified | D: cannot claim newest suitable versions from rolling labels alone. | Keep exact finding open; no fabricated current classification. |
| MCP_CAN_lib | Unpinned default Git ref | [Official repository](https://github.com/coryjfowler/MCP_CAN_lib) | D: reproducibility gap; no validated newest release pin/hardware comparison. | Explicitly deferred; don't invent CAN behavior during HEMS cleanup. |
| Vendored libraries/contracts | Frozen 1.1.1, SDM, VeDirectFrameHandler, SMLParser, CpuTemperature and internal helpers | Vendor repositories/documentation linked in source | D: latest upstream equivalence/licensing and local modifications not established. VE.Direct cites protocol 3.34 without a current official revision check. | No blanket current claim and no blind vendor subtree replacement. |
| User-configured HTTP/MQTT meters/bridges | Local JSON/SML and device-specific URLs | Depends on configured hardware/server | E/D: no fixed vendor URL/version to upgrade globally | Keep configurable transport; per-device migrations need actual configured contract. |
| Legacy local HTTP / optional MQTT TLS | User-selectable HTTP and CA configuration | HTTPS-capable device endpoints vary | D: no safe global switch for arbitrary local hardware; native providers remain verified HTTPS only. | No TLS weakening. Broader inherited transport hardening requires compatibility work. |
| Provider API / source metadata / east-plane incident | See provider rows | Official sources blocked | D: no authoritative v2 schema or live request; no real-device diagnostic capture | Mandatory follow-up, not accepted API-version exception. |

## Firmware update state and provenance

Root cause: lookup start already cleared data, but input edits did not. The old
manifest could remain visible under a newly typed channel. There was no generation
check across fetch/body parsing, and all image inequality used "newer or different".

The explicit state machine is idle → loading → found/current or found/different,
not_found, invalid, or error. The state type reserves newer/older only for proven
ordering; no unproven ordering is emitted. A synchronous channel watcher detaches
manifest/build/error state immediately; each request captures channel/repository
and a generation. Late success, error and body-decode results cannot publish.
Returned channel identity must match the normalized request. Unmount invalidates
in-flight results. Install requires a current accepted different-image result and
captures its channel/build before downloading. 404 and network failures show no
remote metadata, install action or availability banner. Installed metadata stays
visible because it describes the running image, not a failed remote lookup.

Build commit, source commit and tested merge commit stay distinct. Same source with
a different tested build is a different image, not a newer source. Old PR channel
publication dates do not establish ancestry. Regression coverage executes the real
Vue methods and rendered page, including pr-25 → deinemutter and both race orders.

## Forecast.Solar investigation

Code review covers fixed-origin HTTPS/CA/SNI, DNS/TLS/memory codes, source time,
UTC keys, tilt/azimuth/kWp conversion, six-plane bound, enabled ordering, 5-second
spacing, >=2-hour ordinary cadence, quota/server holds, config generations,
atomic aggregate replacement and retained source/transport freshness.

Proven bug: later unattempted planes retained old error/transport metadata after
an earlier plane stopped a subsequent refresh. Per-cycle attempted_at/state now
separates not-attempted, loading, success, failure and disabled. Last successful
fetch time stays available. Aggregate errors attach to the plane being combined;
overview errors name each failed plane and include bounded HTTP/TLS detail.

This does **not** prove the physical east-plane failure was stale UI, TLS, DNS,
quota, resource pressure or the provider. Current upstream contract and live TLS
compatibility remain unverified. No successful aggregate is fabricated, no TLS
fallback is introduced, and no provider was hammered.

## Repository catalog

Canonical source: `dawid-woitaschek/OpenDTU-HEMS`, `integration/hems-next`.
Distribution: `device-profile-catalog/catalog.json` on raw.githubusercontent.com.
The independent catalog workflow compiles the production parser, validates every
profile and automatically publishes deterministic metadata for additions, changes
and deletions. PR tests reject malformed files, category mismatches, duplicate IDs
and drift. Profile-only publication does not depend on a firmware build.

Normal listing: one bounded static catalog request, zero full-profile requests.
Import/update: one selected immutable-SHA profile GET, exact size/raw SHA-256
verification, then existing authenticated backend import (explicit replace query
and confirmation for installed IDs). A second normalized SHA permits installed
current/different detection from lightweight backend summaries. No remote catalog
lives on ESP32; backend parsing, in-use compatibility, persistence and runtime
reload remain authoritative. The list and consumer selectors refresh after success.
Repository failure leaves local administration/manual import usable. Catalog 404,
malformed/index-size errors, selected 404, integrity and profile-shape failures are
separate translated states. First live catalog publication necessarily follows
merging/running its workflow on the canonical ref.

## Legacy / baggage / redundancy findings

| Area | Finding | Classification | Action |
|------|---------|----------------|--------|
| Firmware view | Stale typed-channel result, request races, unproved newer wording | REMOVE | Explicit authoritative state machine and neutral difference wording. |
| HEMS navigation | Global and local menus duplicated destination definitions and used different labels | CONSOLIDATE | Shared four-entry navigation; RouterLink handles alias-active state. Global entry points and local page navigation retained as distinct purposes. |
| Profile hierarchy | Repository import would remain hidden inside Advanced profiles disclosure | REMOVE | Profile administration is directly visible on Devices; only manual file import is a fallback disclosure. |
| Profile imports | Manual/repository persistence, refresh and notification could drift | CONSOLIDATE | Shared submitProfile path with backend validation and profiles-changed event. |
| Locale initialization | Three repeated supplement merge loops | CONSOLIDATE | One loop also merges synchronized profile messages. |
| Diagnostic cross-link | Manual-control link pointed at Settings although controls live on Devices | REMOVE | Correct destination and synchronized labels. |
| Labels | German-only firmware hint, English-only profile preview/metadata/invalid JSON and diagnostic link/labels | REMOVE | EN/DE/FR translations synchronized; concise Devices label shared. |
| Locale keys | Advanced deviceProfiles and duplicated menu HemsInfo/HemsSettings keys became unused | REMOVE | Remove only these proven-unused keys in EN/DE/FR. |
| Docs | Old feature-branch energy-doc URL, no-unit-tests claim, old Node/platform setup and Retry-After statement | REMOVE | Canonical ref link and current test/setup/transport behavior. |
| Vite | CommonJS require/dirname in ESM config; native-loader warning | REMOVE | Dynamic ESM optional config import and import.meta.dirname. |
| CI/tooling duplication | Repeated Node/Corepack setup; Copilot setup lacked checkout before repository requirements | CONSOLIDATE | Shared setup-web composite explicitly provisions current package manager, caches dependencies, and requires checkout first. No legacy Yarn-1 retention. |
| Factory build | Global esptool could win over platform package; missing Python dependency after tool upgrade | REMOVE | Prioritize packaged tool and centralize reproducible build requirements. |
| Profile schemas v1/v2 | Read-only import compatibility versus bounded optional control metadata | KEEP — DISTINCT PURPOSE | Removing v1 would strand installed read-only profiles; it never grants writes. |
| Firmware legacy manifests | Exact image versus recoverable source provenance | KEEP — DISTINCT PURPOSE | Mandatory compatibility remains covered; no shortened SHA identity adoption. |
| Config migration defaults | Old persisted installations require safe mode/permission defaults | KEEP — DISTINCT PURPOSE | Keep fail-closed migration and documented legacy policy defaults. |
| Support snapshot clipboard fallback | Clipboard API unavailable on insecure local HTTP | KEEP — DISTINCT PURPOSE | Manual fallback remains necessary on ESP32-local pages. |
| Overview/settings/diagnostics/manual controls | Same domain appears with different action and detail depth | KEEP — DISTINCT PURPOSE | Inspectable energy/plans, editable settings, raw support data and intentional device commands are not equivalent functions. |
| Historical docs/DeviceProfiles | DTU board/build definitions resemble Modbus profile catalog names | KEEP — DISTINCT PURPOSE | Document distinction; do not feed them to catalog generator. |

## Deferred findings

| Area | Reason deferred | Required decision/validation |
|------|-----------------|------------------------------|
| Energy-Charts v2 | Official contract inaccessible | Check endpoint, parameters, units, resolution, nulls, source/generated semantics, rate limits, license/attribution; strict parser/filter migration and live smoke request. No old-API-support exception. |
| Forecast.Solar live compatibility / east failure | Upstream hosts blocked; no device capture | One bounded official response plus real failing-plane HTTP/DNS/TLS/memory diagnostics; verify clock/trust/parameters and actual quota. |
| Arduino 3 / IDF driver port | Changed shared SPI ownership and network initialization | Port W5500/netif/mbedTLS without hardware semantics change; test Ethernet/Wi-Fi, TLS, stack/heap, CAN/UART/radio and firmware size. |
| Broad vendor/dependency inventory D rows | No verified current authoritative mapping/release comparison | Resolve each named uncertainty, not just a deprecation search. |
| Permission editors | Settings and Devices both edit storage/EVSE permission, with different surrounding context | Product decision before removing a permission-editing entry point; keep control semantics and persistence intact. |
| HEMS profile naming / manual control placement | Broader consolidation risks moving safety-sensitive actions | Product decision and accessibility/usage review; preserve explicit manual action boundaries. |
| Local profile summary requests | Devices and profile administration separately fetch the same lightweight local summary | Consolidate summary ownership in a focused follow-up; current lists have different consumers and the four-slot bound limits work. |
| Duplicate backend error translations | Backend uses English diagnostic messages across inherited endpoints | Stable error-code/API/i18n migration; touched repository errors are translated now. |
| Tests/artifact ignore list | Native executables and __pycache__ clutter git status | Keep generated output out of commits; consolidating Makefile outputs/ignore rules is separate low-value housekeeping. |
| Full-tree dead code removal | A search hit is not proof of unreachable runtime/hardware paths | No speculative removal; actual removed redundancy is listed above. |

## Validation and physical limits

Validation results and final resource measurements are recorded in the PR/report.
Native tests exercise real parsers, profile validation/import constraints, energy
retention, quota/generation handling, firmware contracts and safety/control paths.
Browser APIs are deterministic fixtures, not an ESP32 HTTP server. Web production
build includes TypeScript checks. Target build is generic_esp32s3_usb, 8 MB,
without PSRAM. No firmware was flashed and no physical device command was sent.
Remaining physical validation: WARP3, Marstek, EV Protection timing, live bound
profile reload, Ethernet/radio after dependency updates, provider TLS/DNS/clock,
heap/worker-stack pressure and actual provider failure/quota behavior.

Validated locally: complete `make test` native suite; targeted final energy-service
rebuild; HEMS configuration and firmware channel/provenance Python tests; four
catalog-generation tests; frontend unit tests including EN/DE/FR key parity;
rendered modernization (360/1440), HEMS and economic suites (320/360/768/1440);
full production C++ lint, frontend lint, Prettier, Vue/TypeScript and production
build; workflow YAML syntax and whitespace checks. All passed.

Final target build: **generic_esp32s3_usb SUCCESS**. Static RAM **116456 / 327680
bytes (35.5%)**; linked flash **2861249 / 3932160 bytes (72.8%)**; OTA binary
**2861616 bytes**. No PSRAM flag/dependency was added. These are static/linker
measurements, not proof of worst-case live TLS heap/stack headroom.

Bounded network checks: generated public catalog URL returned **404**, expected
before first canonical publication; one pinned official profile returned **200**,
4457 bytes, SHA-256 equal to the repository source. Provider documentation/API
access remained proxy-denied; no successful Energy-Charts/Forecast.Solar live
validation is claimed. No complete provider payloads were logged.

Yarn 4 validation additionally uses a fresh empty node_modules installation from
the immutable lockfile with network disabled and dependency lifecycle scripts
disabled. This checks that the build does not depend on previous install scripts.
