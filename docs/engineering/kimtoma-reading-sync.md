# kimtoma.com Reading Sync (Korean Firmware)

This integration is compiled only into `gh_release_ko` and
`gh_release_ko_rc`. It is local-first: the reader saves its normal reading
statistics before it creates a sync snapshot, and a network failure never
rolls back local reading data.

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
| `/.crosspoint/reading_sync/queue.json` | Atomic single-pending metadata, sequence, auth-pause, terminal state, and optional cover job. Maximum 8 KiB. |
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
redacted, and restore the previous `firmware-ko.bin` through the device's SD
Card Firmware Update file picker or the web flasher's X4 target. Those paths
write the next OTA partition and update boot selection together. Do not treat a
raw write to a fixed app offset as a complete A/B rollback because OTA boot
metadata may still select the other slot.

For any USB recovery, re-detect the port, verify the ESP32-C3 identity, preserve
the previous image and its hash, and obtain explicit approval before running
the repository's documented flash procedure. Release publication is a separate
production-state checkpoint after the physical matrix passes.
