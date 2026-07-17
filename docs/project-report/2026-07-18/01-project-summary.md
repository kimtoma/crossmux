# CrossMux KR 프로젝트 요약

**기록일:** 2026-07-18  
**대상 기기:** Xteink X4  
**프로젝트 상태:** 진단 펌웨어 설치와 API 인증 검증 성공 · 운영용 릴리스 보류  
**목표:** 한국어 장문 독서에 최적화된 펌웨어와 `kimtoma.com` 최신 독서 현황 자동 연동

## 1. 한눈에 보는 현재 상태

CrossMux KR은 Xteink X4에서 한국어 책을 더 안정적으로 읽고, 유효한 독서 세션이 끝나면 현재 책·저자·진행률·표지를 `kimtoma.com`에 자동 반영하는 개인용 펌웨어 프로젝트다.

| 영역 | 상태 | 현재 결과 |
| --- | --- | --- |
| 제품·펌웨어·API·사이트 설계 | 완료 | 전체 구조와 오류·폴백·운영 규칙 승인 및 문서화 |
| 한국어 글꼴·띄어쓰기 | 완료 | 작은 글꼴 KS X 1001 범위와 한국어 원문 공백 처리 구현 |
| 펌웨어 독서 동기화 | 완료 | 로컬 큐, 기기 토큰, 원본 표지, HTTPS, 리더 종료 연동 구현 |
| Worker·D1·R2 | 완료 | 쓰기 API, 멱등 sequence, 표지 저장, 공개 current API 구현·원격 바인딩 |
| kimtoma.com 화면 | 완료 | 목록·위젯 공용 Provider와 3단계 폴백 구현 |
| Xteink4 설치 | 진단 완료 | 새 TLS 진단 펌웨어 설치, 재부팅, 인증 API 성공 |
| 운영용 펌웨어 확정 | 남음 | 실기기 최소 힙 기준에 근소하게 미달하여 추가 절감 필요 |
| 새 독서 세션 E2E 갱신 | 남음 | 실제로 1%p 이상 읽거나 3분 이상 읽은 뒤 sequence 증가와 사이트 반영 확인 필요 |

현재 Xteink4에는 메모리 측정용 진단 펌웨어가 설치되어 있다. 읽기와 인증 요청은 정상 동작하지만, 최종 운영용 이미지로 확정하거나 배포한 상태는 아니다.

## 2. 프로젝트의 출발점과 계보

세 저장소의 관계를 먼저 확인한 뒤, 안정적인 원류와 한국어 구현 자산을 함께 가져가는 방향을 선택했다.

