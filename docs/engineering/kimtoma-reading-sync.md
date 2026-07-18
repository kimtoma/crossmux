# kimtoma.com Reading Sync (Korean Firmware)

This integration is compiled only into `gh_release_ko` and
`gh_release_ko_rc`. It is local-first: the reader saves its normal reading
statistics before it creates a sync snapshot, and a network failure never
rolls back local reading data.

## Device experience

The Korean build uses the approved 120x120 monochrome profile mark on the
default boot and sleep screens, centered from the oriented screen dimensions,
with `@kimtoma` underneath. Custom sleep bitmaps and book-cover sleep screens
are unchanged. Regenerate and verify the 1,800-byte 1-bit asset with:

```bash
python3 scripts/gen_kimtoma_mark.py --png /tmp/kimtoma-mark.png
python3 -m unittest scripts/test_gen_kimtoma_mark.py
```

The Apps menu contains **kimtoma 서재**. It displays the last server-accepted
book, author, progress, accepted time, pending record, and cover state without
turning Wi-Fi on merely by opening the screen. **다시 동기화** reuses the
one-shot sync worker. **연동 설정** opens the same activity in settings mode.

System settings keeps two separate actions:

1. **kimtoma.com 연동** — status, **연결 테스트**, and manual retry.
2. **온라인 서재 서버** — the existing generic OPDS server manager.

The raw device token is never displayed or edited on the X4. The settings mode
directs the user to the protected File Transfer web settings instead.

## Release gate

Run the complete local gate from the repository root. Build the simulator SKUs
sequentially because i18n generation uses shared generated files.

Activate the repository virtual environment first so the recursive
`requirements.txt` dependencies, including PyYAML, are available:

```bash
source .venv/bin/activate
python3 -m pip install -r requirements.txt
```

```bash
python3 -m unittest lib/EpdFont/scripts/test_build_ko_charset.py -v
python3 scripts/test_ko_workflow_gates.py -v
python3 scripts/test_verify_ko_release.py -v
cmake -S test -B build/test
cmake --build build/test -j
ctest --test-dir build/test --output-on-failure -j
cmake -S simulator -B simulator/build_ko -DSIMULATOR_KOREAN_VERSION=ON
cmake --build simulator/build_ko -j
pio run -e gh_release
pio run -e gh_release_ko
python3 scripts/verify_ko_release.py
git diff --check
git status --short
```

The verifier reads the freshly built `firmware.bin` and `firmware.map` from
`.pio/build/gh_release_ko/`. Nightly uses the same verifier with
`--environment gh_release_ko_rc`; other environments are rejected. Do not
install a binary above the hard maximum of 6,029,312 bytes. A binary above the
preferred target of 5,898,240 bytes needs explicit acceptance before flashing.

The four KO-building workflows run the charset regression, verifier regression,
and binary verifier after the matching KO build. If the gate fails, they retain
`ko-verifier.log` plus that environment's `firmware.map` and `firmware.bin`; no
KO asset is staged or published first.

## Configure a device token

1. Start the reader's Wi-Fi web server and open its settings page.
2. Under **kimtoma.com 독서 연동**, paste the device-only token into the
   password field and choose **저장**.
3. Choose **연결 테스트**. A successful test shows that kimtoma.com accepted
   the token. It does not change the current book or queue sequence.
4. To revoke the local credential, choose **토큰 삭제** and confirm. A new valid
   token also clears an authentication pause.

The token format is `rd1_` followed by 43 URL-safe characters. The browser
field is cleared after saving, status exposes only the final four characters,
and neither the full token nor its stored representation is returned by the
settings API.

The token is stored at `/.crosspoint/reading_sync/config.json`. It is tied to
the device MAC and obfuscated before it is written. **This is obfuscation, not
encryption**: anyone who can read the device and firmware should be treated as
capable of recovering the token. Use a revocable, device-scoped token rather
than a general account secret.

The connection test calls:

```text
POST https://api.kimtoma.com/v1/reading/sync?validateOnly=1
```

with an empty JSON object and the stored device token. `validateOnly=1` checks
authentication without updating current-book data or consuming a sequence.
The local `/api/reading-sync/test` endpoint maps a successful validation to
200, an invalid/revoked token to 401 or 403, and a temporary network or server
failure to 503.

## Queue and cover files

All integration state is kept on the SD card:

