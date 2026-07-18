# @kimtoma Device Branding and Library Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task.

**Goal:** Replace Korean-device ryOS/CrossMux surfaces with the approved centered `@kimtoma` mark, add a truthful `kimtoma 서재` sync dashboard, retain neutral OPDS access in Settings, and install the verified Korean firmware on the connected Xteink X4.

**Architecture:** Keep the change Korean-SKU-only behind `ENABLE_KIMTOMA_READING_SYNC`/`ENABLE_KOREAN_VERSION`. Persist one bounded last-accepted display summary beside the existing atomic reading-sync queue, expose all UI decisions through pure policy functions, and let one coordinator worker own Wi-Fi/TLS for either retry or validation. The activity renders immutable snapshots while idle and atomics while the worker runs; it never loads covers or starts networking on entry.

**Tech Stack:** C++17, Arduino-ESP32/ESP-IDF, FreeRTOS atomics/task, ArduinoJson 7, PlatformIO, CMake/GTest native tests, Python `unittest`, desktop simulator, Xteink X4 WebSerial/OTA installation path.

**Approved specification:** `docs/superpowers/specs/2026-07-18-kimtoma-device-branding-library-design.md`

## File Responsibility Map

| Area | Files | Responsibility |
| --- | --- | --- |
| Version contracts | `src/reading_sync/ReadingSyncTypes.h`, `src/reading_sync/ReadingSyncCoordinator.cpp`, `src/reading_sync/ReadingSyncResponseValidation.cpp`, `test/reading_sync/ReadingSyncPolicyTest.cpp` | Keep API wire schema at 1 while queue document moves to 2. |
| Queue summary | `src/reading_sync/ReadingSyncQueue.h/.cpp`, `src/reading_sync/ReadingSyncUiPolicy.h/.cpp`, `test/reading_sync/ReadingSyncUiPolicyTest.cpp` | Bounded accepted summary, schema-1 migration, accepted/duplicate/stale behavior, UI state policy. |
| Worker operations | `src/reading_sync/ReadingSyncCoordinator.h/.cpp`, `src/reading_sync/ReadingSyncClient.h/.cpp` | Mutually exclusive sync/validate operation, atomic result, existing Wi-Fi/TLS teardown. |
| Device activity | `src/activities/apps/kimtoma/KimtomaLibraryActivity.h/.cpp`, `src/activities/ActivityManager.h/.cpp`, Apps/Settings activities | Two entry modes, snapshot lifecycle, button actions, no network on entry. |
| Branding | `src/images/KimtomaMark120.svg/.h`, `scripts/gen_kimtoma_mark.py`, `scripts/test_gen_kimtoma_mark.py`, Boot/Sleep activities | Deterministic 120x120 1-bit mark centered with `@kimtoma`. |
| Text | English/Korean translation YAML and generated I18n files | Exact labels and state/action strings through `tr()`. |
| Gates/docs | `scripts/verify_ko_release.py`, `scripts/test_verify_ko_release.py`, `docs/engineering/kimtoma-reading-sync.md` | Static release invariants, operating notes, build/install evidence. |

## Preflight: Preserve the Existing Dirty Baseline

**Files:** Existing modified/untracked paths shown by `git status --short`.

1. Record `git status --short` and `git diff --stat` before feature edits.
2. Run the current native reading-sync tests and the existing KO verifier tests before adding new failures:

```bash
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
python3 -m unittest scripts/test_verify_ko_release.py
```

Expected: either all pass, or any baseline failure is captured separately and resolved without resetting user work.

3. Never use broad `git add .`. Stage only paths listed in the active task. Do not stage `.dummy/`, `dependencies 2.lock`, `managed_components/`, SDK configuration artifacts, or generated ignored I18n files.

## Task 1: Separate Queue Schema from API Wire Schema

**Files:**
- Modify: `src/reading_sync/ReadingSyncTypes.h`
- Modify: `src/reading_sync/ReadingSyncCoordinator.cpp`
- Modify: `src/reading_sync/ReadingSyncResponseValidation.cpp`
- Test: `test/reading_sync/ReadingSyncPolicyTest.cpp`

