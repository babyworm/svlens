#!/usr/bin/env python3
"""Evaluate svlens benchmark results against golden expectations."""

import hashlib
import json
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML required. Install: pip install pyyaml", file=sys.stderr)
    sys.exit(1)

from sample_connections import evaluate_annotations, select_sample
from source_recall import audit_source_recall

SCRIPT_DIR = Path(__file__).parent
RESULTS_DIR = SCRIPT_DIR / "results"
GOLDEN_DIR = SCRIPT_DIR / "golden"
CONFIG_FILE = SCRIPT_DIR / "targets.yaml"
FILELIST_DIR = SCRIPT_DIR / "filelists"


def load_metrics(name: str) -> dict:
    path = RESULTS_DIR / name / "metrics.json"
    if not path.exists():
        return {"name": name, "status": "NOT_RUN"}
    with open(path) as f:
        return json.loads(f.read())


def load_json_report(name: str, subdir: str, filename: str) -> dict:
    path = RESULTS_DIR / name / subdir / filename
    if not path.exists():
        return {}
    with open(path) as f:
        return json.load(f)


def load_golden(name: str) -> dict:
    path = GOLDEN_DIR / f"{name}.yaml"
    if not path.exists():
        return {}
    with open(path) as f:
        return yaml.safe_load(f) or {}


def evaluate_cdc(cdc_report: dict, golden: dict) -> dict:
    # Legacy golden files called these "known crossings", but they were only
    # domain-pair topology probes, not signal-level adjudicated paths.
    known = golden.get("domain_pair_probes", golden.get("known_crossings", []))
    references = golden.get("reference_crossings", [])
    guidance_references = golden.get("guidance_references", [])
    reset_unresolved = golden.get("reset_unresolved_references", [])
    result = {
        "total_violations": None, "total_cautions": None,
        "total_info": None, "total_waived": None,
        "known_expected": len(known), "known_found": None,
        "known_pair_coverage": None, "known_root_found": None,
        "known_root_pair_coverage": None, "root_labeled_crossings": None,
        "reference_crossings_expected": len(references),
        "reference_crossings_found": None, "reference_roots_matched": None,
        "reference_categories_expected": sum(bool(ref.get("category")) for ref in references),
        "reference_categories_matched": None,
        "guidance_references_expected": len(guidance_references),
        "guidance_references_matched": None,
        "reset_unresolved_expected": len(reset_unresolved),
        "reset_unresolved_matched": None,
        "reset_mux_usage_count": None, "reset_mux_outputs_count": None,
        "reset_mux_ff_candidate_usage_count": None, "reset_mux_distinct_ff_candidates": None,
        "reset_mux_truncated_count": None, "reset_mux_driver_claims": None,
    }
    if not cdc_report:
        return result

    result.update(total_violations=0, total_cautions=0,
                  total_info=0, total_waived=0, known_found=0)
    reset_usage = cdc_report.get("reset_usage", [])
    result["reset_mux_usage_count"] = sum(bool(usage.get("conditional_muxes")) for usage in reset_usage)
    result["reset_mux_outputs_count"] = len({
        mux.get("output") for usage in reset_usage for mux in usage.get("conditional_muxes", [])
        if mux.get("output")
    })
    result["reset_mux_ff_candidate_usage_count"] = sum(any(
        mux.get("input0_candidate_ffs") or mux.get("input1_candidate_ffs")
        for mux in usage.get("conditional_muxes", [])) for usage in reset_usage)
    result["reset_mux_distinct_ff_candidates"] = len({
        (candidate.get("path"), candidate.get("domain"))
        for usage in reset_usage for mux in usage.get("conditional_muxes", [])
        for branch in ("input0_candidate_ffs", "input1_candidate_ffs")
        for candidate in mux.get(branch, []) if candidate.get("path")
    })
    result["reset_mux_truncated_count"] = sum(
        usage.get("conditional_mux_trace_truncated") is True for usage in reset_usage)
    result["reset_mux_driver_claims"] = sum(
        bool(usage.get("driver_ff")) and any(
            mux.get("selected_input") not in (0, 1) for mux in usage.get("conditional_muxes", []))
        for usage in reset_usage)
    crossings = cdc_report.get("crossings", [])
    for c in crossings:
        cat = c.get("category", "").upper()
        if cat == "VIOLATION":
            result["total_violations"] += 1
        elif cat == "CAUTION":
            result["total_cautions"] += 1
        elif cat == "INFO":
            result["total_info"] += 1
        elif cat == "WAIVED":
            result["total_waived"] += 1

    found_pairs = set()
    root_pairs = set()
    has_root_fields = any("source_root_domain" in c and "dest_root_domain" in c
                          for c in crossings)
    for c in crossings:
        src = c.get("source_domain", "")
        dst = c.get("dest_domain", "")
        if src and dst:
            found_pairs.add((src, dst))
        root_src = c.get("source_root_domain", "")
        root_dst = c.get("dest_root_domain", "")
        if root_src and root_dst:
            root_pairs.add((root_src, root_dst))
    if has_root_fields:
        result["root_labeled_crossings"] = sum(
            bool(c.get("source_root_domain") and c.get("dest_root_domain"))
            for c in crossings)

    by_path = {}
    for crossing in crossings:
        by_path.setdefault((crossing.get("source"), crossing.get("dest")), []).append(crossing)
    result["reference_crossings_found"] = 0
    result["reference_roots_matched"] = 0
    result["reference_categories_matched"] = 0
    for reference in references:
        matches = by_path.get((reference.get("source"), reference.get("dest")), [])
        if matches:
            result["reference_crossings_found"] += 1
        root_matches = [c for c in matches
                        if c.get("source_root_domain") == reference.get("source_root_domain") and
                        c.get("dest_root_domain") == reference.get("dest_root_domain")]
        if reference.get("source_root_domain") and reference.get("dest_root_domain") and root_matches:
            result["reference_roots_matched"] += 1
        if (reference.get("category") and reference.get("source_root_domain") and
                reference.get("dest_root_domain") and any(
                c.get("category", "").upper() == reference["category"].upper()
                for c in root_matches)):
            result["reference_categories_matched"] += 1

    result["guidance_references_matched"] = sum(any(
        (not reference.get("category") or crossing.get("category") == reference["category"]) and
        all(fragment in (crossing.get("recommendation") or "")
            for fragment in reference.get("contains", [])) and
        all(fragment not in (crossing.get("recommendation") or "")
            for fragment in reference.get("excludes", []))
        for crossing in by_path.get((reference.get("source"), reference.get("dest")), []))
        for reference in guidance_references)
    def unresolved_reset_matches(usage: dict, reference: dict) -> bool:
        if (usage.get("signal") != reference.get("signal") or usage.get("asynchronous") is not True or
                (reference.get("dest_domain") and reference["dest_domain"] not in usage.get("dest_domains", [])) or
                usage.get("driver_ff") or usage.get("source_domain") or usage.get("driver_inverted") is not None):
            return False
        muxes = usage.get("conditional_muxes", [])
        expected_mux = reference.get("conditional_mux")
        absent_fields = reference.get("conditional_mux_absent_fields", [])
        if expected_mux is not None and not any(
                all(mux.get(key) == value for key, value in expected_mux.items()) and
                all(field not in mux for field in absent_fields) for mux in muxes):
            return False
        expected_count = reference.get("conditional_mux_count")
        return expected_count is None or len(muxes) == expected_count

    result["reset_unresolved_matched"] = sum(any(
        unresolved_reset_matches(usage, reference)
        for usage in reset_usage) for reference in reset_unresolved)

    if not known:
        return result

    def domain_matches(actual: str, expected: str) -> bool:
        return actual == expected or actual.endswith("." + expected)

    def matched_count(pairs: set) -> int:
        return sum(any(domain_matches(src, kc.get("from_domain", "")) and
                       domain_matches(dst, kc.get("to_domain", ""))
                       for src, dst in pairs) for kc in known)

    matched = matched_count(found_pairs)
    result["known_found"] = matched
    result["known_pair_coverage"] = round(matched / len(known), 3)
    if has_root_fields:
        root_matched = matched_count(root_pairs)
        result["known_root_found"] = root_matched
        result["known_root_pair_coverage"] = round(root_matched / len(known), 3)
    return result


