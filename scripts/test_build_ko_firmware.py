#!/usr/bin/env python3
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_ko_firmware


class KoreanFirmwareBuildPlanTest(unittest.TestCase):
    def test_prepare_reuses_the_hybrid_sdk_probe_sketch(self) -> None:
        platformio = (build_ko_firmware.ROOT / "platformio.ini").read_text(
            encoding="utf-8"
        )
        self.assertIn("+<../.dummy/sketch.cpp>", platformio)
        self.assertFalse(
            (build_ko_firmware.ROOT / "tools/ko_sdk_prepare.cpp").exists()
        )

    def test_prepare_keeps_arduino_library_builder_log_wrapper(self) -> None:
        platformio = (build_ko_firmware.ROOT / "platformio.ini").read_text(
            encoding="utf-8"
        )
        prepare_start = platformio.index("[env:ko_sdk_prepare]")
        prepare_end = platformio.index("\n[", prepare_start + 1)
        prepare_block = platformio[prepare_start:prepare_end]
        self.assertIn("-DCONFIG_LIB_BUILDER_COMPILE=1", prepare_block)

    def test_prepare_removes_project_only_linker_wrappers(self) -> None:
        platformio = (build_ko_firmware.ROOT / "platformio.ini").read_text(
            encoding="utf-8"
        )
        prepare_start = platformio.index("[env:ko_sdk_prepare]")
        prepare_end = platformio.index("\n[", prepare_start + 1)
        prepare_block = platformio[prepare_start:prepare_end]
        self.assertIn(
            "-Wl,--wrap=panic_print_backtrace,--wrap=panic_abort,"
            "--wrap=bootloader_common_check_efuse_blk_validity",
            prepare_block,
        )

    def test_tls_archive_probe_accepts_asymmetric_setup_buffers(self) -> None:
        def runner(*_args, **_kwargs):
            return SimpleNamespace(
                returncode=0,
                stdout="""
00000000 <mbedtls_ssl_setup>:
  2a: 6591 lui a1,0x4
  98: 6585 lui a1,0x1
""",
            )

        self.assertTrue(
            build_ko_firmware.tls_archive_is_bounded(
                Path("/core"), runner=runner
            )
        )

    def test_tls_archive_probe_rejects_stock_setup_buffers(self) -> None:
        def runner(*_args, **_kwargs):
            return SimpleNamespace(
                returncode=0,
                stdout="""
00000000 <mbedtls_ssl_setup>:
  2a: 6591 lui a1,0x4
  98: 6591 lui a1,0x4
""",
            )

        self.assertFalse(
            build_ko_firmware.tls_archive_is_bounded(
                Path("/core"), runner=runner
            )
        )

    def test_tls_archives_are_staged_and_installed_with_sdk_names(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            build = temp / "build"
            library = build / "esp-idf/mbedtls/mbedtls/library"
            library.mkdir(parents=True)
            for name in ("libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a"):
                (library / name).write_bytes(name.encode())

            staged = build_ko_firmware.stage_tls_libraries(
                build_dir=build, staging_dir=temp / "staged"
            )
            core = temp / "core"
            build_ko_firmware.install_tls_libraries(core, staged)
            destination = (
                core
                / "packages/framework-arduinoespressif32-libs/esp32c3/lib"
            )
            self.assertEqual(
                b"libmbedtls.a", (destination / "libmbedtls_2.a").read_bytes()
            )
            self.assertEqual(
                b"libmbedx509.a", (destination / "libmbedx509.a").read_bytes()
            )
            self.assertEqual(
                b"libmbedcrypto.a", (destination / "libmbedcrypto.a").read_bytes()
            )

    def test_stock_diagnostics_library_is_restored_after_sdk_build(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            core = temp / "core"
            diagnostics = (
                core
                / "packages/framework-arduinoespressif32-libs"
                / build_ko_firmware.DIAGNOSTICS_LIBRARY
            )
            diagnostics.parent.mkdir(parents=True)
            diagnostics.write_bytes(b"official __wrap_log_printf archive")

            preserved = build_ko_firmware.preserve_stock_diagnostics(
                core, staging_dir=temp / "preserved"
            )
            diagnostics.write_bytes(b"custom archive")
            build_ko_firmware.restore_stock_diagnostics(core, preserved)
            self.assertEqual(
                b"official __wrap_log_printf archive", diagnostics.read_bytes()
            )

    def test_fresh_sdk_build_generates_embedded_sources_before_prepare(self) -> None:
        plan = build_ko_firmware.build_plan(
            "gh_release_ko", Path("/pio"), Path("/ninja"), sdk_ready=False
        )
        self.assertEqual(
            [
                ["/pio", "run", "-e", "ko_sdk_prepare", "-t", "compiledb"],
                [
                    "/ninja",
                    "-C",
                    str(build_ko_firmware.PREPARE_BUILD),
                    "https_server.crt.S",
                    "rmaker_mqtt_server.crt.S",
                    "rmaker_claim_service_server.crt.S",
                    "rmaker_ota_server.crt.S",
                ],
                ["/pio", "run", "-e", "ko_sdk_prepare"],
                ["/pio", "run", "-e", "gh_release_ko"],
            ],
            plan,
        )

    def test_embedded_sources_are_staged_at_the_path_scons_resolves(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            build = Path(temp_dir) / "build"
            build.mkdir()
            for name in build_ko_firmware.EMBEDDED_SOURCE_TARGETS:
                (build / name).write_text(name, encoding="utf-8")

            staged = build_ko_firmware.stage_embedded_sources_for_scons(
                build_dir=build,
                scons_relative_build=Path(".pio/build/ko_sdk_prepare"),
            )

            self.assertEqual(
                build / ".pio/build/ko_sdk_prepare",
                staged,
            )
            for name in build_ko_firmware.EMBEDDED_SOURCE_TARGETS:
                self.assertEqual(name, (staged / name).read_text(encoding="utf-8"))

    def test_cached_sdk_build_only_builds_requested_firmware(self) -> None:
        plan = build_ko_firmware.build_plan(
            "gh_release_ko_rc", Path("/pio"), Path("/ninja"), sdk_ready=True
        )
        self.assertEqual([["/pio", "run", "-e", "gh_release_ko_rc"]], plan)

    def test_prepare_failure_after_sdk_install_is_tolerated(self) -> None:
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            return SimpleNamespace(returncode=1)

        build_ko_firmware.prepare_sdk(
            Path("/pio"),
            Path("/ninja"),
            Path("/core"),
            runner=runner,
            ready_check=lambda _core: True,
            state_reset=lambda: None,
            stock_preserver=lambda _core: Path("/stock"),
            tls_stager=lambda: Path("/tls"),
            embedded_source_stager=lambda: Path("/embedded"),
            artifact_installer=lambda *_args: None,
        )

        self.assertTrue(calls[-1][1]["check"] is False)

    def test_prepare_failure_before_sdk_install_is_reported(self) -> None:
        def runner(_command, **_kwargs):
            return SimpleNamespace(returncode=1)

        with self.assertRaises(SystemExit):
            build_ko_firmware.prepare_sdk(
                Path("/pio"),
                Path("/ninja"),
                Path("/core"),
                runner=runner,
                ready_check=lambda _core: False,
                state_reset=lambda: None,
                stock_preserver=lambda _core: Path("/stock"),
                tls_stager=lambda: Path("/tls"),
                embedded_source_stager=lambda: Path("/embedded"),
                artifact_installer=lambda *_args: None,
            )


if __name__ == "__main__":
    unittest.main()
