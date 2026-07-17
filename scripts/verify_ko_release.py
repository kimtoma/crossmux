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
READING_SYNC_ROUTES = (
    ("/api/reading-sync/status", "HTTP_GET", "handleReadingSyncStatus", None),
    ("/api/reading-sync/token", "HTTP_POST", "handleReadingSyncTokenPost", "handleReadingSyncTokenRaw"),
    ("/api/reading-sync/token", "HTTP_DELETE", "handleReadingSyncTokenDelete", "handleReadingSyncDiscardRaw"),
    ("/api/reading-sync/test", "HTTP_POST", "handleReadingSyncTest", "handleReadingSyncDiscardRaw"),
    ("/api/reading-sync/retry", "HTTP_POST", "handleReadingSyncRetry", "handleReadingSyncDiscardRaw"),
)


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def extract_balanced(source: str, opening: int, opening_char: str, closing_char: str) -> str:
    depth = 0
    quote = None
    escaped = False
    for index in range(opening, len(source)):
        character = source[index]
        if quote is not None:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
            continue
        if character in ('"', "'"):
            quote = character
        elif character == opening_char:
            depth += 1
        elif character == closing_char:
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    fail(f"unterminated {opening_char}{closing_char} block")


def extract_call(source: str, marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing call contract: {marker}")
    opening = source.find("(", start)
    return extract_balanced(source, opening, "(", ")")


def extract_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        fail(f"missing function contract: {signature}")
    opening = source.find("{", start)
    return extract_balanced(source, opening, "{", "}")


def is_macro_guarded(source: str, position: int, macro: str) -> bool:
    stack: list[tuple[str, bool]] = []
    for line in source[:position].splitlines():
        stripped = line.strip()
        if stripped.startswith("#ifdef ") or stripped.startswith("#ifndef ") or stripped.startswith("#if "):
            stack.append((stripped, False))
        elif stripped.startswith("#else") and stack:
            condition, _ = stack[-1]
            stack[-1] = (condition, True)
        elif stripped.startswith("#elif") and stack:
            stack[-1] = (stripped, False)
        elif stripped.startswith("#endif") and stack:
            stack.pop()
    return any(macro in condition and not in_else for condition, in_else in stack)


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

web_server_path = ROOT / "src/network/CrossPointWebServer.cpp"
web_header_path = ROOT / "src/network/CrossPointWebServer.h"
raw_state_path = ROOT / "src/network/ReadingSyncRawRequestState.h"
web_server = web_server_path.read_text(encoding="utf-8")
web_header = web_header_path.read_text(encoding="utf-8")
settings_page = (ROOT / "src/network/html/SettingsPage.html").read_text(encoding="utf-8")
for route, method, final_handler, raw_handler in READING_SYNC_ROUTES:
    if route not in web_server:
        fail(f"missing reading sync route in web server: {route}")
    if route not in settings_page:
        fail(f"missing reading sync route in settings page: {route}")
    registration = extract_call(web_server, f'server->on("{route}", {method}')
    if final_handler not in registration:
        fail(f"missing final handler for {method} {route}")
    if raw_handler is not None and raw_handler not in registration:
        fail(f"missing raw callback for {method} {route}")

for source_name, source in ((web_server_path.name, web_server), (web_header_path.name, web_header)):
    for match in re.finditer(r"\bhandleReadingSync[A-Za-z0-9_]*\b", source):
        if not is_macro_guarded(source, match.start(), "ENABLE_KIMTOMA_READING_SYNC"):
            fail(f"unguarded reading sync handler in {source_name}: {match.group(0)}")

token_handler = extract_function_body(web_server, "void CrossPointWebServer::handleReadingSyncTokenPost()")
if 'arg("plain")' in token_handler or re.search(r"\b(?:String|std::string)\b", token_handler):
    fail("reading sync token handler must parse only the fixed raw buffer")

for contract in ("RAW_START", "RAW_WRITE", "RAW_END", "RAW_ABORTED"):
    if contract not in web_server:
        fail(f"missing raw request lifecycle contract: {contract}")

if not raw_state_path.exists():
    fail("missing fixed reading sync raw request state")
raw_state = raw_state_path.read_text(encoding="utf-8")
for contract in (
    "kTokenBodyCapacity = 256",
    "std::array<char, kTokenBodyCapacity + 1>",
    "overflowed()",
    "complete()",
):
    if contract not in raw_state:
        fail(f"missing bounded raw state contract: {contract}")
if re.search(r"\b(?:String|std::string|std::vector)\b", raw_state):
    fail("raw request state must not use dynamically sized body storage")

if "tokenObfuscated" in web_server:
    fail("web server must not access the obfuscated reading sync token")
if "tokenForRequest()" in settings_page:
    fail("settings page must not access the reading sync request token")
if re.search(r"rd1_[A-Za-z0-9_-]{20,}", settings_page):
    fail("settings page contains a full reading sync token-like literal")

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