def evaluate_period_probe(base_report: dict, timed_report: dict, references: list | None = None,
                          clock_references: list | None = None) -> dict:
    references = references or []
    clock_references = clock_references or []
    result = {
        "base_crossings": None, "timed_crossings": None,
        "timing_basis_count": None, "timing_basis_values_ns": None,
        "classification_preserved": None,
        "period_references_expected": len(references), "period_references_matched": None,
        "clock_period_references_expected": len(clock_references),
        "clock_period_references_matched": None,
    }
    if not base_report or not timed_report:
        return result

    base = base_report.get("crossings", [])
    timed = timed_report.get("crossings", [])
    def signature(crossing):
        return (crossing.get("source"), crossing.get("dest"), crossing.get("rule"),
                crossing.get("sync_type"), crossing.get("category"), crossing.get("severity"))
    periods = [crossing["timing_basis_ns"] for crossing in timed
               if isinstance(crossing.get("timing_basis_ns"), (int, float)) and
               crossing["timing_basis_ns"] > 0]
    matched = sum(any(
        crossing.get("source") == reference.get("source") and
        crossing.get("dest") == reference.get("dest") and
        crossing.get("timing_basis_ns") == reference.get("timing_basis_ns") and
        crossing.get("source_root_domain") == reference.get("source_root_domain") and
        crossing.get("dest_root_domain") == reference.get("dest_root_domain") and
        crossing.get("category") == reference.get("category")
        for crossing in timed) for reference in references)
    matched_clocks = sum(any(
        domain.get("source") == reference.get("source") and
        domain.get("period_ns") == reference.get("period_ns")
        for domain in timed_report.get("domains", [])) for reference in clock_references)
    result.update(base_crossings=len(base), timed_crossings=len(timed),
                  timing_basis_count=len(periods),
                  timing_basis_values_ns=sorted(set(periods)),
                  classification_preserved=Counter(map(signature, base)) ==
                  Counter(map(signature, timed)),
                  period_references_matched=matched,
                  clock_period_references_matched=matched_clocks)
    return result


