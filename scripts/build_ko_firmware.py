#!/usr/bin/env python3
"""Build a KO firmware with the bounded-memory TLS SDK configuration."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PREPARE_BUILD = ROOT / ".pio" / "build" / "ko_sdk_prepare"
PREPARE_RELATIVE_BUILD = PREPARE_BUILD.relative_to(ROOT)
TLS_MARKERS = (
    "CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y",
    "CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384",
    "CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096",
)
EMBEDDED_SOURCE_TARGETS = (
    "https_server.crt.S",
    "rmaker_mqtt_server.crt.S",
    "rmaker_claim_service_server.crt.S",
    "rmaker_ota_server.crt.S",
)
KO_ENVIRONMENTS = ("gh_release_ko", "gh_release_ko_rc")
DIAGNOSTICS_LIBRARY = Path("esp32c3/lib/libespressif__esp_diagnostics.a")
LOG_WRAPPER_SYMBOL = b"__wrap_log_printf"
TLS_LIBRARY = Path("esp32c3/lib/libmbedtls_2.a")
STOCK_STAGING = ROOT / ".pio" / "ko-sdk-stock"
TLS_STAGING = ROOT / ".pio" / "ko-sdk-tls"
TLS_LIBRARY_NAMES = ("libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a")


def tls_archive_is_bounded(
    core_dir: Path, *, runner=subprocess.run
) -> bool:
    """Verify that mbedTLS setup allocates 16 KiB RX and 4 KiB TX buffers."""
    objdump = (
        core_dir
        / "packages"
        / "toolchain-riscv32-esp"
        / "bin"
        / "riscv32-esp-elf-objdump"
    )
    archive = (
        core_dir
        / "packages"
        / "framework-arduinoespressif32-libs"
        / TLS_LIBRARY
    )
    try:
        result = runner(
            [
                str(objdump),
                "-dr",
                "--disassemble=mbedtls_ssl_setup",
                str(archive),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return False
    if result.returncode != 0:
        return False
    setup = result.stdout.partition("<mbedtls_ssl_setup>:")[2]
    buffer_pages = re.findall(r"\blui\s+a1,0x([0-9a-fA-F]+)", setup)
    return buffer_pages[:2] == ["4", "1"]


def platformio_core_dir() -> Path:
    configured = os.environ.get("PLATFORMIO_CORE_DIR")
    return Path(configured).expanduser() if configured else Path.home() / ".platformio"


def find_tool(name: str, fallback: Path) -> Path:
    discovered = shutil.which(name)
    if discovered:
        return Path(discovered)
    if fallback.is_file():
        return fallback
    raise SystemExit(f"missing required build tool: {name}")


def sdk_is_ready(core_dir: Path | None = None) -> bool:
    core = core_dir or platformio_core_dir()
    generated_defaults = ROOT / "sdkconfig.defaults"
    installed_sdkconfig = (
        core
        / "packages"
        / "framework-arduinoespressif32-libs"
        / "esp32c3"
        / "sdkconfig"
    )
    for path in (generated_defaults, installed_sdkconfig):
        if not path.is_file():
            return False
        content = path.read_text(encoding="utf-8", errors="ignore")
        if not all(marker in content for marker in TLS_MARKERS):
            return False
    diagnostics = (
        core
        / "packages"
        / "framework-arduinoespressif32-libs"
        / DIAGNOSTICS_LIBRARY
    )
    return (
        diagnostics.is_file()
        and LOG_WRAPPER_SYMBOL in diagnostics.read_bytes()
        and tls_archive_is_bounded(core)
    )


def invalidate_prepare_state() -> None:
    """Force pioarduino to rebuild rather than trust a partial SDK install."""
    shutil.rmtree(PREPARE_BUILD, ignore_errors=True)
    for path in (
        ROOT / "sdkconfig.defaults",
        ROOT / "sdkconfig.ko_sdk_prepare",
    ):
        path.unlink(missing_ok=True)


def preserve_stock_diagnostics(
    core: Path, *, staging_dir: Path = STOCK_STAGING
) -> Path:
    """Keep the official log wrapper that pioarduino's SDK build expects."""
    source = (
        core
        / "packages"
        / "framework-arduinoespressif32-libs"
        / DIAGNOSTICS_LIBRARY
    )
    if not source.is_file() or LOG_WRAPPER_SYMBOL not in source.read_bytes():
        raise SystemExit("official ESP diagnostics library is unavailable")
    shutil.rmtree(staging_dir, ignore_errors=True)
    staging_dir.mkdir(parents=True)
    destination = staging_dir / source.name
    shutil.copy2(source, destination)
    return destination


