import unittest

from source_recall import compare_report, expected_scalar_edges


def port(name, direction, symbol):
    expr = {"kind": "NamedValue", "type": "logic", "symbol": symbol}
    if direction == "Out":
        expr = {"kind": "Assignment", "left": expr}
    return {"port": {"name": name, "type": "logic", "direction": direction}, "expr": expr}


class SourceRecallTests(unittest.TestCase):
    def test_unique_scalar_sibling_wires_and_generated_scopes(self):
        def pair(source_name, dest_name, symbol):
            return [{"kind": "Instance", "name": source_name,
                     "connections": [port("q_o", "Out", symbol)]},
                    {"kind": "Instance", "name": dest_name,
                     "connections": [port("d_i", "In", symbol)]}]

        scope = {"kind": "Instance", "body": {"members": [
            *pair("u_source", "u_sink", "net_a"),
            {"kind": "GenerateBlockArray", "name": "gen_lanes", "members": [
                {"kind": "GenerateBlock", "constructIndex": 2,
                 "members": pair("u_gen_source", "u_gen_sink", "net_b")},
            ]},
            *pair("u_other", "u_unused", "shared"),
            {"kind": "Instance", "name": "u_second_driver",
             "connections": [port("q_o", "Out", "shared")]},
        ]}}
        expected = expected_scalar_edges(scope, "top")
        self.assertEqual(expected, [
            ("top.gen_lanes[2].u_gen_source.q_o", "top.gen_lanes[2].u_gen_sink.d_i"),
            ("top.u_source.q_o", "top.u_sink.d_i"),
        ])

    def test_report_separates_direct_approximate_and_missing(self):
        expected = {"scope": [("a", "b"), ("c", "d"), ("e", "f")]}
        report = {"connections": [
            {"source": "a", "dest": "b", "kind": "direct"},
            {"source": "c", "dest": "d", "kind": "approximate"},
        ]}
        result = compare_report(report, expected)
        self.assertEqual(result["expected"], 3)
        self.assertEqual(result["found_direct"], 1)
        self.assertEqual(result["approximate_only"], [("c", "d")])
        self.assertEqual(result["missing"], [("e", "f")])
        self.assertEqual(result["scope_results"]["scope"],
                         {"expected": 3, "found_direct": 1, "approximate_only": 1, "missing": 1})
        self.assertEqual(result["expected_pairs"][0], {"source": "a", "dest": "b"})


if __name__ == "__main__":
    unittest.main()
