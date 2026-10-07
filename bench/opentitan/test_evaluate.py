import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

import evaluate as benchmark_evaluate
from evaluate import (
    evaluate_cdc,
    evaluate_conn,
    evaluate_period_probe,
    evaluate_sva_probe,
    missing_forbidden_paths,
    missing_guidance_references,
    missing_reference_crossings,
    missing_reference_paths,
    missing_reset_unresolved_references,
)
from sample_connections import select_sample


class EvaluateTests(unittest.TestCase):
    def test_connection_sample_audit_rejects_stale_or_contradicted_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            results = root / "results" / "soc"
            (results / "conn").mkdir(parents=True)
            (results / "cdc").mkdir()
            golden = root / "golden"
            golden.mkdir()
            (root / "targets.yaml").write_text(
                "targets:\n  - name: soc\n    level: L4\n"
                "    sample_annotations: golden/sample.yaml\n")
            (results / "metrics.json").write_text(json.dumps({
                "name": "soc", "level": "L4", "conn_status": "reported", "cdc_status": "reported",
            }))
            rows = [{"source": str(index), "dest": "sink", "kind": kind,
                     **({"source_bits": {"low": 0, "high": 0},
                         "dest_bits": {"low": 0, "high": 0}} if ranged else {})}
                    for index, (kind, ranged) in enumerate(
                        (("direct", False), ("direct", True),
                         ("approximate", False), ("approximate", True)))]
            report = {"summary": {"connections_analyzed": 4},
                      "analysis": {"total_ports": 0}, "connections": rows}
            (results / "conn" / "connect_report.json").write_text(json.dumps(report))
            (results / "cdc" / "cdc_report.json").write_text('{"crossings": []}')
            sample = select_sample(report, "seed", 1)
            labels = [{"id": row["id"], "verdict": "confirmed", "evidence": "rtl.sv:1"}
                      for row in sample["samples"]]
            labels[0]["verdict"] = "contradicted"
            annotations = {"seed": "seed", "per_stratum": 1,
                           "population_sha256": sample["population_sha256"], "labels": labels}
            with (mock.patch.object(benchmark_evaluate, "SCRIPT_DIR", root),
                  mock.patch.object(benchmark_evaluate, "CONFIG_FILE", root / "targets.yaml"),
                  mock.patch.object(benchmark_evaluate, "GOLDEN_DIR", golden),
                  mock.patch.object(benchmark_evaluate, "RESULTS_DIR", root / "results"),
                  redirect_stdout(io.StringIO())):
                (golden / "sample.yaml").write_text(benchmark_evaluate.yaml.safe_dump(annotations))
                with self.assertRaisesRegex(SystemExit, "contradicted rows"):
                    benchmark_evaluate.main()
                annotations["population_sha256"] = "stale"
                (golden / "sample.yaml").write_text(benchmark_evaluate.yaml.safe_dump(annotations))
                with self.assertRaisesRegex(ValueError, "stale"):
                    benchmark_evaluate.main()

    def test_sva_probe_checks_secondary_fifo_label_and_full_label_bijection(self):
        crossing = {"source": "top.fifo.fifo_wptr_gray_q", "dest": "top.fifo.sync_wptr.u_sync_1.q_o",
                    "rule": "Ac_cdc01", "sync_type": "two_ff", "category": "CAUTION"}
        primary = "cdc_CAUTION_1_2ff"
        secondary = "cdc_CAUTION_1_fifo_gray"
        base = {"crossings": [crossing]}
        with_sva = {"crossings": [{**crossing, "sva_assertion_id": primary,
                                   "sva_assertion_ids": [primary, secondary]}]}
        reference = [{"source": crossing["source"], "dest": crossing["dest"],
                      "assertion_suffix": "_fifo_gray", "contains": ["$countones(top.fifo.ptr)"]}]
        sva = (f"property p_{primary};\n    1'b1 |=> top.fifo.sync_wptr.u_sync_2.q_o;\nendproperty\n"
               f"{primary}: assert property (p_{primary});\n"
               f"property p_{secondary};\n    $countones(top.fifo.ptr) <= 1;\nendproperty\n"
               f"{secondary}: assert property (p_{secondary});\n")
        matched = evaluate_sva_probe(base, with_sva, sva, reference)
        self.assertEqual(matched["references_matched"], 1)
        self.assertEqual(matched["assertions_emitted"], 2)
        self.assertEqual(matched["assertions_linked"], 2)
        self.assertTrue(matched["all_assertions_linked"])
        primary_only = {"crossings": [{**crossing, "sva_assertion_id": primary}]}
        self.assertEqual(evaluate_sva_probe(base, primary_only, sva, reference)["references_matched"], 0)
        self.assertFalse(evaluate_sva_probe(base, primary_only, sva, reference)["all_assertions_linked"])
        missing_secondary = sva.replace(f"{secondary}: assert property (p_{secondary});\n", "")
        self.assertFalse(evaluate_sva_probe(base, with_sva, missing_secondary, reference)
                         ["all_assertions_linked"])
        wrong_target = sva.replace(f"{secondary}: assert property (p_{secondary});",
                                   f"{secondary}: assert property (p_{primary});")
        self.assertFalse(evaluate_sva_probe(base, with_sva, wrong_target, reference)
                         ["all_assertions_linked"])

    def test_sva_probe_missing_json_link_fails_benchmark_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            results = root / "results" / "aes"
            (results / "conn").mkdir(parents=True)
            (results / "cdc").mkdir()
            (results / "cdc_sva").mkdir()
            golden = root / "golden"
            golden.mkdir()
            (root / "targets.yaml").write_text(
                "targets:\n  - name: aes\n    level: L1\n    sva_probe: true\n")
            (golden / "aes.yaml").write_text(
                "sva_references:\n  - source: aes.u.dst_ack_i\n"
                "    dest: aes.u.src_ack_o\n    assertion_suffix: _data_hold_dst2src\n")
            (results / "metrics.json").write_text(json.dumps({
                "name": "aes", "level": "L1", "conn_status": "reported",
                "cdc_status": "reported", "cdc_sva_status": "reported",
            }))
            (results / "conn" / "connect_report.json").write_text(json.dumps({
                "summary": {"connections_analyzed": 0}, "analysis": {"total_ports": 0},
                "connections": [],
            }))
            crossing = {"source": "aes.u.dst_ack_i", "dest": "aes.u.src_ack_o",
                        "rule": "Ac_cdc01", "category": "CAUTION", "sync_type": "handshake"}
            for subdir in ("cdc", "cdc_sva"):
                (results / subdir / "cdc_report.json").write_text(json.dumps({
                    "crossings": [crossing],
                }))
            (results / "cdc_sva" / "cdc_assertions.sva").write_text(
                "module svlens_cdc_assertions; endmodule\n")
            with (mock.patch.object(benchmark_evaluate, "CONFIG_FILE", root / "targets.yaml"),
                  mock.patch.object(benchmark_evaluate, "GOLDEN_DIR", golden),
                  mock.patch.object(benchmark_evaluate, "RESULTS_DIR", root / "results"),
                  redirect_stdout(io.StringIO()),
                  self.assertRaisesRegex(SystemExit, "CDC SVA probe missing linked assertions")):
                benchmark_evaluate.main()
            (golden / "aes.yaml").write_text("{}\n")
            with (mock.patch.object(benchmark_evaluate, "CONFIG_FILE", root / "targets.yaml"),
                  mock.patch.object(benchmark_evaluate, "GOLDEN_DIR", golden),
                  mock.patch.object(benchmark_evaluate, "RESULTS_DIR", root / "results"),
                  redirect_stdout(io.StringIO()),
                  self.assertRaisesRegex(SystemExit, "requires at least one sva_reference")):
                benchmark_evaluate.main()

    def test_sva_probe_requires_linked_assertion_and_unchanged_crossings(self):
        crossing = {"source": "top.u.dst_ack_i", "dest": "top.u.src_ack_o",
                    "rule": "Ac_cdc01", "sync_type": "handshake", "category": "CAUTION", "severity": "medium"}
        base = {"crossings": [crossing]}
        with_sva = {"crossings": [{**crossing, "sva_assertion_id": "cdc_CAUTION_2_data_hold_dst2src"}]}
        references = [{"source": crossing["source"], "dest": crossing["dest"],
                       "assertion_suffix": "_data_hold_dst2src", "contains": ["$past(top.u.data_o, 2)"]}]
        sva = ("property p_cdc_CAUTION_2_data_hold_dst2src;\n"
               "    src_req && src_ack |-> $past(top.u.data_o, 2) == top.u.data_o;\n"
               "endproperty\n"
               "cdc_CAUTION_2_data_hold_dst2src: assert property (p_cdc_CAUTION_2_data_hold_dst2src);\n")
        matched = evaluate_sva_probe(base, with_sva, sva, references)
        self.assertEqual(matched["references_expected"], 1)
        self.assertEqual(matched["references_matched"], 1)
        self.assertTrue(matched["classification_preserved"])
        self.assertEqual(evaluate_sva_probe(base, with_sva, "", references)["references_matched"], 0)
        misplaced = sva.replace("$past(top.u.data_o, 2)", "other_data") + \
            "// $past(top.u.data_o, 2) belongs to another assertion\n"
        self.assertEqual(evaluate_sva_probe(base, with_sva, misplaced, references)
                         ["references_matched"], 0)
        self.assertEqual(evaluate_sva_probe(base, {"crossings": [crossing]}, sva, references)
                         ["references_matched"], 0)
        changed = {"crossings": [{**with_sva["crossings"][0], "category": "INFO"}]}
        self.assertFalse(evaluate_sva_probe(base, changed, sva, references)["classification_preserved"])
        self.assertIsNone(evaluate_sva_probe(base, {}, sva, references)["classification_preserved"])

    def test_period_probe_counts_context_without_reclassifying_paths(self):
        base = {"crossings": [
            {"source": "top.a", "dest": "top.b", "rule": "Ac_cdc01",
             "sync_type": "none", "category": "VIOLATION", "source_root_domain": "clk_a"},
            {"source": "top.c", "dest": "top.d", "rule": "Ac_cdc01",
             "sync_type": "2ff", "category": "INFO"},
        ]}
        timed = {"crossings": [
            {**base["crossings"][0], "source_root_domain": "A_CLK", "timing_basis_ns": 8.5},
            base["crossings"][1],
        ]}
        result = evaluate_period_probe(base, timed)
        self.assertEqual(result["base_crossings"], 2)
        self.assertEqual(result["timed_crossings"], 2)
        self.assertEqual(result["timing_basis_count"], 1)
        self.assertEqual(result["timing_basis_values_ns"], [8.5])
        self.assertTrue(result["classification_preserved"])
        reference = [{"source": "top.a", "dest": "top.b", "timing_basis_ns": 8.5,
                      "source_root_domain": "A_CLK", "dest_root_domain": None,
                      "category": "VIOLATION"}]
        self.assertEqual(evaluate_period_probe(base, timed, reference)["period_references_matched"], 1)
        self.assertEqual(evaluate_period_probe(base, timed, [{**reference[0], "timing_basis_ns": 10}])
                         ["period_references_matched"], 0)
        clock_ref = [{"source": "A_CLK", "period_ns": 8.5}]
        timed["domains"] = [{"name": "A_CLK", "source": "A_CLK", "period_ns": 8.5}]
        self.assertEqual(evaluate_period_probe(base, timed, reference, clock_ref)
                         ["clock_period_references_matched"], 1)
        self.assertEqual(evaluate_period_probe(base, {**timed, "domains": []}, reference, clock_ref)
                         ["clock_period_references_matched"], 0)
        downgraded = {"crossings": [{**timed["crossings"][0], "category": "INFO"}, timed["crossings"][1]]}
        self.assertFalse(evaluate_period_probe(base, downgraded)["classification_preserved"])
        self.assertIsNone(evaluate_period_probe(base, {})["classification_preserved"])

    def test_cdc_uses_report_category_values_and_labels_pair_coverage(self):
        report = {"crossings": [
            {"category": "VIOLATION", "source_domain": "top.clk_i",
             "dest_domain": "top.clk_edn_i"},
            {"category": "CAUTION", "source_domain": "top.clk_edn_i",
             "dest_domain": "top.clk_i"},
            {"category": "INFO", "source_domain": "top.clk_i",
             "dest_domain": "top.clk_edn_i"},
        ]}
        golden = {"known_crossings": [
            {"from_domain": "clk_i", "to_domain": "clk_edn_i"},
            {"from_domain": "clk_edn_i", "to_domain": "clk_i"},
        ]}
        result = evaluate_cdc(report, golden)
        self.assertEqual(result["total_violations"], 1)
        self.assertEqual(result["total_cautions"], 1)
        self.assertEqual(result["total_info"], 1)
        self.assertEqual(result["known_found"], 2)
        self.assertEqual(result["known_pair_coverage"], 1.0)
        self.assertIsNone(result["known_root_pair_coverage"])
        self.assertNotIn("precision", result)

    def test_root_provenance_pair_probe_is_separate_from_local_domains(self):
        report = {"crossings": [
            {"category": "CAUTION", "source_domain": "local.main_gate",
             "dest_domain": "local.io_gate", "source_root_domain": "clk_main_i",
             "dest_root_domain": "clk_io_i"},
            {"category": "INFO", "source_domain": "other",
             "dest_domain": "unknown", "source_root_domain": "",
             "dest_root_domain": "clk_io_i"},
        ]}
        golden = {"known_crossings": [
            {"from_domain": "clk_main_i", "to_domain": "clk_io_i"},
        ]}
        result = evaluate_cdc(report, golden)
        self.assertEqual(result["known_found"], 0)
        self.assertEqual(result["known_root_found"], 1)
        self.assertEqual(result["known_root_pair_coverage"], 1.0)
        self.assertEqual(result["root_labeled_crossings"], 1)

    def test_domain_pair_probes_are_not_legacy_known_crossing_ground_truth(self):
        report = {"crossings": [{"category": "CAUTION", "source_domain": "top.clk_io_i",
                                 "dest_domain": "top.clk_usb_i"}]}
        golden = {"domain_pair_probes": [{"from_domain": "clk_io_i", "to_domain": "clk_usb_i"}],
                  "known_crossings": [{"from_domain": "clk_main_i", "to_domain": "clk_usb_i"}]}
        result = evaluate_cdc(report, golden)
        self.assertEqual(result["known_expected"], 1)
        self.assertEqual(result["known_found"], 1)

    def test_cdc_signal_references_require_both_path_and_expected_roots(self):
        golden = {"reference_crossings": [
            {"source": "top.fifo.fifo_wptr_gray_q", "dest": "top.fifo.sync_wptr.u_sync_1.q_o",
             "source_root_domain": "clk_io_i", "dest_root_domain": "clk_usb_i"},
        ]}
        crossing = {"source": "top.fifo.fifo_wptr_gray_q", "dest": "top.fifo.sync_wptr.u_sync_1.q_o",
                    "source_domain": "local_wr", "dest_domain": "local_rd",
                    "source_root_domain": "clk_io_i", "dest_root_domain": "clk_usb_i",
                    "category": "CAUTION"}
        present = evaluate_cdc({"crossings": [crossing]}, golden)
        self.assertEqual(present["reference_crossings_expected"], 1)
        self.assertEqual(present["reference_crossings_found"], 1)
        self.assertEqual(present["reference_roots_matched"], 1)
        self.assertEqual(missing_reference_crossings([{"name": "soc", "cdc": present}]), [])

        wrong_roots = evaluate_cdc({"crossings": [{**crossing, "dest_root_domain": "clk_main_i"}]}, golden)
        self.assertEqual(wrong_roots["reference_crossings_found"], 1)
        self.assertEqual(wrong_roots["reference_roots_matched"], 0)
        self.assertEqual(missing_reference_crossings([{"name": "soc", "cdc": wrong_roots}]),
                         ["soc (paths 1/1, roots 0/1)"])
        absent = evaluate_cdc({"crossings": []}, golden)
        self.assertEqual(absent["reference_crossings_found"], 0)
        self.assertEqual(missing_reference_crossings([{"name": "soc", "cdc": absent}]),
                         ["soc (paths 0/1, roots 0/1)"])
        self.assertIsNone(evaluate_cdc({}, golden)["reference_crossings_found"])

    def test_cdc_reference_can_pin_category_without_claiming_protocol_safety(self):
        golden = {"reference_crossings": [
            {"source": "top.pwrmgr.cause_q", "dest": "top.pwrmgr.cause_o",
             "source_root_domain": "clk_aon_i", "dest_root_domain": "clk_io_i",
             "category": "VIOLATION"},
        ]}
        crossing = {"source": "top.pwrmgr.cause_q", "dest": "top.pwrmgr.cause_o",
                    "source_root_domain": "clk_aon_i", "dest_root_domain": "clk_io_i",
                    "category": "VIOLATION"}
        matched = evaluate_cdc({"crossings": [crossing]}, golden)
        self.assertEqual(matched["reference_categories_expected"], 1)
        self.assertEqual(matched["reference_categories_matched"], 1)
        self.assertEqual(missing_reference_crossings([{"name": "soc", "cdc": matched}]), [])

        downgraded = evaluate_cdc({"crossings": [{**crossing, "category": "INFO"}]}, golden)
        self.assertEqual(downgraded["reference_crossings_found"], 1)
        self.assertEqual(downgraded["reference_roots_matched"], 1)
        self.assertEqual(downgraded["reference_categories_matched"], 0)
        self.assertEqual(missing_reference_crossings([{"name": "soc", "cdc": downgraded}]),
                         ["soc (paths 1/1, roots 1/1, categories 0/1)"])

        wrong_root = evaluate_cdc({"crossings": [{**crossing, "dest_root_domain": "clk_main_i"}]}, golden)
        self.assertEqual(wrong_root["reference_categories_matched"], 0)

        no_root_golden = {"reference_crossings": [
            {"source": crossing["source"], "dest": crossing["dest"], "category": "VIOLATION"},
        ]}
        self.assertEqual(evaluate_cdc({"crossings": [crossing]}, no_root_golden)
                         ["reference_categories_matched"], 0)

    def test_cdc_guidance_probe_requires_path_category_and_safe_wording(self):
        golden = {"guidance_references": [
            {"source": "top.spi.src_q", "dest": "top.spi.dst_q", "category": "VIOLATION",
             "contains": ["Review the clock relationship", "pulse/toggle"],
             "excludes": ["Insert 2-FF synchronizer"]},
        ]}
        crossing = {"source": "top.spi.src_q", "dest": "top.spi.dst_q", "category": "VIOLATION",
                    "recommendation": "Review the clock relationship; use pulse/toggle for events"}
        matched = evaluate_cdc({"crossings": [crossing]}, golden)
        self.assertEqual(matched["guidance_references_expected"], 1)
        self.assertEqual(matched["guidance_references_matched"], 1)
        self.assertEqual(missing_guidance_references([{"name": "soc", "cdc": matched}]), [])

        unsafe = evaluate_cdc({"crossings": [{**crossing,
                               "recommendation": "Insert 2-FF synchronizer"}]}, golden)
        self.assertEqual(unsafe["guidance_references_matched"], 0)
        self.assertEqual(missing_guidance_references([{"name": "soc", "cdc": unsafe}]),
                         ["soc (guidance 0/1)"])
        downgraded = evaluate_cdc({"crossings": [{**crossing, "category": "INFO"}]}, golden)
        self.assertEqual(downgraded["guidance_references_matched"], 0)

    def test_unresolved_reset_probe_requires_sink_without_claimed_ff_driver(self):
        golden = {"reset_unresolved_references": [
            {"signal": "top.hmac.rst_ni", "dest_domain": "clk_hmac",
             "conditional_mux": {"output": "top.rstmgr.u_mux.clk_o",
                                 "input0": "top.rstmgr.u_mux.clk0_i",
                                 "input1": "top.rstmgr.u_mux.clk1_i",
                                 "select": "top.rstmgr.u_mux.sel_i",
                                 "input0_source": "top.rstmgr.rst_sync_n[0]",
                                 "input1_source": "top.rstmgr.scan_rst_ni",
                                 "select_dependencies": ["top.rstmgr.scanmode[3]"],
                                 "input0_candidate_ffs": [{"path": "top.rstmgr.rst_q", "domain": "clk_a",
                                                           "inverted": False}]},
             "conditional_mux_absent_fields": ["select_source", "selected_input"],
             "conditional_mux_count": 1},
        ]}
        reset = {"signal": "top.hmac.rst_ni", "asynchronous": True,
                 "dest_domains": ["clk_hmac"],
                 "conditional_muxes": [golden["reset_unresolved_references"][0]["conditional_mux"]]}
        matched = evaluate_cdc({"crossings": [], "reset_usage": [reset]}, golden)
        self.assertEqual(matched["reset_unresolved_expected"], 1)
        self.assertEqual(matched["reset_unresolved_matched"], 1)
        self.assertEqual(matched["reset_mux_usage_count"], 1)
        self.assertEqual(matched["reset_mux_outputs_count"], 1)
        self.assertEqual(matched["reset_mux_ff_candidate_usage_count"], 1)
        self.assertEqual(matched["reset_mux_distinct_ff_candidates"], 1)
        self.assertEqual(matched["reset_mux_truncated_count"], 0)
        self.assertEqual(matched["reset_mux_driver_claims"], 0)
        self.assertEqual(missing_reset_unresolved_references([{"name": "soc", "cdc": matched}]), [])

        no_mux = evaluate_cdc({"crossings": [], "reset_usage": [{**reset, "conditional_muxes": []}]}, golden)
        self.assertEqual(no_mux["reset_unresolved_matched"], 0)
        wrong_mux = evaluate_cdc({"crossings": [], "reset_usage": [
            {**reset, "conditional_muxes": [{**reset["conditional_muxes"][0], "input1": "top.other"}]}
        ]}, golden)
        self.assertEqual(wrong_mux["reset_unresolved_matched"], 0)
        invented_select = evaluate_cdc({"crossings": [], "reset_usage": [
            {**reset, "conditional_muxes": [{**reset["conditional_muxes"][0], "select_source": "top.scanmode"}]}
        ]}, golden)
        self.assertEqual(invented_select["reset_unresolved_matched"], 0)
        extra_mux = evaluate_cdc({"crossings": [], "reset_usage": [
            {**reset, "conditional_muxes": reset["conditional_muxes"] + [{"output": "top.other"}]}
        ]}, golden)
        self.assertEqual(extra_mux["reset_unresolved_matched"], 0)

        claimed = evaluate_cdc({"crossings": [], "reset_usage": [{**reset, "driver_ff": "top.rst_q"}]}, golden)
        self.assertEqual(claimed["reset_unresolved_matched"], 0)
        self.assertEqual(claimed["reset_mux_driver_claims"], 1)
        fixed = evaluate_cdc({"crossings": [], "reset_usage": [
            {**reset, "driver_ff": "top.rst_q", "conditional_muxes": [
                {**reset["conditional_muxes"][0], "selected_input": 0}]}
        ]}, golden)
        self.assertEqual(fixed["reset_mux_driver_claims"], 0)
        self.assertEqual(missing_reset_unresolved_references([{"name": "soc", "cdc": claimed}]),
                         ["soc (reset unresolved 0/1)"])

    def test_conn_uses_nested_summary_and_analysis(self):
        report = {"summary": {"connections_analyzed": 2, "bit_flow_gap_count": 3,
                              "bit_flow_gap_reasons": {"nonstructural_source": 2,
                                                       "unresolved_source_range": 1}},
                  "analysis": {"total_ports": 9},
                  "issues": [{"type": "WIDTH_MISMATCH"}],
                  "connections": [{"kind": "direct"}, {"kind": "approximate"}]}
        result = evaluate_conn(report)
        self.assertEqual(result["total_connections"], 2)
        self.assertEqual(result["total_ports"], 9)
        self.assertEqual(result["issues"], {"WIDTH_MISMATCH": 1})
        self.assertEqual(result["direct_connections"], 1)
        self.assertEqual(result["approximate_connections"], 1)
        self.assertEqual(result["bit_flow_gaps"], 3)
        self.assertEqual(result["bit_flow_gap_reasons"],
                         {"nonstructural_source": 2, "unresolved_source_range": 1})

    def test_conn_without_kind_does_not_claim_direct_precision(self):
        result = evaluate_conn({"summary": {"connections_analyzed": 1},
                                "connections": [{"source": "a", "dest": "b"}]})
        self.assertIsNone(result["direct_connections"])
        self.assertIsNone(result["approximate_connections"])
        self.assertIsNone(result["bit_flow_gaps"])
        self.assertIsNone(result["bit_flow_gap_reasons"])

    def test_conn_gap_reason_counts_must_match_total(self):
        with self.assertRaisesRegex(ValueError, "bit-flow gap reason count mismatch"):
            evaluate_conn({"summary": {"connections_analyzed": 0, "bit_flow_gap_count": 2,
                                       "bit_flow_gap_reasons": {"width_limit": 1}},
                           "connections": []})

    def test_conn_reference_path_presence_is_reported_separately(self):
        golden = {"known_connections": [
            {"source": "hmac.u_socket.tl_d_o", "dest": "hmac.u_reg_if.tl_i[106:0]",
             "description": "socket array element to register adapter"},
        ]}
        report = {"summary": {"connections_analyzed": 1},
                  "connections": [{"source": "hmac.u_socket.tl_d_o",
                                   "dest": "hmac.u_reg_if.tl_i[106:0]",
                                   "kind": "approximate"}]}
        present = evaluate_conn(report, golden)
        self.assertEqual(present["reference_paths_expected"], 1)
        self.assertEqual(present["reference_paths_found"], 1)
        missing = evaluate_conn({"summary": {"connections_analyzed": 0},
                                 "connections": []}, golden)
        self.assertEqual(missing["reference_paths_found"], 0)
        self.assertIsNone(evaluate_conn({}, golden)["reference_paths_found"])
        self.assertEqual(missing_reference_paths([{"name": "hmac", "conn": missing}]), ["hmac (0/1)"])
        self.assertEqual(missing_reference_paths([{"name": "hmac", "conn": present}]), [])

    def test_conn_reference_can_require_exact_lane_and_evidence_kind(self):
        golden = {"known_connections": [
            {"source": "soc.u_edn.edn_o[15:0]", "dest": "soc.u_sink.edn_i[7:0]",
             "kind": "direct", "source_bits": {"low": 8, "high": 15},
             "dest_bits": {"low": 0, "high": 7}, "exclusive": True},
        ]}
        row = {"source": "soc.u_edn.edn_o[15:0]", "dest": "soc.u_sink.edn_i[7:0]",
               "kind": "direct", "source_bits": {"low": 8, "high": 15},
               "dest_bits": {"low": 0, "high": 7}}
        report = {"summary": {"connections_analyzed": 1}, "connections": [row]}
        self.assertEqual(evaluate_conn(report, golden)["reference_paths_found"], 1)
        wrong_lane = {**row, "source_bits": {"low": 0, "high": 7}}
        self.assertEqual(evaluate_conn({"summary": {"connections_analyzed": 1},
                                        "connections": [wrong_lane]}, golden)["reference_paths_found"], 0)
        self.assertEqual(evaluate_conn({"summary": {"connections_analyzed": 2},
                                        "connections": [row, wrong_lane]}, golden)["reference_paths_found"], 0)
        wrong_kind = {**row, "kind": "approximate"}
        self.assertEqual(evaluate_conn({"summary": {"connections_analyzed": 1},
                                        "connections": [wrong_kind]}, golden)["reference_paths_found"], 0)

    def test_conn_summary_must_match_emitted_connection_rows(self):
        with self.assertRaisesRegex(ValueError, "connection count mismatch"):
            evaluate_conn({"summary": {"connections_analyzed": 2},
                           "connections": [{"kind": "direct"}]})

    def test_forbidden_connection_requires_observed_endpoints_and_absent_pair(self):
        golden = {"forbidden_connections": [{"source": "soc.edn1.o", "dest": "soc.aes.i"}]}
        rows = [{"source": "soc.edn1.o", "dest": "soc.otbn.i", "kind": "direct"},
                {"source": "soc.edn0.o", "dest": "soc.aes.i", "kind": "direct"}]
        report = {"summary": {"connections_analyzed": len(rows)}, "connections": rows}
        clean = evaluate_conn(report, golden)
        self.assertEqual(clean["forbidden_paths_expected"], 1)
        self.assertEqual(clean["forbidden_paths_checked"], 1)
        self.assertEqual(clean["forbidden_paths_absent"], 1)
        self.assertEqual(missing_forbidden_paths([{"name": "soc", "conn": clean}]), [])

        present_rows = rows + [{"source": "soc.edn1.o", "dest": "soc.aes.i", "kind": "approximate"}]
        present = evaluate_conn({"summary": {"connections_analyzed": 3}, "connections": present_rows}, golden)
        self.assertEqual(present["forbidden_paths_absent"], 0)
        self.assertEqual(missing_forbidden_paths([{"name": "soc", "conn": present}]),
                         ["soc (absent 0/1, endpoints 1/1)"])

        missing_endpoint = evaluate_conn({"summary": {"connections_analyzed": 1},
                                          "connections": rows[:1]}, golden)
        self.assertEqual(missing_endpoint["forbidden_paths_checked"], 0)
        self.assertEqual(missing_endpoint["forbidden_paths_absent"], 0)
        self.assertEqual(missing_forbidden_paths([{"name": "soc", "conn": missing_endpoint}]),
                         ["soc (absent 0/1, endpoints 0/1)"])
        self.assertIsNone(evaluate_conn({}, golden)["forbidden_paths_absent"])

    def test_forbidden_connection_rejects_empty_endpoints(self):
        with self.assertRaisesRegex(ValueError, "forbidden connection needs source and dest"):
            evaluate_conn({"summary": {"connections_analyzed": 0}, "connections": []},
                          {"forbidden_connections": [{"source": "soc.edn1.o"}]})

    def test_forbidden_connection_rejects_duplicate_or_positive_pair(self):
        pair = {"source": "soc.edn1.o", "dest": "soc.aes.i"}
        report = {"summary": {"connections_analyzed": 0}, "connections": []}
        with self.assertRaisesRegex(ValueError, "duplicate forbidden connection"):
            evaluate_conn(report, {"forbidden_connections": [pair, pair]})
        with self.assertRaisesRegex(ValueError, "both required and forbidden"):
            evaluate_conn(report, {"known_connections": [pair], "forbidden_connections": [pair]})

    def test_missing_cdc_report_keeps_expected_pairs_but_no_measurement(self):
        result = evaluate_cdc({}, {"known_crossings": [
            {"from_domain": "clk_a", "to_domain": "clk_b"},
        ]})
        self.assertEqual(result["known_expected"], 1)
        self.assertIsNone(result["known_found"])
        self.assertIsNone(result["total_violations"])
        self.assertIsNone(result["known_pair_coverage"])


if __name__ == "__main__":
    unittest.main()
