#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class KoreanWorkflowGateTest(unittest.TestCase):
    def assert_gate_contract(
        self, workflow_path: str, environment: str, build_marker: str, stage_marker: str | None = None
    ) -> str:
        workflow = (ROOT / workflow_path).read_text(encoding="utf-8")
        gate_marker = "- name: Gate Korean"
        diagnostics_marker = "- name: Upload Korean gate failure diagnostics"

        build_position = workflow.index(build_marker)
        gate_position = workflow.index(gate_marker, build_position)
        diagnostics_position = workflow.index(diagnostics_marker, gate_position)
        self.assertLess(build_position, gate_position)
        self.assertLess(gate_position, diagnostics_position)
        self.assertIn('"PyYAML>=6.0.3"', workflow[:gate_position])
        if stage_marker is not None:
            self.assertLess(diagnostics_position, workflow.index(stage_marker, diagnostics_position))

        gate_block = workflow[gate_position:diagnostics_position]
        self.assertGreaterEqual(gate_block.count("set -euo pipefail"), 2)
        self.assertIn("python3 -m unittest lib/EpdFont/scripts/test_build_ko_charset.py -v", gate_block)
        self.assertIn("python3 scripts/test_ko_workflow_gates.py -v", gate_block)
        self.assertIn("python3 scripts/test_verify_ko_release.py -v", gate_block)
        verifier = "python3 scripts/verify_ko_release.py"
        if environment.endswith("_rc"):
            verifier += f" --environment {environment}"
        self.assertIn(verifier, gate_block)
        self.assertIn(") 2>&1 | tee ko-verifier.log", gate_block)

        diagnostics_block = workflow[diagnostics_position:]
        if stage_marker is not None:
            diagnostics_block = diagnostics_block[: diagnostics_block.index(stage_marker)]
        self.assertIn("if: failure()", diagnostics_block)
        self.assertIn("ko-verifier.log", diagnostics_block)
        self.assertIn(f".pio/build/{environment}/firmware.map", diagnostics_block)
        self.assertIn(f".pio/build/{environment}/firmware.bin", diagnostics_block)
        return workflow

    def test_ci_gate_runs_after_native_tests_and_is_required(self) -> None:
        workflow = self.assert_gate_contract(
            ".github/workflows/ci.yml", "gh_release_ko", "- name: Build Korean release"
        )
        gate_job = workflow[workflow.index("  ko-release-gate:") : workflow.index("  # This job is used")]
        self.assertIn("needs: unit-tests", gate_job)
        test_status = workflow[workflow.index("  test-status:") :]
        self.assertIn("- ko-release-gate", test_status)

    def test_stable_release_gate_precedes_asset_staging(self) -> None:
        self.assert_gate_contract(
            ".github/workflows/release.yml",
            "gh_release_ko",
            "- name: Build ryOS CrossMux (international + TC + SC + JA + KO)",
            "- name: Stage release assets",
        )

    def test_nightly_gate_uses_matching_rc_artifacts_before_staging(self) -> None:
        self.assert_gate_contract(
            ".github/workflows/nightly.yml",
            "gh_release_ko_rc",
            "- name: Build ryOS CrossMux Nightly (international + TC + SC + JA + KO)",
            "- name: Stage nightly assets",
        )

    def test_upstream_sync_gate_checks_stable_ko_artifacts(self) -> None:
        self.assert_gate_contract(
            ".github/workflows/sync-build.yml", "gh_release_ko", "- name: Build gh_release + TC/SC/JA/KO"
        )

    def test_logged_gate_pipeline_propagates_an_early_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            log_path = Path(directory) / "gate.log"
            result = subprocess.run(
                [
                    "bash",
                    "-c",
                    "set -euo pipefail\n(set -euo pipefail; false; true) 2>&1 | tee \"$1\" >/dev/null",
                    "bash",
                    str(log_path),
                ],
                check=False,
            )
        self.assertNotEqual(0, result.returncode)


if __name__ == "__main__":
    unittest.main()
