# CrossMux KR 펌웨어 설계

**날짜:** 2026-07-17
**상태:** 승인된 내용을 문서화함 · 사용자 문서 검토 대기
**대상 기기:** Xteink X4
**기준 환경:** `ryokun6/crossmux` `main`의 PlatformIO `gh_release_ko`

## 1. 제품 정의

CrossMux KR은 게임이나 범용 클라우드 기능을 늘리는 SKU가 아니라 한국어 장문 독서와 `kimtoma.com` 최신 읽기 연동에 집중하는 개인용 펌웨어다.

보존할 기능:

- EPUB/TXT/XTC 등 기존 리더와 라이브러리·최근 책·로컬 독서 통계
- 파일 전송, 저장된 Wi-Fi, 일반 OPDS 서버 추가·검색
- SD 카드 글꼴과 일반 설정
- OTA와 SD 카드 펌웨어 복구
- X3/X4 공용 HAL 경계와 기존 저메모리 규칙

한국어 SKU에서 제외할 기능:

- AirPage 화면과 MQTT 실시간 푸시
- ryOS Cloud/KOReader 클라우드 동기화 클라이언트, 전용 자격 증명 UI, 기본 `os.ryo.lu/api/kosync`
- `ryOS Books` 기본 OPDS 카탈로그 자동 등록과 서비스 전용 문구
- 한국어 SKU에서 쓰지 않는 ryOS 서비스 브랜딩. 라이선스와 원저작자 표시는 유지한다.

2048·스도쿠 등 게임 소스는 이미 모든 빌드에서 제외된다(`platformio.ini:46-60`). 따라서 해당 항목은 이번 작업의 절감 용량으로 계산하지 않는다. 일반 OPDS 저장소 자체는 유지하되, 새 설치에 `ryOS Books`를 넣는 `seedDefaultServer()` 동작만 한국어 SKU에서 끈다(`src/OpdsServerStore.cpp:14-17`, `src/OpdsServerStore.cpp:24-41`, `src/OpdsServerStore.cpp:65-66`).

## 2. 한국어 글꼴과 EPUB 조판

### 2.1 글꼴 계층

한국어 내장 글꼴은 Resource Han Rounded KR Regular를 사용한다. 기존 `gh_release_ko`는 14pt에 전체 현대 한글 11,172자와 교육용 한자 1,800자를 넣고 다른 크기는 UI 문자만 넣는다(`docs/engineering/japanese-korean-build.md:45-59`). 이를 아래처럼 바꾼다.

| 크기 | 문자 범위 | 용도 |
| --- | --- | --- |
| 8/10/12pt | KS X 1001의 공통 한글 음절 2,350자 + 현대 자모 + `korean.yaml` 필수 문자 | 작은 본문·목록·메타데이터 |
| 14pt | 현대 한글 음절 11,172자 + 교육용 한자 1,800자 + 현대 자모 + EPUB 기호 + UI 문자 | 기본 본문 |
| 16/18pt | 현대 자모 + `korean.yaml` 필수 문자 | UI 전용 |

