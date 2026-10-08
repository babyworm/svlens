import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))
import svlens


class ApiTests(unittest.TestCase):
    binary = Path(os.environ.get("SVLENS_BINARY", ROOT / "build" / "svlens"))

    def test_conn_returns_findings_even_when_exit_is_nonzero(self):
        report = svlens.conn(
            [ROOT / "tests/sv/conn_modport_width_pos.sv"],
            top="conn_modport_width_pos", binary=self.binary)
        self.assertGreater(report["summary"]["warnings"], 0)
        self.assertTrue(any(issue.get("file") for issue in report["issues"]))

    def test_metrics_and_all_modes_return_json(self):
        fixture = ROOT / "tests/sv/metrics/simple_cone.sv"
        metrics = svlens.metrics([fixture], top="simple_cone", binary=self.binary)
        self.assertGreater(metrics["summary"]["outputs_analyzed"], 0)
        combined = svlens.all_modes([fixture], top="simple_cone", binary=self.binary)
        self.assertEqual(combined["summary"]["mode"], "all")
        self.assertIn("roots", combined["metrics"])

    def test_missing_binary_is_clear(self):
        with self.assertRaises(svlens.SvlensError):
            svlens.conn(["missing.sv"], top="top", binary="/nonexistent/svlens")


if __name__ == "__main__":
    unittest.main()