def restore_stock_diagnostics(core: Path, preserved: Path) -> None:
    destination = (
        core
        / "packages"
        / "framework-arduinoespressif32-libs"
        / DIAGNOSTICS_LIBRARY
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(preserved, destination)


def stage_tls_libraries(
    *, build_dir: Path = PREPARE_BUILD, staging_dir: Path = TLS_STAGING
) -> Path:
    """Save nested mbedTLS archives before pioarduino removes the IDF build."""
    source_dir = build_dir / "esp-idf" / "mbedtls" / "mbedtls" / "library"
    shutil.rmtree(staging_dir, ignore_errors=True)
    staging_dir.mkdir(parents=True)
    for name in TLS_LIBRARY_NAMES:
        source = source_dir / name
        if not source.is_file():
            raise SystemExit(f"custom TLS archive was not built: {source}")
        shutil.copy2(source, staging_dir / name)
    return staging_dir


def stage_embedded_sources_for_scons(
    *,
    build_dir: Path = PREPARE_BUILD,
    scons_relative_build: Path = PREPARE_RELATIVE_BUILD,
) -> Path:
    """Mirror CMake-generated sources where pioarduino's SCons pass resolves them."""
    destination = build_dir / scons_relative_build
    destination.mkdir(parents=True, exist_ok=True)
    for name in EMBEDDED_SOURCE_TARGETS:
        source = build_dir / name
        if not source.is_file():
            raise SystemExit(f"embedded source was not generated: {source}")
        shutil.copy2(source, destination / name)
    return destination


def install_tls_libraries(core: Path, staged: Path) -> None:
    destination = (
        core
        / "packages"
        / "framework-arduinoespressif32-libs"
        / "esp32c3"
        / "lib"
    )
    destination.mkdir(parents=True, exist_ok=True)
    names = {
        "libmbedtls.a": "libmbedtls_2.a",
        "libmbedx509.a": "libmbedx509.a",
        "libmbedcrypto.a": "libmbedcrypto.a",
    }
    for source_name, destination_name in names.items():
        shutil.copy2(staged / source_name, destination / destination_name)


def install_prepared_sdk_artifacts(
    core: Path, preserved_diagnostics: Path, staged_tls: Path
) -> None:
    restore_stock_diagnostics(core, preserved_diagnostics)
    install_tls_libraries(core, staged_tls)


def build_plan(
    environment: str, pio: Path, ninja: Path, *, sdk_ready: bool
) -> list[list[str]]:
    if environment not in KO_ENVIRONMENTS:
        raise ValueError(f"unsupported KO environment: {environment}")
    commands: list[list[str]] = []
    if not sdk_ready:
        commands.extend(
            [
                [str(pio), "run", "-e", "ko_sdk_prepare", "-t", "compiledb"],
                [
                    str(ninja),
                    "-C",
                    str(PREPARE_BUILD),
                    *EMBEDDED_SOURCE_TARGETS,
                ],
                [str(pio), "run", "-e", "ko_sdk_prepare"],
            ]
        )
    commands.append([str(pio), "run", "-e", environment])
    return commands


def prepare_sdk(
    pio: Path,
    ninja: Path,
    core: Path,
    *,
    runner=subprocess.run,
    ready_check=sdk_is_ready,
    state_reset=invalidate_prepare_state,
    stock_preserver=preserve_stock_diagnostics,
    tls_stager=stage_tls_libraries,
    embedded_source_stager=stage_embedded_sources_for_scons,
    artifact_installer=install_prepared_sdk_artifacts,
) -> None:
    preserved_diagnostics = stock_preserver(core)
    state_reset()
    commands = build_plan("gh_release_ko", pio, ninja, sdk_ready=False)[:-1]
    for command in commands[:-1]:
        runner(command, cwd=ROOT, check=True)

    embedded_source_stager()
    staged_tls = tls_stager()

    # pioarduino installs the rebuilt SDK before its final tiny probe link.
    # That probe can fail on project-specific linker wrappers even though the
    # SDK installation itself completed, so validate the installed artifacts.
    result = runner(commands[-1], cwd=ROOT, check=False)
    artifact_installer(core, preserved_diagnostics, staged_tls)
    if not ready_check(core):
        raise SystemExit(
            "custom KO SDK preparation did not install a complete TLS SDK "
            f"(prepare exit={result.returncode})"
        )


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--environment", choices=KO_ENVIRONMENTS, default="gh_release_ko"
    )
    args = parser.parse_args(argv)

    core = platformio_core_dir()
    pio = find_tool("pio", core / "penv" / "bin" / "pio")
    ninja = find_tool("ninja", core / "packages" / "tool-ninja" / "ninja")
    if not sdk_is_ready(core):
        prepare_sdk(pio, ninja, core)
    subprocess.run(
        [str(pio), "run", "-e", args.environment], cwd=ROOT, check=True
    )


if __name__ == "__main__":
    main()
