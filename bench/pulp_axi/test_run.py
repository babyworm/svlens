import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run as benchmark_run


class PulpAxiRunnerTests(unittest.TestCase):
    def test_request_fields_are_driven_by_runtime_inputs(self):
        fixture = (Path(__file__).resolve().parent / "xbar_probe.sv").read_text()
        for signal in ("aw_addr", "aw_prot", "aw_valid", "w_data", "w_strb", "w_valid",
                       "ar_addr", "ar_prot", "ar_valid"):
            self.assertIn(f"assign bus.{signal} = {signal}_i;", fixture)
        for lane in range(2):
            self.assertIn(f".aw_addr_i(aw_addr_i[{lane}])", fixture)
            self.assertIn(f".ar_addr_i(ar_addr_i[{lane}])", fixture)
            self.assertIn(f".r_data_i(r_data_i[{lane}])", fixture)
            self.assertIn(f".b_resp_i(b_resp_i[{lane}])", fixture)
        self.assertIn("start_addr: 0, end_addr: 128", fixture)
        self.assertIn("start_addr: 128, end_addr: 0", fixture)
        self.assertIn(".rule_t(rule_t)", fixture)

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

    def test_fixture_drift_requires_re_adjudication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "svlens"
            binary.touch()
            with (mock.patch.object(benchmark_run, "check_clone"),
                  mock.patch.object(benchmark_run, "FIXTURE_SHA256", "stale")):
                with self.assertRaisesRegex(ValueError, "fixture changed"):
                    benchmark_run.run(root / "axi", root / "common", binary, root / "report")


if __name__ == "__main__":
    unittest.main()
