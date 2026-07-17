#!/usr/bin/env node

import fs from 'node:fs/promises'
import path from 'node:path'
import { createRequire } from 'node:module'
import { pathToFileURL } from 'node:url'

const EXPECTED_USER_MESSAGES = 33
const EXPECTED_ASSISTANT_MESSAGES = 309

function parseYamlScalar(rawValue) {
  const value = rawValue.trim()
  if ((value.startsWith('"') && value.endsWith('"')) ||
      (value.startsWith("'") && value.endsWith("'"))) {
    return value.slice(1, -1)
  }
  if (value === 'true') return true
  if (value === 'false') return false
  if (/^-?\d+(?:\.\d+)?$/.test(value)) return Number(value)
  return value
}

export function stripFrontMatter(source) {
  if (!source.startsWith('---\n')) return { data: {}, content: source }
  const closingIndex = source.indexOf('\n---\n', 4)
  if (closingIndex === -1) return { data: {}, content: source }

  const yaml = source.slice(4, closingIndex)
  const data = {}
  for (const line of yaml.split('\n')) {
    const match = line.match(/^([A-Za-z0-9_]+):\s*(.*)$/)
    if (match) data[match[1]] = parseYamlScalar(match[2])
  }

  return { data, content: source.slice(closingIndex + 5).replace(/^\n/, '') }
}

export function escapeHtml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;')
}

export function extractVisibleMessages(lines, cutoff) {
  return lines.flatMap((line) => {
    if (!line.trim()) return []
    const event = JSON.parse(line)
    if (event.timestamp > cutoff || event.type !== 'response_item') return []

    const item = event.payload
    if (item?.type !== 'message' || !['user', 'assistant'].includes(item.role)) return []

    const text = (item.content ?? [])
      .filter((part) => part.type === 'input_text' || part.type === 'output_text')
      .map((part) => part.text)
      .join('\n')

    if (!text || (item.role === 'user' && text.startsWith('<recommended_plugins>'))) return []

    return [{
      timestamp: event.timestamp,
      role: item.role,
      phase: item.phase ?? '',
      text,
    }]
  })
}

function formatTimestamp(timestamp) {
  const formatter = new Intl.DateTimeFormat('ko-KR', {
    timeZone: 'Asia/Seoul',
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
  })
  return formatter.format(new Date(timestamp)) + ' KST'
}

function roleLabel(message) {
  if (message.role === 'user') return '사용자 Input'
  if (message.phase === 'commentary') return 'Codex Output · 진행 메시지'
  if (message.phase === 'final_answer') return 'Codex Output · 최종 응답'
  return 'Codex Output'
}

export function renderTranscriptMarkdown(messages, { cutoff, sourceLabel }) {
  const userCount = messages.filter((message) => message.role === 'user').length
  const assistantCount = messages.length - userCount
  const sections = messages.map((message, index) => {
    const phase = message.phase ? ` · ${message.phase}` : ''
    return [
      `### ${index + 1}. ${roleLabel(message)}`,
      '',
      `- 시각: ${message.timestamp} (${formatTimestamp(message.timestamp)})`,
      `- 역할: \`${message.role}\`${phase}`,
      '',
      message.text,
    ].join('\n')
  })

  return [
    '# CrossMux KR 전체 세션 대화 원문',
    '',
    `- 수록 범위: 최초 사용자 요청부터 ${cutoff}까지`,
    `- 원본 기록: \`${sourceLabel}\``,
    `- 메시지: 총 ${messages.length}개 (사용자 ${userCount}개, Codex ${assistantCount}개)`,
    '- 포함: 실제 화면에 표시된 사용자 입력, Codex 진행 메시지와 최종 응답',
    '- 제외: 시스템·개발자 지시, 내부 추론, 도구 호출·원시 출력, 주입된 실행 환경 정보',
    '- 보존 원칙: 각 메시지 본문은 JSON 디코딩 후 원문을 수정하지 않음',
    '',
    '> 이 파일은 로컬 프로젝트 기록용입니다. 공개 블로그 게시물에 포함하지 않습니다.',
    '',
    '---',
    '',
    ...sections.flatMap((section, index) => index === 0 ? [section] : ['', '---', '', section]),
    '',
  ].join('\n')
}

