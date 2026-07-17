# CrossMux KR Project Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce three authoritative Korean project records - summary, visible-session transcript, and kimtoma.com-compatible blog post - in Markdown and standalone HTML.

**Architecture:** Markdown is the source of truth for the summary and blog post. A small Node.js generator extracts the transcript directly from the Codex JSONL session, converts trusted Markdown with kimtoma.com's installed `marked` package, and wraps all three documents in standalone responsive HTML templates. Validation compares transcript counts with the source log, parses the blog front matter with kimtoma.com's `gray-matter`, scans public copy for private identifiers, and renders all HTML in a real browser.

**Tech Stack:** Node.js ESM, JSONL, `marked`, `gray-matter`, standalone HTML/CSS, Codex Chrome browser control.

## Global Constraints

- Final output directory is exactly `docs/project-report/2026-07-18/`.
- Create exactly three Markdown deliverables and three matching HTML deliverables.
- Transcript source is `/Users/macmini/.codex/sessions/2026/07/17/rollout-2026-07-17T04-54-12-019f6c7e-6b44-7360-bcb0-cb788d780e8e.jsonl`.
- Transcript cutoff is `2026-07-17T19:36:14.760Z`, the approved documentation-design turn.
- Transcript contains 33 actual user messages and 309 visible assistant messages at that cutoff.
- Exclude injected plugin/context messages, system and developer instructions, reasoning, summaries, tool calls, and tool outputs.
- Preserve user and assistant visible message text byte-for-byte after JSON decoding.
- Blog front matter must be accepted by `/Users/macmini/Projects/kimtoma.com/src/lib/posts.ts` conventions.
- Blog public copy must not contain tokens, Authorization values, local IP addresses, device MAC, USB device path, or local absolute paths.
- Do not publish, copy into `kimtoma.com/content/posts/`, deploy, or flash firmware in this task.
- State the measured diagnostic result exactly: API `200`, minimum free heap `50,852B`, gate `51,200B`, shortfall `348B`, cleanup free heap `137,872B`.

---

## File Structure

- Create `scripts/generate_project_report.mjs`: extract visible transcript messages and generate standalone HTML from the three Markdown sources.
- Create `scripts/test_generate_project_report.mjs`: test injected-message filtering, cutoff behavior, exact text preservation, HTML escaping, and front-matter stripping.
- Create `docs/project-report/2026-07-18/01-project-summary.md`: authoritative process/result summary.
- Create `docs/project-report/2026-07-18/01-project-summary.html`: generated archival HTML.
- Create `docs/project-report/2026-07-18/02-session-transcript.md`: generated visible-message transcript.
- Create `docs/project-report/2026-07-18/02-session-transcript.html`: generated transcript HTML with role navigation.
- Create `docs/project-report/2026-07-18/03-kimtoma-blog-post.md`: publish-ready kimtoma.com post source.
- Create `docs/project-report/2026-07-18/03-kimtoma-blog-post.html`: site-aligned standalone preview.

---

### Task 1: Deterministic report generator

**Files:**
- Create: `scripts/generate_project_report.mjs`
- Create: `scripts/test_generate_project_report.mjs`

**Interfaces:**
- Consumes: `--rollout <absolute-jsonl>`, `--out-dir <absolute-directory>`, `--kimtoma-root <absolute-directory>`, `--cutoff <ISO-8601>`.
- Produces: `extractVisibleMessages(lines, cutoff) -> Array<{timestamp, role, phase, text}>`, `renderTranscriptMarkdown(messages) -> string`, `renderDocumentHtml(options) -> string`, and six final files when executed as CLI.

- [ ] **Step 1: Write focused generator tests**

Use `node:assert/strict` with an in-memory JSONL fixture containing:

```js
const fixture = [
  response('2026-07-18T00:00:00.000Z', 'user', '<recommended_plugins>injected</recommended_plugins>'),
  response('2026-07-18T00:00:01.000Z', 'user', '사용자 원문\n둘째 줄'),
  response('2026-07-18T00:00:02.000Z', 'assistant', '응답 **원문**', 'commentary'),
  { timestamp: '2026-07-18T00:00:03.000Z', type: 'response_item', payload: { type: 'reasoning', summary: ['internal'] } },
  response('2026-07-18T00:00:04.000Z', 'assistant', '<script>alert(1)</script>', 'final_answer'),
  response('2026-07-18T00:00:05.000Z', 'user', 'cutoff 이후'),
]
```

Assertions:

```js
assert.deepEqual(messages.map(({ role, text }) => ({ role, text })), [
  { role: 'user', text: '사용자 원문\n둘째 줄' },
  { role: 'assistant', text: '응답 **원문**' },
  { role: 'assistant', text: '<script>alert(1)</script>' },
])
assert.match(transcriptMarkdown, /사용자 원문\n둘째 줄/)
assert.match(transcriptHtml, /&lt;script&gt;alert\(1\)&lt;\/script&gt;/)
assert.doesNotMatch(transcriptHtml, /<script>alert/)
assert.doesNotMatch(blogHtml, /^---/m)
```

- [ ] **Step 2: Run the test and verify the generator is missing**

Run:

```bash
node scripts/test_generate_project_report.mjs
```

Expected: failure because `scripts/generate_project_report.mjs` does not exist.

- [ ] **Step 3: Implement the generator**

Implementation requirements:

```js
export function extractVisibleMessages(lines, cutoff) {
  return lines.flatMap((line) => {
    const event = JSON.parse(line)
    if (event.timestamp > cutoff || event.type !== 'response_item') return []
    const item = event.payload
    if (item?.type !== 'message' || !['user', 'assistant'].includes(item.role)) return []
    const text = (item.content ?? [])
      .filter((part) => part.type === 'input_text' || part.type === 'output_text')
      .map((part) => part.text)
      .join('\n')
    if (!text || (item.role === 'user' && text.startsWith('<recommended_plugins>'))) return []
    return [{ timestamp: event.timestamp, role: item.role, phase: item.phase ?? '', text }]
  })
}
```

The CLI must:

1. Read and split the JSONL without modifying it.
2. Assert exactly 33 user and 309 assistant messages.
3. Write transcript Markdown with YAML-free metadata, numbered role headings, timestamps, and original text.
4. Escape transcript text before placing it in HTML `<pre class="message-body">` blocks.
5. Resolve `marked` and `gray-matter` using `createRequire()` rooted at the supplied kimtoma directory.
6. Strip blog front matter before HTML rendering while using parsed title and metadata in the preview header.
7. Generate light/dark responsive CSS with a 760px reading column, overflow-safe tables/code, and kimtoma green `#00c441` / `#39ff14` accents.

- [ ] **Step 4: Run focused tests**

Run:

```bash
node scripts/test_generate_project_report.mjs
```

Expected: `project report generator tests: PASS`.

- [ ] **Step 5: Commit the generator unit**

```bash
git add scripts/generate_project_report.mjs scripts/test_generate_project_report.mjs
git commit -m "docs: add deterministic CrossMux report generator"
```

---

### Task 2: Authoritative project summary

**Files:**
- Create: `docs/project-report/2026-07-18/01-project-summary.md`
- Generate: `docs/project-report/2026-07-18/01-project-summary.html`

**Interfaces:**
- Consumes: approved firmware/integration designs, implementation plans, firmware worktree status, platform worktree status, build artifact measurements, device flash log, and remote reading status.
- Produces: a fact/plan-separated summary used as the factual source for the blog post.

- [ ] **Step 1: Write the summary Markdown**

Use these exact top-level sections:

```markdown
# CrossMux KR 프로젝트 요약
## 1. 한눈에 보는 현재 상태
## 2. 프로젝트의 출발점과 계보
## 3. 사용자 결정과 진행 과정
## 4. 승인된 전체 구조
## 5. 펌웨어 구현 결과
## 6. API와 데이터 모델 결과
## 7. kimtoma.com 표시·폴백·운영 결과
## 8. 빌드와 Xteink4 실기기 검증
## 9. 산출물 인덱스
## 10. 완료·미완료·다음 단계
```

Include tables for source repositories, milestone decisions, component responsibilities, verification measurements, and status. Label every statement as `완료`, `진단 완료`, or `남음` where ambiguity is possible.

- [ ] **Step 2: Check factual anchors before generation**

Run:

```bash
rg -n "50,852|51,200|348|137,872|5,966,384|c33a36a6" docs/project-report/2026-07-18/01-project-summary.md
```

Expected: all six measured anchors appear exactly once in the verification/result sections.

- [ ] **Step 3: Generate summary HTML**

Run:

```bash
node scripts/generate_project_report.mjs \
  --rollout /Users/macmini/.codex/sessions/2026/07/17/rollout-2026-07-17T04-54-12-019f6c7e-6b44-7360-bcb0-cb788d780e8e.jsonl \
  --out-dir /Users/macmini/Documents/CrossMux-KR/docs/project-report/2026-07-18 \
  --kimtoma-root /Users/macmini/Projects/kimtoma.com \
  --cutoff 2026-07-17T19:36:14.760Z
```

Expected: `01-project-summary.html` and both transcript outputs are generated; blog generation waits until Task 3 source exists or reports that source as pending without partial HTML.

- [ ] **Step 4: Commit the summary unit**

```bash
git add docs/project-report/2026-07-18/01-project-summary.md docs/project-report/2026-07-18/01-project-summary.html
git commit -m "docs: summarize CrossMux KR project results"
```

---

### Task 3: kimtoma.com-compatible blog post

**Files:**
- Create: `docs/project-report/2026-07-18/03-kimtoma-blog-post.md`
- Generate: `docs/project-report/2026-07-18/03-kimtoma-blog-post.html`

**Interfaces:**
- Consumes: Task 2 factual summary and kimtoma.com's `content/posts` front-matter contract.
- Produces: a publish-ready Markdown source and a standalone site-aligned preview; it does not copy or deploy either file.

- [ ] **Step 1: Write the blog source**

Use this exact front matter:

```yaml
---
layout: post
title: "주머니 속 한국어 전자책 리더를 직접 만들었다"
ai: true
ai_pct: 90
ai_note: "방향과 승인 판단은 사람, 조사·구현·검증·초안 작성은 AI와 협업."
---
```

Use these narrative sections:

```markdown
## 한글이 보이는 것과 한국어로 읽기 좋은 것은 다르다
## 세 개의 오픈소스 저장소에서 출발했다
## 이번 펌웨어에서 지키기로 한 것
## 책을 덮으면 웹사이트의 READING이 바뀐다
## 380KB 안에서 HTTPS를 실행하는 일
## 실제 Xteink4에 설치했다
## 348바이트 때문에 멈췄다
## 다음 버전에서 할 일
```

Write in first person, link the three public repositories and CrossPoint web flasher, and explain the API/sequence/fallback design without exposing private operational data.

- [ ] **Step 2: Validate front matter and privacy before HTML generation**

Run a Node assertion using `gray-matter` from kimtoma.com and confirm:

```js
assert.equal(data.layout, 'post')
assert.equal(data.ai, true)
assert.equal(data.ai_pct, 90)
assert.ok(content.includes('## 348바이트 때문에 멈췄다'))
```

Run:

```bash
rg -n "172\\.30\\.|/dev/cu\\.|8c:bf:|Authorization|rd1_|/Users/macmini" docs/project-report/2026-07-18/03-kimtoma-blog-post.md
```

Expected: no matches.

- [ ] **Step 3: Generate the blog preview HTML**

