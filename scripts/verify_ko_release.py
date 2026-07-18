#!/usr/bin/env python3
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
KO_ENVIRONMENTS = ("gh_release_ko", "gh_release_ko_rc")
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
    marker_pattern = re.escape(marker).replace(r"\ ", r"\s*")
    marker_pattern = marker_pattern.replace(r"\(", r"\(\s*")
    match = re.search(marker_pattern, source)
    if match is None:
        fail(f"missing call contract: {marker}")
    start = match.start()
    opening = source.find("(", start)
    return extract_balanced(source, opening, "(", ")")


def extract_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        fail(f"missing function contract: {signature}")
    opening = source.find("{", start)
    return extract_balanced(source, opening, "{", "}")


def is_macro_guarded(source: str, position: int, macro: str) -> bool:
    # Each frame tracks whether the selected branch can be reached when the
    # target macro is undefined/defined, followed by whether a later elif/else
    # remains reachable for those two states. Unknown expressions deliberately
    # keep both paths possible so they cannot create a false positive guard.
    stack: list[list[bool]] = []
    for line in source[:position].splitlines():
        stripped = line.strip()
        if stripped.startswith("#ifdef ") or stripped.startswith("#ifndef ") or stripped.startswith("#if "):
            can_be_true, can_be_false = condition_possibilities(stripped, macro)
            stack.append([can_be_true[0], can_be_true[1], can_be_false[0], can_be_false[1]])
        elif stripped.startswith("#else") and stack:
            frame = stack[-1]
            frame[0], frame[1] = frame[2], frame[3]
            frame[2], frame[3] = False, False
        elif stripped.startswith("#elif") and stack:
            frame = stack[-1]
            can_be_true, can_be_false = condition_possibilities(stripped, macro)
            frame[0], frame[1] = frame[2] and can_be_true[0], frame[3] and can_be_true[1]
            frame[2], frame[3] = frame[2] and can_be_false[0], frame[3] and can_be_false[1]
        elif stripped.startswith("#endif") and stack:
            stack.pop()
    reachable_without_macro = all(frame[0] for frame in stack)
    reachable_with_macro = all(frame[1] for frame in stack)
    return not reachable_without_macro and reachable_with_macro


def condition_possibilities(directive: str, macro: str) -> tuple[tuple[bool, bool], tuple[bool, bool]]:
    if directive.startswith("#ifdef "):
        if directive.split(maxsplit=1)[1] == macro:
            return (False, True), (True, False)
        return (True, True), (True, True)
    if directive.startswith("#ifndef "):
        if directive.split(maxsplit=1)[1] == macro:
            return (True, False), (False, True)
        return (True, True), (True, True)

    expression = directive.split(maxsplit=1)[1].strip()
    escaped_macro = re.escape(macro)
    positive = rf"(?:defined\s*\(\s*{escaped_macro}\s*\)|defined\s+{escaped_macro}|{escaped_macro})"
    if re.fullmatch(positive, expression):
        return (False, True), (True, False)
    if re.fullmatch(rf"!\s*{positive}", expression):
        return (True, False), (False, True)
    return (True, True), (True, True)


def build_directory(environment: str) -> Path:
    if environment not in KO_ENVIRONMENTS:
        raise ValueError(f"unsupported KO environment: {environment}")
    return ROOT / ".pio" / "build" / environment


def format_success(environment: str, size: int, headroom: int, target: str) -> str:
    if environment == "gh_release_ko":
        return f"OK size={size} headroom={headroom} target={target}"
    return f"OK environment={environment} size={size} headroom={headroom} target={target}"


def environment_section(ini: str, environment: str) -> str:
    marker = f"[env:{environment}]"
    start = ini.find(marker)
    if start < 0:
        fail(f"missing PlatformIO environment: {environment}")
    following = re.search(r"^\[env:[^]]+\]", ini[start + len(marker) :], re.MULTILINE)
    end = start + len(marker) + following.start() if following else len(ini)
    return ini[start:end]