def evaluate_sva_probe(base_report: dict, sva_report: dict, sva_text: str,
                       references: list | None = None) -> dict:
    references = references or []
    result = {"references_expected": len(references), "references_matched": None,
              "classification_preserved": None, "assertions_emitted": None,
              "assertions_linked": None, "all_assertions_linked": None}
    if not base_report or not sva_report:
        return result

    def signature(crossing):
        return (crossing.get("source"), crossing.get("dest"), crossing.get("rule"),
                crossing.get("sync_type"), crossing.get("category"), crossing.get("severity"))

    crossings = sva_report.get("crossings", [])

    def crossing_ids(crossing: dict) -> list[str] | None:
        primary = crossing.get("sva_assertion_id")
        ids = crossing.get("sva_assertion_ids")
        if ids is None:
            return [primary] if isinstance(primary, str) and primary else ([] if primary is None else None)
        if (not isinstance(ids, list) or not ids or not all(isinstance(value, str) and value for value in ids) or
                ids[0] != primary):
            return None
        return ids

    json_ids = [crossing_ids(crossing) for crossing in crossings]
    linked_ids = [label for ids in json_ids if ids is not None for label in ids]
    emitted_ids = re.findall(r"(?m)^([A-Za-z_][A-Za-z_0-9]*): assert property \(p_\1\);$", sva_text)

    def matches(reference: dict) -> bool:
        suffix = reference.get("assertion_suffix")
        if not isinstance(suffix, str) or not suffix:
            return False
        for crossing in crossings:
            if (crossing.get("source") != reference.get("source") or
                    crossing.get("dest") != reference.get("dest")):
                continue
            for assertion_id in crossing_ids(crossing) or []:
                if not assertion_id.endswith(suffix):
                    continue
                escaped = re.escape(assertion_id)
                property_match = re.search(
                    rf"(?ms)^property p_{escaped};\s*(.*?)^endproperty\s*$", sva_text)
                if (property_match and assertion_id in emitted_ids and
                        all(fragment in property_match.group(1)
                            for fragment in reference.get("contains", []))):
                    return True
        return False

    result["references_matched"] = sum(matches(reference) for reference in references)
    result["assertions_emitted"] = len(emitted_ids)
    result["assertions_linked"] = len(linked_ids)
    result["all_assertions_linked"] = (all(ids is not None for ids in json_ids) and
                                       len(linked_ids) == len(set(linked_ids)) and
                                       len(emitted_ids) == len(set(emitted_ids)) and
                                       set(linked_ids) == set(emitted_ids))
    result["classification_preserved"] = Counter(map(signature, base_report.get("crossings", []))) == \
        Counter(map(signature, crossings))
    return result


def evaluate_conn(conn_report: dict, golden: dict | None = None) -> dict:
    references = (golden or {}).get("known_connections", [])
    forbidden = (golden or {}).get("forbidden_connections", [])
    required_pairs = {(reference.get("source"), reference.get("dest"))
                      for reference in references if isinstance(reference, dict)}
    seen_forbidden = set()
    for reference in forbidden:
        if not isinstance(reference, dict) or not isinstance(reference.get("source"), str) or \
                not reference["source"] or \
                not isinstance(reference.get("dest"), str) or not reference["dest"]:
            raise ValueError("forbidden connection needs source and dest")
        pair = (reference["source"], reference["dest"])
        if pair in seen_forbidden:
            raise ValueError("duplicate forbidden connection")
        if pair in required_pairs:
            raise ValueError("connection cannot be both required and forbidden")
        seen_forbidden.add(pair)
    result = {"total_connections": None, "total_ports": None, "issues": {},
              "direct_connections": None, "approximate_connections": None,
              "bit_flow_gaps": None, "bit_flow_gap_reasons": None,
              "reference_paths_expected": len(references), "reference_paths_found": None,
              "forbidden_paths_expected": len(forbidden), "forbidden_paths_checked": None,
              "forbidden_paths_absent": None}
    if not conn_report:
        return result
    result["total_connections"] = conn_report.get("summary", {}).get("connections_analyzed")
    result["bit_flow_gaps"] = conn_report.get("summary", {}).get("bit_flow_gap_count")
    reasons = conn_report.get("summary", {}).get("bit_flow_gap_reasons")
    if isinstance(reasons, dict):
        if result["bit_flow_gaps"] is not None and sum(reasons.values()) != result["bit_flow_gaps"]:
            raise ValueError("bit-flow gap reason count mismatch")
        result["bit_flow_gap_reasons"] = reasons
    result["total_ports"] = conn_report.get("analysis", {}).get("total_ports")
    connections = conn_report.get("connections")
    if isinstance(connections, list) and result["total_connections"] is not None and \
            result["total_connections"] != len(connections):
        raise ValueError("connection count mismatch between summary and rows")
    for issue in conn_report.get("issues", []):
        t = issue.get("type", "unknown")
        result["issues"][t] = result["issues"].get(t, 0) + 1
    if isinstance(connections, list) and all(c.get("kind") in ("direct", "approximate") for c in connections):
        result["direct_connections"] = sum(c["kind"] == "direct" for c in connections)
        result["approximate_connections"] = sum(c["kind"] == "approximate" for c in connections)
    if isinstance(connections, list):
        sources = {row.get("source") for row in connections}
        destinations = {row.get("dest") for row in connections}
        pairs = {(row.get("source"), row.get("dest")) for row in connections}
        result["forbidden_paths_checked"] = sum(
            reference["source"] in sources and reference["dest"] in destinations
            for reference in forbidden)
        result["forbidden_paths_absent"] = sum(
            reference["source"] in sources and reference["dest"] in destinations and
            (reference["source"], reference["dest"]) not in pairs
            for reference in forbidden)

        def matches_reference(reference: dict) -> bool:
            if not reference.get("source") or not reference.get("dest"):
                return False
            endpoint_rows = [row for row in connections
                             if row.get("source") == reference["source"] and
                             row.get("dest") == reference["dest"]]
            evidence_keys = tuple(key for key in ("kind", "source_bits", "dest_bits") if key in reference)
            if not any(all(row.get(key) == reference[key] for key in evidence_keys) for row in endpoint_rows):
                return False
            return not reference.get("exclusive") or all(
                all(row.get(key) == reference[key] for key in evidence_keys) for row in endpoint_rows)

        result["reference_paths_found"] = sum(matches_reference(reference) for reference in references)
    return result