| Path | Purpose |
|---|---|
| `/.crosspoint/reading_sync/queue.json` | Atomic single-pending metadata, sequence, auth-pause, terminal state, optional cover job, and optional last-accepted display summary. Maximum 8 KiB. |
| `/.crosspoint/reading_sync/queue.json.tmp` | Same-directory atomic-write temporary file. |
| `/.crosspoint/reading_sync/queue.json.corrupt` | Preserved invalid queue for diagnosis. |
| `/.crosspoint/reading_sync/config.json` | Schema version and obfuscated device token. Maximum 8 KiB. |
| `/.crosspoint/reading_sync/config.json.tmp` | Credential atomic-write temporary file. |
| `/.crosspoint/reading_sync/covers/.tmp` | Cover stream temporary file. |
| `/.crosspoint/reading_sync/covers/{sha256}.jpg` | Immutable staged JPEG original. |
| `/.crosspoint/reading_sync/covers/{sha256}.png` | Immutable staged PNG original. |

An invalid `queue.json` is preserved as `queue.json.corrupt`, the integration
stops sending, and local reading remains available. Power the device off before
repair: copy `queue.json` and `queue.json.corrupt` for diagnosis, remove only
the invalid `queue.json`, then reboot. This restarts with an empty sync queue;
it does not remove the normal reading-statistics store.

`queue.json` is schema 2. The network API payload remains wire schema 1; these
versions are deliberately independent. A schema-1 queue migrates atomically to
schema 2 while preserving pending metadata, cover work, authentication pause,
terminal state, and accepted fingerprint. It starts without a display summary
until the next `accepted` or `duplicate` response. `stale` advances the queue
but does not replace the last-accepted display book. Unknown future queue
schemas are preserved as corrupt instead of being overwritten.

The dashboard state priority is fixed: queue corrupt, token not configured,
authentication required, worker running, pending, then ready. A connection
test and a sync cannot run concurrently. Both operations release reading-stat
memory before Wi-Fi/TLS and restore it while tearing Wi-Fi down on every exit.

Only original JPEG and PNG covers up to 2,097,152 bytes are staged. A missing,
unsupported, malformed, or oversized cover does not block metadata sync. The
firmware uploads a staged cover only when the server returns
`coverRequired: true`; metadata acceptance is not rolled back if that separate
upload fails.

## Retry and HTTP behavior

The latest qualifying snapshot is coalesced into one pending item. A session
qualifies after 180,000 ms, a progress increase of at least one percentage
point, or completion. The worker uses the last saved Wi-Fi network, allows 8 s
to associate, uses a 15 s HTTPS timeout, and tears Wi-Fi down after the one-shot
attempt. **지금 동기화** schedules another attempt for the next home-screen
trigger; it does not bypass authentication or queue-corruption pauses.

| Result | Metadata queue | Cover job |
|---|---|---|
| 200 `accepted`, `duplicate`, or `stale` | Clear matching pending item and advance from `lastAcceptedSequence`. | Upload only if required; clear after a successful upload. |
| 400 | Drop the matching unusable metadata as terminal `bad_request`; preserve local stats. | Drop only the unusable cover job. |
| 401 / 403 | Keep pending data and persist an authentication pause until the token changes or is removed. | Keep the cover and apply the same authentication pause. |
| 413 | Drop the matching unusable metadata as terminal `payload_too_large`; preserve local stats. | Drop only the oversized/rejected cover job. |
| 422 | Drop the matching unusable metadata as terminal `unprocessable`; preserve local stats. | Drop only the rejected cover job. |
| 429, 5xx, timeout, offline, or cancellation | Keep pending data for a later trigger. | Keep the staged cover for a later trigger. |

## Physical acceptance and rollback

Do not flash until a staged API and device token exist and the final binary-size
checkpoint is explicitly accepted. Keep the previous known-good
`firmware-ko.bin` and record its SHA-256 before installing the candidate. The
physical acceptance record must cover the Korean font tiers and source spaces,
session thresholds and coalescing, offline/5xx/auth/stale behavior, original
JPEG/PNG/no-cover/oversized-cover cases, cancellation, and ten consecutive
syncs with free heap above 50 KiB and no downward trend.

If acceptance fails, stop the test, preserve serial logs with credentials
redacted, and restore the previous `firmware-ko.bin` through the web flasher's
X4 target. Some installed builds do not expose an SD Card Firmware Update menu,
so do not depend on that path. The supported flasher writes the next OTA
partition and updates boot selection together. Do not treat a raw write to a
fixed app offset as a complete A/B rollback because OTA boot metadata may still
select the other slot.

For any USB recovery, re-detect the port, verify the ESP32-C3 identity, preserve
the previous image and its hash, and obtain explicit approval before running
the repository's documented flash procedure. Release publication is a separate
production-state checkpoint after the physical matrix passes.
