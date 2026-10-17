import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run_sim


class PulpAxiSimulationRunnerTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
