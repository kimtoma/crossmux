# @kimtoma Device Branding and Library Design

**Date:** 2026-07-18
**Status:** Approved in conversation; written specification pending user review
**Target:** Xteink X4 Korean firmware (`gh_release_ko`)
**Branch:** `feature/crossmux-kr-firmware`

## 1. Goal

Replace the remaining ryOS-facing surfaces in the Korean firmware with a personal
`@kimtoma` identity and turn the Korean Apps-menu OPDS shortcut into a dedicated
`kimtoma 서재` status screen for the existing `kimtoma.com` reading sync.

The completed experience has four visible outcomes:

1. Boot and default sleep screens show the approved monochrome profile mark in
   the exact center with `@kimtoma` underneath.
2. Apps contains `kimtoma 서재`, not `ryOS 서재`.
3. `kimtoma 서재` shows the last server-accepted book, progress, sync time,
   pending work, cover state, and actionable connection status.
4. System settings contains `kimtoma.com 연동`; generic OPDS management remains
   available under the neutral name `온라인 서재 서버`.

This is a Korean-SKU customization. Other language builds retain their current
brand mark, product string, Apps entry, and OPDS labels.

## 2. Evidence and Existing Boundaries

- Boot and default sleep currently draw the same 120x120 mark and the same
  `STR_CROSSPOINT` label at `src/activities/boot_sleep/BootActivity.cpp:16-18`
  and `src/activities/boot_sleep/SleepActivity.cpp:154-161`.
- The image source is `src/images/Logo120.svg`; the generated 1-bit header is
  `src/images/Logo120.h`; `scripts/gen_boot_logo.py:12-15` documents the
  120x120, row-major, MSB-first, 1-bit format.
- The Apps menu maps `STR_OPDS_BROWSER` directly to
  `ActivityManager::goToBrowser()` at
  `src/activities/apps/AppsMenuActivity.cpp:22-29`.
- System settings maps `STR_OPDS_SERVERS` to the OPDS server list at
  `src/activities/settings/SettingsActivity.cpp:62-71` and
  `src/activities/settings/SettingsActivity.cpp:250-252`.
- Korean translations still identify `STR_CROSSPOINT` as `ryOS CrossMux`,
  `STR_OPDS_BROWSER` as `ryOS 서재`, and `STR_OPDS_SERVERS` as `ryOS 서재` at
  `lib/I18n/translations/korean.yaml:6`, `:273`, and `:349`.
- The existing protected web settings expose token presence, masked token
  status, auth pause, and queue corruption at
  `src/network/CrossPointWebServer.cpp:1351-1357`.
- The sync client commits accepted metadata through
  `ReadingSyncQueue::applyServerResult()` at
  `src/reading_sync/ReadingSyncClient.cpp:395-409`.
- The queue already uses an 8 KiB bounded JSON document and atomic temp-file
  replacement at `src/reading_sync/ReadingSyncQueue.cpp:356-430`.
- Library management is in scope at `SCOPE.md:25`; periodic active connectivity
  is out of scope at `SCOPE.md:41-43`.

## 3. Approved Visual Direction

### 3.1 Profile mark

The approved mark is a pure black-and-white, face-only simplification of the
provided profile avatar. Its required identifying features are:

- swept black hair with the small left-side tuft;
- oversized thick rectangular glasses;
- half-lidded eyes and black pupils;
- two distinct black nostrils;
- a small curved smile;
- a rounded face outline and ears;
- no text, color, gray, texture, clothing, or decorative frame.

The two nostrils must remain distinct after the final 120x120 1-bit threshold.
The image is centered using the existing orientation-aware expression:

```cpp
(pageWidth - 120) / 2, (pageHeight - 120) / 2
```

This keeps the mark centered in every screen orientation without hardcoded
800x480 coordinates.

### 3.2 Source and generation

Create a deterministic vector/geometric source and generator rather than
committing the large private profile input or depending on an image API during
the build:

- `src/images/KimtomaMark120.svg` is the human-reviewable source.
- `scripts/gen_kimtoma_mark.py` rasterizes the same geometry using the Python
  standard library.
- `src/images/KimtomaMark120.h` is the generated 1-bit firmware asset.
- A generator test verifies dimensions, packed size, deterministic output, pure
  1-bit pixels, center occupancy, and two separated nostril components.

The generated array is exactly `120 * 120 / 8 = 1,800` bytes in flash. It does
not allocate RAM and does not add a second framebuffer.

### 3.3 Boot and sleep rendering