function baseStyles(variant) {
  const transcriptStyles = variant === 'transcript' ? `
.transcript-summary { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px; margin: 28px 0 36px; }
.metric { border: 1px solid var(--rule); border-radius: 14px; padding: 16px; background: var(--surface); }
.metric strong { display: block; font: 700 1.45rem/1 var(--sans); margin-bottom: 6px; }
.metric span { color: var(--muted); font-size: .82rem; }
.message { border: 1px solid var(--rule); border-radius: 16px; margin: 20px 0; overflow: hidden; background: var(--surface); }
.message.user { border-left: 4px solid var(--accent); }
.message.assistant { border-left: 4px solid var(--assistant); }
.message-header { display: flex; justify-content: space-between; gap: 16px; padding: 12px 16px; border-bottom: 1px solid var(--rule); font: 600 .82rem/1.4 var(--sans); }
.message-header time { color: var(--muted); font-weight: 500; text-align: right; }
.message-body { margin: 0; padding: 16px; border-radius: 0; background: transparent; color: var(--fg); white-space: pre-wrap; overflow-wrap: anywhere; font: 400 .94rem/1.65 var(--mono); }
@media (max-width: 620px) { .transcript-summary { grid-template-columns: 1fr; } .message-header { flex-direction: column; } .message-header time { text-align: left; } }
` : ''

  return `
:root {
  color-scheme: light dark;
  --bg: #fafafa;
  --surface: #ffffff;
  --fg: #1c1c1c;
  --muted: #6b6b6b;
  --rule: #e4e4e4;
  --accent: #00c441;
  --assistant: #5b6cff;
  --code-bg: #f0f1f1;
  --code-fg: #252525;
  --sans: -apple-system, BlinkMacSystemFont, "Pretendard", "Apple SD Gothic Neo", system-ui, sans-serif;
  --serif: "Iowan Old Style", "Noto Serif KR", "AppleMyungjo", Georgia, serif;
  --mono: "SFMono-Regular", Consolas, "Liberation Mono", monospace;
}
@media (prefers-color-scheme: dark) {
  :root { --bg: #101110; --surface: #171817; --fg: #e9e9e6; --muted: #9a9a96; --rule: #30322f; --accent: #39ff14; --assistant: #8c98ff; --code-bg: #202220; --code-fg: #e5e5e2; }
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; overflow-x: hidden; background: var(--bg); color: var(--fg); font-family: var(--serif); font-size: 17px; line-height: 1.76; -webkit-font-smoothing: antialiased; }
.wrap { width: min(100% - 32px, 760px); margin: 0 auto; padding: 56px 0 96px; }
.eyebrow { color: var(--accent); font: 700 .72rem/1.2 var(--mono); letter-spacing: .08em; text-transform: uppercase; }
h1, h2, h3, h4 { font-family: var(--sans); line-height: 1.28; letter-spacing: -.025em; text-wrap: balance; }
h1 { margin: 10px 0 14px; font-size: clamp(2rem, 6vw, 3.4rem); }
h2 { margin: 2.8em 0 .7em; padding-top: .9em; border-top: 1px solid var(--rule); font-size: clamp(1.4rem, 4vw, 1.85rem); }
h3 { margin: 2em 0 .55em; font-size: 1.18rem; }
h4 { margin: 1.5em 0 .4em; font-size: 1rem; }
p { margin: 0 0 1.05em; }
ul, ol { margin: 0 0 1.2em; padding-left: 1.45em; }
li { margin: .3em 0; }
a { color: var(--accent); text-decoration-thickness: 1px; text-underline-offset: 3px; overflow-wrap: anywhere; }
blockquote { margin: 1.4em 0; padding: .1em 0 .1em 1.15em; border-left: 3px solid var(--accent); color: var(--muted); }
code { padding: .14em .38em; border-radius: 5px; background: var(--code-bg); color: var(--code-fg); font-family: var(--mono); font-size: .88em; }
pre { max-width: 100%; margin: 1.3em 0; padding: 16px; overflow-x: auto; border-radius: 12px; background: var(--code-bg); color: var(--code-fg); line-height: 1.55; }
pre code { padding: 0; background: transparent; }
.table-wrap { max-width: 100%; overflow-x: auto; margin: 1.35em 0; border: 1px solid var(--rule); border-radius: 12px; }
table { width: 100%; min-width: 560px; border-collapse: collapse; font-family: var(--sans); font-size: .9rem; }
th, td { padding: .72em .85em; text-align: left; vertical-align: top; border-bottom: 1px solid var(--rule); }
th { background: var(--code-bg); font-weight: 700; }
tr:last-child td { border-bottom: 0; }
hr { margin: 2.8em 0; border: 0; border-top: 1px solid var(--rule); }
.meta { margin: 0 0 34px; padding: 0 0 22px; border-bottom: 1px solid var(--rule); color: var(--muted); font: 500 .84rem/1.65 var(--sans); }
.meta span { display: inline-block; margin-right: 14px; }
.status-note { margin: 24px 0 34px; padding: 16px 18px; border: 1px solid color-mix(in srgb, var(--accent) 45%, var(--rule)); border-radius: 14px; background: color-mix(in srgb, var(--accent) 7%, var(--surface)); font-family: var(--sans); }
img { max-width: 100%; height: auto; }
${transcriptStyles}
@media (max-width: 620px) { body { font-size: 16px; } .wrap { width: min(100% - 24px, 760px); padding-top: 36px; } h1 { font-size: 2rem; } }
`
}

