import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run_sim


class PulpAxiSimulationRunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.axi = self.root / "axi"
        self.common = self.root / "common"
        self.output = self.root / "output"
        for root, sources in ((self.axi, run_sim.AXI_SOURCES),
                              (self.common, run_sim.COMMON_SOURCES)):
            for name in sources:
                source = root / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.touch()
        clone_check = mock.patch.object(run_sim, "check_clone")
        clone_check.start()
        self.addCleanup(clone_check.stop)

    def simulate(self, build_code=0, create_binary=True, normal_code=0,
                 normal_output=None, mutation_code=1, mutation_output=None):
        def execute(command, **kwargs):
            if "--binary" in command:
                self.assertIn("--assert", command)
                simulator = self.output / "obj" / "Vtb_xbar_probe"
                if create_binary:
                    simulator.parent.mkdir(parents=True, exist_ok=True)
                    simulator.touch()
                code, output = build_code, "build diagnostic\n"
            elif len(command) > 1:
                self.assertEqual(command[1], "+SVLENS_AXI_MUTATE_RESPONSE")
                code = mutation_code
                output = mutation_output if mutation_output is not None else (
                    "Assertion failed: wrong or duplicate R response\n")
            else:
                code = normal_code
                output = normal_output if normal_output is not None else run_sim.SUCCESS_MARKER
            kwargs["stdout"].write(output)
            return subprocess.CompletedProcess(command, code)
        return mock.patch.object(run_sim.subprocess, "run", side_effect=execute)

    def run_probe(self):
        return run_sim.run(self.axi, self.common, self.output, Path("/bin/true"))

    def test_missing_verilator_is_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (mock.patch.object(run_sim, "check_clone"),
                  mock.patch.object(run_sim.shutil, "which", return_value=None)):
                with self.assertRaisesRegex(FileNotFoundError, "Verilator"):
                    run_sim.run(root / "axi", root / "common", root / "output")

    def test_testbench_drift_requires_re_adjudication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with (mock.patch.object(run_sim, "check_clone"),
                  mock.patch.object(run_sim, "TESTBENCH_SHA256", "stale")):
                with self.assertRaisesRegex(ValueError, "testbench changed"):
                    run_sim.run(root / "axi", root / "common", root / "output", Path("/bin/true"))

    def test_existing_evidence_or_partial_build_is_rejected(self):
        for name in ("simulation.json", "obj/unfinished.o"):
            with self.subTest(name=name):
                stale = self.output / name
                stale.parent.mkdir(parents=True, exist_ok=True)
                stale.write_text("stale")
                with self.assertRaisesRegex(FileExistsError, "nonempty"):
                    self.run_probe()
                stale.unlink()
                if stale.parent != self.output:
                    stale.parent.rmdir()

    def test_build_failure_or_missing_binary_cannot_publish_success(self):
        for code, create in ((1, True), (0, False)):
            with self.subTest(code=code, create=create):
                self.output = self.root / f"build-{code}"
                with self.simulate(build_code=code, create_binary=create):
                    with self.assertRaisesRegex(RuntimeError, "build failed"):
                        self.run_probe()
                self.assertIn("build diagnostic", (self.output / "build.log").read_text())
                self.assertFalse((self.output / "simulation.json").exists())

    def test_failure_or_missing_marker_cannot_publish_success(self):
        for code, output in ((1, run_sim.SUCCESS_MARKER), (0, "no completion")):
            with self.subTest(code=code):
                self.output = self.root / f"normal-{code}"
                with self.simulate(normal_code=code, normal_output=output):
                    with self.assertRaisesRegex(RuntimeError, "simulation failed"):
                        self.run_probe()
                self.assertFalse((self.output / "simulation.json").exists())

    def test_negative_control_must_fail_with_the_expected_assertion(self):
        for code, output in ((0, "wrong or duplicate R response"), (1, "unrelated crash")):
            with self.subTest(code=code):
                self.output = self.root / f"mutation-{code}"
                with self.simulate(mutation_code=code, mutation_output=output):
                    with self.assertRaisesRegex(RuntimeError, "negative control"):
                        self.run_probe()
                self.assertFalse((self.output / "simulation.json").exists())

    def test_success_records_completed_transactions_and_negative_control(self):
        with self.simulate():
            result = self.run_probe()
        self.assertEqual(result["route_cases_passed"], 6)
        self.assertEqual(result["write_transactions_passed"], 12)
        self.assertEqual(result["read_transactions_passed"], 12)
        self.assertTrue(result["negative_control_detected"])
        self.assertEqual(result["axi_revision"], run_sim.AXI_REV)
        self.assertEqual(result["fixture_sha256"], run_sim.FIXTURE_SHA256)
        self.assertTrue((self.output / "simulation.json").is_file())
        for name in ("build.log", "simulation.log", "negative-control.log"):
            self.assertTrue((self.output / name).is_file())

    def test_timeout_keeps_diagnostics_without_publishing_success(self):
        def timeout(command, **kwargs):
            kwargs["stdout"].write("compiler timeout diagnostic")
            raise subprocess.TimeoutExpired(command, kwargs["timeout"])
        with mock.patch.object(run_sim.subprocess, "run", side_effect=timeout):
            with self.assertRaisesRegex(RuntimeError, "timed out"):
                self.run_probe()
        self.assertIn("compiler timeout diagnostic", (self.output / "build.log").read_text())
        self.assertFalse((self.output / "simulation.json").exists())


if __name__ == "__main__":
    unittest.main()