def verify_kimtoma_device_contracts() -> None:
    korean = (ROOT / "lib/I18n/translations/korean.yaml").read_text(encoding="utf-8")
    for exact in (
        'STR_KIMTOMA_BRAND: "@kimtoma"',
        'STR_KIMTOMA_LIBRARY: "kimtoma 서재"',
        'STR_KIMTOMA_INTEGRATION: "kimtoma.com 연동"',
        'STR_OPDS_SERVERS: "온라인 서재 서버"',
    ):
        if exact not in korean:
            fail(f"missing exact kimtoma Korean label: {exact}")

    for relative_path in (
        "src/activities/boot_sleep/BootActivity.cpp",
        "src/activities/boot_sleep/SleepActivity.cpp",
    ):
        source = (ROOT / relative_path).read_text(encoding="utf-8")
        for contract in (
            "ENABLE_KOREAN_VERSION",
            "KIMTOMA_MARK_120",
            "STR_KIMTOMA_BRAND",
            "Logo120",
            "STR_CROSSPOINT",
            "(pageWidth - 120) / 2",
            "(pageHeight - 120) / 2",
        ):
            if contract not in source:
                fail(f"missing Korean branding contract in {relative_path}: {contract}")

    mark_header = (ROOT / "src/images/KimtomaMark120.h").read_text(encoding="utf-8")
    mark_bytes = re.findall(r"\b0x[0-9a-f]{2}\b", mark_header)
    if "KIMTOMA_MARK_120" not in mark_header or len(mark_bytes) != 1800:
        fail("kimtoma mark must be exactly 120x120 1-bit (1,800 bytes)")

    apps = (ROOT / "src/activities/apps/AppsMenuActivity.cpp").read_text(encoding="utf-8")
    settings = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(encoding="utf-8")
    activity = (ROOT / "src/activities/apps/kimtoma/KimtomaLibraryActivity.cpp").read_text(encoding="utf-8")
    for contract in ("ENABLE_KIMTOMA_READING_SYNC", "STR_KIMTOMA_LIBRARY", "goToKimtomaLibrary"):
        if contract not in apps:
            fail(f"missing kimtoma Apps route contract: {contract}")
    for contract in ("SettingAction::KimtomaIntegration", "STR_KIMTOMA_INTEGRATION", "STR_OPDS_SERVERS"):
        if contract not in settings:
            fail(f"missing kimtoma System route contract: {contract}")
    if "tokenForRequest" in activity or "default:" in activity:
        fail("kimtoma activity must keep tokens out of rendering and use closed mode dispatch")

    queue_header = (ROOT / "src/reading_sync/ReadingSyncQueue.h").read_text(encoding="utf-8")
    types_header = (ROOT / "src/reading_sync/ReadingSyncTypes.h").read_text(encoding="utf-8")
    if "kSchemaVersion = 2" not in queue_header or "kWireSchemaVersion = 1" not in types_header:
        fail("queue schema 2 and API wire schema 1 must remain decoupled")


def verify(environment: str) -> None:
    build = build_directory(environment)
    binary = build / "firmware.bin"
    map_file = build / "firmware.map"

    ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    ko = environment_section(ini, environment)
    verify_kimtoma_device_contracts()
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

    if not binary.exists() or not map_file.exists():
        fail(f"build {environment} before verification")

    size = binary.stat().st_size
    if size > HARD_MAX:
        fail(f"firmware {size} exceeds hard maximum {HARD_MAX}")

    map_text = map_file.read_text(encoding="utf-8", errors="ignore")
    linked = [symbol for symbol in FORBIDDEN if re.search(rf"\b{re.escape(symbol)}\b", map_text)]
    if linked:
        fail("forbidden linked symbols: " + ", ".join(linked))

    headroom = 6_553_600 - size
    target = "met" if size <= TARGET_MAX else "not-met"
    print(format_success(environment, size, headroom, target))


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Verify a Korean release firmware build")
    parser.add_argument("--environment", choices=KO_ENVIRONMENTS, default="gh_release_ko")
    args = parser.parse_args(argv)
    verify(args.environment)


if __name__ == "__main__":
    main()
