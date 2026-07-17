import assert from 'node:assert/strict'

import {
  extractVisibleMessages,
  renderDocumentHtml,
  renderTranscriptHtml,
  renderTranscriptMarkdown,
  stripFrontMatter,
} from './generate_project_report.mjs'

function response(timestamp, role, text, phase = '') {
  return {
    timestamp,
    type: 'response_item',
    payload: {
      type: 'message',
      role,
      phase,
      content: [{ type: role === 'assistant' ? 'output_text' : 'input_text', text }],
    },
  }
}

const fixture = [
  response(
    '2026-07-18T00:00:00.000Z',
    'user',
    '<recommended_plugins>injected</recommended_plugins>',
  ),
  response('2026-07-18T00:00:01.000Z', 'user', '사용자 원문\n둘째 줄'),
  response('2026-07-18T00:00:02.000Z', 'assistant', '응답 **원문**', 'commentary'),
  {
    timestamp: '2026-07-18T00:00:03.000Z',
    type: 'response_item',
    payload: { type: 'reasoning', summary: ['internal'] },
  },
  response(
    '2026-07-18T00:00:04.000Z',
    'assistant',
    '<script>alert(1)</script>',
    'final_answer',
  ),
  response('2026-07-18T00:00:05.000Z', 'user', 'cutoff 이후'),
]

const lines = fixture.map((item) => JSON.stringify(item))
const messages = extractVisibleMessages(lines, '2026-07-18T00:00:04.000Z')

assert.deepEqual(
  messages.map(({ role, text }) => ({ role, text })),
  [
    { role: 'user', text: '사용자 원문\n둘째 줄' },
    { role: 'assistant', text: '응답 **원문**' },
    { role: 'assistant', text: '<script>alert(1)</script>' },
  ],
)

const transcriptMarkdown = renderTranscriptMarkdown(messages, {
  cutoff: '2026-07-18T00:00:04.000Z',
  sourceLabel: 'fixture.jsonl',
})
assert.match(transcriptMarkdown, /사용자 원문\n둘째 줄/)
assert.equal((transcriptMarkdown.match(/^### \d+\. /gm) ?? []).length, 3)

const transcriptHtml = renderTranscriptHtml(messages, {
  cutoff: '2026-07-18T00:00:04.000Z',
})
assert.match(transcriptHtml, /&lt;script&gt;alert\(1\)&lt;\/script&gt;/)
assert.doesNotMatch(transcriptHtml, /<script>alert/)
assert.equal((transcriptHtml.match(/<article class="message /g) ?? []).length, 3)

const blogSource = `---
layout: post
title: "테스트 제목"
ai: true
---

# 본문 제목

테스트 본문
`
const stripped = stripFrontMatter(blogSource)
assert.equal(stripped.data.layout, 'post')
assert.equal(stripped.data.title, '테스트 제목')
assert.equal(stripped.data.ai, true)
assert.doesNotMatch(stripped.content, /^---/m)

const blogHtml = renderDocumentHtml({
  title: stripped.data.title,
  markdownBody: stripped.content,
  variant: 'blog',
  markedParse: (markdown) => `<main>${markdown.trim()}</main>`,
  metadataHtml: '<span>AI 90%</span>',
})
assert.doesNotMatch(blogHtml, /^---/m)
assert.match(blogHtml, /lang="ko"/)
assert.match(blogHtml, /<meta charset="utf-8">/)

console.log('project report generator tests: PASS')
