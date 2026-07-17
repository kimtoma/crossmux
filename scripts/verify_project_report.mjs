#!/usr/bin/env node

import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import { createRequire } from 'node:module'

import { extractVisibleMessages } from './generate_project_report.mjs'

const ROOT = path.resolve(import.meta.dirname, '..')
const OUT_DIR = path.join(ROOT, 'docs', 'project-report', '2026-07-18')
const ROLLOUT = '/Users/macmini/.codex/sessions/2026/07/17/rollout-2026-07-17T04-54-12-019f6c7e-6b44-7360-bcb0-cb788d780e8e.jsonl'
const CUTOFF = '2026-07-17T19:36:14.760Z'
const KIMTOMA_PACKAGE = '/Users/macmini/Projects/kimtoma.com/.worktrees/live-reading-platform/package.json'
const EXPECTED_FILES = [
  '01-project-summary.html',
  '01-project-summary.md',
  '02-session-transcript.html',
  '02-session-transcript.md',
  '03-kimtoma-blog-post.html',
  '03-kimtoma-blog-post.md',
]

function read(name) {
  return fs.readFileSync(path.join(OUT_DIR, name), 'utf8')
}

function count(source, pattern) {
  return (source.match(pattern) ?? []).length
}

function assertHtmlContract(name, source) {
  assert.ok(source.startsWith('<!DOCTYPE html>\n'), `${name}: doctype`)
  assert.match(source, /<html lang="ko">/, `${name}: lang`)
  assert.match(source, /<meta charset="utf-8">/, `${name}: charset`)
  assert.match(source, /<meta name="viewport"/, `${name}: viewport`)
  assert.match(source, /<title>[^<]+<\/title>/, `${name}: title`)
  assert.match(source, /overflow-x: hidden/, `${name}: mobile overflow containment`)
  assert.ok(source.endsWith('</html>\n'), `${name}: closing html`)
}

const files = fs.readdirSync(OUT_DIR).filter((name) => /\.(?:md|html)$/.test(name)).sort()
assert.deepEqual(files, EXPECTED_FILES)

const summaryMd = read('01-project-summary.md')
const summaryHtml = read('01-project-summary.html')
const transcriptMd = read('02-session-transcript.md')
const transcriptHtml = read('02-session-transcript.html')
const blogMd = read('03-kimtoma-blog-post.md')
const blogHtml = read('03-kimtoma-blog-post.html')

for (const [name, source] of [
  ['01-project-summary.html', summaryHtml],
  ['02-session-transcript.html', transcriptHtml],
  ['03-kimtoma-blog-post.html', blogHtml],
]) {
  assertHtmlContract(name, source)
}

const rolloutLines = fs.readFileSync(ROLLOUT, 'utf8').split(/\r?\n/)
const messages = extractVisibleMessages(rolloutLines, CUTOFF)
const userCount = messages.filter((message) => message.role === 'user').length
const assistantCount = messages.length - userCount
assert.equal(messages.length, 342)
assert.equal(userCount, 33)
assert.equal(assistantCount, 309)
assert.equal(count(transcriptMd, /^### \d+\. /gm), 342)
assert.equal(count(transcriptHtml, /<article class="message /g), 342)
assert.equal(count(transcriptHtml, /<article class="message user"/g), 33)
assert.equal(count(transcriptHtml, /<article class="message assistant"/g), 309)
assert.doesNotMatch(transcriptMd, /<recommended_plugins>/)
assert.doesNotMatch(transcriptHtml, /<recommended_plugins>/)
assert.doesNotMatch(transcriptHtml, /<script>alert/)
assert.ok(transcriptMd.includes(messages[0].text), 'first message text preserved')
assert.ok(transcriptMd.includes(messages.at(-1).text), 'last message text preserved')

for (const anchor of [
  '5,966,384B',
  'c33a36a62f6af1c0b12c567b3b662f9935288f0c6aad36e220d5d293cd180ab0',
  '50,852B',
  '51,200B',
  '348B',
  '137,872B',
]) {
  assert.equal(summaryMd.split(anchor).length - 1, 1, `summary anchor ${anchor}`)
}

const requireFromKimtoma = createRequire(KIMTOMA_PACKAGE)
const matter = requireFromKimtoma('gray-matter')
const parsedBlog = matter(blogMd)
assert.equal(parsedBlog.data.layout, 'post')
assert.equal(parsedBlog.data.title, '주머니 속 한국어 전자책 리더를 직접 만들었다')
assert.equal(parsedBlog.data.ai, true)
assert.equal(parsedBlog.data.ai_pct, 90)
assert.ok(parsedBlog.content.includes('## 348바이트 때문에 멈췄다'))
assert.doesNotMatch(blogHtml, /layout: post/)

const privatePatterns = [
  /172\.30\./,
  /\/dev\/cu\./,
  /\b[0-9a-f]{2}(?::[0-9a-f]{2}){5}\b/i,
  /\brd1_[A-Za-z0-9_-]+/,
  /Authorization/i,
  /\/Users\/macmini/,
]
for (const pattern of privatePatterns) {
  assert.doesNotMatch(blogMd, pattern, `blog privacy pattern ${pattern}`)
  assert.doesNotMatch(blogHtml, pattern, `blog HTML privacy pattern ${pattern}`)
}

for (const sourceName of ['01-project-summary.md', '03-kimtoma-blog-post.md']) {
  const source = read(sourceName)
  const links = [...source.matchAll(/\[[^\]]+\]\(([^)]+)\)/g)].map((match) => match[1])
  for (const link of links) {
    if (/^(?:https?:|\/)/.test(link)) continue
    const target = path.resolve(OUT_DIR, link)
    assert.ok(fs.existsSync(target), `${sourceName}: missing relative link ${link}`)
  }
}

console.log('project report verification: PASS')
console.log(`files: ${files.length}`)
console.log(`transcript: ${messages.length} (${userCount} user, ${assistantCount} assistant)`)
console.log('blog front matter: kimtoma gray-matter compatible')
console.log('privacy scan: PASS')
console.log('relative links: PASS')
