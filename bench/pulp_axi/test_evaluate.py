import unittest
from unittest import mock

import evaluate
from evaluate import evaluate_report, expected_rows


class PulpAxiEvaluatorTests(unittest.TestCase):
    def setUp(self):
        self.frames = expected_rows()
        self.rows = [row for pairs in self.frames.values() for row in pairs]
        self.report = {"top": "svlens_axi_xbar_probe",
                       "summary": {"connections_analyzed": len(self.rows), "errors": 0,
                                   "bit_flow_gap_count": 0},
                       "connections": self.rows}

    def test_frame_covers_both_lanes_and_internal_response_candidates(self):
        self.assertEqual({name: len(rows) for name, rows in self.frames.items()},
                         {"boundary": 76, "decoder": 4, "response": 4, "request": 24})
        result = evaluate_report(self.report)
        self.assertEqual(result["boundary_found"], 76)
        self.assertEqual(result["decoder_found"], 4)
        self.assertEqual(result["response_found"], 4)
        self.assertEqual(result["request_found"], 24)
        with mock.patch.object(evaluate, "EXPECTED_SHA256", "stale"):
            with self.assertRaisesRegex(ValueError, "frame changed"):
                evaluate_report(self.report)

    def test_missing_downgraded_and_crossed_lanes_fail(self):
        with self.assertRaisesRegex(ValueError, "boundary"):
            evaluate_report({**self.report, "connections": self.rows[1:],
                             "summary": {**self.report["summary"], "connections_analyzed": len(self.rows) - 1}})

        downgraded = [{**self.rows[0], "kind": "approximate"}, *self.rows[1:]]
        with self.assertRaisesRegex(ValueError, "boundary"):
            evaluate_report({**self.report, "connections": downgraded})

        wrong = {**self.frames["decoder"][0],
                 "dest": self.frames["decoder"][0]["dest"].replace("gen_slv_port_demux[0]",
                                                                         "gen_slv_port_demux[1]")}
        with self.assertRaisesRegex(ValueError, "wrong-lane"):
            evaluate_report({**self.report, "connections": [*self.rows, wrong],
                             "summary": {**self.report["summary"], "connections_analyzed": len(self.rows) + 1}})

        crossed_boundary = {**self.frames["boundary"][0],
                            "dest": self.frames["boundary"][0]["dest"].replace("slv_ports[0]", "slv_ports[1]")}
        with self.assertRaisesRegex(ValueError, "wrong-lane"):
            evaluate_report({**self.report, "connections": [*self.rows, crossed_boundary],
                             "summary": {**self.report["summary"], "connections_analyzed": len(self.rows) + 1}})

        wrong_range = [{**self.rows[0], "source_bits": {"low": 0, "high": 6}}, *self.rows[1:]]
        with self.assertRaisesRegex(ValueError, "boundary"):
            evaluate_report({**self.report, "connections": wrong_range})

    def test_ranged_response_is_not_replaced_by_coarse_edge(self):
        coarse = {key: value for key, value in self.frames["response"][0].items()
                  if key not in ("source_bits", "dest_bits")}
        replaced = [*self.frames["boundary"], *self.frames["decoder"], coarse,
                    *self.frames["response"][1:], *self.frames["request"]]
        with self.assertRaisesRegex(ValueError, "response"):
            evaluate_report({**self.report, "connections": replaced})

        direct_request = [{**self.frames["request"][0], "kind": "direct"},
                          *self.frames["request"][1:]]
        replaced = [*self.frames["boundary"], *self.frames["decoder"],
                    *self.frames["response"], *direct_request]
        with self.assertRaisesRegex(ValueError, "request"):
            evaluate_report({**self.report, "connections": replaced})


if __name__ == "__main__":
    unittest.main()