### Step 1: Write the failing contract test

Add compile-time assertions:

```cpp
static_assert(ReadingSyncQueue::kSchemaVersion == 2);
static_assert(ReadingSyncMetadata::kWireSchemaVersion == 1);
```

Add a runtime bound test proving metadata schema 2 is rejected by `isReadingSyncMetadataBounded()`.

### Step 2: Run the focused test and confirm RED

```bash
cmake --build build/test --target reading_sync_tests
ctest --test-dir build/test -R ReadingSync --output-on-failure
```

Expected: compile failure because `kWireSchemaVersion` does not exist and the queue is still schema 1.

### Step 3: Implement the contract

Use a class constant, not a queue constant:

```cpp
struct ReadingSyncMetadata {
  static constexpr uint8_t kWireSchemaVersion = 1;
  uint8_t schemaVersion = kWireSchemaVersion;
  // existing fields
};
```

Change coordinator enqueue to:

```cpp
metadata.schemaVersion = ReadingSyncMetadata::kWireSchemaVersion;
```

Change response validation to compare against the wire constant. Change the queue document constant to 2 only after this separation is in place.

### Step 4: Run GREEN and inspect the diff

Run the focused test again. Confirm no API payload path references `ReadingSyncQueue::kSchemaVersion`.

## Task 2: Add Pure Accepted-Summary and UI Policies

**Files:**
- Create: `src/reading_sync/ReadingSyncUiPolicy.h`
- Create: `src/reading_sync/ReadingSyncUiPolicy.cpp`
- Create: `test/reading_sync/ReadingSyncUiPolicyTest.cpp`
- Modify: `test/reading_sync/CMakeLists.txt`

### Step 1: Write failing state-priority tests

Define and test closed enums:

```cpp
enum class KimtomaSyncUiState : uint8_t {
  NotConfigured, Ready, Pending, Syncing, AuthenticationRequired, QueueCorrupt
};
enum class KimtomaConnectionTestState : uint8_t {
  Idle, Running, Succeeded, AuthenticationFailed, NetworkFailed
};
```

Test exact priority:

```cpp
EXPECT_EQ(KimtomaSyncUiState::QueueCorrupt,
          resolveKimtomaSyncUiState({true, false, true, true, true}));
EXPECT_EQ(KimtomaSyncUiState::NotConfigured,
          resolveKimtomaSyncUiState({false, false, true, true, false}));
```

Cover every remaining state and verify an operation arbiter rejects retry while validate is running and validate while sync is running.

### Step 2: Write failing accepted-summary tests

Define:

```cpp
enum class ReadingSyncCoverState : uint8_t { None, Pending, Uploaded };
struct ReadingSyncAcceptedSummary {
  std::string title;
  std::string author;
  uint8_t progressPercent = 0;
  std::string lastReadAt;
  uint32_t acceptedAt = 0;
  ReadingSyncCoverState coverState = ReadingSyncCoverState::None;
};
```

Test title/author/timestamp bounds using the existing wire limits, progress `<= 100`, cover-state string conversion, and server-result policy:

- accepted -> replace summary;
- duplicate -> replace summary;
- stale -> preserve summary;
- unknown -> reject.

### Step 3: Run RED

Configure/build `reading_sync_tests`; expected failure is missing policy files and symbols.

### Step 4: Implement allocation-free decisions

Policy resolution accepts booleans/enums by value. Validation reads strings but does not allocate. No `std::vector`, `new`, or render-time copies are introduced.

### Step 5: Run GREEN

```bash
cmake --build build/test --target reading_sync_tests
ctest --test-dir build/test -R 'Kimtoma|ReadingSync' --output-on-failure
```

## Task 3: Persist Queue Schema 2 and Migrate Schema 1

