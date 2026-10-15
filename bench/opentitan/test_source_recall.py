import unittest

from source_recall import (compare_control_report, compare_report, expected_passthrough_edges,
                           expected_simple_edges, expected_ternary_control_edges, simple_logic_width)


def port(name, direction, symbol, type_name="logic", net_type=None):
    expr = {"kind": "NamedValue", "type": net_type or type_name, "symbol": symbol}
    if direction == "Out":
        expr = {"kind": "Assignment", "left": expr}
    return {"port": {"name": name, "type": type_name, "direction": direction}, "expr": expr}


class SourceRecallTests(unittest.TestCase):
    def test_ternary_control_oracle_requires_unique_direct_selector_and_output(self):
        conditional = {"kind": "ConditionalOp", "type": "logic[7:0]",
                       "conditions": [{"expr": {"kind": "NamedValue", "type": "logic", "symbol": "sel"}}],
                       "left": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "a"},
                       "right": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "b"}}
        assignment = {"kind": "ContinuousAssign", "assignment": {
            "kind": "Assignment",
            "left": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "out"},
            "right": conditional}}
        members = [
            {"kind": "Instance", "name": "u_selector", "connections": [port("sel_o", "Out", "sel")]},
            {"kind": "Instance", "name": "u_data_a", "connections": [port("data_o", "Out", "a", "logic[7:0]")]},
            {"kind": "Instance", "name": "u_data_b", "connections": [port("data_o", "Out", "b", "logic[7:0]")]},
            {"kind": "Instance", "name": "u_sink", "connections": [port("data_i", "In", "out", "logic[7:0]")]},
            assignment,
        ]
        scope = {"kind": "Instance", "body": {"kind": "InstanceBody", "members": members}}
        pair = ("top.u_selector.sel_o", "top.u_sink.data_i[7:0]")
        self.assertEqual(expected_ternary_control_edges(scope, "top"), [pair])
        duplicate_selector = {"kind": "Instance", "name": "u_second_selector",
                              "connections": [port("sel_o", "Out", "sel")]}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members + [duplicate_selector]}}, "top"), [])
        generated_selector = {"kind": "GenerateBlock", "name": "gen_driver",
                              "members": [duplicate_selector]}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members + [generated_selector]}}, "top"), [])
        extra_write = {"kind": "ProceduralBlock", "body": {"kind": "Assignment",
                       "left": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "out"}}}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members + [extra_write]}}, "top"), [])
        generated_write = {"kind": "GenerateBlock", "name": "gen_write", "members": [extra_write]}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members + [generated_write]}}, "top"), [])
        constant = {**conditional, "conditions": [{"expr": {"kind": "IntegerLiteral", "type": "logic",
                                                           "value": "1'b1"}}]}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members[:-1] + [{
                "kind": "ContinuousAssign", "assignment": {**assignment["assignment"], "right": constant}}]}},
            "top"), [])
        same_arms = {**conditional, "right": {**conditional["left"], "addr": 42}}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members[:-1] + [{
                "kind": "ContinuousAssign", "assignment": {**assignment["assignment"], "right": same_arms}}]}},
            "top"), [])
        same_literals = {**conditional,
                         "left": {"kind": "IntegerLiteral", "type": "logic[7:0]", "value": "8'h00", "addr": 1},
                         "right": {"kind": "IntegerLiteral", "type": "logic[7:0]", "value": "8'h00", "addr": 2}}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members[:-1] + [{
                "kind": "ContinuousAssign", "assignment": {**assignment["assignment"], "right": same_literals}}]}},
            "top"), [])
        narrow_arm = {**conditional, "right": {"kind": "NamedValue", "type": "logic[3:0]", "symbol": "b"}}
        self.assertEqual(expected_ternary_control_edges(
            {"kind": "Instance", "body": {"members": members[:-1] + [{
                "kind": "ContinuousAssign", "assignment": {**assignment["assignment"], "right": narrow_arm}}]}},
            "top"), [])

    def test_control_report_requires_range_free_approximate_evidence(self):
        pair = ("top.u_selector.sel_o", "top.u_sink.data_i[7:0]")
        by_scope = {"top": [pair]}
        direct = {"source": pair[0], "dest": pair[1], "kind": "direct"}
        self.assertEqual(compare_control_report({"connections": [direct]}, by_scope)["not_coarse"], [pair])
        ranged = {**direct, "kind": "approximate", "source_bits": {"low": 0, "high": 0},
                  "dest_bits": {"low": 0, "high": 7}}
        self.assertEqual(compare_control_report({"connections": [ranged]}, by_scope)["not_coarse"], [pair])
        coarse = {**direct, "kind": "approximate"}
        checked = compare_control_report({"connections": [coarse]}, by_scope)
        self.assertEqual(checked["found_approximate"], 1)
        self.assertEqual(checked["missing"], [])
        self.assertEqual(checked["not_coarse"], [])

    def test_direct_module_copy_oracle_excludes_clocked_and_extra_drivers(self):
        def wrapper(name, source, dest, *, clocked=False, extra_write=False):
            input_symbol = f"{name}.data_i"
            output_symbol = f"{name}.data_o"
            assignment = {"kind": "Assignment",
                          "left": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": output_symbol},
                          "right": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": input_symbol}}
            members = [
                {"kind": "Port", "name": "data_i", "type": "logic[7:0]", "direction": "In",
                 "internalSymbol": input_symbol},
                {"kind": "Port", "name": "data_o", "type": "logic[7:0]", "direction": "Out",
                 "internalSymbol": output_symbol},
                {"kind": "ProceduralBlock", "body": assignment} if clocked else
                {"kind": "ContinuousAssign", "assignment": assignment},
            ]
            if extra_write:
                members.append({"kind": "ProceduralBlock", "body": assignment})
            return {"kind": "Instance", "name": name,
                    "connections": [port("data_i", "In", source, "logic[7:0]"),
                                    port("data_o", "Out", dest, "logic[7:0]")],
                    "body": {"kind": "InstanceBody", "members": members}}

        scope = {"kind": "Instance", "body": {"members": [
            {"kind": "Instance", "name": "u_source",
             "connections": [port("data_o", "Out", "first", "logic[7:0]")]},
            wrapper("u_copy0", "first", "second"),
            wrapper("u_copy1", "second", "third"),
            {"kind": "Instance", "name": "u_sink",
             "connections": [port("data_i", "In", "third", "logic[7:0]")]},
        ]}}
        self.assertEqual(expected_passthrough_edges(scope, "top"), [
            ("top.u_copy0.data_o[7:0]", "top.u_sink.data_i[7:0]"),
            ("top.u_source.data_o[7:0]", "top.u_copy1.data_i[7:0]"),
            ("top.u_source.data_o[7:0]", "top.u_sink.data_i[7:0]"),
        ])

        for unsafe in (wrapper("u_copy1", "second", "third", clocked=True),
                       wrapper("u_copy1", "second", "third", extra_write=True)):
            altered = {"kind": "Instance", "body": {"members": [
                *scope["body"]["members"][:2], unsafe, scope["body"]["members"][-1],
            ]}}
            self.assertEqual(expected_passthrough_edges(altered, "top"), [
                ("top.u_source.data_o[7:0]", "top.u_copy1.data_i[7:0]"),
            ])

        canonical = wrapper("u_copy0", "first", "second")
        canonical["body"]["addr"] = 123
        reused = wrapper("u_copy1", "second", "third")
        reused["body"] = "123 copy_body"
        shared_body = {"kind": "Instance", "body": {"members": [
            scope["body"]["members"][0], canonical, reused, scope["body"]["members"][-1],
        ]}}
        self.assertEqual(expected_passthrough_edges(shared_body, "top"),
                         expected_passthrough_edges(scope, "top"))

        second_driver = {"kind": "Instance", "name": "u_extra",
                         "connections": [port("data_o", "Out", "third", "logic[3:0]", "logic[7:0]")]}
        shared_body["body"]["members"].append(second_driver)
        self.assertEqual(expected_passthrough_edges(shared_body, "top"), [
            ("top.u_source.data_o[7:0]", "top.u_copy1.data_i[7:0]"),
        ])

    def test_whole_vector_wires_use_report_width_spelling_and_reject_conversions(self):
        self.assertIsNone(simple_logic_width("logic[1:0][3:0]"))
        self.assertIsNone(simple_logic_width("logic[4096:0]"))
        scope = {"kind": "Instance", "body": {"members": [
            {"kind": "Instance", "name": "u_source",
             "connections": [port("data_o", "Out", "bus", "logic[0:7]")]},
            {"kind": "Instance", "name": "u_sink",
             "connections": [port("data_i", "In", "bus", "logic[7:0]")]},
            {"kind": "Instance", "name": "u_narrow",
             "connections": [port("data_i", "In", "bus", "logic[3:0]", "logic[7:0]")]},
            {"kind": "Instance", "name": "u_struct",
             "connections": [port("data_i", "In", "bus", "pkg::payload_t")]},
        ]}}
        self.assertEqual(expected_simple_edges(scope, "top"),
                         [("top.u_source.data_o[7:0]", "top.u_sink.data_i[7:0]")])

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
        expected = expected_simple_edges(scope, "top")
        self.assertEqual(expected, [
            ("top.gen_lanes[2].u_gen_source.q_o", "top.gen_lanes[2].u_gen_sink.d_i"),
            ("top.u_source.q_o", "top.u_sink.d_i"),
        ])

    def test_unique_direct_continuous_alias_connects_sibling_ports(self):
        alias = {"kind": "ContinuousAssign", "assignment": {
            "kind": "Assignment",
            "left": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "forwarded"},
            "right": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "driven"},
        }}
        members = [
            {"kind": "Instance", "name": "u_source",
             "connections": [port("data_o", "Out", "driven", "logic[7:0]")]},
            {"kind": "Instance", "name": "u_sink",
             "connections": [port("data_i", "In", "forwarded", "logic[7:0]")]},
            alias,
        ]
        expected = [("top.u_source.data_o[7:0]", "top.u_sink.data_i[7:0]")]
        self.assertEqual(expected_simple_edges({"body": {"members": members}}, "top"), expected)

        # A second driver or a computed RHS cannot establish a direct alias.
        second = {"kind": "ContinuousAssign", "assignment": {
            "kind": "Assignment",
            "left": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "forwarded"},
            "right": {"kind": "NamedValue", "type": "logic[7:0]", "symbol": "other"},
        }}
        self.assertEqual(expected_simple_edges({"body": {"members": members + [second]}}, "top"), [])
        second_source = {"kind": "Instance", "name": "u_other",
                         "connections": [port("data_o", "Out", "driven", "logic[7:0]")]}
        self.assertEqual(expected_simple_edges({"body": {"members": members + [second_source]}}, "top"), [])
        procedural = {"kind": "ProceduralBlock", "body": {"kind": "ExpressionStatement", "expr": {
            "kind": "Assignment", "left": alias["assignment"]["left"],
            "right": {"kind": "IntegerLiteral", "type": "logic[7:0]", "value": "8'h00"},
        }}}
        self.assertEqual(expected_simple_edges({"body": {"members": members + [procedural]}}, "top"), [])
        partial_write = {"kind": "ProceduralBlock", "body": {"kind": "ExpressionStatement", "expr": {
            "kind": "Assignment", "left": {
                "kind": "ElementSelect", "value": alias["assignment"]["left"],
                "selector": {"kind": "IntegerLiteral", "value": "0"}},
            "right": {"kind": "IntegerLiteral", "type": "logic", "value": "1'b0"},
        }}}
        self.assertEqual(expected_simple_edges({"body": {"members": members + [partial_write]}}, "top"), [])
        computed = {"kind": "ContinuousAssign", "assignment": {
            "kind": "Assignment", "left": alias["assignment"]["left"],
            "right": {"kind": "BinaryOp", "type": "logic[7:0]"},
        }}
        self.assertEqual(expected_simple_edges({"body": {"members": members[:2] + [computed]}}, "top"), [])
        converted = {"kind": "ContinuousAssign", "assignment": {
            "kind": "Assignment", "left": alias["assignment"]["left"],
            "right": {"kind": "NamedValue", "type": "logic[3:0]", "symbol": "driven"},
        }}
        self.assertEqual(expected_simple_edges({"body": {"members": members[:2] + [converted]}}, "top"), [])

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