| 저장소 | 역할 | 이번 프로젝트에서의 사용 |
| --- | --- | --- |
| [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) | 최초 원류와 범용 Xteink 리더 기준선 | 하드웨어·리더·웹 플래셔·OTA 구조의 안정성 기준 |
| [0x1abin/crossmux](https://github.com/0x1abin/crossmux) | CrossPoint 기반 기능 확장 포크 | 앱·CJK 빌드 계보와 기존 서비스 경로 참고 |
| [ryokun6/crossmux](https://github.com/ryokun6/crossmux) | CJK·세로쓰기·한국어·그레이스케일에 집중한 후속 포크 | 실제 구현 기준선과 `gh_release_ko` 빌드 환경 |

최종 선택은 `ryokun6/crossmux`의 최신 `main`과 PlatformIO 환경 `gh_release_ko`를 기반으로 하되, 범용 클라우드 기능을 더하는 대신 한국어 독서와 개인 사이트 연동에 필요한 기능만 유지하는 방식이었다.

## 3. 사용자 결정과 진행 과정

| 단계 | 확정한 결정 |
| --- | --- |
| 방향 선택 | `ryokun6/crossmux` 기반의 독서 중심 한국어 펌웨어 |
| 자동 연동 시점 | 유효한 독서 세션 종료 후 홈 화면에서 Wi-Fi one-shot 전송 |
| 공개 범위 | 표지, 제목, 저자, 전체 진행률, 최근 동기화 시각 |
| 표지 전략 | EPUB 원본 표지를 필요할 때 한 번 업로드하고 ISBN은 선택적으로 보강 |
| 인증 | 펌웨어에 비밀을 넣지 않고 웹 설정에서 기기 전용 토큰 등록 |
| 전체 구조 | 펌웨어 → `api.kimtoma.com` Worker → D1/R2 → 공용 Reading Provider |
| 사이트 폴백 | 공개 API → 브라우저 마지막 성공값 → 공통 정적 기본값 |
| 실행 방식 | 설계·계획 승인 후 격리된 펌웨어·플랫폼 작업 트리에서 구현과 검증 |
| 실기기 설치 | 브라우저 웹 플래셔로 연결된 Xteink4에 진단 펌웨어 설치 |

진행 과정은 설계 승인 단계를 펌웨어 내부, API·데이터 모델, 사이트 표시·폴백·운영으로 나눴다. 각 단계의 승인 후 통합 설계와 구현 계획을 문서화하고, 두 저장소에서 독립적으로 구현했다.

## 4. 승인된 전체 구조

```text
Xteink4 / CrossMux KR
  ├─ 로컬 독서 통계 저장
  ├─ 최신 pending 1건을 SD 카드에 원자 저장
  ├─ 필요할 때 EPUB 원본 JPG/PNG 표지 준비
  └─ 저장된 Wi-Fi로 HTTPS one-shot 전송
             │
             ▼
api.kimtoma.com / Cloudflare Worker
  ├─ 기기 토큰 SHA-256 검증
  ├─ sequence 기반 accepted / duplicate / stale 처리
  ├─ READING_DB(D1)에 device·book·event·current 저장
  └─ READING_COVERS(R2)에 원본 표지 저장
             │
             ▼
kimtoma.com / ReadingProvider
  ├─ 홈 목록형 READING
  ├─ BookCoverWidget
  └─ API 실패 시 localStorage → 정적 fallback
```

핵심 원칙은 로컬 독서 우선이다. 네트워크 실패, 토큰 만료, 서버 오류, 표지 실패가 책 읽기와 로컬 통계 저장을 막지 않는다. 펌웨어는 최신 pending 하나만 보존하고 다음 안전한 trigger에서 다시 시도한다.

### 동기화 후보

다음 중 하나를 만족할 때만 새 독서 상태를 만든다.

- 세션 누적 독서 시간 3분 이상
- 시작 대비 진행률 1%p 이상 증가
- 책을 완료

동일한 `bookId + title + author + progressPercent`는 다시 보내지 않는다. 잠깐 열었다 닫은 책이나 동기화 시각만 바뀐 상태가 사이트의 현재 책을 덮어쓰지 않도록 했다.

## 5. 펌웨어 구현 결과

펌웨어 구현은 `/Users/macmini/Documents/CrossMux-KR/.worktrees/crossmux-kr-firmware`의 `feature/crossmux-kr-firmware` 브랜치에서 진행했다.

### 한국어 읽기

- 8·10·12pt 내장 글꼴에 KS X 1001 공통 한글 2,350자와 한국어 UI 문자를 포함했다.
- 기본 14pt는 현대 한글 11,172자와 교육용 기초 한자 1,800자 범위를 유지했다.
- 한국어 EPUB 원문의 실제 공백을 보존하도록 파서와 렌더러 경계를 보강했다.
- 한국어 section cache 버전을 76으로 올려 이전 토큰 캐시가 재사용되지 않게 했다.

### 한국어 SKU 범위와 용량

- AirPage와 MQTT 경로를 한국어 빌드에서 제외했다.
- KOReader/ryOS 전용 클라우드 설정·credential·동기화 클라이언트를 제외했다.
- 새 설치에 `ryOS Books` 기본 OPDS 서버를 자동 등록하는 동작을 껐다.
- 일반 OPDS, 파일 전송, 저장된 Wi-Fi, 로컬 통계, SD 카드 글꼴, OTA와 복구 경로는 유지했다.

### 독서 동기화 컴포넌트

| 컴포넌트 | 책임 |
| --- | --- |
| `ReadingSyncPolicy` | 세션 자격, fingerprint, 요청·재시도 정책 |
| `ReadingSyncQueue` | sequence와 최신 pending을 SD 카드에 원자 저장 |
| `ReadingSyncCredentialStore` | 기기 토큰 난독화 저장과 설정 상태 제공 |
| `EpubOriginalCoverSource` | 원본 JPG/PNG 표지를 제한된 크기로 스트리밍 |
| `ReadingSyncClient` | Wi-Fi, HTTPS, 응답 검증, 자원 정리 |
| `ReadingSyncCoordinator` | 리더 종료 후 enqueue와 홈 one-shot worker 수명주기 |

리더 종료에서는 기존 `ReadingStatsStore::endSession()`과 로컬 파일 저장을 먼저 끝낸다. 이후 scalar 메타데이터만 큐에 넣고 리더가 소유한 EPUB·렌더러 자원을 해제한 뒤 네트워크 작업을 시작한다. worker는 리더 객체나 framebuffer를 참조하지 않는다.

웹 설정에는 토큰 상태, 저장, 연결 테스트, 연동 해제를 추가했다. 토큰 문자열과 Authorization header는 설정 조회나 로그에 노출하지 않고, 요청 본문도 제한된 크기로 처리한다.

## 6. API와 데이터 모델 결과

플랫폼 구현은 `/Users/macmini/Projects/kimtoma.com/.worktrees/live-reading-platform`의 `feature/live-reading-platform` 브랜치에서 진행했다.

### 저장소 경계

- `READING_DB`: 채팅 DB와 분리한 Cloudflare D1 데이터베이스
- `READING_COVERS`: 독서 표지만 저장하는 R2 버킷
- `reading_devices`: token hash, 상태, 마지막 sequence
- `reading_books`: 제목·저자·선택 메타데이터·표지 주소
- `reading_events`: accepted 요청 감사·멱등 기록
- `reading_current`: 사이트가 읽는 현재 한 권

### API

| 경로 | 인증 | 역할 |
| --- | --- | --- |
| `POST /v1/reading/sync` | Bearer device token | metadata 검증과 sequence 기반 current 갱신 |
| `PUT /v1/reading/covers/{sha256}` | Bearer device token | 요청된 JPG/PNG 원본 표지 저장 |
| `GET /v1/reading/current` | 공개 | 사이트에 필요한 최소 현재 독서 상태 반환 |
| `POST /v1/reading/sync?validateOnly=1` | Bearer device token | current·sequence를 바꾸지 않는 연결 검사 |

`sequence`는 SD 카드에 영속 저장한다. 서버는 같은 번호의 재전송을 `duplicate`, 서버보다 낮은 번호를 `stale`로 처리하고, 낮은 sequence가 현재 상태를 되돌리지 못하도록 D1 갱신을 조건부로 수행한다. 401·403은 토큰 변경 전까지 자동 동기화를 멈추고, 429·5xx·timeout은 pending을 보존한다.

공개 응답에는 제목, 저자, 진행률, 동기화 시각, 표지 URL만 포함한다. 파일 경로, 토큰, 기기 MAC, 세션 시간, 통계 원본, 내부 sequence는 공개하지 않는다.

## 7. kimtoma.com 표시·폴백·운영 결과

목록형 홈과 위젯형 홈이 서로 다른 책을 표시하던 구조를 `ReadingProvider` 하나로 통합했다.

| 우선순위 | 데이터 원본 | 동작 |
| ---: | --- | --- |
| 1 | 공개 current API | 페이지 진입 시 조회, 보이는 탭에서만 60초 주기 갱신 |
| 2 | 브라우저 마지막 성공값 | API 실패 시 같은 책을 유지 |
| 3 | 공통 정적 fallback | 첫 방문·캐시 손상·오프라인 시 즉시 표시 |

API 오류는 사용자에게 경고 배너로 노출하지 않는다. 읽지 않은 기간이 길다는 이유만으로 책을 숨기지도 않는다. API 성공 시 목록형 `LiveReading`과 `BookCoverWidget`이 같은 상태를 공유하므로 화면 전환 중 중복 fetch나 책 불일치가 없다.

원격 저장소에는 현재 `하드씽`(벤 호로위츠), 진행률 8%, 표지 존재, 마지막 accepted sequence 4가 기록되어 있다. 이번 진단 요청은 `validateOnly`였기 때문에 이 current와 sequence를 변경하지 않은 것이 정상이다.

## 8. 빌드와 Xteink4 실기기 검증

### 최종 진단 바이너리

| 항목 | 측정값 | 판정 |
| --- | ---: | --- |
| `firmware.bin` | 5,966,384B | 6,029,312B 하드 크기 상한 통과 |
| SHA-256 | `c33a36a62f6af1c0b12c567b3b662f9935288f0c6aad36e220d5d293cd180ab0` | 설치 파일 식별 완료 |
| TLS 버퍼 | 수신 16KiB / 송신 4KiB | 실제 링크된 mbedTLS archive에서 확인 |
| 동기화 인증 | HTTP 200 · result 0 | 성공 |
| 최소 여유 힙 | 50,852B | 51,200B 기준보다 348B 부족 |
| 수명주기 정리 후 여유 힙 | 137,872B | 네트워크 자원 회복 확인 |

Chrome의 [CrossPoint web flasher](https://crosspointreader.com/#flash-tools)에서 Xteink X4와 custom firmware를 선택해 설치했다. 플래셔의 연결, partition 검증, OTA 읽기, firmware 쓰기, boot partition 갱신, 연결 해제 단계가 모두 완료됐다. 이후 ESP32-C3를 hard reset하고 직렬 로그로 새 진단 코드의 실행을 확인했다.

관측된 핵심 로그는 `wifi-connected`, `before-validate`, `after-validate`, `validate status=200 result=0`, `lifecycle-cleaned`였다. 인증과 자원 정리는 성공했지만 최소 힙이 승인 게이트를 근소하게 밑돌았다. 이 때문에 진단 코드를 제거한 운영용 펌웨어 빌드·재설치 단계로 넘어가지 않았다.

## 9. 산출물 인덱스

### 설계와 계획

- [전체 연동 설계](../../superpowers/specs/2026-07-17-crossmux-kr-integration-design.md)
- [펌웨어 설계](../../superpowers/specs/2026-07-17-crossmux-kr-firmware-design.md)
- [펌웨어 구현 계획](../../superpowers/plans/2026-07-17-crossmux-kr-firmware.md)
- [프로젝트 기록 패키지 설계](../../superpowers/specs/2026-07-18-crossmux-kr-project-report-design.md)
- [프로젝트 기록 패키지 실행 계획](../../superpowers/plans/2026-07-18-crossmux-kr-project-report.md)
- 플랫폼 설계: `/Users/macmini/Projects/kimtoma.com/.worktrees/live-reading-platform/docs/superpowers/specs/2026-07-17-live-reading-platform-design.md`
- 플랫폼 구현 계획: `/Users/macmini/Projects/kimtoma.com/.worktrees/live-reading-platform/docs/superpowers/plans/2026-07-17-live-reading-platform.md`

### 구현

- 펌웨어 동기화: `src/reading_sync/`
- 펌웨어 설정·리더 종료·홈 worker 통합: `src/network/`, `src/activities/reader/`, `src/activities/home/`
- Worker API: `workers/gemini-proxy/src/reading/`
- D1 migration·운영 도구: `workers/gemini-proxy/migrations/reading/`, `workers/gemini-proxy/scripts/reading-admin.mjs`
- 사이트 상태 계층: `src/lib/reading.ts`, `src/lib/reading-client.ts`, `src/components/reading/`

### 이 기록 패키지

- `01-project-summary.md` / `.html`: 프로젝트 프로세스와 결과 요약
- `02-session-transcript.md` / `.html`: 전체 화면 표시 대화 원문
- `03-kimtoma-blog-post.md` / `.html`: 게시 가능한 블로그 원고와 미리보기

## 10. 완료·미완료·다음 단계

### 완료

- 한국어 읽기 품질과 제품 범위 설계
- 작은 내장 글꼴 한글 범위와 원문 공백 보존 구현
- 로컬 우선 동기화 큐·토큰·표지·HTTPS 구현
- sequence 멱등 API와 D1/R2 저장 구조 구현
- kimtoma.com 목록·위젯 공용 데이터와 폴백 구현
- 원격 Worker storage binding과 현재 독서 상태 확인
- 연결된 Xteink4에 진단 펌웨어 설치 및 API 200 확인

### 아직 운영 완료가 아닌 이유

- 승인한 50KiB 최소 힙 기준을 근소하게 통과하지 못했다.
- 현재 기기에 설치된 이미지는 메모리 로그가 포함된 진단용이다.
- 새 qualifying 독서 세션을 만들어 기기 → Worker → D1/R2 → 사이트 표시 전체 흐름을 한 번 더 확인해야 한다.
- 반복 동기화 후 최소 힙과 기준선 회복이 누적 저하 없이 유지되는지 확인해야 한다.

### 권장 다음 단계

1. 1~2KiB의 추가 안전 여유를 만드는 작은 메모리 최적화만 수행한다.
2. 동일한 진단 펌웨어로 최소 힙 기준을 다시 측정한다.
3. 기준 통과 후 진단 로그와 임시 cache 정리를 제거한 운영용 이미지를 빌드한다.
4. 운영용 이미지를 Xteink4에 재설치하고 정상 부팅·한국어 독서·토큰 비노출을 확인한다.
5. 책을 3분 이상 읽거나 1%p 이상 진행한 뒤 종료해 sequence 증가와 `kimtoma.com` 표시 변경을 확인한다.

이 프로젝트의 현재 결론은 “핵심 구조와 실기기 통신은 동작한다. 하지만 정한 안전 기준을 근소하게 넘지 못했으므로, 한 번 더 줄인 뒤 운영용으로 확정한다”이다.