**Files:**
- Modify: `src/reading_sync/ReadingSyncQueue.h`
- Modify: `src/reading_sync/ReadingSyncQueue.cpp`
- Test: `test/reading_sync/ReadingSyncUiPolicyTest.cpp`
- Create: `scripts/test_reading_sync_queue_schema.py`

### Step 1: Add failing source/fixture checks

The Python test constructs schema-1 and schema-2 fixture JSON and asserts the queue source exposes:

- accepted-summary getter;
- queue schema 2;
- explicit acceptance of source schema 1 or 2;
- pending `schemaVersion` compared with `ReadingSyncMetadata::kWireSchemaVersion`;
- `lastAccepted` serialization and an unchanged `MAX_QUEUE_BYTES = 8192`.

It also serializes a worst-case valid fixture (300 title bytes, 200 author bytes, 64 timestamp bytes, maximum cover fields) and asserts UTF-8 JSON length `<= 8192`.

### Step 2: Run RED

```bash
python3 -m unittest scripts/test_reading_sync_queue_schema.py
```

Expected: missing schema-2 summary and migration handling.

### Step 3: Implement bounded persistence

Add:

```cpp
const ReadingSyncAcceptedSummary* lastAccepted() const;
```

Store `hasLastAccepted_` plus one summary member. During load:

1. accept only root schema 1 or 2;
2. parse all existing pending/cover/auth/terminal/fingerprint fields identically;
3. require pending wire schema 1 for either queue schema;
4. parse/validate `lastAccepted` only for schema 2;
5. leave summary absent when migrating schema 1;
6. reject unknown future schemas using the existing corrupt preservation path.

During `applyServerResult()`, copy validated display fields from pending before clearing only for accepted/duplicate. Use `TimeUtils::getAuthoritativeTimestamp()`; zero is permitted. Derive cover state from the accepted response and retained cover job without storing cover bytes.

Serialize all fields once through the existing bounded `StaticJsonDocument<8192>`, measure before write, and retain rollback-on-save-failure.

### Step 4: Run GREEN

Run Python fixture/source tests and native reading-sync tests. Inspect `git diff` to verify no 8 KiB cap change and no token/book path in the summary.

## Task 4: Add a Single Mutually Exclusive Sync/Validate Worker

**Files:**
- Modify: `src/reading_sync/ReadingSyncCoordinator.h`
- Modify: `src/reading_sync/ReadingSyncCoordinator.cpp`
- Modify: `src/reading_sync/ReadingSyncClient.h`
- Modify: `src/reading_sync/ReadingSyncClient.cpp`
- Test: `test/reading_sync/ReadingSyncUiPolicyTest.cpp`
- Test: `test/reading_sync/ReadingSyncClientPolicyTest.cpp`

### Step 1: Write failing API-shape assertions

Assert the coordinator exposes:

```cpp
bool requestManualRetryAndStart();
bool requestConnectionTest();
KimtomaConnectionTestState connectionTestState() const;
ReadingSyncWorkerOperation workerOperation() const;
```

Verify a closed `ReadingSyncWorkerOperation { None, Sync, Validate }` policy permits only `None -> Sync` and `None -> Validate`.

### Step 2: Run RED

Build focused native tests; expected compile failure on missing methods/types.

### Step 3: Implement one owner and atomic results

Use one atomic operation and the existing `running_` CAS. `taskEntry()` switches exhaustively:

```cpp
switch (operation) {
  case ReadingSyncWorkerOperation::Sync:
    READING_SYNC_CLIENT.performPendingSync(...);
    break;
  case ReadingSyncWorkerOperation::Validate:
    result = READING_SYNC_CLIENT.performValidation(...);
    break;
  case ReadingSyncWorkerOperation::None:
    break;
}
```

`performValidation()` must reuse the same `NetworkLifecycle`, reading-stats release/reload, saved-Wi-Fi connection, token handling, cancellation, and teardown as sync. Map 200/auth/network outcomes to the connection-test enum. Never expose the token in result objects or logs.

If the credential is absent, return `false` without creating a task or enabling Wi-Fi. If an operation is already running, return `false`; do not queue another operation.