def missing_reference_paths(evals: list) -> list[str]:
    return [f"{ev['name']} ({ev['conn']['reference_paths_found']}/{ev['conn']['reference_paths_expected']})"
            for ev in evals
            if ev["conn"]["reference_paths_expected"] and
            ev["conn"]["reference_paths_found"] != ev["conn"]["reference_paths_expected"]]


def missing_forbidden_paths(evals: list) -> list[str]:
    return [f"{ev['name']} (absent {ev['conn']['forbidden_paths_absent']}/"
            f"{ev['conn']['forbidden_paths_expected']}, endpoints "
            f"{ev['conn']['forbidden_paths_checked']}/"
            f"{ev['conn']['forbidden_paths_expected']})"
            for ev in evals
            if ev["conn"]["forbidden_paths_expected"] and
            (ev["conn"]["forbidden_paths_checked"] != ev["conn"]["forbidden_paths_expected"] or
             ev["conn"]["forbidden_paths_absent"] != ev["conn"]["forbidden_paths_expected"])]


def missing_reference_crossings(evals: list) -> list[str]:
    return [f"{ev['name']} (paths {ev['cdc']['reference_crossings_found']}/"
            f"{ev['cdc']['reference_crossings_expected']}, roots {ev['cdc']['reference_roots_matched']}/"
            f"{ev['cdc']['reference_crossings_expected']}"
            + (f", categories {ev['cdc']['reference_categories_matched']}/"
               f"{ev['cdc']['reference_categories_expected']}"
               if ev['cdc']['reference_categories_expected'] else "") + ")"
            for ev in evals
            if ev["cdc"]["reference_crossings_expected"] and
            (ev["cdc"]["reference_crossings_found"] != ev["cdc"]["reference_crossings_expected"] or
             ev["cdc"]["reference_roots_matched"] != ev["cdc"]["reference_crossings_expected"] or
             ev["cdc"]["reference_categories_matched"] != ev["cdc"]["reference_categories_expected"])]


def missing_guidance_references(evals: list) -> list[str]:
    return [f"{ev['name']} (guidance {ev['cdc']['guidance_references_matched']}/"
            f"{ev['cdc']['guidance_references_expected']})"
            for ev in evals if ev["cdc"]["guidance_references_expected"] and
            ev["cdc"]["guidance_references_matched"] != ev["cdc"]["guidance_references_expected"]]


def missing_reset_unresolved_references(evals: list) -> list[str]:
    return [f"{ev['name']} (reset unresolved {ev['cdc']['reset_unresolved_matched']}/"
            f"{ev['cdc']['reset_unresolved_expected']})"
            for ev in evals if ev["cdc"]["reset_unresolved_expected"] and
            ev["cdc"]["reset_unresolved_matched"] != ev["cdc"]["reset_unresolved_expected"]]