function wrapTables(html) {
  return html.replaceAll('<table>', '<div class="table-wrap"><table>').replaceAll('</table>', '</table></div>')
}

export function renderDocumentHtml({
  title,
  markdownBody,
  variant = 'archive',
  markedParse,
  metadataHtml = '',
  statusHtml = '',
}) {
  const rendered = wrapTables(markedParse(markdownBody))
  return `<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>${escapeHtml(title)}</title>
<style>${baseStyles(variant)}</style>
</head>
<body>
<main class="wrap">
<div class="eyebrow">${variant === 'blog' ? 'kimtoma.com / blog preview' : 'CrossMux KR / project archive'}</div>
<h1>${escapeHtml(title)}</h1>
${metadataHtml ? `<div class="meta">${metadataHtml}</div>` : ''}
${statusHtml ? `<div class="status-note">${statusHtml}</div>` : ''}
${rendered}
</main>
</body>
</html>
`
}

export function renderTranscriptHtml(messages, { cutoff }) {
  const userCount = messages.filter((message) => message.role === 'user').length
  const assistantCount = messages.length - userCount
  const articles = messages.map((message, index) => `
<article class="message ${message.role}" id="message-${index + 1}">
  <header class="message-header">
    <span>${index + 1}. ${escapeHtml(roleLabel(message))}</span>
    <time datetime="${escapeHtml(message.timestamp)}">${escapeHtml(formatTimestamp(message.timestamp))}</time>
  </header>
  <pre class="message-body">${escapeHtml(message.text)}</pre>
</article>`).join('\n')

  return `<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>CrossMux KR 전체 세션 대화 원문</title>
<style>${baseStyles('transcript')}</style>
</head>
<body>
<main class="wrap">
<div class="eyebrow">CrossMux KR / visible session transcript</div>
<h1>전체 세션 대화 원문</h1>
<div class="meta">수록 마감: ${escapeHtml(cutoff)} · 시스템 지시, 내부 추론, 도구 출력 제외</div>
<section class="transcript-summary" aria-label="대화 통계">
  <div class="metric"><strong>${messages.length}</strong><span>전체 메시지</span></div>
  <div class="metric"><strong>${userCount}</strong><span>사용자 Input</span></div>
  <div class="metric"><strong>${assistantCount}</strong><span>Codex Output</span></div>
</section>
${articles}
</main>
</body>
</html>
`
}

function parseArgs(argv) {
  const args = {}
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index]
    const value = argv[index + 1]
    if (!key?.startsWith('--') || value === undefined) {
      throw new Error(`Invalid argument near ${key ?? '<end>'}`)
    }
    args[key.slice(2)] = value
  }
  for (const required of ['rollout', 'out-dir', 'kimtoma-root', 'cutoff']) {
    if (!args[required]) throw new Error(`Missing --${required}`)
  }
  return args
}