### Step 4: Run GREEN and regression tests

Run native tests and verify current heap diagnostic behavior is preserved. Confirm `startOneShotIfPending()` still performs automatic one-shot sync exactly as before.

## Task 5: Generate and Verify the Approved 120x120 Mark

**Files:**
- Create: `src/images/KimtomaMark120.svg`
- Create: `scripts/gen_kimtoma_mark.py`
- Create: `scripts/test_gen_kimtoma_mark.py`
- Generate: `src/images/KimtomaMark120.h`

**Visual reference:** `/Users/macmini/Documents/CrossMux-KR/.superpowers/brainstorm/25553-1784347695/content/kimtoma-face-nostrils-120.png`

### Step 1: Write failing generator tests

Tests must assert:

- canvas exactly 120x120;
- packed header contains exactly 1,800 bytes;
- rerunning the generator is byte-identical;
- only black/white pixels are emitted;
- black bounding box center differs from canvas center by at most two pixels;
- two separated nostril components exist in the expected central-lower face region;
- checked-in header equals freshly generated header.

### Step 2: Run RED

```bash
python3 -m unittest scripts/test_gen_kimtoma_mark.py
```

Expected: generator/source/header missing.

### Step 3: Implement deterministic geometry

Use Python standard-library polygon/ellipse/line rasterization or a tiny deterministic SVG parser owned by the script. Do not add Pillow or an online generation dependency. Emit row-major, MSB-first bytes with:

```cpp
constexpr uint8_t KIMTOMA_MARK_120[1800] = { ... };
```

Keep the two nostrils at least two white pixels apart after thresholding. Create the reviewable SVG from the identical geometry constants.

### Step 4: Run GREEN and create preview

Generate a PBM/PNG preview using the script's deterministic output, inspect it locally, and compare against the already approved sample.

## Task 6: Apply Korean Boot/Sleep Branding and Exact Translations

**Files:**
- Modify: `src/activities/boot_sleep/BootActivity.cpp`
- Modify: `src/activities/boot_sleep/SleepActivity.cpp`
- Modify: `lib/I18n/translations/english.yaml`
- Modify: `lib/I18n/translations/korean.yaml`
- Modify generated I18n files locally only as required to build
- Test: `scripts/test_verify_ko_release.py`

### Step 1: Add failing KO verifier tests

Require Korean guards and exact strings:

- `STR_KIMTOMA_BRAND: "@kimtoma"`;
- `STR_KIMTOMA_LIBRARY: "kimtoma 서재"`;
- `STR_KIMTOMA_INTEGRATION: "kimtoma.com 연동"`;
- `STR_OPDS_SERVERS: "온라인 서재 서버"` in Korean;
- Korean Boot/Sleep render `KIMTOMA_MARK_120` and brand;
- default non-Korean branch still renders `Logo120` and `STR_CROSSPOINT`.

### Step 2: Run RED

```bash
python3 -m unittest scripts/test_verify_ko_release.py
```

### Step 3: Implement guarded rendering

Use compile-time aliases in each source:

```cpp
#ifdef ENABLE_KOREAN_VERSION
#include "images/KimtomaMark120.h"
constexpr const uint8_t* kBootMark = KIMTOMA_MARK_120;
constexpr StrId kBootBrand = StrId::STR_KIMTOMA_BRAND;
#else
#include "images/Logo120.h"
constexpr const uint8_t* kBootMark = Logo120;
constexpr StrId kBootBrand = StrId::STR_CROSSPOINT;
#endif
```

Draw at `(pageWidth - 120) / 2`, `(pageHeight - 120) / 2`. Do not change custom bitmap/book-cover sleep branches or dark inversion.

Add all new device text to both English fallback and Korean source YAML; render only via `tr()`. Regenerate I18n with:

```bash
python3 scripts/gen_i18n.py --strip-unused --src-dirs src lib/EpdFont
```

Do not stage `.gitignore`d generated headers.

### Step 4: Run GREEN

Run asset tests, KO verifier tests, and compile the simulator target far enough to catch I18n identifiers.