Run the generator command from Task 2 again.

Expected: `03-kimtoma-blog-post.html` exists, begins with `<!DOCTYPE html>`, and does not contain a YAML fence.

- [ ] **Step 4: Commit the blog unit**

```bash
git add docs/project-report/2026-07-18/03-kimtoma-blog-post.md docs/project-report/2026-07-18/03-kimtoma-blog-post.html
git commit -m "docs: draft CrossMux KR kimtoma blog post"
```

---

### Task 4: Full visible-session transcript

**Files:**
- Generate: `docs/project-report/2026-07-18/02-session-transcript.md`
- Generate: `docs/project-report/2026-07-18/02-session-transcript.html`

**Interfaces:**
- Consumes: immutable JSONL session and fixed cutoff.
- Produces: role- and timestamp-labelled local archive with exact decoded visible text.

- [ ] **Step 1: Generate transcript outputs**

Run the generator command from Task 2.

Expected CLI summary:

```text
transcript: 342 messages (33 user, 309 assistant)
generated: 6 report files
```

- [ ] **Step 2: Compare source and output boundaries**

Assert the Markdown contains the first actual request, the last approval, and no injected plugin preamble:

```bash
rg -n "한국어에 최적화된 kor 버전|ㅇㅇ 좋아|<recommended_plugins>" docs/project-report/2026-07-18/02-session-transcript.md
```

Expected: first and last phrases match; `<recommended_plugins>` has no match.

- [ ] **Step 3: Verify exact message counts and HTML safety**

Run the generator test plus a report verifier that checks:

```text
Markdown role headings: 342
HTML message articles: 342
user: 33
assistant: 309
raw <script> in transcript HTML: 0
```

- [ ] **Step 4: Commit the transcript unit**

```bash
git add docs/project-report/2026-07-18/02-session-transcript.md docs/project-report/2026-07-18/02-session-transcript.html
git commit -m "docs: archive CrossMux KR visible session transcript"
```

---

### Task 5: Cross-file validation and browser QA

**Files:**
- Modify only if validation finds defects: the six files under `docs/project-report/2026-07-18/` or the generator/tests.

**Interfaces:**
- Consumes: six generated deliverables.
- Produces: evidence that content, privacy, HTML structure, and real rendering meet the design.

- [ ] **Step 1: Run mechanical validation**

Run:

```bash
node scripts/test_generate_project_report.mjs
git diff --check
find docs/project-report/2026-07-18 -maxdepth 1 -type f | sort
```

Expected: tests pass, no whitespace errors, and exactly six report files are listed.

- [ ] **Step 2: Validate HTML contracts**

Check each HTML for `<!DOCTYPE html>`, `lang="ko"`, UTF-8, viewport, non-empty title, responsive table/code CSS, and closing `</html>`.

Expected: all three documents pass every structural assertion.

- [ ] **Step 3: Inspect all three HTML files in Chrome**

Open the local files in one browser group. Inspect each at the normal desktop viewport, then at a narrow viewport near 390px. Confirm:

- Korean text renders without mojibake.
- Tables and code blocks scroll within the reading column.
- Transcript role cards remain distinguishable and long messages wrap safely.
- Blog preview visually follows kimtoma.com's typography and green accent.
- No clipped, overlapping, or off-screen primary content exists.

- [ ] **Step 4: Validate kimtoma compatibility without publishing**

Use the real kimtoma Markdown parser against `03-kimtoma-blog-post.md`, then render its body with the site's Markdown renderer or equivalent `marked` options.

Expected: title, AI metadata, excerpt, headings, tables, code blocks, and links parse without errors. Do not copy to `content/posts/` and do not deploy.

- [ ] **Step 5: Final status and commit audit**

Run:

```bash
git status --short
git log -6 --oneline --decorate
```

Expected: only pre-existing unrelated `.superpowers/` remains untracked; all report and generator files are committed in focused commits.