구현은 [PR #18](https://github.com/ryokun6/crossmux/pull/18)의 문자 풀·생성 스크립트·생성 헤더를 기준으로 하되 구현 시작 시 최신 `main`에 다시 적용한다. PR의 2026-07-17 측정값은 5,687,488바이트에서 6,301,872바이트로 614,384바이트 증가한 상태다. 이 수치는 용량 계획의 입력일 뿐 최종 합격값이 아니다.

글꼴 재생성은 기존 `build-ko-builtin-fonts.sh` 경로와 생성 헤더 커밋 규칙을 따른다(`docs/engineering/japanese-korean-build.md:61-88`). 모든 크기에서 `korean.yaml` 문자를 강제 포함하고, 고대·옛한글 자모는 v1 범위에서 제외한다.

### 2.2 원문 공백 보존

[PR #19](https://github.com/ryokun6/crossmux/pull/19)의 원문 공백 보존 로직을 적용한다. CJK 문자 사이의 자동 조판 규칙은 유지하되, EPUB 소스에 실제 공백이 있으면 한국어 단어 경계로 보존한다.

파서 결과가 달라지므로 한국어 EPUB section cache 버전을 반드시 올린다. 이전 캐시를 재사용해 수정 전 토큰 흐름이 남지 않도록 KO 캐시 버전은 다른 flavor와 구분한다. 캐시 버전 변경은 `docs/engineering/cache-management.md`와 `docs/file-formats.md`에도 함께 기록한다.

### 2.3 합격 검사

- KS X 1001 2,350자 전체가 8/10/12pt 헤더에 들어 있는지 생성 단계에서 검사한다.
- `korean.yaml`의 모든 문자가 8/10/12/14/16/18pt에 존재하는지 검사한다.
- 공백 유무, 한글+라틴, 숫자, 문장부호, 가로쓰기/세로쓰기 표본 EPUB을 native test와 simulator에서 확인한다.
- 실제 X4에서 임의 한국어 제목·저자·경로와 본문을 각 크기로 연다.

## 3. 플래시 예산과 릴리스 게이트

X4는 두 개의 `0x640000` OTA app slot을 사용한다(`partitions.csv:4-5`). 한 슬롯은 6,553,600바이트다.

| 기준 | 남겨야 할 여유 | 허용되는 최대 `firmware.bin` |
| --- | ---: | ---: |
| 하드 게이트 | 512KiB = 524,288B | 6,029,312B |
| 목표 | 640KiB = 655,360B | 5,898,240B |

PR #18의 6,301,872바이트를 그대로 사용하면 여유가 251,728바이트뿐이다. 따라서 하드 게이트까지 최소 272,560바이트, 목표까지 403,632바이트를 실제 링크 결과에서 줄여야 한다.

절감 순서는 다음과 같다.

1. KO 전용 compile flag와 source filter로 AirPage 관련 소스를 제외한다. 현재 AirPage는 외부 HTTPS 서비스와 익명 MQTT broker를 사용한다(`src/activities/apps/standby/AirPageFace.cpp:23-46`).
2. KO에서 AirPage가 사라지면 PubSubClient가 실제 링크에서 빠졌는지 `.map`으로 확인한다. 의존성 선언만 지운 것을 절감으로 계산하지 않는다(`platformio.ini:74-85`).
3. KOReader/ryOS Cloud 네트워크 클라이언트와 전용 설정·credential 경로를 KO에서 제외한다. 콘텐츠 기반 `BookIdentity` 계산 코드는 보존한다.
4. `ryOS Books` 자동 seed와 전용 문자열을 제외한다. 일반 OPDS 클라이언트·서버 저장은 유지한다.
5. `.map`, section size, 최종 바이너리 크기를 전후 비교한다. 측정되지 않은 예상 절감량은 문서나 릴리스 판단에 사용하지 않는다.

하드 게이트를 통과하지 못하면 실제 설치·릴리스 단계로 진행하지 않는다. 목표 640KiB에 못 미치지만 하드 게이트를 넘는 경우에는 측정 결과와 남은 위험을 사용자에게 다시 제시하고 승인을 받아야 한다.

## 4. 로컬 우선 동기화 구조

### 4.1 기존 저장 지점

`ReadingStatsStore::endSession()`은 스냅샷을 만든 뒤 `saveToFile()`을 호출한다(`src/ReadingStatsStore.cpp:866-902`). 기존 스냅샷은 bookId, 경로, 세션 시간, 시작·종료 진행률과 완료 여부를 제공한다(`src/ReadingStatsStore.h:36-46`). 이 순서를 바꾸지 않는다.

현재 EPUB 종료 코드는 통계 저장을 마친 뒤에도 `epub`을 정리하기 전까지 객체가 살아 있다(`src/activities/reader/EpubReaderActivity.cpp:271-302`). 이 경계에서 종료 순서를 다음과 같이 확장한다.

1. 기존 `READING_STATS.endSession()`과 로컬 통계 저장 완료
2. scalar 메타데이터만 읽어 동기화 큐에 원자적으로 저장
3. 리더가 공유하던 `Epub` 핸들을 정리하고 홈 화면 전환
4. 홈이 활성화된 뒤 별도 one-shot 동기화 작업 시작

동기화 작업은 리더의 `Epub` 객체나 framebuffer를 참조하지 않는다. 표지가 필요하면 책 경로로 별도의 읽기 전용 EPUB 인스턴스를 열고, 1KB 단위 스트림을 사용한다. 전경에서 새 리더/네트워크 작업이 시작되면 작업을 취소하고 큐를 남긴다.

### 4.2 컴포넌트

| 컴포넌트 | 책임 |
| --- | --- |
| `ReadingSyncCoordinator` | 세션 판정, enqueue, 홈 전환 후 시도 예약, 중복 작업 방지 |
| `ReadingSyncQueue` | sequence·pending·표지 job을 SD에 원자 저장, 결과에 따른 삭제/유지 |
| `ReadingSyncClient` | Wi-Fi 수명주기, HTTPS 요청, 응답 분류, 항상 연결 종료 |
| `ReadingSyncCredentialStore` | X4 전용 토큰 난독화 저장·마스킹 상태 제공 |
| `EpubOriginalCoverSource` | JPG/PNG 원본 표지를 EPUB에서 제한된 크기로 스트리밍 |

구현 파일은 `src/reading_sync/` 아래에 모으고, 하드웨어·파일·네트워크 접근은 기존 HAL과 공용 downloader 패턴을 사용한다. 전역 raw pointer나 예외 기반 흐름을 추가하지 않는다.

### 4.3 SD 카드 파일

`/.crosspoint/reading_sync/` 아래를 사용한다.

| 파일 | 내용 |
| --- | --- |
| `config.json` | token 설정 여부와 MAC-XOR+base64 난독화된 token |
| `queue.json` | schema version, `nextSequence`, 최신 pending snapshot, 마지막 성공 fingerprint, auth pause 상태, cover job manifest |
| `covers/.tmp` | 추출 중인 임시 표지 |
| `covers/{sha256}.jpg|png` | 업로드 대기 중인 원본 표지 |

JSON은 동일 디렉터리의 temp 파일을 완전히 flush/close한 뒤 rename하는 방식으로 교체한다. `queue.json` 한 파일 안에서 next sequence와 pending을 함께 바꿔, 정전 시 같은 sequence가 새 스냅샷에 재할당되지 않게 한다. 손상된 queue는 삭제하지 않고 `.corrupt`로 보존하고 동기화를 정지하되 로컬 독서는 계속한다.

## 5. 어떤 세션을 올리는가

아래 중 하나를 만족할 때만 동기화 후보가 된다.

- 세션 누적 독서 시간이 3분 이상
- 시작 대비 진행률이 1%p 이상 증가
- 이번 세션에서 책을 완료

후보가 된 뒤 `bookId + title + author + progressPercent` fingerprint가 마지막 accepted 또는 현재 pending과 같으면 새 요청을 만들지 않는다. `lastReadAt`만 달라진 것은 사이트에 보이는 독서 상태가 같으므로 중복으로 본다. 실수로 책을 열었다 바로 닫는 동작은 current를 바꾸지 않는다.

큐에는 최신 pending 하나만 둔다. 더 최신 후보가 생기면 이전 미전송 pending을 교체하고 새 sequence를 배정한다. 이전 sequence가 서버에 도달하지 않아 생긴 번호 간격은 정상이다. 표지 job은 메타데이터와 별도로 hash별로 유지하며, 새 current가 다른 책으로 바뀌면 현재 책에 필요 없는 미전송 표지 job은 정리한다.

## 6. 책 식별자와 메타데이터

기존 `BookIdentity::resolveStableBookId()`를 사용한다. 파일이 존재하면 KOReader 호환 콘텐츠 ID를 계산하고, 실패한 경우에만 `legacy:{normalizedPath}`로 폴백한다(`src/util/BookIdentity.cpp:87-130`). 서버에는 path를 보내지 않는다. `legacy:` ID는 서버 전송 전에 SHA-256으로 불투명화하여 로컬 경로가 bookId 문자열에 섞이지 않게 한다.

보내는 필드:

- 필수: `schemaVersion=1`, `sequence`, `bookId`, `title`, `author`, `progressPercent`
- 선택: `lastReadAt`, `isbn13`, `coverSha256`, `coverMime`

보내지 않는 필드:

- 파일 경로와 알려진 경로 목록
- 장 제목과 장 진행률
- 세션 시간·누적 독서 시간·통계
- Wi-Fi SSID, 기기 MAC, 펌웨어 로그

현재 EPUB 파서에는 신뢰할 수 있는 ISBN 추출기가 없으므로 `isbn13`은 v1에서 `null`이어도 정상이다. ISBN 보강은 동기화 성공 조건이 아니다.

## 7. 원본 표지 처리

기존 EPUB 코드는 cover item을 ZIP에서 1KB 단위로 임시 JPG/PNG에 스트리밍한 뒤 BMP로 변환한다(`lib/Epub/Epub.cpp:566-658`). 또한 일반 item을 stream으로 읽고 압축 해제 크기를 미리 확인할 수 있다(`lib/Epub/Epub.cpp:779-792`, `lib/Epub/Epub.h:61-64`). 이 경로를 재사용하되 사이트 업로드에는 1-bit BMP가 아니라 원본 JPG/PNG를 사용한다.

처리 순서:

1. metadata 요청 전에 EPUB cover href와 inflated size를 확인한다.
2. 형식이 JPG/JPEG/PNG이고 크기가 1바이트 이상 2MB 이하일 때만 임시 파일로 스트리밍한다.
3. 스트리밍하면서 SHA-256을 계산하고 실제 magic bytes와 선언 MIME을 대조한다.
4. 성공하면 hash 이름으로 원자 rename하고 metadata payload에 hash/MIME을 넣는다.
5. 서버가 `coverRequired: true`를 반환할 때만 별도 PUT으로 업로드한다.
6. 업로드 성공 또는 이미 존재 응답이면 로컬 표지 job을 삭제한다.

표지가 없거나 지원하지 않는 형식이거나 너무 크면 cover 필드를 생략한다. XTC/TXT/PDF 등 v1에서 원본 표지를 안전하게 얻지 못하는 형식도 사이트 대체 표지를 사용한다. 표지 추출·업로드 실패는 metadata accepted를 실패로 바꾸지 않는다.

## 8. sequence와 응답 처리

sequence는 `1..4,294,967,295` 범위이며 런타임 세션 serial과 별개다. 기존 `sessionSerialCounter`는 네트워크 메모리 해제 때 0으로 초기화되므로 동기화 순서에 사용할 수 없다(`src/ReadingStatsStore.cpp:1125-1150`).

metadata 응답 규칙:

| 결과 | 큐 처리 |
| --- | --- |
| HTTP 200 + `accepted` | metadata pending 삭제, 성공 fingerprint 기록, `lastAcceptedSequence` 반영, 필요하면 표지 업로드 |
| HTTP 200 + `duplicate` | accepted와 동일하게 처리, 서버가 표지를 요구하면 업로드 |
| HTTP 200 + `stale` | metadata pending 삭제, `nextSequence`를 서버의 `lastAcceptedSequence + 1` 이상으로 전진, current를 되돌리지 않음 |
| HTTP 400/413/422 | 같은 payload 자동 재시도 중지, terminal reason 보존 |
| HTTP 401/403 | 큐 유지, token 변경 전까지 자동 동기화 일시 정지 |
| HTTP 429/5xx/timeout/network | 큐 유지, 다음 trigger에서 재시도 |

순서 번호 소진은 개인용 1대의 v1에서는 현실적으로 발생하지 않지만, 최대값에 도달하면 큐를 덮어쓰지 않고 동기화를 terminal 상태로 멈춘다. 자동 0 리셋은 하지 않는다.

HTTP 200 response에는 `status`, 요청 `sequence`, 서버의 `lastAcceptedSequence`, `coverRequired`가 포함된다. 이 값 반영과 pending 삭제는 같은 `queue.json` 원자 교체에서 처리한다. 처리 도중 전원이 꺼져 같은 요청을 다시 보내도 서버의 duplicate 규칙으로 수렴한다.

## 9. 네트워크 수명주기

동기화 trigger는 다음 세 가지다.

- qualifying session 종료 후 홈 진입
- 부팅 후 홈이 안정된 시점에 pending이 있을 때
- 웹 설정의 명시적 연결 테스트 또는 수동 재시도

배터리를 위해 주기적으로 Wi-Fi를 깨우는 타이머는 v1에 넣지 않는다. 한 trigger당 one-shot 시도 하나만 실행한다.

1. 저장된 Wi-Fi를 사용해 연결한다. 최대 8초.
2. token이 없거나 auth-paused이면 네트워크 요청 없이 종료한다.
3. 표지 준비가 필요하면 SD에서 제한된 크기로 준비한다.
4. metadata HTTPS 요청을 보낸다. 요청 timeout 기본값은 15초이며 compile-time/test 설정으로 조절 가능하게 한다.
5. 응답이 요구할 때 표지를 PUT한다. 동일한 15초 기본 timeout을 적용한다.
6. 성공·실패와 관계없이 HTTP handle을 닫고 `WiFi.disconnect(false)`, `WIFI_OFF`, `esp_wifi_deinit()`까지 수행한다. AirPage가 이미 쓰는 정리 순서를 참고한다(`src/activities/apps/standby/AirPageFace.cpp:80-96`).
7. 네트워크용으로 해제한 로컬 통계 메모리는 기존 `reloadAfterNetwork()` 규칙으로 복구한다(`src/ReadingStatsStore.cpp:1125-1168`). 큐는 이 메모리와 분리되어 있어야 한다.

HTTPS 전체 경로는 홈 렌더를 기다리게 하지 않는다. 작업용 stack/버퍼 상한은 구현 시 `.map`과 실제 heap 로그로 결정하며, 읽기 통계 전체를 JSON payload에 복제하지 않는다.

## 10. 기기 토큰과 설정 UX

토큰은 `rd1_` 접두사 뒤에 32바이트 난수의 base64url(no padding)을 붙인 불투명 문자열이다. 펌웨어에 기본 토큰을 넣지 않는다.

X4 파일 전송 웹 설정에 `kimtoma.com 독서 연동` 섹션을 추가한다.

- 상태: `설정됨` / `설정 안 됨` / `인증 확인 필요`
- token 입력: password field, 빈 제출은 기존 값 유지
- `연결 테스트`: `POST /v1/reading/sync?validateOnly=1`을 호출하며 current·sequence를 바꾸지 않음
- `연동 해제`: 확인 후 로컬 token과 auth pause만 삭제. 로컬 독서 통계·대기 큐는 보존

설정 조회는 token 문자열 대신 `configured: true|false`만 반환한다. OPDS/Wi-Fi API가 비밀번호 대신 `hasPassword`만 반환하는 기존 원칙을 따른다(`src/network/CrossPointWebServer.cpp:1324-1325`, `src/network/CrossPointWebServer.cpp:1438-1440`). token과 Authorization header는 어느 log level에서도 출력하지 않는다.

저장에는 공용 `ObfuscationUtils`의 MAC-XOR+base64 패턴을 재사용하되, 클래스·파일은 KOReader credential과 완전히 분리한다. 기존 저장소도 이 방식을 암호학적으로 안전하지 않은 난독화라고 명시한다(`lib/KOReaderSync/KOReaderCredentialStore.h:11-16`).

## 11. 메모리·동시성 안전 기준

- ESP32-C3의 제한된 RAM을 고려해 metadata JSON은 8KB 이하, cover 스트림 chunk는 기본 1KB로 고정한다.
- TLS 중에는 원본 표지 전체, EPUB 전체, reading stats 전체를 RAM에 올리지 않는다.
- one-shot worker는 하나만 존재하며 중복 trigger는 pending flag로 합친다.
- reader-owned 객체, renderer, framebuffer를 worker에서 참조하지 않는다.
- 모든 network exit path에서 파일·HTTP·Wi-Fi 자원을 닫는다.
- 실제 X4에서 동기화 전, TLS 중 최저, 종료 후 free heap과 largest block을 기록한다.
- 합격 기준은 free heap 50KB 초과와 반복 동기화 후 기준선으로의 회복이다. 감소가 누적되면 릴리스하지 않는다.

## 12. 빌드·릴리스

필수 자동 검사:

1. native unit tests와 한국어 parser/cache tests
2. Korean simulator 별도 build directory
3. `pio run -e gh_release_ko`
4. `pio run -e gh_release` 기본 SKU 회귀 빌드
5. `pio check`/프로젝트 cppcheck와 format 검사
6. 최종 `firmware.bin` 크기 gate
7. 생성 글꼴 glyph coverage 검사

릴리스 workflow는 이미 `gh_release_ko`를 빌드하고 `firmware-ko.bin`을 자산으로 복사한다(`.github/workflows/release.yml:43-55`). OTA updater도 KO SKU에서 이 파일명을 선택한다. 크기 gate는 release asset staging보다 먼저 실행해 초과 바이너리가 게시되지 않게 한다.

기기 검증:

- 기존 펌웨어와 SD 카드 백업 후 X4에 설치
- 한국어 UI, 제목, 저자, 경로, 작은/기본 글꼴 확인
- 원문 공백 EPUB 확인
- 정상 세션, 3분 미만 무진행 세션, 1%p 진행, 완료 세션
- Wi-Fi 없음, timeout, 429, 5xx, token 폐기, duplicate, stale
- JPG/PNG 표지, 무표지, 2MB 초과, 잘못된 MIME
- 재부팅 후 pending 복구와 OTA 업데이트

## 13. 완료 기준

- [ ] 8/10/12pt KS X 1001 공통 한글 2,350자와 모든 한국어 UI glyph 포함
- [ ] 14pt 전체 현대 한글과 교육용 한자 범위 유지
- [ ] 한국어 EPUB 원문 공백과 cache invalidation 확인
- [ ] 로컬 통계 저장이 enqueue보다 먼저이며 전원 중단 복구 가능
- [ ] qualifying·중복 억제·coalescing·persistent sequence 규칙 통과
- [ ] token 비노출과 401/403 pause 확인
- [ ] metadata/cover 독립 실패와 retry 규칙 확인
- [ ] 최종 바이너리 하드 여유 512KiB 이상, 목표 640KiB
- [ ] 실제 X4 free heap 50KB 초과, 반복 동기화 누수 없음
- [ ] 기본 국제 빌드와 기존 읽기·파일전송·OPDS·OTA 회귀 없음

## 14. 비목표

- AirPage 대체 서비스 개발
- 범용 KOReader cloud sync 유지 또는 새 cloud account UI
- 여러 서버 URL을 기기 UI에서 선택하는 기능
- 여러 kimtoma 계정·여러 활성 X4
- 독서 이력 전체, 장 정보, 세션 시간 업로드
- ISBN 추출 정확도를 v1 출시 조건으로 삼는 것
- 2MB 초과 표지 리사이즈·재인코딩