## Task 7: Implement `KimtomaLibraryActivity` and Navigation

**Files:**
- Create: `src/activities/apps/kimtoma/KimtomaLibraryActivity.h`
- Create: `src/activities/apps/kimtoma/KimtomaLibraryActivity.cpp`
- Modify: `src/activities/ActivityManager.h`
- Modify: `src/activities/ActivityManager.cpp`
- Modify: `src/activities/apps/AppsMenuActivity.cpp`
- Modify: `src/activities/settings/SettingsActivity.h`
- Modify: `src/activities/settings/SettingsActivity.cpp`
- Modify: English/Korean translation YAML
- Test: `scripts/test_verify_ko_release.py`

### Step 1: Add failing routing checks

Tests require:

- Apps row under `ENABLE_KIMTOMA_READING_SYNC` uses `STR_KIMTOMA_LIBRARY` and `goToKimtomaLibrary`;
- other builds retain OPDS row;
- Korean System list contains `kimtoma.com 연동` then neutral OPDS;
- `SettingAction::KimtomaIntegration` dispatches to Settings mode;
- the mode switch has no default;
- token text is absent from the activity renderer.

### Step 2: Run RED

Run the KO verifier tests; expected missing activity/routing symbols.

### Step 3: Implement lifecycle and bounded snapshot

Declare:

```cpp
enum class KimtomaLibraryMode : uint8_t { Library, Settings };
```

The activity owns only:

- mode and selected action index;
- one optional queue snapshot loaded while coordinator is stopped;
- last observed `running` flag;
- a fixed-size/enum status message, not an unbounded log string.

`onEnter()` loads persisted state and calls `requestUpdate()` only. It must not invoke Wi-Fi, validation, or sync.

`loop()`:

- uses `ButtonNavigator` and mapped Confirm/Back/Up/Down;
- retry calls `requestManualRetryAndStart()`;
- connection test calls `requestConnectionTest()`;
- settings action opens the same activity in Settings mode;
- observes worker true -> false and reloads the snapshot once;
- ignores duplicate requests with translated `작업 진행 중` state.

`onExit()` calls cancel/wait before releasing snapshot storage.

### Step 4: Render through GUI only

Use oriented dimensions and UITheme metrics. Render a header, state badge, book/author, integer progress bar, last sync time, pending/cover state, action list, and mapped button hints. Do not open EPUB/cover files and do not allocate a vector in `render()`; use fixed action tables and direct draw calls.

Entry-mode switch must be exhaustive:

```cpp
switch (mode_) {
  case KimtomaLibraryMode::Library: /* retry, settings */ break;
  case KimtomaLibraryMode::Settings: /* validate, retry */ break;
}
```

### Step 5: Run GREEN

Run native tests, KO verifier tests, and the Korean simulator build.

## Task 8: Update Engineering Docs and Run All Software Gates

**Files:**
- Modify: `docs/engineering/kimtoma-reading-sync.md`
- Modify: `scripts/verify_ko_release.py`
- Modify: `scripts/test_verify_ko_release.py`

### Step 1: Document observable contracts

Document:

- exact device labels;
- queue schema 2 vs API wire schema 1;
- schema-1 migration;
- dashboard state priority;
- token remains web-only;
- retry/connection test behavior and Wi-Fi teardown;
- asset regeneration command;
- heap and device acceptance gates.

### Step 2: Run formatting

```bash
PATH="/usr/lib/llvm-21/bin:$PATH" ./bin/clang-format-fix
```

Review formatting diff and ensure unrelated dirty files were not reformatted unexpectedly.

### Step 3: Run full test suite

```bash
python3 -m unittest scripts/test_gen_kimtoma_mark.py scripts/test_reading_sync_queue_schema.py scripts/test_verify_ko_release.py
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
```

Expected: all pass.

### Step 4: Build and inspect Korean simulator

```bash
cmake -S simulator -B simulator/build_ko -DSIMULATOR_KOREAN_VERSION=ON
cmake --build simulator/build_ko -j2
```

