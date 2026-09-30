"""The completion gate must not turn assisted or stale evidence into a UI pass."""
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[2] / "scripts/dune_speedrun_evidence.py"
spec = importlib.util.spec_from_file_location("evidence", MODULE)
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)

END = "Speedrun: day 87: step 56 OK: the Emperor's throne room, the end\nSpeedrun: day 87: end\n"


class EvidenceTests(unittest.TestCase):
    def test_legacy_success_is_never_player_ui_proof(self):
        result = evidence.assess(END)
        self.assertTrue(result["passed"])
        self.assertTrue(result["legacy_coverage_unknown"])
        self.assertFalse(result["player_ui_completion_verified"])

    def test_historical_direct_fallback_fails_default_gate(self):
        result = evidence.assess("Speedrun: day 1: orders: troop 2 moves (set directly)\n" + END)
        self.assertFalse(result["passed"])
        self.assertEqual(result["direct_order_fallbacks"], 1)

    def test_explicit_assisted_mode_preserves_bypass_count(self):
        result = evidence.assess("Speedrun: day 1: orders: troop 2 moves (set directly)\n" + END,
                                 allow_order_bypasses=True)
        self.assertTrue(result["passed"])
        self.assertEqual(result["direct_order_fallbacks"], 1)
        self.assertFalse(result["player_ui_completion_verified"])

    def test_refusing_an_order_is_not_a_bypass(self):
        result = evidence.assess("Speedrun: day 1: orders: troop 2 defers movement (no enabled move row)\n" + END)
        self.assertTrue(result["passed"])
        self.assertEqual(result["direct_order_fallbacks"], 0)

    def test_full_forced_fails_even_in_assisted_override(self):
        self.assertFalse(evidence.assess("Speedrun: day 1: FORCED phase\n" + END,
                                        allow_order_bypasses=True)["passed"])

    def test_campaign_seed_is_explicit(self):
        result = evidence.assess("Speedrun: day 1: FORCED campaign start: seeded army\n"
                                 "Speedrun: day 3: FORCED day 3: troops fetched the sietches' free weapons (time passes)\n" + END,
                                 part="campaign")
        self.assertTrue(result["passed"])
        self.assertEqual(result["forced_shortcuts"], 2)
        self.assertEqual(result["forced_setup_shortcuts"], 2)
        self.assertEqual(result["forced_progression_shortcuts"], 0)

    def test_campaign_progression_shortcut_fails_even_in_override(self):
        result = evidence.assess("Speedrun: day 1: FORCED campaign start: seeded army\n"
                                 "Speedrun: day 70: FORCED final attack stage 3 -> 4\n" + END,
                                 part="campaign", allow_order_bypasses=True)
        self.assertFalse(result["passed"])
        self.assertEqual(result["forced_setup_shortcuts"], 1)
        self.assertEqual(result["forced_progression_shortcuts"], 1)

    def test_campaign_unknown_forced_marker_is_not_accepted_setup(self):
        self.assertFalse(evidence.assess("Speedrun: day 1: FORCED phase\n" + END,
                                        part="campaign")["passed"])

    def test_losing_battle_then_reloading_is_not_a_blocker(self):
        result = evidence.assess('Story: the ending "Winning a battle..."\nSaves: slot 1 loaded\n' + END)
        self.assertTrue(result["passed"])
        self.assertFalse(result["player_ui_completion_verified"])

    def test_actual_blocker_fails_even_with_an_ending_marker(self):
        self.assertFalse(evidence.assess("Speedrun: day 1: BLOCKED phase\n" + END)["passed"])

    def test_crash_or_stale_tail_fails(self):
        self.assertFalse(evidence.assess(END, process_exit=9)["passed"])
        self.assertFalse(evidence.assess(END + "unfinished next run\n")["passed"])
        self.assertFalse(evidence.assess("")["passed"])

    def test_ui_mode_does_not_trust_a_self_declared_marker(self):
        result = evidence.assess("Speedrun: day 1: coverage player-ui\n" + END,
                                 require_player_ui=True)
        self.assertFalse(result["passed"])
        self.assertFalse(result["player_ui_completion_verified"])

    def test_declared_assisted_run_is_still_not_ui_proof(self):
        result = evidence.assess("Speedrun: day 1: coverage engine-assisted-v1\n" + END)
        self.assertTrue(result["passed"])
        self.assertFalse(result["legacy_coverage_unknown"])
        self.assertFalse(result["player_ui_completion_verified"])

    def test_cli_retains_exact_log_and_binary_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            log, binary, output = [directory / name for name in ("run.log", "engine", "evidence.json")]
            log.write_text(END)
            binary.write_bytes(b"test-binary-identity")
            completed = subprocess.run([sys.executable, str(MODULE), str(log), "--binary", str(binary),
                                        "--json", str(output)], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(output.read_text())
            self.assertEqual(result["binary_sha256"], hashlib.sha256(binary.read_bytes()).hexdigest())
            self.assertEqual(result["log_sha256"], hashlib.sha256(log.read_bytes()).hexdigest())
            self.assertIn("assisted engine completion", completed.stdout)
            self.assertFalse(result["player_ui_completion_verified"])


if __name__ == "__main__":
    unittest.main()
