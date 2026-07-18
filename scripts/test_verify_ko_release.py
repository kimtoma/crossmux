#!/usr/bin/env python3
import unittest

import verify_ko_release


class CallExtractionTest(unittest.TestCase):
    def test_extract_call_accepts_clang_format_line_break_after_open_paren(self) -> None:
        source = """server->on(
    "/api/reading-sync/token", HTTP_POST,
    [] { handleReadingSyncTokenPost(); });
"""

        try:
            call = verify_ko_release.extract_call(
                source, 'server->on("/api/reading-sync/token", HTTP_POST'
            )
        except SystemExit as error:
            self.fail(f"formatted route registration was rejected: {error}")

        self.assertIn("handleReadingSyncTokenPost", call)


class MacroGuardTest(unittest.TestCase):
    def assert_guarded(self, source: str, expected: bool) -> None:
        position = source.index("handleReadingSyncStatus")
        self.assertEqual(
            expected,
            verify_ko_release.is_macro_guarded(
                source, position, "ENABLE_KIMTOMA_READING_SYNC"
            ),
        )

    def test_ifndef_active_branch_is_not_positive_guard(self) -> None:
        self.assert_guarded(
            """#ifndef ENABLE_KIMTOMA_READING_SYNC
void handleReadingSyncStatus();
#endif
""",
            False,
        )

    def test_ifndef_else_branch_is_positive_guard(self) -> None:
        self.assert_guarded(
            """#ifndef ENABLE_KIMTOMA_READING_SYNC
void unrelated();
#else
void handleReadingSyncStatus();
#endif
""",
            True,
        )

    def test_ifdef_active_branch_is_positive_guard(self) -> None:
        self.assert_guarded(
            """#ifdef ENABLE_KIMTOMA_READING_SYNC
void handleReadingSyncStatus();
#endif
""",
            True,
        )

    def test_ifdef_else_branch_is_not_positive_guard(self) -> None:
        self.assert_guarded(
            """#ifdef ENABLE_KIMTOMA_READING_SYNC
void unrelated();
#else
void handleReadingSyncStatus();
#endif
""",
            False,
        )

    def test_elif_positive_guard_after_unrelated_condition(self) -> None:
        self.assert_guarded(
            """#if ENABLE_OTHER_FEATURE
void unrelated();
#elif defined(ENABLE_KIMTOMA_READING_SYNC)
void handleReadingSyncStatus();
#endif
""",
            True,
        )

    def test_elif_after_positive_branch_is_not_positive_guard(self) -> None:
        self.assert_guarded(
            """#if defined(ENABLE_KIMTOMA_READING_SYNC)
void unrelated();
#elif ENABLE_OTHER_FEATURE
void handleReadingSyncStatus();
#endif
""",
            False,
        )

    def test_contradictory_nested_guards_are_not_positive(self) -> None:
        self.assert_guarded(
            """#ifndef ENABLE_KIMTOMA_READING_SYNC
#ifdef ENABLE_KIMTOMA_READING_SYNC
void handleReadingSyncStatus();
#endif
#endif
""",
            False,
        )

    def test_or_expression_is_not_a_sufficient_guard(self) -> None:
        self.assert_guarded(
            """#if defined(ENABLE_KIMTOMA_READING_SYNC) || ENABLE_OTHER_FEATURE
void handleReadingSyncStatus();
#endif
""",
            False,
        )


class EnvironmentSelectionTest(unittest.TestCase):
    def test_nightly_environment_selects_matching_build_directory(self) -> None:
        self.assertEqual(
            verify_ko_release.ROOT / ".pio/build/gh_release_ko_rc",
            verify_ko_release.build_directory("gh_release_ko_rc"),
        )

    def test_non_ko_environment_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            verify_ko_release.build_directory("gh_release")

    def test_default_success_output_keeps_existing_contract(self) -> None:
        self.assertEqual(
            "OK size=100 headroom=200 target=met",
            verify_ko_release.format_success("gh_release_ko", 100, 200, "met"),
        )

    def test_nightly_success_output_names_selected_environment(self) -> None:
        self.assertEqual(
            "OK environment=gh_release_ko_rc size=100 headroom=200 target=not-met",
            verify_ko_release.format_success("gh_release_ko_rc", 100, 200, "not-met"),
        )