Run at 1x in the background with the dedicated `simulator/sd_root`. Navigate by keyboard only:

1. Apps -> `kimtoma 서재`;
2. verify no-token/ready/pending render;
3. Back -> Apps;
4. Settings -> System -> `kimtoma.com 연동`;
5. verify `연결 테스트`, `다시 동기화`, and `온라인 서재 서버` remain separate;
6. capture screenshots of Boot/Sleep and both dashboard modes.

### Step 5: Build firmware and static analysis

```bash
pio run -e gh_release_ko
pio run
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
python3 scripts/verify_ko_release.py
```

Record firmware path, SHA-256, size, KO gate limit, and build output. Do not claim runtime memory success from build flags.

### Step 6: Self-review the final diff

Check:

- no `ryOS`/`CrossMux` in the four Korean user-facing surfaces;
- no bare `new`, raw SDK storage/display/input use, render-loop vector growth, or token logging;
- no generated/ignored files staged;
- no unknown-schema destructive migration;
- no default branch hiding a new closed-enum case;
- queue max remains 8 KiB;
- all existing dirty work is preserved.

## Task 9: Install and Verify on the Connected Xteink X4

**Files:** Built `gh_release_ko` firmware artifact and installation evidence only.

### Step 1: Confirm live device path

Verify the connected device endpoint, expected from the prior session as `http://172.30.1.92`, without assuming it is still current. Check the device page/API and identify the supported firmware upload route. If browser WebSerial is required, use the existing Custom `.bin` flow.

### Step 2: Capture pre-install safety data

Record current firmware/version, connectivity, and a rollback artifact if available. Confirm the generated firmware is for Xteink X4 and below the OTA size cap.

### Step 3: Install the verified artifact

Upload only the newly built Korean firmware. Keep power and USB connected until the device reboots. Do not use an unverified SD-card flow when the device firmware has no SD update menu.

### Step 4: Run physical acceptance checks

Verify on the actual panel:

- centered boot mark and `@kimtoma`;
- both nostrils remain visibly separate;
- centered light/dark default sleep mark;
- custom sleep image and cover modes unchanged;
- Apps -> `kimtoma 서재`;
- Settings -> System -> `kimtoma.com 연동` and `온라인 서재 서버`;
- no-token/pending/success/auth/network/corrupt states as safely reproducible;
- retry and validation return Wi-Fi to off;
- minimum free heap above 50 KiB and no downward free/largest-block trend across ten repeated operations.

If the live-memory gate fails, keep the installed build classified as diagnostic and do not publish/deploy it as a release.

### Step 5: Report evidence

Return the firmware SHA-256/size, device-reported version, installation method, visible UI checks, and live heap measurements. State any state that could not be safely reproduced instead of inferring success.

## Commit Discipline

Make small commits only after each task's tests pass. Use explicit path staging. Suggested messages:

1. `test(sync): separate queue and wire schema contracts`
2. `feat(sync): persist kimtoma accepted summary`
3. `feat(sync): add manual validation worker`
4. `feat(ui): add kimtoma device mark`
5. `feat(ui): add kimtoma library dashboard`
6. `docs(sync): document kimtoma device integration`

Before every commit, run `git diff --cached --check` and list staged files. Never include the pre-existing unrelated SDK/generated artifacts.

## Plan Self-Review

- Every requirement in the approved specification maps to a task and verification command.
- Queue document schema 2 and API wire schema 1 are explicitly decoupled before persistence changes.
- New dynamic memory is limited to one bounded cold-path accepted summary; no cover/framebuffer/render-loop allocation is added.
- Network operations remain user-initiated or existing one-shot sync only; opening the activity never enables Wi-Fi.
- Non-Korean Boot/Sleep/Apps/OPDS behavior is retained behind compile-time guards.
- Physical installation is gated on native tests, simulator inspection, firmware builds, KO size verification, and live heap evidence.
- Placeholder scan: no TODO, TBD, unresolved option, or omitted decision remains in this plan.
