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