Under `ENABLE_KOREAN_VERSION`, BootActivity and the default SleepActivity use
`KIMTOMA_MARK_120` and `tr(STR_KIMTOMA_BRAND)`. The Korean translation is
`@kimtoma`. Other builds keep `Logo120` and `STR_CROSSPOINT`.

The existing sleep dark-mode inversion remains unchanged. The same 1-bit mark
therefore renders as the approved light-on-dark inverse without a second asset.

Custom bitmap sleep screens and book-cover sleep screens are not changed.

## 4. Navigation and Labels

### 4.1 Apps menu

For `ENABLE_KIMTOMA_READING_SYNC`, replace the Apps-menu OPDS row with:

```text
kimtoma 서재 -> ActivityManager::goToKimtomaLibrary()
```

Non-Korean/non-kimtoma builds retain the current OPDS Apps row.

### 4.2 System settings

For the Korean sync build, append these two independent System actions:

1. `kimtoma.com 연동` -> the kimtoma status/action screen.
2. `온라인 서재 서버` -> the existing generic `OpdsServerListActivity`.

The existing OPDS implementation, server credentials, browsing, search, and
downloads remain unchanged. Only the Korean user-facing service-specific label
is removed.

### 4.3 Shared activity

Use one `KimtomaLibraryActivity` with a closed entry mode:

```cpp
enum class KimtomaLibraryMode : uint8_t { Library, Settings };
```

Apps opens `Library`; System settings opens `Settings`. Mode-dependent rendering
uses an exhaustive switch with no default. The two modes share status loading,
button handling, connection-test state, and retry behavior instead of creating
two network-aware activities.

## 5. Displayed Data

### 5.1 Library mode

Render through `GUI`, UITheme metrics, oriented screen dimensions, and
`MappedInputManager::Button` only. Show:

- header: profile icon plus `kimtoma 서재`;
- connection badge;
- last server-accepted title and author;
- progress percentage and a monochrome progress bar;
- last successful sync time;
- pending metadata/cover state;
- actions: `다시 동기화`, `연동 설정`;
- normal mapped Back/Select/Up/Down hints.

The screen does not download or render a cover. Avoiding an EPUB/image decoder
and cover buffer is deliberate: the screen is a status surface, and its purpose
does not justify TLS/image heap pressure.

### 5.2 Settings mode

Show the same state summary with actions focused on:

- `연결 테스트`;
- `다시 동기화`;
- guidance that the secret token is managed in File Transfer web settings.

The device UI does not display or edit the raw token. The long `rd1_...` secret
remains a password field in the protected web settings, matching the approved
credential boundary. No new persistent on/off setting is added.

### 5.3 Empty states

- No token: `설정 안 됨` and the web-settings guidance.
- Token but no accepted book: `연동됨`, `아직 동기화된 책 없음`.
- Accepted book but invalid clock: show book/progress and `시간 정보 없음`.
- No pending work: `보낼 기록 없음`.

## 6. State Model

Use an explicit UI state rather than combinations of ad-hoc booleans:

```cpp
enum class KimtomaSyncUiState : uint8_t {
  NotConfigured,
  Ready,
  Pending,
  Syncing,
  AuthenticationRequired,
  QueueCorrupt,
};
```

Priority is deterministic and covered by a pure policy test:

1. `QueueCorrupt`
2. `NotConfigured`
3. `AuthenticationRequired`
4. `Syncing`
5. `Pending`
6. `Ready`

The ordering prevents a configured token or an active worker from hiding a
queue-integrity failure.

Connection testing has a separate closed state because it is a user-requested
operation, not a persistent connection condition:

```cpp
enum class KimtomaConnectionTestState : uint8_t {
  Idle,
  Running,
  Succeeded,
  AuthenticationFailed,
  NetworkFailed,
};
```

Both enums are dispatched with exhaustive switches.

## 7. Persistence

### 7.1 Queue schema

Bump `ReadingSyncQueue::kSchemaVersion` from 1 to 2. Add an optional bounded
`lastAccepted` object to the same atomic queue JSON:

```json
{
  "schemaVersion": 2,
  "lastAccepted": {
    "title": "B밀의 숲",
    "author": "김형석",
    "progressPercent": 37,
    "lastReadAt": "2026-07-17T12:34:56Z",
    "acceptedAt": 1784262908,
    "coverState": "uploaded"
  }
}
```

Do not duplicate `bookId`, ISBN, cover hash, MIME, or token in this UI summary.
`acceptedAt` uses `TimeUtils::getAuthoritativeTimestamp()` after a validated
server response; zero is valid and renders as `시간 정보 없음`.

