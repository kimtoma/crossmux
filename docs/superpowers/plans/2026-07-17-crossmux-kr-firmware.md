# CrossMux KR Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Xteink X4용 `gh_release_ko`를 한국어 장문 독서에 맞게 다듬고, 유효한 EPUB 독서 세션의 최신 책·진행률·원본 표지를 `api.kimtoma.com`으로 로컬 우선 동기화한다.

**Architecture:** 현재 `ryokun6/crossmux`의 한국어 SKU에 검증된 글꼴·공백 변경을 고정 커밋으로 통합하고, KO 전용 source filter로 사용하지 않는 네트워크 기능을 제거해 OTA 여유를 되찾는다. 독서 통계 저장 뒤 scalar snapshot을 SD의 원자적 단일 pending 큐에 기록하고, 홈 화면에서 취소 가능한 FreeRTOS one-shot 작업이 짧게 Wi-Fi/TLS를 열어 metadata와 선택적 원본 표지를 전송한다.

**Tech Stack:** C++20, PlatformIO/Arduino ESP32-C3, FreeRTOS, `esp_http_client`, ArduinoJson, SdFat 기반 `HalStorage`, GoogleTest 1.17, Python 3 생성·검증 스크립트.

## Global Constraints

- 대상은 Xteink X4이며 기존 X3/X4 공용 HAL 경계를 유지한다. 하드웨어 접근은 `lib/hal`을 통한다.
- 제품 범위는 한국어 읽기, 로컬 통계, 일반 OPDS, 파일 전송, OTA에 한정한다. 게임·다중 사용자·공개 이력·본문/장 제목 업로드는 추가하지 않는다.
- `gh_release_ko`의 8/10/12pt는 KS X 1001 공통 한글 2,350자와 현대 자모·한국어 UI 문자를, 14pt는 현대 한글 11,172자·교육용 한자 1,800자·현대 자모·EPUB 기호·UI 문자를, 16/18pt는 현대 자모·UI 문자만 포함한다.
- 한국어 section cache version은 `76`이며 source whitespace 변경 전 캐시를 재사용하지 않는다.
- OTA slot은 6,553,600B, 하드 최대 바이너리는 6,029,312B(512KiB 여유), 목표 최대는 5,898,240B(640KiB 여유)다. 하드 최대를 넘으면 설치하지 않는다.
- 세션은 `sessionMs >= 180000`, 진행률 증가 `>= 1%p`, 또는 해당 세션 완료 중 하나일 때만 후보가 된다.
- metadata JSON은 8KB 이하, 표지는 JPG/PNG 원본만 1B..2,097,152B, 스트림 chunk는 1,024B다. BMP 썸네일은 업로드하지 않는다.
- `sequence`는 영속 `uint32_t` 1..4,294,967,295이며 최신 pending 하나만 coalesce한다. fingerprint에는 `lastReadAt`을 넣지 않는다.
- API base는 `https://api.kimtoma.com/v1/reading`, Wi-Fi 연결 제한은 8초, metadata/cover HTTPS 기본 timeout은 각각 15초다.
- 토큰은 `rd1_` 뒤에 base64url 43자를 붙인 별도 256-bit 값이며 기기에는 MAC 기반 XOR+base64 난독화로 저장한다. 암호화로 표현하지 않고 GET·로그·내보내기에 원문을 노출하지 않는다.
- 배경 작업은 리더의 `Epub`, framebuffer, Activity 소유 객체를 참조하지 않는다. 홈 이탈 요청을 받으면 다음 chunk/네트워크 경계에서 종료하고 Wi-Fi를 항상 teardown한다.
- 실제 X4 동기화 중 free heap은 항상 50KB 초과여야 하고, 반복 10회에서 하강 추세나 누수가 없어야 한다.
- 기존 코드의 bare `new` 금지, nothrow allocation, 작은 스트림 버퍼, `Storage` 직렬화 규칙을 따른다.

---

## Approved Inputs and Pinned Upstream Changes

- 통합 설계: `docs/superpowers/specs/2026-07-17-crossmux-kr-integration-design.md`
- 펌웨어 설계: `docs/superpowers/specs/2026-07-17-crossmux-kr-firmware-design.md`
- 글꼴 기준: `ryokun6/crossmux` PR #18의 `da3b08d` (`feat(ko): embed KS X 1001 common syllables in UI font sizes (8/10/12pt)`)
- 공백 기준: `ryokun6/crossmux` PR #19의 `bb831f6` (`fix(cjk): preserve source spaces between CJK words (Korean word spacing)`)
- 두 PR의 `AGENTS.md` 변경은 가져오지 않는다. 현재 작업 브랜치의 `AGENTS.md`를 보존한다.

모든 명령은 `/Users/macmini/Documents/CrossMux-KR`에서 실행한다. 구현 시작 때 `superpowers:using-git-worktrees`로 격리한 뒤 이 계획의 태스크 순서대로 진행한다.

## File Responsibility Map

### 생성

- `lib/Epub/Epub/CjkSourceSpacing.h` — explicit source space와 CJK segment break를 구분하는 순수 판정 함수.
- `src/reading_sync/ReadingSyncTypes.h` — wire와 queue에서 공용으로 쓰는 bounded scalar type.
- `src/reading_sync/ReadingSyncPolicy.h/.cpp` — qualifying, fingerprint, HTTP/sequence 결과 정책. 네트워크·파일 의존성 없음.
- `src/reading_sync/ReadingSyncQueue.h/.cpp` — `/.crosspoint/reading_sync/queue.json` 원자 저장, coalescing, sequence 복구.
- `src/reading_sync/ReadingSyncCredentialStore.h/.cpp` — 토큰 난독화 저장과 masked 상태.
- `src/reading_sync/EpubOriginalCoverSource.h/.cpp` — EPUB 원본 JPG/PNG를 임시 파일로 1KB 스트리밍하고 SHA-256 계산.
- `src/reading_sync/ReadingSyncClient.h/.cpp` — Wi-Fi 수명주기, sync/validate/cover API, HTTP 상태 매핑, 항상 연결 종료.
- `src/reading_sync/ReadingSyncResponseValidation.h/.cpp` — ArduinoJson과 분리된 네 필드 응답 의미 검증.
- `src/reading_sync/ReadingSyncCoordinator.h/.cpp` — 홈 화면 one-shot 작업, Wi-Fi 수명주기, 취소, queue 적용.
- `test/cjk_source_spacing/*` — source whitespace 순수 판정 native test.
- `test/reading_sync/*` — 세션·fingerprint·상태 전이 native test.
- `lib/EpdFont/scripts/test_build_ko_charset.py` — 2,350자 및 UI 문자 범위 검증.
- `scripts/verify_ko_release.py` — source filter, 링크 map, 바이너리 hard/target gate 검증.
- `docs/engineering/kimtoma-reading-sync.md` — 설정, 큐, API, 장애, X4 점검 절차.

### 수정