def generate_report(evals: list) -> str:
    def fmt(value):
        return "N/A" if value is None else str(value)

    first_metrics = evals[0]["metrics"] if evals else {}
    lines = [
        "# svlens OpenTitan Benchmark Report", "",
        f"> Generated: {datetime.now(tz=timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}", "",
        f"OpenTitan tag: `{first_metrics.get('opentitan_tag', 'unknown')}`  ",
        f"OpenTitan commit: `{first_metrics.get('opentitan_commit', 'unknown')}`", "",
        f"svlens version: `{first_metrics.get('svlens_version', 'unknown')}`  ",
        f"svlens binary SHA-256: `{first_metrics.get('svlens_binary_sha256', 'unknown')}`  ",
        f"svlens commit: `{first_metrics.get('svlens_commit', 'unknown')}`  ",
        f"Worktree dirty: `{first_metrics.get('svlens_dirty', 'unknown')}`", "",
        "Counts below are observed analyzer output. Pair probes only check",
        "whether a listed domain pair appears; neither is crossing-level",
        "recall or precision. Root provenance follows direct aliases and",
        "recognized one-input gates/dividers without changing CDC safety",
        "classification. Signal-level CDC references gate path presence,",
        "root labels, and optional categories; connectivity references can gate exact bit lanes,",
        "evidence kind, and exclusivity. Guidance probes gate wording and",
        "category, not the safety of a CDC protocol. Neither is whole-design precision",
        "or recall.",
        "",
        "## Performance", "",
        "| Target | Level | Conn Status | CDC Status | Conn Exit | CDC Exit | Conn Time | CDC Time |",
        "|--------|-------|-------------|------------|-----------|----------|-----------|----------|",
    ]
    for ev in evals:
        m = ev["metrics"]
        lines.append(
            f"| {m.get('name','?')} | {m.get('level','?')} | "
            f"{m.get('conn_status','not_run')} | {m.get('cdc_status','not_run')} | "
            f"{m.get('conn_exit','?')} | {m.get('cdc_exit','?')} | "
            f"{m.get('conn_ms','N/A')}ms | {m.get('cdc_ms','N/A')}ms |"
        )
    lines += ["", "## CDC Analysis", "",
        "| Target | Violations | Cautions | Info | Reference crossings | Guidance probes | Reset unresolved | Local pair probes | Root pair probes | Both roots labeled |",
        "|--------|-----------|----------|------|---------------------|-----------------|------------------|-------------------|------------------|--------------------|"]
    for ev in evals:
        c = ev["cdc"]
        coverage = (f"{c['known_pair_coverage']:.1%}"
                    if c["known_pair_coverage"] is not None else "N/A")
        pair_count = (f"{c['known_found']}/{c['known_expected']} ({coverage})"
                      if c['known_expected'] and c['known_found'] is not None else "N/A")
        root_coverage = (f"{c['known_root_pair_coverage']:.1%}"
                         if c["known_root_pair_coverage"] is not None else "N/A")
        root_pair_count = (f"{c['known_root_found']}/{c['known_expected']} ({root_coverage})"
                           if c['known_expected'] and c['known_root_found'] is not None else "N/A")
        references = (f"{c['reference_crossings_found']}/{c['reference_crossings_expected']} "
                      f"(roots {c['reference_roots_matched']}/{c['reference_crossings_expected']}"
                      if c['reference_crossings_expected'] else "N/A")
        if c['reference_categories_expected']:
            references += (f", categories {c['reference_categories_matched']}/"
                           f"{c['reference_categories_expected']}")
        if c['reference_crossings_expected']:
            references += ")"
        guidance = (f"{c['guidance_references_matched']}/{c['guidance_references_expected']}"
                    if c['guidance_references_expected'] else "N/A")
        reset_probe = (f"{c['reset_unresolved_matched']}/{c['reset_unresolved_expected']}"
                       if c['reset_unresolved_expected'] else "N/A")
        lines.append(f"| {ev['name']} | {fmt(c['total_violations'])} | {fmt(c['total_cautions'])} | "
                     f"{fmt(c['total_info'])} | {references} | {guidance} | {reset_probe} | {pair_count} | "
                     f"{root_pair_count} | {fmt(c['root_labeled_crossings'])} |")
    lines += ["", "## Reset Mux Topology", "",
              "Counts are structural reset routes; `selected_input` marks an elaboration-time branch, not RDC safety.",
              "",
              "| Target | Reset paths with mux candidates | Distinct mux outputs | Paths with FF candidates | Distinct FF candidates | Truncated traces | Unresolved-mux FF claims |",
              "|--------|-------------------------------:|---------------------:|-------------------------:|-----------------------:|-----------------:|-------------------------:|"]
    for ev in evals:
        c = ev["cdc"]
        lines.append(f"| {ev['name']} | {fmt(c['reset_mux_usage_count'])} | "
                     f"{fmt(c['reset_mux_outputs_count'])} | {fmt(c['reset_mux_ff_candidate_usage_count'])} | "
                     f"{fmt(c['reset_mux_distinct_ff_candidates'])} | {fmt(c['reset_mux_truncated_count'])} | "
                     f"{fmt(c['reset_mux_driver_claims'])} |")
    period_evals = [ev for ev in evals if ev.get("period_sdc")]
    if period_evals:
        lines += ["", "## CDC Period Context Probe", "",
                  "This benchmark-only projection supplies base-clock periods, not a sign-off SDC.",
                  "A populated timing basis is context, not a measured path delay or proof of CDC safety.", "",
                  "| Target | Period SDC | SDC SHA-256 | Base periods | Timed crossings | Basis values (ns) | Same classifications | Period references | Time |",
                  "|--------|------------|---------------|--------------|-----------------|-------------------|----------------------|-------------------|------|"]
        for ev in period_evals:
            probe = ev["cdc_periods"]
            metrics = ev["metrics"]
            timed_count = (f"{probe['timing_basis_count']}/{probe['timed_crossings']}"
                           if probe["timing_basis_count"] is not None else "N/A")
            values = (", ".join(str(value) for value in probe["timing_basis_values_ns"])
                      if probe["timing_basis_values_ns"] is not None else "N/A")
            refs = (f"{probe['period_references_matched']}/{probe['period_references_expected']}"
                    if probe["period_references_matched"] is not None else "N/A")
            clocks = (f"{probe['clock_period_references_matched']}/"
                      f"{probe['clock_period_references_expected']}"
                      if probe["clock_period_references_matched"] is not None else "N/A")
            lines.append(f"| {ev['name']} | `{ev['period_sdc']}` | "
                         f"`{metrics.get('cdc_periods_sdc_sha256', 'unknown')}` | "
                         f"{clocks} | {timed_count} | {values} | {fmt(probe['classification_preserved'])} | "
                         f"{refs} | {metrics.get('cdc_periods_ms', 'N/A')}ms |")
    sva_evals = [ev for ev in evals if ev.get("sva_probe")]
    if sva_evals:
        lines += ["", "## CDC SVA Probe", "",
                  "References link selected primitive contracts to exact CDC JSON crossings;",
                  "all emitted assert labels must also match JSON IDs. Neither proves CDC safety.", "",
                  "| Target | References | JSON/SVA labels | Exact links | Same classifications | Time |",
                  "|--------|------------|-----------------|-------------|----------------------|------|"]
        for ev in sva_evals:
            probe = ev["cdc_sva"]
            references = (f"{probe['references_matched']}/{probe['references_expected']}"
                          if probe["references_matched"] is not None else "N/A")
            labels = (f"{probe['assertions_linked']}/{probe['assertions_emitted']}"
                      if probe["assertions_linked"] is not None else "N/A")
            lines.append(f"| {ev['name']} | {references} | {labels} | "
                         f"{fmt(probe['all_assertions_linked'])} | "
                         f"{fmt(probe['classification_preserved'])} | "
                         f"{ev['metrics'].get('cdc_sva_ms', 'N/A')}ms |")
    lines += ["", "## Connectivity Analysis", "",
        "| Target | Connections | Direct | Approximate | Bit-flow gaps | Reference paths | Absent-path probes | Ports | Issues |",
        "|--------|------------|--------|-------------|---------------|-----------------|--------------------|-------|--------|"]
    for ev in evals:
        co = ev["conn"]
        iss = ", ".join(f"{k}:{v}" for k,v in co["issues"].items()) or "none"
        connections = co['total_connections'] if co['total_connections'] is not None else "N/A"
        ports = co['total_ports'] if co['total_ports'] is not None else "N/A"
        direct = co['direct_connections'] if co['direct_connections'] is not None else "N/A"
        approximate = co['approximate_connections'] if co['approximate_connections'] is not None else "N/A"
        gaps = co['bit_flow_gaps'] if co['bit_flow_gaps'] is not None else "N/A"
        reference_paths = (f"{co['reference_paths_found']}/{co['reference_paths_expected']}"
                           if co['reference_paths_expected'] else "N/A")
        absent_paths = (f"{fmt(co['forbidden_paths_absent'])}/{co['forbidden_paths_expected']} "
                        f"(endpoints {fmt(co['forbidden_paths_checked'])}/{co['forbidden_paths_expected']})"
                        if co['forbidden_paths_expected'] else "N/A")
        lines.append(f"| {ev['name']} | {connections} | {direct} | {approximate} | {gaps} | "
                     f"{reference_paths} | {absent_paths} | {ports} | {iss} |")
    reasons = sorted({reason for ev in evals
                      for reason in (ev["conn"]["bit_flow_gap_reasons"] or {})})
    if reasons:
        lines += ["", "Bit-flow gaps count elaborated assignment instances, not missing edges or recall.",
                  "Examples in JSON are capped at four per reason.", "",
                  "### Bit-flow gap reasons", "",
                  "| Reason | " + " | ".join(ev["name"] for ev in evals) + " |",
                  "|--------|" + "--------|" * len(evals)]
        for reason in reasons:
            counts = ["N/A" if ev["conn"]["bit_flow_gap_reasons"] is None
                      else str(ev["conn"]["bit_flow_gap_reasons"].get(reason, 0)) for ev in evals]
            lines.append("| " + reason + " | " + " | ".join(counts) + " |")
    audited = [ev for ev in evals if ev.get("conn_sample")]
    if audited:
        lines += ["", "## Connection Sample Audit", "",
                  "Deterministic SHA-256 sampling takes five rows from each kind/range stratum.",
                  "Source-backed labels are confirmed, contradicted, or unresolved; this small,",
                  "correlated sample is not a whole-SoC precision or recall estimate.", "",
                  "| Target | Stratum | Population | Confirmed | Contradicted | Unresolved |",
                  "|--------|---------|-----------:|----------:|-------------:|-----------:|"]
        for ev in audited:
            audit = ev["conn_sample"]
            for stratum, counts in audit["by_stratum"].items():
                lines.append(f"| {ev['name']} | {stratum} | {audit['population'][stratum]} | "
                             f"{counts['confirmed']} | {counts['contradicted']} | {counts['unresolved']} |")
            lines.append(f"| {ev['name']} | **total** | {sum(audit['population'].values())} | "
                         f"{audit['overall']['confirmed']} | {audit['overall']['contradicted']} | "
                         f"{audit['overall']['unresolved']} |")
            lines += ["", f"Population SHA-256: `{audit['population_sha256']}`."]
    source_audits = [ev for ev in evals if ev.get("source_recall")]
    if source_audits:
        lines += ["", "## Source-Derived Direct Wiring Recall", "",
                  "A separate Slang AST walk enumerates unique-output, single-bit named-net",
                  "connections between sibling instance ports in the listed SoC IP scopes.",
                  "This is bounded pattern recall, not whole-design recall.", "",
                  "| Target | Scope | Expected | Direct | Approximate only | Missing |",
                  "|--------|-------|---------:|-------:|-----------------:|--------:|"]
        for ev in source_audits:
            result = ev["source_recall"]
            for scope, counts in result["scope_results"].items():
                lines.append(f"| {ev['name']} | `{scope}` | {counts['expected']} | "
                             f"{counts['found_direct']} | {counts['approximate_only']} | "
                             f"{counts['missing']} |")
            lines.append(f"| {ev['name']} | **total** | {result['expected']} | "
                         f"{result['found_direct']} | {len(result['approximate_only'])} | "
                         f"{len(result['missing'])} |")
            lines += ["", f"Expected-pair SHA-256: `{result['expected_sha256']}`."]
    lines += ["", "---", ""]
    return "\n".join(lines)


