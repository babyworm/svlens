import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run as benchmark_run


class PulpAxiRunnerTests(unittest.TestCase):
    def test_pins_and_dirty_clones_are_rejected(self):
        with mock.patch.object(benchmark_run.subprocess, "check_output", side_effect=["wrong\n", ""]):
            with self.assertRaisesRegex(ValueError, "clean at"):
                benchmark_run.check_clone(Path("axi"), benchmark_run.AXI_REV)
        with mock.patch.object(benchmark_run.subprocess, "check_output",
                               side_effect=[benchmark_run.AXI_REV + "\n", " M src/axi_pkg.sv\n"]):
            with self.assertRaisesRegex(ValueError, "clean at"):
                benchmark_run.check_clone(Path("axi"), benchmark_run.AXI_REV)

    def test_existing_report_cannot_pass_as_fresh_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "svlens"
            binary.touch()
            output = root / "report"
            output.mkdir()
            (output / "connect_report.json").write_text("{}")
            with mock.patch.object(benchmark_run, "check_clone"):
                with self.assertRaisesRegex(FileExistsError, "existing report"):
                    benchmark_run.run(root / "axi", root / "common", binary, output)


if __name__ == "__main__":
    unittest.main()