On `accepted` or `duplicate`, move/copy the bounded display fields from the
pending metadata before clearing pending state, then persist the queue once.
`stale` advances sequence as today but does not replace the last accepted book
with a server-older local value.

### 7.2 Migration

Schema 1 files migrate in memory to schema 2 without discarding pending
metadata, cover jobs, auth pause, terminal state, or the last accepted
fingerprint. The migrated file has no `lastAccepted` display object until the
next successful sync. Unknown future schemas remain corrupt and are preserved
under the existing corrupt-queue behavior.

The 8 KiB serialized limit remains unchanged. A worst-case boundary test must
prove that maximum permitted pending metadata plus maximum display summary and
cover job fit. If it does not fit, the implementation must reduce display-only
limits; it must not raise the 8 KiB queue cap without a separate memory review.

### 7.3 Heap cost

At most one accepted display summary and one pending metadata object coexist.
The accepted summary contains only title, author, last-read time, progress,
accepted epoch, and cover state. Worst-case dynamic string content is bounded
by the existing 300 UTF-16-unit title, 200-unit author, and 64-unit timestamp
limits. This is a cold-path, long-lived queue allocation; it is not created in
a render loop.

The activity takes a bounded snapshot only while the coordinator is stopped.
It releases that snapshot in `onExit()`. No repeated string growth is allowed
while rendering; strings reserve their validated byte lengths before copying.

## 8. Network and Concurrency

Opening either kimtoma screen never turns on Wi-Fi. It renders the persisted
status immediately.

### 8.1 Manual retry

`다시 동기화` reuses the existing coordinator one-shot path. The activity:

1. confirms that no worker is running;
2. captures the display snapshot;
3. requests a manual retry;
4. starts the existing one-shot worker;
5. renders only atomics while the worker owns queue/network state;
6. reloads the snapshot after `isRunning()` transitions to false.

On exit it calls the existing cancel/wait path before destruction.

### 8.2 Connection test

Add a coordinator validation operation that reuses the same bounded Wi-Fi/TLS
lifecycle as pending sync and calls the existing
`POST /v1/reading/sync?validateOnly=1`. Its result is stored in an atomic enum;
the activity never reads queue strings while the worker runs.

Only one worker operation may run at a time. A validation request during sync,
or a retry during validation, is ignored with an on-screen `작업 진행 중`
message rather than queued as a second network task.

Every exit path retains the current teardown contract: close HTTP/file state,
disconnect Wi-Fi, turn Wi-Fi off, deinitialize the driver, and restore reading
stats memory.

There is no timer, polling loop, or automatic GET of public
`/v1/reading/current`.

## 9. Error Handling

| Condition | Device display | Data action |
| --- | --- | --- |
| Token absent | `설정 안 됨` | No network request |
| 401/403 | `인증 확인 필요` | Preserve queue; keep auth paused |
| Wi-Fi/timeout/TLS failure | `네트워크 오류 · 다시 시도 가능` | Preserve queue |
| Queue parse failure | `대기열 복구 필요` | Preserve corrupt file; no sync |
| Pending metadata | `동기화 대기` | User may retry |
| Worker active | `동기화 중` | Disable duplicate actions |
| Accepted/duplicate | `연동됨` | Persist last accepted summary |
| Invalid wall clock | `시간 정보 없음` | Keep accepted book/progress |

Logs may contain operation and error categories, never token values,
Authorization headers, or private file paths.

## 10. File Changes

### Create

- `src/images/KimtomaMark120.svg`
- `src/images/KimtomaMark120.h` (generated)
- `scripts/gen_kimtoma_mark.py`
- `scripts/test_gen_kimtoma_mark.py`
- `src/activities/apps/kimtoma/KimtomaLibraryActivity.h`
- `src/activities/apps/kimtoma/KimtomaLibraryActivity.cpp`
- `src/reading_sync/ReadingSyncUiPolicy.h`
- `src/reading_sync/ReadingSyncUiPolicy.cpp`
- `test/reading_sync/ReadingSyncUiPolicyTest.cpp`

### Modify

