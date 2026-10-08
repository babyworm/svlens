import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from report_convert import to_pr_comment, to_pr_comments, to_sarif


class ReportConvertTests(unittest.TestCase):
    def test_sarif_preserves_real_file_location_and_logical_cdc_path(self):
        conn = {"issues": [{"type": "WIDTH_MISMATCH", "severity": "ERROR",
                            "port": "top.u_a.o_data", "detail": "width differs",
                            "file": "tests/sv/width_mismatch.sv", "line": 7, "column": 4}]}
        cdc = {"crossings": [{"rule": "Ac_cdc01", "category": "VIOLATION",
                             "source": "top.q_a", "dest": "top.q_b",
                             "dest_file": "tests/cdc/basic/03_two_ff_sync.sv",
                             "dest_line": 8,
                             "recommendation": "insert synchronizer"}]}
        sarif = to_sarif(conn, cdc, Path.cwd())
        self.assertEqual(sarif["version"], "2.1.0")
        self.assertEqual(len(sarif["runs"]), 2)
        conn_result = sarif["runs"][0]["results"][0]
        self.assertEqual(conn_result["level"], "error")
        self.assertEqual(conn_result["locations"][0]["physicalLocation"]
                         ["artifactLocation"]["uri"], "tests/sv/width_mismatch.sv")
        cdc_result = sarif["runs"][1]["results"][0]
        self.assertEqual(cdc_result["ruleId"], "CDC/Ac_cdc01")
        self.assertEqual(cdc_result["locations"][0]["physicalLocation"]
                         ["region"]["startLine"], 8)

    def test_pr_summary_skips_waived_crossing(self):
        text = to_pr_comment(None, {"crossings": [
            {"category": "WAIVED", "rule": "Ac_cdc01", "dest": "top.q0"},
            {"category": "CAUTION", "rule": "Ac_cdc10", "dest": "top.q1"},
        ]})
        self.assertIn("1 findings", text)
        self.assertIn("Ac_cdc10", text)
        self.assertNotIn("top.q0", text)

    def test_review_comment_candidates_require_source_location(self):
        conn = {"issues": [
            {"type": "WIDTH_MISMATCH", "severity": "WARN", "detail": "8 to 16",
             "file": "tests/sv/width_mismatch.sv", "line": 3},
            {"type": "CONVENTION", "detail": "no location"},
        ]}
        comments = to_pr_comments(conn, None, Path.cwd())
        self.assertEqual(len(comments), 1)
        self.assertEqual(comments[0]["path"], "tests/sv/width_mismatch.sv")
        self.assertEqual(comments[0]["line"], 3)
        self.assertEqual(comments[0]["side"], "RIGHT")


if __name__ == "__main__":
    unittest.main()