async function locateNodeModules(kimtomaRoot) {
  const candidates = [
    path.join(kimtomaRoot, 'node_modules'),
    path.join(kimtomaRoot, '.worktrees', 'live-reading-platform', 'node_modules'),
  ]
  for (const candidate of candidates) {
    try {
      await fs.access(path.join(candidate, 'marked'))
      await fs.access(path.join(candidate, 'gray-matter'))
      return candidate
    } catch {
      // Keep looking for the repo's installed dependency tree.
    }
  }
  throw new Error(`kimtoma.com dependencies not found under ${kimtomaRoot}`)
}

async function runCli() {
  const args = parseArgs(process.argv.slice(2))
  const outDir = path.resolve(args['out-dir'])
  const rolloutPath = path.resolve(args.rollout)
  const nodeModules = await locateNodeModules(path.resolve(args['kimtoma-root']))
  const requireFromKimtoma = createRequire(path.join(nodeModules, '..', 'package.json'))
  const { marked } = requireFromKimtoma('marked')
  const matter = requireFromKimtoma('gray-matter')

  const rollout = await fs.readFile(rolloutPath, 'utf8')
  const messages = extractVisibleMessages(rollout.split(/\r?\n/), args.cutoff)
  const userCount = messages.filter((message) => message.role === 'user').length
  const assistantCount = messages.length - userCount
  if (userCount !== EXPECTED_USER_MESSAGES || assistantCount !== EXPECTED_ASSISTANT_MESSAGES) {
    throw new Error(
      `Transcript count mismatch: expected ${EXPECTED_USER_MESSAGES}/${EXPECTED_ASSISTANT_MESSAGES}, got ${userCount}/${assistantCount}`,
    )
  }

  await fs.mkdir(outDir, { recursive: true })
  const transcriptMarkdown = renderTranscriptMarkdown(messages, {
    cutoff: args.cutoff,
    sourceLabel: rolloutPath,
  })
  await fs.writeFile(path.join(outDir, '02-session-transcript.md'), transcriptMarkdown, 'utf8')
  await fs.writeFile(
    path.join(outDir, '02-session-transcript.html'),
    renderTranscriptHtml(messages, { cutoff: args.cutoff }),
    'utf8',
  )

  const summaryPath = path.join(outDir, '01-project-summary.md')
  try {
    const summary = await fs.readFile(summaryPath, 'utf8')
    await fs.writeFile(
      path.join(outDir, '01-project-summary.html'),
      renderDocumentHtml({
        title: 'CrossMux KR 프로젝트 요약',
        markdownBody: summary.replace(/^# .+\n+/, ''),
        variant: 'archive',
        markedParse: (markdown) => marked.parse(markdown, { async: false }),
        metadataHtml: '<span>기록일 2026-07-18</span><span>Xteink4 실기기 검증 포함</span>',
        statusHtml: '진단 펌웨어 설치와 API 검증은 성공했습니다. 운영 릴리스는 50KiB 힙 게이트를 348B 밑돌아 보류 상태입니다.',
      }),
      'utf8',
    )
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
  }

  const blogPath = path.join(outDir, '03-kimtoma-blog-post.md')
  try {
    const blog = await fs.readFile(blogPath, 'utf8')
    const parsed = matter(blog)
    await fs.writeFile(
      path.join(outDir, '03-kimtoma-blog-post.html'),
      renderDocumentHtml({
        title: parsed.data.title,
        markdownBody: parsed.content,
        variant: 'blog',
        markedParse: (markdown) => marked.parse(markdown, { async: false }),
        metadataHtml: `<span>2026.07.18</span><span>AI ${escapeHtml(parsed.data.ai_pct)}%</span><span>${escapeHtml(parsed.data.ai_note ?? '')}</span>`,
      }),
      'utf8',
    )
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
  }

  const generated = (await fs.readdir(outDir)).filter((name) => /\.(?:md|html)$/.test(name)).length
  console.log(`transcript: ${messages.length} messages (${userCount} user, ${assistantCount} assistant)`)
  console.log(`generated: ${generated} report files`)
}

const isMain = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href
if (isMain) {
  runCli().catch((error) => {
    console.error(error.message)
    process.exitCode = 1
  })
}
