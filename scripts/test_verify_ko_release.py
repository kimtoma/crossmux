#!/usr/bin/env python3
import unittest

import verify_ko_release


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


if __name__ == "__main__":
    unittest.main()