class KoreanTlsMemoryConfigTest(unittest.TestCase):
    def test_korean_environments_keep_full_rx_and_use_bounded_tx(self) -> None:
        platformio = (verify_ko_release.ROOT / "platformio.ini").read_text(
            encoding="utf-8"
        )
        self.assertIn("[ko_tls]", platformio)

        tls_start = platformio.index("[ko_tls]")
        tls_end = platformio.index("\n[", tls_start + 1)
        tls_block = platformio[tls_start:tls_end]
        self.assertIn("CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y", tls_block)
        self.assertIn("CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384", tls_block)
        self.assertIn("CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096", tls_block)

        prepare_start = platformio.index("[env:ko_sdk_prepare]")
        prepare_end = platformio.find("\n[", prepare_start + 1)
        if prepare_end == -1:
            prepare_end = len(platformio)
        prepare_block = platformio[prepare_start:prepare_end]
        self.assertIn("custom_sdkconfig = ${ko_tls.custom_sdkconfig}", prepare_block)
        self.assertNotIn("-fno-lto", prepare_block)
        self.assertIn("-DCONFIG_LIB_BUILDER_COMPILE=1", prepare_block)
        self.assertIn("-<*>", prepare_block)
        self.assertIn("+<../.dummy/sketch.cpp>", prepare_block)

        for environment in ("gh_release_ko", "gh_release_ko_rc"):
            env_start = platformio.index(f"[env:{environment}]")
            env_end = platformio.find("\n[", env_start + 1)
            if env_end == -1:
                env_end = len(platformio)
            env_block = platformio[env_start:env_end]
            self.assertIn("custom_sdkconfig = ${ko_tls.custom_sdkconfig}", env_block)


class KimtomaDeviceBrandingTest(unittest.TestCase):
    def test_exact_korean_branding_strings_exist(self) -> None:
        korean = (verify_ko_release.ROOT / "lib/I18n/translations/korean.yaml").read_text(
            encoding="utf-8"
        )
        self.assertIn('STR_KIMTOMA_BRAND: "@kimtoma"', korean)
        self.assertIn('STR_KIMTOMA_LIBRARY: "kimtoma 서재"', korean)
        self.assertIn('STR_KIMTOMA_INTEGRATION: "kimtoma.com 연동"', korean)
        self.assertIn('STR_OPDS_SERVERS: "온라인 서재 서버"', korean)

    def test_boot_and_sleep_keep_korean_and_default_branches(self) -> None:
        for relative_path in (
            "src/activities/boot_sleep/BootActivity.cpp",
            "src/activities/boot_sleep/SleepActivity.cpp",
        ):
            source = (verify_ko_release.ROOT / relative_path).read_text(encoding="utf-8")
            self.assertIn("ENABLE_KOREAN_VERSION", source)
            self.assertIn("KIMTOMA_MARK_120", source)
            self.assertIn("STR_KIMTOMA_BRAND", source)
            self.assertIn("Logo120", source)
            self.assertIn("STR_CROSSPOINT", source)
            self.assertIn("(pageWidth - 120) / 2", source)
            self.assertIn("(pageHeight - 120) / 2", source)


class KimtomaDeviceRoutingTest(unittest.TestCase):
    def test_apps_uses_kimtoma_library_only_for_sync_build(self) -> None:
        source = (verify_ko_release.ROOT / "src/activities/apps/AppsMenuActivity.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("ENABLE_KIMTOMA_READING_SYNC", source)
        self.assertIn("STR_KIMTOMA_LIBRARY", source)
        self.assertIn("goToKimtomaLibrary", source)
        self.assertIn("STR_OPDS_BROWSER", source)
        self.assertIn("goToBrowser", source)

    def test_system_keeps_kimtoma_and_neutral_opds_actions_separate(self) -> None:
        header = (verify_ko_release.ROOT / "src/activities/settings/SettingsActivity.h").read_text(
            encoding="utf-8"
        )
        source = (verify_ko_release.ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("SettingAction::KimtomaIntegration", source)
        self.assertIn("STR_KIMTOMA_INTEGRATION", source)
        self.assertIn("STR_OPDS_SERVERS", source)
        self.assertIn("KimtomaIntegration", header)

    def test_activity_has_two_closed_modes_and_no_token_rendering(self) -> None:
        header = (
            verify_ko_release.ROOT
            / "src/activities/apps/kimtoma/KimtomaLibraryActivity.h"
        ).read_text(encoding="utf-8")
        source = (
            verify_ko_release.ROOT
            / "src/activities/apps/kimtoma/KimtomaLibraryActivity.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("enum class KimtomaLibraryMode", header)
        self.assertIn("case KimtomaLibraryMode::Library", source)
        self.assertIn("case KimtomaLibraryMode::Settings", source)
        self.assertNotIn("default:", source)
        self.assertNotIn("tokenForRequest", source)


if __name__ == "__main__":
    unittest.main()
