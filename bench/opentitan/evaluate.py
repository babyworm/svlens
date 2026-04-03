#!/usr/bin/env python3
"""Evaluate svlens benchmark results against golden expectations."""

import json
import os
import sys
from datetime import datetime
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML required. Install: pip install pyyaml", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).parent
RESULTS_DIR = SCRIPT_DIR / "results"
GOLDEN_DIR = SCRIPT_DIR / "golden"
CONFIG_FILE = SCRIPT_DIR / "targets.yaml"


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
    result = {
        "total_violations": 0, "total_cautions": 0,
        "total_info": 0, "total_waived": 0,
        "known_expected": 0, "known_found": 0,
        "recall": None, "precision": None,
    }
    if not cdc_report:
        return result

    crossings = cdc_report.get("crossings", [])
    for c in crossings:
        cat = c.get("category", "")
        if cat == "Violation": result["total_violations"] += 1
        elif cat == "Caution": result["total_cautions"] += 1
        elif cat == "Info": result["total_info"] += 1
        elif cat == "Waived": result["total_waived"] += 1

    known = golden.get("known_crossings", [])
    result["known_expected"] = len(known)
    if not known:
        return result

    found_pairs = set()
    for c in crossings:
        src = c.get("source_domain", "")
        dst = c.get("dest_domain", "")
        if src and dst:
            found_pairs.add((src, dst))

    matched = 0
    for kc in known:
        fd = kc.get("from_domain", "")
        td = kc.get("to_domain", "")
        for (src, dst) in found_pairs:
            if fd in src and td in dst:
                matched += 1
                break

    result["known_found"] = matched
    if known:
        result["recall"] = round(matched / len(known), 3)
    actionable = result["total_violations"] + result["total_cautions"]
    if actionable > 0 and matched > 0:
        result["precision"] = round(matched / actionable, 3)
    return result


def evaluate_conn(conn_report: dict) -> dict:
    result = {"total_connections": 0, "total_ports": 0, "issues": {}}
    if not conn_report:
        return result
    result["total_connections"] = conn_report.get("total_connections", 0)
    result["total_ports"] = len(conn_report.get("ports", []))
    for issue in conn_report.get("issues", []):
        t = issue.get("type", "unknown")
        result["issues"][t] = result["issues"].get(t, 0) + 1
    return result


def generate_report(evals: list) -> str:
    lines = [
        "# svlens OpenTitan Benchmark Report", "",
        f"> Generated: {datetime.now().strftime('%Y-%m-%d %H:%M')}", "",
        "## Performance", "",
        "| Target | Level | Conn Exit | CDC Exit | Conn Time | CDC Time | Conn RSS | CDC RSS |",
        "|--------|-------|-----------|----------|-----------|----------|----------|---------|",
    ]
    for ev in evals:
        m = ev["metrics"]
        def fmt_rss(v):
            if isinstance(v, (int, float)) and v > 0: return f"{int(v)//1024}MB"
            return "N/A"
        lines.append(
            f"| {m.get('name','?')} | {m.get('level','?')} | "
            f"{m.get('conn_exit','?')} | {m.get('cdc_exit','?')} | "
            f"{m.get('conn_ms','N/A')}ms | {m.get('cdc_ms','N/A')}ms | "
            f"{fmt_rss(m.get('conn_rss_kb',0))} | {fmt_rss(m.get('cdc_rss_kb',0))} |"
        )
    lines += ["", "## CDC Analysis", "",
        "| Target | Violations | Cautions | Info | Recall | Precision |",
        "|--------|-----------|----------|------|--------|-----------|"]
    for ev in evals:
        c = ev["cdc"]
        rec = f"{c['recall']:.1%}" if c["recall"] is not None else "N/A"
        pre = f"{c['precision']:.1%}" if c["precision"] is not None else "N/A"
        lines.append(f"| {ev['name']} | {c['total_violations']} | {c['total_cautions']} | "
                     f"{c['total_info']} | {rec} | {pre} |")
    lines += ["", "## Connectivity Analysis", "",
        "| Target | Connections | Ports | Issues |",
        "|--------|------------|-------|--------|"]
    for ev in evals:
        co = ev["conn"]
        iss = ", ".join(f"{k}:{v}" for k,v in co["issues"].items()) or "none"
        lines.append(f"| {ev['name']} | {co['total_connections']} | {co['total_ports']} | {iss} |")
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
        evals.append({
            "name": name,
            "metrics": load_metrics(name),
            "cdc": evaluate_cdc(
                load_json_report(name, "cdc", "cdc_report.json"),
                load_golden(name)),
            "conn": evaluate_conn(
                load_json_report(name, "conn", "connect_report.json")),
        })
    report = generate_report(evals)
    out = RESULTS_DIR / "bench_report.md"
    out.parent.mkdir(exist_ok=True)
    with open(out, "w") as f:
        f.write(report)
    print(f"\nReport: {out}")
    print(report)


if __name__ == "__main__":
    main()