- `platformio.ini` — KO/KO RC의 source filter와 `ENABLE_KIMTOMA_READING_SYNC`.
- `lib/EpdFont/scripts/build_ko_charset.py`, `build-ko-builtin-fonts.sh`, charset/font headers — pinned PR #18 내용.
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.*`, `ParsedText.*`, `Section.cpp`, `lib/GfxRenderer/*`, `lib/Utf8/Utf8.h` — pinned PR #19 내용과 순수 helper 사용.
- `docs/engineering/japanese-korean-build.md`, `cache-management.md`, `docs/file-formats.md` — 글꼴 tier와 cache 76.
- `src/activities/apps/standby/StandbyActivity.cpp` — KO에서 AirPage face를 구성하지 않음.
- `src/main.cpp`, `src/SettingsList.h`, `src/activities/settings/SettingsActivity.*`, `src/activities/reader/EpubReaderActivity.*` — KOReader cloud path 제외와 독서 snapshot enqueue.
- `src/OpdsServerStore.cpp` — KO 새 설치에서 `ryOS Books` seed를 생략.
- `lib/Epub/Epub.h/.cpp` — 원본 cover href accessor.
- `src/network/HttpDownloader.h/.cpp` — 상태 코드 보존 JSON POST와 파일 PUT 스트리밍.
- `src/activities/home/HomeActivity.cpp` — 첫 render 뒤 sync 시작, 이탈 시 취소.
- `src/network/CrossPointWebServer.cpp`, `src/network/html/SettingsPage.html`, 한국어/영어 번역 — token 저장·삭제·연결 테스트 UI.
- `test/CMakeLists.txt`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`, `.github/workflows/nightly.yml`, `.github/workflows/sync-build.yml` — 테스트와 KO size gate.

---

### Task 1: Pin and integrate the Korean font coverage

**Files:**
- Create: `lib/EpdFont/scripts/test_build_ko_charset.py`
- Modify from pinned commit: `lib/EpdFont/scripts/build_ko_charset.py`
- Modify from pinned commit: `lib/EpdFont/scripts/build-ko-builtin-fonts.sh`
- Create from pinned commit: `lib/EpdFont/scripts/chars_ko_ks1001_2350.txt`
- Create from pinned commit: `lib/EpdFont/scripts/ko_ui_chars.txt`
- Modify from pinned commit: `lib/EpdFont/builtinFonts/notosans_ko_8.h`
- Modify from pinned commit: `lib/EpdFont/builtinFonts/notosans_ko_10.h`
- Modify from pinned commit: `lib/EpdFont/builtinFonts/notosans_ko_12.h`
- Modify: `docs/engineering/japanese-korean-build.md`

**Interfaces:**
- Consumes: `korean.yaml`, Resource Han Rounded KR font input, pinned commit `da3b08d`.
- Produces: deterministic 8/10/12pt headers containing all 2,350 KS X 1001 syllables plus all required Korean UI characters.

- [ ] **Step 1: Write the failing charset verification**

Create `lib/EpdFont/scripts/test_build_ko_charset.py` with this content:

```python
from pathlib import Path
import re
import unittest
import yaml

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
KS = ROOT / "chars_ko_ks1001_2350.txt"
YAML = REPO / "lib/I18n/translations/korean.yaml"


def codepoints(path: Path) -> set[int]:
    text = path.read_text(encoding="utf-8")
    return {ord(ch) for ch in text if not ch.isspace()}


def header_codepoints(size: int) -> set[int]:
    text = (REPO / f"lib/EpdFont/builtinFonts/notosans_ko_{size}.h").read_text(encoding="utf-8")
    return {int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{4,6})", text)}


def ui_codepoints() -> set[int]:
    data = yaml.safe_load(YAML.read_text(encoding="utf-8"))
    return {ord(ch) for value in data.values() if isinstance(value, str) for ch in value if not ch.isspace()}


class KoreanCharsetTest(unittest.TestCase):
    def test_ks_x_1001_pool_is_exact(self):
        chars = codepoints(KS)
        self.assertEqual(2350, len(chars))
        self.assertTrue(all(0xAC00 <= cp <= 0xD7A3 for cp in chars))

    def test_small_sizes_cover_pool_and_ui(self):
        required = codepoints(KS) | ui_codepoints()
        for size in (8, 10, 12):
            with self.subTest(size=size):
                self.assertTrue(required <= header_codepoints(size))

    def test_all_sizes_cover_ui(self):
        required = ui_codepoints()
        for size in (8, 10, 12, 14, 16, 18):
            with self.subTest(size=size):
                self.assertTrue(required <= header_codepoints(size))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it and confirm the current small-font coverage fails**

Run: `python3 -m unittest lib/EpdFont/scripts/test_build_ko_charset.py -v`

Expected: FAIL because `chars_ko_ks1001_2350.txt` is absent or 8/10/12pt headers do not contain the required pool.

- [ ] **Step 3: Apply the pinned upstream content without importing its repository instructions**

Run:

```bash
git cherry-pick --no-commit da3b08d
git restore --source=HEAD -- AGENTS.md
git diff --check
git status --short
```

Expected: only font scripts/data/headers and `docs/engineering/japanese-korean-build.md` remain modified; `AGENTS.md` is clean.

- [ ] **Step 4: Regenerate and verify**

Run:

```bash
python3 -m unittest lib/EpdFont/scripts/test_build_ko_charset.py -v
pio run -e gh_release_ko
```

Expected: three unittest cases pass and PlatformIO ends with `SUCCESS`. Record `.pio/build/gh_release_ko/firmware.bin` size in the commit body; do not claim that size as the final gate before Task 3.

- [ ] **Step 5: Commit the isolated font change**

```bash
git add lib/EpdFont/scripts lib/EpdFont/builtinFonts/notosans_ko_8.h lib/EpdFont/builtinFonts/notosans_ko_10.h lib/EpdFont/builtinFonts/notosans_ko_12.h docs/engineering/japanese-korean-build.md
git commit -m "feat(ko): add KS X 1001 coverage to small fonts"
```

---

### Task 2: Preserve Korean source spaces and invalidate cache 76

**Files:**
- Create: `lib/Epub/Epub/CjkSourceSpacing.h`
- Create: `test/cjk_source_spacing/CMakeLists.txt`
- Create: `test/cjk_source_spacing/CjkSourceSpacingTest.cpp`
- Modify from pinned commit: `lib/Epub/Epub/ParsedText.cpp`
- Modify from pinned commit: `lib/Epub/Epub/ParsedText.h`
- Modify from pinned commit: `lib/Epub/Epub/Section.cpp`
- Modify from pinned commit: `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`
- Modify from pinned commit: `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h`
- Modify from pinned commit: `lib/GfxRenderer/GfxRenderer.cpp`
- Modify from pinned commit: `lib/GfxRenderer/GfxRenderer.h`
- Modify from pinned commit: `lib/Utf8/Utf8.h`
- Modify: `test/CMakeLists.txt`
- Modify: `docs/engineering/cache-management.md`
- Modify: `docs/file-formats.md`

**Interfaces:**
- Consumes: `utf8IsHangul(uint32_t)` from the pinned change and parser flags `pendingRealSpace`, `pendingSegmentBreak`.
- Produces: `constexpr bool shouldInsertCjkSourceSpace(bool explicitSpace, bool segmentBreak, uint32_t left, uint32_t right)` and KO cache version 76.

- [ ] **Step 1: Add the failing pure test**

Create `test/cjk_source_spacing/CjkSourceSpacingTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "Epub/Epub/CjkSourceSpacing.h"

TEST(CjkSourceSpacing, ExplicitSpaceAlwaysSurvives) {
  EXPECT_TRUE(shouldInsertCjkSourceSpace(true, false, U'한', U'글'));
  EXPECT_TRUE(shouldInsertCjkSourceSpace(true, false, U'中', U'文'));
}

TEST(CjkSourceSpacing, KoreanSegmentBoundarySurvives) {
  EXPECT_TRUE(shouldInsertCjkSourceSpace(false, true, U'책', U'읽'));
  EXPECT_TRUE(shouldInsertCjkSourceSpace(false, true, U'A', U'한'));
  EXPECT_TRUE(shouldInsertCjkSourceSpace(false, true, U'A', U'B'));
}

TEST(CjkSourceSpacing, ChineseFormattingNewlineDoesNotBecomeSpace) {
  EXPECT_FALSE(shouldInsertCjkSourceSpace(false, true, U'中', U'文'));
  EXPECT_FALSE(shouldInsertCjkSourceSpace(false, false, U'한', U'글'));
  EXPECT_FALSE(shouldInsertCjkSourceSpace(false, true, 0, U'한'));
}
```

Create `test/cjk_source_spacing/CMakeLists.txt`:

```cmake
add_executable(cjk_source_spacing_tests CjkSourceSpacingTest.cpp)
target_link_libraries(cjk_source_spacing_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(cjk_source_spacing_tests)
```

Append to `test/CMakeLists.txt`:

```cmake
add_subdirectory(cjk_source_spacing)
```

- [ ] **Step 2: Run the focused test and confirm it fails to compile**

Run:

```bash
cmake -S test -B build/test
cmake --build build/test --target cjk_source_spacing_tests
```

Expected: FAIL because `Epub/Epub/CjkSourceSpacing.h` does not exist.

- [ ] **Step 3: Apply the pinned parser change and extract the decision helper**

Run:

```bash
git cherry-pick --no-commit bb831f6
git restore --source=HEAD -- AGENTS.md
git diff --check
```

Create `lib/Epub/Epub/CjkSourceSpacing.h`:

```cpp
#pragma once
#include <cstdint>
#include <Utf8.h>

inline bool shouldInsertCjkSourceSpace(const bool explicitSpace, const bool segmentBreak,
                                       const uint32_t left, const uint32_t right) {
  if (explicitSpace) return true;
  if (!segmentBreak || left == 0 || right == 0) return false;
  const bool leftNoSpaceCjk = utf8IsCjkBreakable(left) && !utf8IsHangul(left);
  const bool rightNoSpaceCjk = utf8IsCjkBreakable(right) && !utf8IsHangul(right);
  return !(leftNoSpaceCjk && rightNoSpaceCjk);
}
```

In `ChapterHtmlSlimParser.cpp`, include the helper and replace the pinned inline decision with this exact call:

```cpp
const bool wordSpaceBefore = shouldInsertCjkSourceSpace(
    self->pendingRealSpace, self->pendingSegmentBreak, self->lastTextCodepoint, firstCodepoint);
```

Keep the pinned `ParsedText::wordSpaceBefore` propagation and renderer `forceWordSpace` behavior unchanged. In `Section.cpp`, keep the pinned constants exactly:

```cpp
#if defined(ENABLE_CHINESE_VERSION)
#ifdef CHINESE_UI_SIMPLIFIED
constexpr uint8_t SECTION_FILE_VERSION = 74;
#else
constexpr uint8_t SECTION_FILE_VERSION = 73;
#endif
#elif defined(ENABLE_JAPANESE_VERSION)
constexpr uint8_t SECTION_FILE_VERSION = 75;
#elif defined(ENABLE_KOREAN_VERSION)
constexpr uint8_t SECTION_FILE_VERSION = 76;
#else
constexpr uint8_t SECTION_FILE_VERSION = 54;
#endif
```

Document cache 76 and the source-space semantic change in both cache documents.

- [ ] **Step 4: Run focused and full native tests plus the Korean simulator**

Run:

```bash
cmake --build build/test --target cjk_source_spacing_tests
ctest --test-dir build/test --output-on-failure -j
cmake -S simulator -B simulator/build_ko -DSIMULATOR_KOREAN_VERSION=ON
cmake --build simulator/build_ko -j
```

Expected: focused test reports 3 passed, all existing ctest cases pass, Korean simulator build succeeds.

- [ ] **Step 5: Commit the spacing/cache unit**

```bash
git add lib/Epub lib/GfxRenderer lib/Utf8 test docs/engineering/cache-management.md docs/file-formats.md
git commit -m "fix(ko): preserve Korean source word spacing"
```

---

### Task 3: Remove KO-only service paths and enforce the firmware size gate

**Files:**
- Create: `scripts/verify_ko_release.py`
- Modify: `platformio.ini`
- Modify: `src/activities/apps/standby/StandbyActivity.cpp`
- Modify: `src/main.cpp`
- Modify: `src/SettingsList.h`
- Modify: `src/activities/settings/SettingsActivity.cpp`
- Modify: `src/activities/settings/SettingsActivity.h`
- Modify: `src/activities/reader/EpubReaderActivity.cpp`
- Modify: `src/activities/reader/EpubReaderActivity.h`
- Modify: `src/activities/reader/EpubReaderMenuActivity.cpp`
- Modify: `src/OpdsServerStore.cpp`

**Interfaces:**
- Consumes: current non-KO code paths and `gh_release_ko` link map.
- Produces: KO builds with AirPage/MQTT, KOReader cloud/settings, and default ryOS Books seed unreachable and unlinked; general OPDS and `BookIdentity` remain.

- [ ] **Step 1: Create the failing release verifier**

Create `scripts/verify_ko_release.py`:

```python
#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".pio/build/gh_release_ko"
BIN = BUILD / "firmware.bin"
MAP = BUILD / "firmware.map"
HARD_MAX = 6_029_312
TARGET_MAX = 5_898_240
FORBIDDEN = ("AirPageFace", "PubSubClient", "KOReaderSyncClient", "KOReaderCredentialStore")


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
ko = ini.split("[env:gh_release_ko]", 1)[1].split("[env:gh_release_ko_rc]", 1)[0]
for fragment in (
    "-<activities/apps/standby/AirPageFace.cpp>",
    "-<activities/apps/standby/AirPageDeviceId.cpp>",
    "-<activities/settings/KOReaderAuthActivity.cpp>",
    "-<activities/settings/KOReaderSettingsActivity.cpp>",
    "-<activities/reader/KOReaderSyncActivity.cpp>",
):
    if fragment not in ko:
        fail(f"missing KO source filter {fragment}")

if not BIN.exists() or not MAP.exists():
    fail("build gh_release_ko before verification")

size = BIN.stat().st_size
if size > HARD_MAX:
    fail(f"firmware {size} exceeds hard maximum {HARD_MAX}")

map_text = MAP.read_text(encoding="utf-8", errors="ignore")
linked = [symbol for symbol in FORBIDDEN if re.search(rf"\b{re.escape(symbol)}\b", map_text)]
if linked:
    fail("forbidden linked symbols: " + ", ".join(linked))

headroom = 6_553_600 - size
target = "met" if size <= TARGET_MAX else "not-met"
print(f"OK size={size} headroom={headroom} target={target}")
```

- [ ] **Step 2: Build and confirm the verifier rejects the current KO source contract**

Run:

```bash
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
```

Expected: FAIL naming the first missing KO source filter; after Task 1 the binary may also exceed the hard maximum.

- [ ] **Step 3: Add exact KO source filters and compile guards**

Add to both KO environments:

```ini
build_src_filter =
  ${base.build_src_filter}
  -<activities/apps/standby/AirPageFace.cpp>
  -<activities/apps/standby/AirPageDeviceId.cpp>
  -<activities/settings/KOReaderAuthActivity.cpp>
  -<activities/settings/KOReaderSettingsActivity.cpp>
  -<activities/reader/KOReaderSyncActivity.cpp>
```

Wrap the AirPage include and factory row exactly as follows:

```cpp
#if !defined(ENABLE_KOREAN_VERSION)
#include "AirPageFace.h"
#endif

#if !defined(ENABLE_KOREAN_VERSION)
    {[]() -> std::unique_ptr<StandbyFace> { return makeUniqueNoThrow<AirPageFace>(); },
     [](int, int) { return true; }},
#endif
```

Apply the same guard to these existing KOReader-only units without changing their non-KO bodies: `#include "KOReaderCredentialStore.h"` and `KOREADER_STORE.loadFromFile()` in `main.cpp`; the KOReader include plus `SettingAction::KOReaderSync` push/case in `SettingsActivity.cpp`; the KOReader include and four `DynamicString`/`DynamicEnum` entries between the `ryOS Cloud Sync` and `Status Bar Settings` comments in `SettingsList.h`; the two KOReader includes, `LP_MENU_KOSYNC` call body, reader-menu `SYNC` call body, and complete `launchKOReaderSync()` method in `EpubReaderActivity.cpp`; the matching declaration in `EpubReaderActivity.h`; and the `MenuAction::SYNC` item push in `EpubReaderMenuActivity.cpp`.

Keep persisted enum values unchanged. Immediately after `SETTINGS.loadFromFile()` in KO builds, migrate the removed action once:

```cpp
#if defined(ENABLE_KOREAN_VERSION)
  if (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_KOSYNC) {
    SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_BOOKMARK;
    SETTINGS.saveToFile();
  }
#endif
```

In KO only, render the long-press menu with a `DynamicEnum` mapping of displayed index 0→`LP_MENU_DISABLED`, 1→`LP_MENU_BOOKMARK`, with labels `{StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION}`. Its getter returns 1 only for bookmark and its setter stores only disabled/bookmark. Thus existing settings files migrate without shifting the global enum and no dead cloud choice is visible.

Do not guard `BookIdentity`, `KOReaderDocumentId`, EPUB content hashing, general OPDS, or OTA. In `OpdsServerStore::loadFromFile()`, replace only the empty-store seed return with:

```cpp
#if defined(ENABLE_KOREAN_VERSION)
  return saveToFile();
#else
  return seedDefaultServer();
#endif
```

The KO path writes an empty valid store once; non-KO builds retain the current seed behavior.

- [ ] **Step 4: Measure the real link result**

Run:

```bash
pio run -t clean -e gh_release_ko
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
pio run -e gh_release
```

Expected: verifier prints `OK`, none of the forbidden symbols appear in the map, and the normal release still builds. If `target=not-met` but hard maximum passes, stop before installation and present the measured size/headroom for explicit user approval. If the hard maximum fails, inspect `.map` and remove only additional approved ryOS service paths; do not reduce the specified Korean font tiers.

- [ ] **Step 5: Commit the KO product boundary**

```bash
git add platformio.ini scripts/verify_ko_release.py src/activities/apps/standby/StandbyActivity.cpp src/main.cpp src/SettingsList.h src/activities/settings/SettingsActivity.cpp src/activities/settings/SettingsActivity.h src/activities/reader/EpubReaderActivity.cpp src/activities/reader/EpubReaderActivity.h src/activities/reader/EpubReaderMenuActivity.cpp src/OpdsServerStore.cpp
git commit -m "build(ko): trim unused service clients and gate size"
```

---

### Task 4: Define and test the pure reading-sync policy

**Files:**
- Create: `src/reading_sync/ReadingSyncTypes.h`
- Create: `src/reading_sync/ReadingSyncPolicy.h`
- Create: `src/reading_sync/ReadingSyncPolicy.cpp`
- Create: `test/reading_sync/CMakeLists.txt`
- Create: `test/reading_sync/ReadingSyncPolicyTest.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ReadingSessionSnapshot` scalar values and HTTP/server response scalars.
- Produces: `qualifiesForReadingSync`, `makeReadingFingerprint`, `classifyReadingSyncResult`, `advanceReadingSequence` used by queue/client/coordinator.

- [ ] **Step 1: Write the failing policy tests**

Create `test/reading_sync/ReadingSyncPolicyTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "reading_sync/ReadingSyncPolicy.h"

TEST(ReadingSyncPolicy, QualifiesAtAnyApprovedThreshold) {
  EXPECT_TRUE(qualifiesForReadingSync({180000, 20, 20, false}));
  EXPECT_TRUE(qualifiesForReadingSync({1000, 20, 21, false}));
  EXPECT_TRUE(qualifiesForReadingSync({1000, 99, 100, true}));
  EXPECT_FALSE(qualifiesForReadingSync({179999, 20, 20, false}));
  EXPECT_FALSE(qualifiesForReadingSync({1000, 21, 20, false}));
}

TEST(ReadingSyncPolicy, FingerprintExcludesTimestamp) {
  ReadingSyncMetadata a{1, 7, "book", "제목", "저자", 37, "2026-07-17T00:00:00Z", "", "", ""};
  ReadingSyncMetadata b = a;
  b.lastReadAt = "2026-07-17T12:00:00Z";
  EXPECT_EQ(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b.coverSha256 = std::string(64, 'a');
  b.coverMime = "image/jpeg";
  EXPECT_EQ(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b.progressPercent = 38;
  EXPECT_NE(makeReadingFingerprint(a), makeReadingFingerprint(b));
}

TEST(ReadingSyncPolicy, MapsHttpClassesWithoutDroppingRetryableWork) {
  EXPECT_EQ(ReadingSyncDisposition::DeletePending,
            classifyReadingSyncResult(200, ReadingSyncServerStatus::Accepted));
  EXPECT_EQ(ReadingSyncDisposition::DeletePending,
            classifyReadingSyncResult(200, ReadingSyncServerStatus::Stale));
  EXPECT_EQ(ReadingSyncDisposition::PauseAuthentication,
            classifyReadingSyncResult(401, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::DeleteTerminal,
            classifyReadingSyncResult(422, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::Retry,
            classifyReadingSyncResult(503, ReadingSyncServerStatus::Unknown));
}

TEST(ReadingSyncPolicy, AdvancesPastServerAndDetectsExhaustion) {
  EXPECT_EQ(43u, advanceReadingSequence(8, 42));
  EXPECT_EQ(9u, advanceReadingSequence(8, 7));
  EXPECT_EQ(0u, advanceReadingSequence(UINT32_MAX, UINT32_MAX));
}
```

Create `test/reading_sync/CMakeLists.txt`:

```cmake
add_executable(reading_sync_tests
  ReadingSyncPolicyTest.cpp
  ${REPO_ROOT}/src/reading_sync/ReadingSyncPolicy.cpp
)
target_include_directories(reading_sync_tests PRIVATE ${REPO_ROOT}/src)
target_link_libraries(reading_sync_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(reading_sync_tests)
```

Append `add_subdirectory(reading_sync)` to `test/CMakeLists.txt`.

- [ ] **Step 2: Run and confirm the missing-module failure**

Run: `cmake -S test -B build/test && cmake --build build/test --target reading_sync_tests`

Expected: FAIL because `reading_sync/ReadingSyncPolicy.h` does not exist.

- [ ] **Step 3: Implement the exact public types and rules**

Create `src/reading_sync/ReadingSyncTypes.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>

struct ReadingSyncMetadata {
  uint8_t schemaVersion = 1;
  uint32_t sequence = 0;
  std::string bookId;
  std::string title;
  std::string author;
  uint8_t progressPercent = 0;
  std::string lastReadAt;
  std::string isbn13;
  std::string coverSha256;
  std::string coverMime;
};

struct ReadingSyncSessionCandidate {
  uint32_t sessionMs;
  uint8_t startProgressPercent;
  uint8_t endProgressPercent;
  bool completedThisSession;
};

enum class ReadingSyncServerStatus : uint8_t { Unknown, Accepted, Duplicate, Stale };
enum class ReadingSyncDisposition : uint8_t { DeletePending, DeleteTerminal, PauseAuthentication, Retry };
```

Create `src/reading_sync/ReadingSyncPolicy.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include "ReadingSyncTypes.h"

bool qualifiesForReadingSync(const ReadingSyncSessionCandidate& candidate);
std::string makeReadingFingerprint(const ReadingSyncMetadata& metadata);
ReadingSyncDisposition classifyReadingSyncResult(int httpStatus, ReadingSyncServerStatus status);
uint32_t advanceReadingSequence(uint32_t nextSequence, uint32_t lastAcceptedSequence);
```

Create `src/reading_sync/ReadingSyncPolicy.cpp`:

```cpp
#include "ReadingSyncPolicy.h"
#include <climits>

bool qualifiesForReadingSync(const ReadingSyncSessionCandidate& c) {
  const int delta = static_cast<int>(c.endProgressPercent) - static_cast<int>(c.startProgressPercent);
  return c.sessionMs >= 180000u || delta >= 1 || c.completedThisSession;
}

std::string makeReadingFingerprint(const ReadingSyncMetadata& m) {
  return m.bookId + "\x1f" + m.title + "\x1f" + m.author + "\x1f" +
         std::to_string(m.progressPercent);
}

ReadingSyncDisposition classifyReadingSyncResult(const int code, const ReadingSyncServerStatus status) {
  if (code == 200 && status != ReadingSyncServerStatus::Unknown) return ReadingSyncDisposition::DeletePending;
  if (code == 400 || code == 413 || code == 422) return ReadingSyncDisposition::DeleteTerminal;
  if (code == 401 || code == 403) return ReadingSyncDisposition::PauseAuthentication;
  return ReadingSyncDisposition::Retry;
}

uint32_t advanceReadingSequence(const uint32_t next, const uint32_t accepted) {
  if (accepted == UINT32_MAX) return 0;
  const uint32_t serverNext = accepted + 1u;
  return next > serverNext ? next : serverNext;
}
```

- [ ] **Step 4: Run focused and complete tests**

Run: `cmake --build build/test --target reading_sync_tests && ctest --test-dir build/test --output-on-failure -j`

Expected: four policy tests and all prior tests pass.

- [ ] **Step 5: Commit the pure policy**

```bash
git add src/reading_sync/ReadingSyncTypes.h src/reading_sync/ReadingSyncPolicy.h src/reading_sync/ReadingSyncPolicy.cpp test/CMakeLists.txt test/reading_sync
git commit -m "feat(sync): define reading sync policy"
```

---

### Task 5: Persist the atomic queue and obfuscated device credential

**Files:**
- Create: `src/reading_sync/ReadingSyncQueue.h`
- Create: `src/reading_sync/ReadingSyncQueue.cpp`
- Create: `src/reading_sync/ReadingSyncCredentialStore.h`
- Create: `src/reading_sync/ReadingSyncCredentialStore.cpp`
- Modify: `test/reading_sync/ReadingSyncPolicyTest.cpp`

**Interfaces:**
- Consumes: `ReadingSyncMetadata`, fingerprint and sequence helpers from Task 4; `Storage`, `HalFile`, `ObfuscationUtils`.
- Produces: singleton macros `READING_SYNC_QUEUE`, `READING_SYNC_CREDENTIALS`; queue methods listed below. Later tasks never write queue JSON directly.

- [ ] **Step 1: Extend the failing state-transition tests**

Append to `ReadingSyncPolicyTest.cpp`:

```cpp
TEST(ReadingSyncPolicy, CoalescingKeepsOnlyNewestFingerprint) {
  ReadingSyncMetadata oldValue{1, 10, "book", "제목", "저자", 20, "2026-07-17T00:00:00Z", "", "", ""};
  ReadingSyncMetadata sameValue = oldValue;
  sameValue.sequence = 11;
  sameValue.lastReadAt = "2026-07-17T12:00:00Z";
  EXPECT_EQ(makeReadingFingerprint(oldValue), makeReadingFingerprint(sameValue));
  sameValue.progressPercent = 21;
  EXPECT_NE(makeReadingFingerprint(oldValue), makeReadingFingerprint(sameValue));
}
```

Before creating the queue headers, add a compile-only include in the test:

```cpp
#include "reading_sync/ReadingSyncQueue.h"
static_assert(ReadingSyncQueue::kSchemaVersion == 1);
```

- [ ] **Step 2: Run and confirm the missing queue header failure**

Run: `cmake --build build/test --target reading_sync_tests`

Expected: FAIL because `ReadingSyncQueue.h` does not exist.

- [ ] **Step 3: Implement the queue API and exact on-disk contract**

Create `ReadingSyncQueue.h` with this public surface:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include "ReadingSyncTypes.h"

struct ReadingCoverJob {
  std::string bookId;
  std::string sha256;
  std::string mime;
  std::string path;
};

class ReadingSyncQueue {
 public:
  static constexpr uint8_t kSchemaVersion = 1;
  static ReadingSyncQueue& getInstance();
  bool loadFromFile();
  bool enqueue(ReadingSyncMetadata metadata, const ReadingCoverJob* cover);
  const ReadingSyncMetadata* pending() const;
  const ReadingCoverJob* coverPending() const;
  bool applyServerResult(uint32_t requestSequence, uint32_t lastAcceptedSequence,
                         ReadingSyncServerStatus status, bool keepCover);
  bool dropTerminal(uint32_t requestSequence, const std::string& reason);
  void pauseAuthentication();
  void resumeAuthentication();
  bool authenticationPaused() const;
  bool isCorrupt() const;

 private:
  bool saveAtomic() const;
  uint32_t nextSequence_ = 1;
  bool hasPending_ = false;
  bool hasCover_ = false;
  bool authPaused_ = false;
  bool terminal_ = false;
  bool corrupt_ = false;
  std::string lastAcceptedFingerprint_;
  std::string terminalReason_;
  ReadingSyncMetadata pending_;
  ReadingCoverJob cover_;
};

#define READING_SYNC_QUEUE ReadingSyncQueue::getInstance()
```

Implement `ReadingSyncQueue.cpp` with `StaticJsonDocument<8192>`, exact paths
`/.crosspoint/reading_sync/queue.json`, `.tmp`, `.corrupt`, and these rules:

```cpp
// enqueue
if (corrupt_ || nextSequence_ == 0) return false;
const std::string nextFingerprint = makeReadingFingerprint(metadata);
if (hasPending_ && nextFingerprint == makeReadingFingerprint(pending_)) return true;
if (nextFingerprint == lastAcceptedFingerprint_) return true;
metadata.sequence = nextSequence_++;
pending_ = std::move(metadata);
hasPending_ = true;
if (cover != nullptr) { cover_ = *cover; hasCover_ = true; }
return saveAtomic();

// accepted, duplicate, stale
if (!hasPending_ || pending_.sequence != requestSequence) return false;
const uint32_t advanced = advanceReadingSequence(nextSequence_, lastAcceptedSequence);
if (advanced == 0) { terminal_ = true; terminalReason_ = "sequence_exhausted"; return saveAtomic(); }
nextSequence_ = advanced;
if (status == ReadingSyncServerStatus::Accepted || status == ReadingSyncServerStatus::Duplicate) {
  lastAcceptedFingerprint_ = makeReadingFingerprint(pending_);
}
hasPending_ = false;
pending_ = {};
if (!keepCover) { hasCover_ = false; cover_ = {}; }
return saveAtomic();
```

`saveAtomic()` must: ensure `/.crosspoint/reading_sync` and its `covers` child; open `.tmp`; serialize exactly `schemaVersion`, `nextSequence`, `lastAcceptedFingerprint`, `authPaused`, `terminal`, `terminalReason`, optional `pending`, optional `cover`; reject serialized size over 8192; flush; close; remove old target; rename `.tmp` to target. `dropTerminal()` stores one of `bad_request`, `payload_too_large`, `unprocessable`, clears the matching metadata and its unusable cover job, and preserves local ReadingStats. `loadFromFile()` must reject schema mismatch, invalid sequence, missing required pending fields, percent over 100, or file over 8192B; rename a bad file to `.corrupt`, set `corrupt_=true`, and never delete it.

Create the credential store with this exact public surface:

```cpp
class ReadingSyncCredentialStore {
 public:
  static ReadingSyncCredentialStore& getInstance();
  bool loadFromFile();
  bool setToken(const std::string& token);
  bool clearToken();
  bool hasToken() const;
  const std::string& tokenForRequest() const;
  std::string maskedStatus() const;
 private:
  std::string token_;
};
#define READING_SYNC_CREDENTIALS ReadingSyncCredentialStore::getInstance()
```

Accept only `^rd1_[A-Za-z0-9_-]{43}$`. Persist only the two keys represented by `{"schemaVersion":1,"tokenObfuscated":"AQIDBA=="}` at `/.crosspoint/reading_sync/config.json` using `ObfuscationUtils::obfuscate()`/`deobfuscate()`. `maskedStatus()` returns `설정됨 (rd1_…마지막4자)` or `설정 안 됨`; no method returns obfuscated JSON to the web layer.

- [ ] **Step 4: Verify host policy, Korean simulator, and SD behavior**

Run:

```bash
cmake --build build/test --target reading_sync_tests
ctest --test-dir build/test --output-on-failure -j
cmake --build simulator/build_ko -j
```

Expected: host tests pass and the simulator compiles the ArduinoJson/HalStorage implementation. In simulator storage, enqueue twice with equal fingerprint and verify sequence does not advance; enqueue progress+1 and verify one pending with the newer sequence; truncate `queue.json` and verify `.corrupt` preservation and sync stop.

- [ ] **Step 5: Commit durable state**

```bash
git add src/reading_sync/ReadingSyncQueue.h src/reading_sync/ReadingSyncQueue.cpp src/reading_sync/ReadingSyncCredentialStore.h src/reading_sync/ReadingSyncCredentialStore.cpp test/reading_sync/ReadingSyncPolicyTest.cpp
git commit -m "feat(sync): persist reading queue and device token"
```

---

### Task 6: Stream and hash the original EPUB cover

**Files:**
- Modify: `lib/Epub/Epub.h`
- Modify: `lib/Epub/Epub.cpp`
- Create: `src/reading_sync/EpubOriginalCoverSource.h`
- Create: `src/reading_sync/EpubOriginalCoverSource.cpp`
- Create: `test/reading_sync/EpubCoverPolicyTest.cpp`
- Modify: `test/reading_sync/CMakeLists.txt`

**Interfaces:**
- Consumes: loaded `Epub`, `getItemSize`, `readItemContentsToStream`, mbedTLS SHA-256.
- Produces: `bool stageOriginalEpubCover(const Epub&, const std::string& bookId, ReadingCoverJob&)`; success means an immutable temp JPG/PNG and lowercase hash exist, failure leaves no upload job.

- [ ] **Step 1: Write failing magic/size tests**

Create `EpubCoverPolicyTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "reading_sync/EpubOriginalCoverSource.h"

TEST(EpubCoverPolicy, AcceptsOnlyJpegAndPngMagic) {
  const uint8_t jpg[] = {0xff, 0xd8, 0xff, 0xe0};
  const uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  const uint8_t bmp[] = {0x42, 0x4d, 0x00, 0x00};
  EXPECT_EQ("image/jpeg", detectReadingCoverMime(jpg, sizeof(jpg)));
  EXPECT_EQ("image/png", detectReadingCoverMime(png, sizeof(png)));
  EXPECT_TRUE(detectReadingCoverMime(bmp, sizeof(bmp)).empty());
}

TEST(EpubCoverPolicy, EnforcesInclusiveTwoMegabyteLimit) {
  EXPECT_FALSE(isReadingCoverSizeAllowed(0));
  EXPECT_TRUE(isReadingCoverSizeAllowed(1));
  EXPECT_TRUE(isReadingCoverSizeAllowed(2097152));
  EXPECT_FALSE(isReadingCoverSizeAllowed(2097153));
}
```

Add the test file to `reading_sync_tests` in its CMake file.

- [ ] **Step 2: Run and confirm missing cover helpers**

Run: `cmake -S test -B build/test && cmake --build build/test --target reading_sync_tests`

Expected: FAIL because `EpubOriginalCoverSource.h` is absent.

- [ ] **Step 3: Implement bounded original-cover staging**

Add to `Epub`:

```cpp
const std::string& getOriginalCoverItemHref() const;
```

and implement it by returning `bookMetadataCache->coreMetadata.coverItemHref` when loaded, otherwise a static empty string.

Create `EpubOriginalCoverSource.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "ReadingSyncQueue.h"
class Epub;

inline bool isReadingCoverSizeAllowed(const size_t size) {
  return size >= 1 && size <= 2097152;
}

inline std::string detectReadingCoverMime(const uint8_t* prefix, const size_t size) {
  if (size >= 3 && prefix[0] == 0xff && prefix[1] == 0xd8 && prefix[2] == 0xff) return "image/jpeg";
  static constexpr uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  if (size >= sizeof(png) && std::equal(std::begin(png), std::end(png), prefix)) return "image/png";
  return {};
}

bool stageOriginalEpubCover(const Epub& epub, const std::string& bookId, ReadingCoverJob& out);
```

Add `<algorithm>` and `<iterator>` includes to the header so the inline pure helpers compile in the host test. Keep all Epub/Hal/mbedTLS work in `EpubOriginalCoverSource.cpp`; native tests never link the device-only staging function.

The implementation must use a `Print` sink that writes each incoming chunk to `/.crosspoint/reading_sync/covers/.tmp`, updates `mbedtls_sha256_context`, captures the first eight bytes, and rejects total bytes over 2,097,152. Call `epub.readItemContentsToStream(href, sink, 1024)` once. After close, validate actual count, magic, and `epub.getItemSize()` agreement; compute 64 lowercase hex characters; rename to `/.crosspoint/reading_sync/covers/{sha256}.jpg` or `.png`; fill `bookId`, `sha256`, `mime`, `path`. Remove `.tmp` on every failure. Never use `getCoverBmpPath()`.

- [ ] **Step 4: Run native, simulator, and fixture checks**

Run:

```bash
cmake --build build/test --target reading_sync_tests
ctest --test-dir build/test --output-on-failure -j
cmake --build simulator/build_ko -j
```

Expected: policy tests pass and simulator builds. Open one JPG-cover EPUB, one PNG-cover EPUB, one no-cover EPUB, and one generated 2,097,153B cover fixture; only the first two create immutable staged files and their `shasum -a 256` values match the queue.

- [ ] **Step 5: Commit cover streaming**

```bash
git add lib/Epub/Epub.h lib/Epub/Epub.cpp src/reading_sync/EpubOriginalCoverSource.h src/reading_sync/EpubOriginalCoverSource.cpp test/reading_sync
git commit -m "feat(sync): stage original EPUB covers"
```

---

### Task 7: Add status-aware HTTPS and the reading API client

**Files:**
- Modify: `src/network/HttpDownloader.h`
- Modify: `src/network/HttpDownloader.cpp`
- Create: `src/reading_sync/ReadingSyncClient.h`
- Create: `src/reading_sync/ReadingSyncClient.cpp`
- Create: `src/reading_sync/ReadingSyncResponseValidation.h`
- Create: `src/reading_sync/ReadingSyncResponseValidation.cpp`
- Create: `test/reading_sync/ReadingSyncClientPolicyTest.cpp`
- Modify: `test/reading_sync/CMakeLists.txt`

**Interfaces:**
- Consumes: queue metadata/cover job and bearer token.
- Produces: `HttpResult postJsonWithStatus`, `HttpResult putFileWithStatus`, `validateReadingSyncResponse`, `ReadingSyncResponse sync`, `validate`, `uploadCover`. Existing `postJson` callers keep working.

- [ ] **Step 1: Write failing response validation tests**

Create `ReadingSyncClientPolicyTest.cpp` with accepted, duplicate, stale, missing-field, and mismatched-sequence wire fixtures. JSON decoding stays device-side; the host test covers the pure semantic validator:

```cpp
#include <gtest/gtest.h>
#include "reading_sync/ReadingSyncResponseValidation.h"

TEST(ReadingSyncResponseValidation, RequiresEveryFieldAndMatchingSequence) {
  ReadingSyncWireResponse wire{true, "accepted", true, 42, true, 42, true, true};
  ReadingSyncResponse response;
  EXPECT_TRUE(validateReadingSyncResponse(wire, 42, response));
  EXPECT_EQ(ReadingSyncServerStatus::Accepted, response.status);
  EXPECT_EQ(42u, response.lastAcceptedSequence);
  EXPECT_TRUE(response.coverRequired);
  EXPECT_FALSE(validateReadingSyncResponse(wire, 41, response));
  wire.hasLastAcceptedSequence = false;
  EXPECT_FALSE(validateReadingSyncResponse(wire, 42, response));
}

TEST(ReadingSyncResponseValidation, EnforcesDuplicateAndStaleSemantics) {
  ReadingSyncResponse response;
  ReadingSyncWireResponse duplicate{true, "duplicate", true, 42, true, 42, true, false};
  EXPECT_TRUE(validateReadingSyncResponse(duplicate, 42, response));
  ReadingSyncWireResponse stale{true, "stale", true, 41, true, 42, true, false};
  EXPECT_TRUE(validateReadingSyncResponse(stale, 41, response));
  EXPECT_EQ(ReadingSyncServerStatus::Stale, response.status);
  stale.coverRequired = true;
  EXPECT_FALSE(validateReadingSyncResponse(stale, 41, response));
  stale.status = "other";
  EXPECT_FALSE(validateReadingSyncResponse(stale, 41, response));
}
```

Add this source to `reading_sync_tests` in `test/reading_sync/CMakeLists.txt`:

```cmake
${REPO_ROOT}/src/reading_sync/ReadingSyncResponseValidation.cpp
```

Create `ReadingSyncResponseValidation.h` with the compile contract that makes the test meaningful:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include "ReadingSyncTypes.h"

struct ReadingSyncResponse {
  ReadingSyncServerStatus status = ReadingSyncServerStatus::Unknown;
  uint32_t sequence = 0;
  uint32_t lastAcceptedSequence = 0;
  bool coverRequired = false;
};

struct ReadingSyncWireResponse {
  bool hasStatus = false;
  std::string status;
  bool hasSequence = false;
  uint64_t sequence = 0;
  bool hasLastAcceptedSequence = false;
  uint64_t lastAcceptedSequence = 0;
  bool hasCoverRequired = false;
  bool coverRequired = false;
};

bool validateReadingSyncResponse(const ReadingSyncWireResponse& wire,
                                 uint32_t expectedSequence, ReadingSyncResponse& out);
```

- [ ] **Step 2: Run and confirm the client module is missing**

Run: `cmake --build build/test --target reading_sync_tests`

Expected: FAIL with an undefined reference to `validateReadingSyncResponse` because the implementation does not exist.

- [ ] **Step 3: Implement the transport without breaking current callers**

Add to `HttpDownloader.h`:

```cpp
struct HttpResult {
  DownloadError error = HTTP_ERROR;
  int statusCode = 0;
};

static HttpResult postJsonWithStatus(const std::string& url, const std::string& payload,
                                     const std::string& bearerToken,
                                     const std::function<bool(Stream&)>& onResponse,
                                     int timeoutMs = 15000, bool* cancelFlag = nullptr);
static HttpResult putFileWithStatus(const std::string& url, const std::string& path,
                                   const std::string& mime, const std::string& sha256,
                                   const std::string& bearerToken,
                                   const std::function<bool(Stream&)>& onResponse,
                                   int timeoutMs = 15000, bool* cancelFlag = nullptr,
                                   size_t chunkSize = 1024);
```

Refactor the existing POST helper to preserve the HTTP code even when non-200. Keep `postJson` as a wrapper returning true only for transport OK, status 200, and successful response consumer. PUT opens the staged `HalFile`, sets `Content-Type`, `Content-Length`, `X-Cover-SHA256`, and streams 1,024B chunks; it checks `cancelFlag` before each read/write, never logs bearer/body/title/author, and closes the file/client on every return.

Create `ReadingSyncResponseValidation.cpp`:

```cpp
#include "ReadingSyncResponseValidation.h"
#include <climits>

bool validateReadingSyncResponse(const ReadingSyncWireResponse& wire,
                                 const uint32_t expectedSequence, ReadingSyncResponse& out) {
  if (!wire.hasStatus || !wire.hasSequence || !wire.hasLastAcceptedSequence || !wire.hasCoverRequired) return false;
  if (wire.sequence == 0 || wire.sequence > UINT32_MAX || wire.lastAcceptedSequence > UINT32_MAX) return false;
  if (wire.sequence != expectedSequence) return false;

  ReadingSyncServerStatus status = ReadingSyncServerStatus::Unknown;
  if (wire.status == "accepted") status = ReadingSyncServerStatus::Accepted;
  else if (wire.status == "duplicate") status = ReadingSyncServerStatus::Duplicate;
  else if (wire.status == "stale") status = ReadingSyncServerStatus::Stale;
  else return false;

  if ((status == ReadingSyncServerStatus::Accepted || status == ReadingSyncServerStatus::Duplicate) &&
      wire.lastAcceptedSequence < wire.sequence) return false;
  if (status == ReadingSyncServerStatus::Stale &&
      (wire.lastAcceptedSequence <= wire.sequence || wire.coverRequired)) return false;

  out.status = status;
  out.sequence = static_cast<uint32_t>(wire.sequence);
  out.lastAcceptedSequence = static_cast<uint32_t>(wire.lastAcceptedSequence);
  out.coverRequired = wire.coverRequired;
  return true;
}
```

Create `ReadingSyncClient.h` with:

```cpp
#include "ReadingSyncResponseValidation.h"
class ReadingSyncQueue;
class ReadingSyncCredentialStore;

class ReadingSyncClient {
 public:
  static ReadingSyncClient& getInstance();
  HttpDownloader::HttpResult validate(const std::string& token, bool* cancelFlag);
  HttpDownloader::HttpResult sync(const ReadingSyncMetadata& metadata, const std::string& token,
                                  ReadingSyncResponse& response, bool* cancelFlag);
  HttpDownloader::HttpResult uploadCover(const ReadingCoverJob& cover, const std::string& token,
                                         bool* cancelFlag);
  void performPendingSync(ReadingSyncQueue& queue, ReadingSyncCredentialStore& credentials,
                          bool* cancelFlag);
};
#define READING_SYNC_CLIENT ReadingSyncClient::getInstance()
```

Serialize metadata into a `StaticJsonDocument<8192>` and reject an output of 8192B or more. Use exactly:

```text
POST https://api.kimtoma.com/v1/reading/sync
POST https://api.kimtoma.com/v1/reading/sync?validateOnly=1
PUT  https://api.kimtoma.com/v1/reading/books/{url-encoded-bookId}/cover
```

In `ReadingSyncClient.cpp`, deserialize the response stream with ArduinoJson into `ReadingSyncWireResponse`, setting each `has*` flag from `JsonVariant::is<T>()`, then call `validateReadingSyncResponse`. This keeps malformed/missing fields distinct from zero/false and keeps the semantic validator host-testable. `performPendingSync` owns saved-Wi-Fi connection, the 8-second connect limit, ReadingStats network-memory release/reload, metadata/cover requests, queue disposition, complete HTTP/file closure, `WiFi.disconnect(false)`, `WIFI_OFF`, and `esp_wifi_deinit()` on every exit.

- [ ] **Step 4: Verify policy and both builds**

Run:

```bash
cmake --build build/test --target reading_sync_tests
ctest --test-dir build/test --output-on-failure -j
cmake --build simulator/build_ko -j
pio run -e gh_release_ko
```

Expected: parser fixtures pass, existing HTTP callers still compile, KO firmware builds.

- [ ] **Step 5: Commit transport/client**

```bash
git add src/network/HttpDownloader.h src/network/HttpDownloader.cpp src/reading_sync/ReadingSyncClient.h src/reading_sync/ReadingSyncClient.cpp src/reading_sync/ReadingSyncResponseValidation.h src/reading_sync/ReadingSyncResponseValidation.cpp test/reading_sync
git commit -m "feat(sync): add kimtoma reading API client"
```

---

### Task 8: Enqueue after local save and run one-shot sync from Home

**Files:**
- Create: `src/reading_sync/ReadingSyncCoordinator.h`
- Create: `src/reading_sync/ReadingSyncCoordinator.cpp`
- Modify: `src/main.cpp`
- Modify: `src/activities/reader/EpubReaderActivity.cpp`
- Modify: `src/activities/home/HomeActivity.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Consumes: Tasks 4–7, loaded `Epub` only during enqueue, existing Wi-Fi manager.
- Produces: `READING_SYNC.enqueueAfterSession(snapshot, book, epub)`, `startOneShotIfPending()`, `requestCancel()`, `isRunning()`.

- [ ] **Step 1: Add a failing compile contract**

Add a compile-only block to `ReadingSyncPolicyTest.cpp`:

```cpp
#include "reading_sync/ReadingSyncCoordinator.h"
static_assert(ReadingSyncCoordinator::kWifiTimeoutMs == 8000);
static_assert(ReadingSyncCoordinator::kHttpTimeoutMs == 15000);
```

Run: `cmake --build build/test --target reading_sync_tests`

Expected: FAIL because the coordinator header is missing.

- [ ] **Step 2: Create the coordinator API**

Create `ReadingSyncCoordinator.h`:

```cpp
#pragma once
#include <atomic>
#include "ReadingStatsStore.h"
class Epub;

class ReadingSyncCoordinator {
 public:
  static constexpr uint32_t kWifiTimeoutMs = 8000;
  static constexpr uint32_t kHttpTimeoutMs = 15000;
  static ReadingSyncCoordinator& getInstance();
  bool loadFromFile();
  bool enqueueAfterSession(const ReadingSessionSnapshot& snapshot,
                           const ReadingBookStats& book, const Epub& epub);
  void startOneShotIfPending();
  void requestManualRetry();
  void requestCancel();
  bool isRunning() const;
 private:
  static void taskEntry(void* context);
  std::atomic_bool cancelRequested_{false};
  std::atomic_bool manualRetryRequested_{false};
  std::atomic_bool running_{false};
};
#define READING_SYNC ReadingSyncCoordinator::getInstance()
```

- [ ] **Step 3: Implement the exact lifecycle and activity hooks**

`enqueueAfterSession` returns without mutation unless qualifying, resolves the content ID with `BookIdentity::resolveStableBookId()`, replaces a `legacy:` fallback with lowercase SHA-256 so no path leaves the device, fills metadata from `ReadingBookStats`, converts a valid epoch `lastReadAt` to UTC ISO 8601 and otherwise omits it, stages cover while the supplied `Epub` is alive, and calls queue enqueue only after `READING_STATS.endSession()` has completed. It never stores file path or chapter title in metadata.

`startOneShotIfPending` requires pending work, token, unpaused auth, and `running_ == false`; it creates one 4096-byte FreeRTOS task and sets `running_` before creation to prevent duplicates. `requestManualRetry()` clears no queue state; it marks a retry trigger that `HomeActivity::onEnter()` consumes, and if Home is already active it calls `startOneShotIfPending()`. The task calls `READING_SYNC_CLIENT.performPendingSync(READING_SYNC_QUEUE, READING_SYNC_CREDENTIALS, &cancelRequested_)`, then clears `running_` and deletes itself. `ReadingSyncClient::performPendingSync` follows exactly:

```text
connect saved Wi-Fi for at most 8,000ms
release ReadingStats network memory before TLS and remember whether reload is required
POST metadata with 15,000ms timeout
200 accepted/duplicate/stale: atomically apply lastAcceptedSequence and metadata deletion
when coverRequired and cover job matches: PUT cover; delete staged file/job only on 200
cover 400/413/422: drop only that terminal cover job and preserve accepted metadata/current
cover 401/403: keep cover job and persist authPaused; cover 429/5xx/timeout/network: keep cover job
400/413/422: terminal-delete matching metadata, retain local ReadingStats
401/403: keep queue and persist authPaused
429/5xx/timeout/network/cancel: keep queue
close HTTP/file handles and fully deinitialize Wi-Fi on every exit
reload ReadingStats after Wi-Fi teardown when it was released
set running=false and vTaskDelete(nullptr)
```

In `EpubReaderActivity::onExit()`, immediately after existing `READING_STATS.endSession()` and before `section.reset()`/`epub.reset()`, add under `ENABLE_KIMTOMA_READING_SYNC`:

```cpp
const ReadingSessionSnapshot& snapshot = READING_STATS.getLastSessionSnapshot();
if (snapshot.valid && epub) {
  const ReadingBookStats* book = READING_STATS.findBook(snapshot.bookId);
  if (book) READING_SYNC.enqueueAfterSession(snapshot, *book, *epub);
}
```

In `HomeActivity::onEnter()`, call `READING_SYNC.startOneShotIfPending()` only after `requestUpdate()`. In `HomeActivity::onExit()`, call `READING_SYNC.requestCancel()` before releasing the home cover buffer. In `main.cpp`, load queue/credentials after `READING_STATS.loadFromFile()`.

Add `-DENABLE_KIMTOMA_READING_SYNC` only to `gh_release_ko` and `gh_release_ko_rc`.

- [ ] **Step 4: Verify no reader-owned object crosses the task boundary**

Run:

```bash
cmake --build build/test --target reading_sync_tests
ctest --test-dir build/test --output-on-failure -j
cmake --build simulator/build_ko -j
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
```

Expected: all tests/builds pass and size remains below hard maximum. Inspect the task capture list: it contains only singleton/static state and scalar queue paths, never `Epub*`, `shared_ptr<Epub>`, `GfxRenderer*`, framebuffer, or Activity pointer.

- [ ] **Step 5: Commit lifecycle integration**

```bash
git add src/reading_sync/ReadingSyncCoordinator.h src/reading_sync/ReadingSyncCoordinator.cpp src/main.cpp src/activities/reader/EpubReaderActivity.cpp src/activities/home/HomeActivity.cpp platformio.ini
git commit -m "feat(sync): upload reading state after reader exit"
```

---

### Task 9: Add safe web settings and validate-only UX

**Files:**
- Modify: `src/network/CrossPointWebServer.cpp`
- Modify: `src/network/html/SettingsPage.html`
- Modify: `lib/I18n/translations/english.yaml`
- Modify: `lib/I18n/translations/korean.yaml`
- Modify: generated HTML/i18n outputs through existing build scripts.

**Interfaces:**
- Consumes: credential store and client validate call.
- Produces: masked status GET, token set/delete POST, connection test POST. No response ever contains token or obfuscated value.

- [ ] **Step 1: Add failing route contract checks**

Extend `scripts/verify_ko_release.py` before the build checks:

```python
web = (ROOT / "src/network/CrossPointWebServer.cpp").read_text(encoding="utf-8")
html = (ROOT / "src/network/html/SettingsPage.html").read_text(encoding="utf-8")
for route in ("/api/reading-sync/status", "/api/reading-sync/token", "/api/reading-sync/test", "/api/reading-sync/retry"):
    if route not in web or route not in html:
        fail(f"missing reading sync settings route {route}")
if "tokenObfuscated" in web or "tokenForRequest()" in html:
    fail("credential material exposed to settings response")
```

Run: `python3 scripts/verify_ko_release.py`

Expected: FAIL on the first missing route.

- [ ] **Step 2: Implement four exact endpoints**

Register only under `ENABLE_KIMTOMA_READING_SYNC`:

```text
GET    /api/reading-sync/status -> {"configured":true|false,"masked":"설정됨 (마스킹됨)"|"설정 안 됨","authPaused":true|false,"queueCorrupt":true|false}
POST   /api/reading-sync/token  body {"token":"rd1_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"} -> 204 or 422 {"error":"invalid_token"}
DELETE /api/reading-sync/token  -> 204, clears token and auth pause, and preserves ReadingStats and pending queue
POST   /api/reading-sync/test   -> validateOnly request; 200 {"ok":true}, 401/403 {"ok":false,"error":"authentication_failed"}, other failure 503 {"ok":false,"error":"temporarily_unavailable"}
POST   /api/reading-sync/retry  -> 202 {"scheduled":true}; calls requestManualRetry and leaves queue/sequence unchanged
```

Limit token body to 256B. Do not log request JSON. GET uses only `maskedStatus()` and booleans. Successful token set calls `resumeAuthentication()`.

- [ ] **Step 3: Add the Korean-first form**

Add a `kimtoma.com 독서 연동` section with password input, `저장`, `토큰 삭제`, `연결 테스트`, `지금 동기화`, and this exact help copy:

```text
기기 전용 토큰은 난독화하여 저장됩니다. 연결 실패가 독서 기록 저장을 막지는 않습니다.
```

The page initially calls status GET, never fills the input from stored state, clears the input after POST, and disables buttons while a request is active. Add equivalent English strings so non-KO generation remains complete.

- [ ] **Step 4: Verify generated assets and masked behavior**

Run:

```bash
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
rg -n "rd1_[A-Za-z0-9_-]{20,}" .pio/build/gh_release_ko src/network/html
```

Expected: build/verifier pass and the final search returns no token-like fixture. In simulator, status never includes `token`, `tokenObfuscated`, or full suffix beyond four characters; invalid input returns 422; validate-only does not change queue sequence.

- [ ] **Step 5: Commit settings UX**

```bash
git add src/network/CrossPointWebServer.cpp src/network/html/SettingsPage.html lib/I18n/translations/english.yaml lib/I18n/translations/korean.yaml
git commit -m "feat(sync): add protected reading sync settings"
```

---

### Task 10: Wire CI gates, document operations, and verify real X4 behavior

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `.github/workflows/release.yml`
- Modify: `.github/workflows/nightly.yml`
- Modify: `.github/workflows/sync-build.yml`
- Create: `docs/engineering/kimtoma-reading-sync.md`
- Modify: `docs/engineering/index.md`

**Interfaces:**
- Consumes: complete KO firmware and the platform API from the companion plan.
- Produces: release-blocking automated gates plus a reproducible physical-device acceptance record.

- [ ] **Step 1: Add CI commands before release packaging**

Add these exact commands to CI after native tests and to every workflow that builds `gh_release_ko` before copying `firmware-ko.bin`:

```bash
python3 -m unittest lib/EpdFont/scripts/test_build_ko_charset.py -v
python3 scripts/verify_ko_release.py
```

The workflow must upload `.pio/build/gh_release_ko/firmware.map`, `firmware.bin`, and the verifier output when the KO gate fails.

- [ ] **Step 2: Write the operation document with fixed commands**

Document:

```bash
cmake -S test -B build/test
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j
cmake -S simulator -B simulator/build_ko -DSIMULATOR_KOREAN_VERSION=ON
cmake --build simulator/build_ko -j
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
```

Also document token entry/removal, `validateOnly`, queue/corrupt paths, 200/400/401/403/413/422/429/5xx behavior, no-cover behavior, and rollback to the prior `firmware-ko.bin`. State explicitly that token storage is obfuscation, not encryption.

- [ ] **Step 3: Run the complete local gate**

Run the six commands above plus:

```bash
git diff --check
git status --short
```

Expected: all tests and builds pass, verifier reports size at or under 6,029,312B, and status shows only intended files. If size is over 5,898,240B but within the hard gate, pause for explicit user acceptance before flashing.

- [ ] **Step 4: Perform the physical X4 acceptance matrix**

After the companion platform plan has a staged API and a device token, flash `firmware-ko.bin` and record:

```text
8/10/12pt: random KS X 1001 title, author, path, and body render
14pt: modern Hangul plus education Hanja sample render
16/18pt: UI strings render; unsupported body glyph behavior matches documented tier
Korean source spaces: horizontal and vertical EPUB samples preserve real word spaces
session 179,999ms with 0pp: no queue
session 180,000ms: one pending
session +1pp and completion: one coalesced latest pending each
offline and API 5xx: local stats remain and queue remains
duplicate/stale: metadata pending clears; stale advances nextSequence
401/403: queue remains and auth pauses until token update
JPG/PNG/no-cover/over-2MB: only supported bounded originals upload
home exit during request: cancel, Wi-Fi teardown, no Activity/Epub access
10 consecutive syncs: free heap stays above 50KB and has no downward trend
site current updates after one qualifying real-book session
```

Capture serial heap lines and the final API response with token/header redacted. Do not install if hard size or 50KB heap gate fails.

- [ ] **Step 5: Commit CI/docs and stop before production release publication**

```bash
git add .github/workflows docs/engineering/kimtoma-reading-sync.md docs/engineering/index.md
git commit -m "docs(sync): gate and operate CrossMux KR reading sync"
```

Production OTA publication is a separate external-state checkpoint. Present build hash, binary size, headroom, X4 heap minimum, 10-run trend, API result, and rollback image to the user before publishing a release.

---

## Final Verification Gate

Run from a clean implementation worktree:

```bash
python3 -m unittest lib/EpdFont/scripts/test_build_ko_charset.py -v
cmake -S test -B build/test
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j
cmake -S simulator -B simulator/build_ko -DSIMULATOR_KOREAN_VERSION=ON
cmake --build simulator/build_ko -j
pio run -e gh_release
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
git diff --check
```

Expected final state: all automated gates pass; normal release regression build passes; KO binary is under the hard gate; the physical X4 matrix is recorded; credentials are absent from logs/artifacts; release publication still waits at the explicit production checkpoint.

## Self-Review Record

- Spec coverage: product scope and KO service exclusions map to Tasks 1–3; font tiers/cache 76 to Tasks 1–2; exact size budget to Tasks 3/10; local-first session/queue/sequence to Tasks 4–5/8; original cover to Task 6; API/token/status policy to Tasks 7–9; heap, hardware, CI, rollback to Task 10. No approved requirement is left without a task.
- Cross-contract check: API base, four endpoints, `X-Cover-SHA256`, 8KB/2MB limits, 8s/15s timeouts, accepted/duplicate/stale, `lastAcceptedSequence`, `coverRequired`, ISO `lastReadAt`, and public-safe book ID match the platform plan.
- Type consistency: `ReadingSyncMetadata`, `ReadingCoverJob`, `ReadingSyncServerStatus`, `ReadingSyncResponse`, queue/coordinator/client methods use the same names and scalar ranges in all later tasks. The response semantic validator is separated from ArduinoJson so its native test links cleanly; cover magic/size helpers are header-inline for the same reason.
- Persistence consistency: every queue/config/cover path is under `/.crosspoint/reading_sync/`; fingerprint is exactly book ID + title + author + progress and excludes time/ISBN/cover; accepted fingerprint and terminal reason are persisted atomically with sequence state.
- Placeholder scan: `rg` found no unfinished markers, deferred implementation phrases, vague error/edge-case instructions, or fake token substitutions. Markdown fence count is even and `git diff --check` passes.
- Scope correction during review: the plan now preserves persisted KOReader enum indices while removing the visible/action path, clears auth pause on unlink, includes manual retry scheduling, distinguishes terminal cover errors from accepted metadata, and never stages an unrelated worktree file.