def main():
    with open(CONFIG_FILE) as f:
        config = yaml.safe_load(f)
    print("=== Evaluating Benchmark Results ===")
    evals = []
    for target in config["targets"]:
        name = target["name"]
        print(f"  Evaluating {name}...")
        metrics = load_metrics(name)
        golden = load_golden(name)
        if target.get("sva_probe") and not golden.get("sva_references"):
            raise SystemExit(f"{name} SVA probe requires at least one sva_reference")
        if target.get("source_recall_scopes") and not target.get("source_recall_expected_sha256"):
            raise SystemExit(f"{name} source-recall probe requires an expected frame hash")
        base_cdc = (load_json_report(name, "cdc", "cdc_report.json")
                    if metrics.get("cdc_status") == "reported" else {})
        timed_cdc = (load_json_report(name, "cdc_periods", "cdc_report.json")
                     if metrics.get("cdc_periods_status") == "reported" else {})
        sva_cdc = (load_json_report(name, "cdc_sva", "cdc_report.json")
                   if metrics.get("cdc_sva_status") == "reported" else {})
        sva_path = RESULTS_DIR / name / "cdc_sva" / "cdc_assertions.sva"
        sva_text = sva_path.read_text() if sva_cdc and sva_path.is_file() else ""
        conn_report = (load_json_report(name, "conn", "connect_report.json")
                       if metrics.get("conn_status") == "reported" else {})
        conn_sample = None
        if target.get("sample_annotations") and conn_report:
            annotations = yaml.safe_load((SCRIPT_DIR / target["sample_annotations"]).read_text()) or {}
            sample = select_sample(conn_report, annotations.get("seed"), annotations.get("per_stratum"))
            conn_sample = evaluate_annotations(sample, annotations)
        source_recall = None
        if target.get("source_recall_scopes") and conn_report:
            source_recall = audit_source_recall(conn_report, FILELIST_DIR / f"{name}.f",
                                                target["top_module"], target["source_recall_scopes"])
            (RESULTS_DIR / name / "source_recall.json").write_text(json.dumps(source_recall, indent=2) + "\n")
        evals.append({
            "name": name,
            "metrics": metrics,
            "period_sdc": target.get("period_sdc"),
            "sva_probe": target.get("sva_probe"),
            "cdc": evaluate_cdc(base_cdc, golden),
            "cdc_periods": evaluate_period_probe(base_cdc, timed_cdc, golden.get("period_references", []),
                                                  golden.get("clock_period_references", []))
            if target.get("period_sdc") else None,
            "cdc_sva": evaluate_sva_probe(base_cdc, sva_cdc, sva_text,
                                           golden.get("sva_references", []))
            if target.get("sva_probe") else None,
            "conn": evaluate_conn(conn_report, golden),
            "conn_sample": conn_sample,
            "source_recall": source_recall,
            "source_recall_expected_sha256": target.get("source_recall_expected_sha256"),
        })
    report = generate_report(evals)
    out = RESULTS_DIR / "bench_report.md"
    out.parent.mkdir(exist_ok=True)
    with open(out, "w") as f:
        f.write(report)
    print(f"\nReport: {out}")
    print(report)
    incomplete = [
        f"{ev['name']}/{mode}"
        for ev in evals for mode in ("conn", "cdc")
        if ev["metrics"].get(f"{mode}_status") != "reported"
    ]
    incomplete.extend(f"{ev['name']}/cdc_periods" for ev in evals
                      if ev["period_sdc"] and ev["metrics"].get("cdc_periods_status") != "reported")
    incomplete.extend(f"{ev['name']}/cdc_sva" for ev in evals
                      if ev["sva_probe"] and ev["metrics"].get("cdc_sva_status") != "reported")
    if incomplete:
        raise SystemExit("Benchmark reports missing or timed out: " + ", ".join(incomplete))
    changed_source_frames = [ev["name"] for ev in evals if ev["source_recall"] and
                             ev["source_recall"]["expected_sha256"] !=
                             ev["source_recall_expected_sha256"]]
    if changed_source_frames:
        raise SystemExit("Source-derived recall frame changed; re-adjudicate RTL paths: " +
                         ", ".join(changed_source_frames))
    missing_source_recall = [ev["name"] for ev in evals if ev["source_recall"] and
                             (ev["source_recall"]["missing"] or ev["source_recall"]["approximate_only"])]
    if missing_source_recall:
        raise SystemExit("Source-derived direct wiring paths missing or downgraded: " +
                         ", ".join(missing_source_recall))
    contradicted_samples = [ev["name"] for ev in evals if ev["conn_sample"] and
                            ev["conn_sample"]["overall"]["contradicted"]]
    if contradicted_samples:
        raise SystemExit("Connection sample audit found contradicted rows: " +
                         ", ".join(contradicted_samples))
    stale_sdcs = [ev["name"] for ev in evals if ev["period_sdc"] and
                  (not (SCRIPT_DIR / ev["period_sdc"]).is_file() or
                   ev["metrics"].get("cdc_periods_sdc_sha256") != hashlib.sha256(
                       (SCRIPT_DIR / ev["period_sdc"]).read_bytes()).hexdigest())]
    if stale_sdcs:
        raise SystemExit("Period SDC hash changed since benchmark run: " + ", ".join(stale_sdcs))
    invalid_periods = [ev["name"] for ev in evals if ev["period_sdc"] and
                       (not ev["cdc_periods"]["classification_preserved"] or
                        not ev["cdc_periods"]["timing_basis_count"] or
                        ev["cdc_periods"]["period_references_matched"] !=
                        ev["cdc_periods"]["period_references_expected"] or
                        ev["cdc_periods"]["clock_period_references_matched"] !=
                        ev["cdc_periods"]["clock_period_references_expected"])]
    if invalid_periods:
        raise SystemExit("CDC period probe missing context or changed classification: " +
                         ", ".join(invalid_periods))
    invalid_sva = [ev["name"] for ev in evals if ev["sva_probe"] and
                   (not ev["cdc_sva"]["classification_preserved"] or
                    not ev["cdc_sva"]["all_assertions_linked"] or
                    ev["cdc_sva"]["references_matched"] != ev["cdc_sva"]["references_expected"])]
    if invalid_sva:
        raise SystemExit("CDC SVA probe missing linked assertions or changed classification: " +
                         ", ".join(invalid_sva))
    missing_connections = missing_reference_paths(evals)
    if missing_connections:
        raise SystemExit("Known connectivity paths missing: " + ", ".join(missing_connections))
    forbidden_paths = missing_forbidden_paths(evals)
    if forbidden_paths:
        raise SystemExit("Forbidden connectivity paths present or endpoints unobserved: " +
                         ", ".join(forbidden_paths))
    missing_crossings = missing_reference_crossings(evals)
    if missing_crossings:
        raise SystemExit("Known CDC reference crossings missing or mislabeled: " + ", ".join(missing_crossings))
    missing_guidance = missing_guidance_references(evals)
    if missing_guidance:
        raise SystemExit("CDC guidance references missing or unsafe: " + ", ".join(missing_guidance))
    missing_resets = missing_reset_unresolved_references(evals)
    if missing_resets:
        raise SystemExit("CDC unresolved reset references missing or overclaimed: " + ", ".join(missing_resets))
    unsafe_mux_claims = [ev["name"] for ev in evals if ev["cdc"]["reset_mux_driver_claims"]]
    if unsafe_mux_claims:
        raise SystemExit("Unresolved conditional reset mux paths claim unique FF drivers: " +
                         ", ".join(unsafe_mux_claims))


if __name__ == "__main__":
    main()