- `src/activities/boot_sleep/BootActivity.cpp`
- `src/activities/boot_sleep/SleepActivity.cpp`
- `src/activities/apps/AppsMenuActivity.cpp`
- `src/activities/ActivityManager.h`
- `src/activities/ActivityManager.cpp`
- `src/activities/settings/SettingsActivity.h`
- `src/activities/settings/SettingsActivity.cpp`
- `src/reading_sync/ReadingSyncQueue.h`
- `src/reading_sync/ReadingSyncQueue.cpp`
- `src/reading_sync/ReadingSyncCoordinator.h`
- `src/reading_sync/ReadingSyncCoordinator.cpp`
- `src/reading_sync/ReadingSyncClient.h`
- `src/reading_sync/ReadingSyncClient.cpp`
- `lib/I18n/translations/english.yaml`
- `lib/I18n/translations/korean.yaml`
- generated i18n headers through the existing generator
- `test/CMakeLists.txt`
- `docs/engineering/kimtoma-reading-sync.md`
- `scripts/verify_ko_release.py`

The exact generated i18n diff may be large in the Korean-only build workflow;
only the source YAML and generator-selected output belong to this concern.

## 11. Test Strategy

Follow red-green-refactor for each behavior.

### 11.1 Asset tests

- generator test fails before `KIMTOMA_MARK_120` exists;
- output is exactly 1,800 bytes and deterministic;
- all pixels are 1-bit;
- black geometry is centered inside the 120x120 canvas;
- two nostril components survive threshold and remain separated;
- generated header matches the checked-in header.

### 11.2 Policy tests

- every UI-state priority combination;
- connection-test result mapping;
- no default branch in enum rendering/action dispatch;
- retry/validation mutual exclusion.

### 11.3 Queue tests

- schema 1 pending/cover/auth state migrates without loss;
- schema 2 last accepted summary round-trips;
- accepted and duplicate update the summary;
- stale does not replace it;
- invalid or oversized summary is rejected;
- worst-case queue remains at or below 8 KiB.

### 11.4 Build and simulator

- run the native reading-sync tests;
- regenerate and verify i18n;
- build the Korean desktop simulator in its separate build directory;
- navigate Apps and Settings using keyboard events only;
- verify Back/Confirm/Up/Down and both entry modes;
- build `gh_release_ko` and the default environment;
- run the existing KO release size gate and confirm the new asset/code remains
  below the approved OTA maximum.

### 11.5 Physical X4

- boot mark centered in portrait and the configured orientation;
- default light and dark sleep marks centered and readable;
- both nostrils visible on the real panel;
- custom sleep bitmap and book-cover modes unchanged;
- no-token, pending, success, auth failure, network failure, and corrupt queue
  states display correctly;
- connection test and retry always tear down Wi-Fi;
- free heap remains above 50 KiB during sync and returns to baseline;
- ten repeated operations show no downward heap or largest-block trend.

## 12. Acceptance Criteria

- No Korean boot, default sleep, Apps OPDS shortcut, or System OPDS label shows
  `ryOS` or `CrossMux` service branding.
- Boot and default sleep show the approved profile mark centered with
  `@kimtoma`.
- The Korean Apps menu opens `kimtoma 서재` and never opens OPDS from that row.
- Generic OPDS remains reachable through `온라인 서재 서버`.
- System settings shows `kimtoma.com 연동` exactly.
- The dashboard shows last accepted book/progress and truthful connection,
  pending, auth, and queue states without enabling Wi-Fi on entry.
- Raw tokens never appear on device, in GET responses, or in logs.
- New UI/image work adds no framebuffer and no render-loop allocation.
- Native tests, Korean simulator, default build, Korean build, and KO size gate
  pass before device installation.

## 13. Non-Goals

- Removing OPDS support from the firmware.
- Browsing `kimtoma.com` as a web page on the device.
- Periodically polling the public current-reading API.
- Editing or displaying the raw device token on the X4 screen.
- Downloading cover art for the kimtoma status screen.
- Rebranding non-Korean firmware builds or removing license/upstream credits.
- Adding multiple kimtoma accounts, devices, history, or analytics screens.

## 14. Self-Review

- **Placeholder scan:** No TBD, TODO, unresolved option, or placeholder behavior
  remains.
- **Consistency:** The Apps screen, System action, queue summary, and manual
  network triggers all use the same state model.
- **Scope:** The work extends existing reading sync and library navigation; it
  does not add active polling or a browser.
- **Memory:** The asset is 1,800 flash bytes; accepted/pending strings are
  bounded cold-path state; no image buffer or hot-path allocation is added.
- **Migration:** Schema 1 queue data is explicitly preserved; unknown future
  schemas remain protected.
- **Privacy:** Token entry stays web-only and secret values never enter the UI
  model.
- **Ambiguity:** `kimtoma.com 연동`, `kimtoma 서재`, `온라인 서재 서버`, and
  `@kimtoma` are exact approved strings.
