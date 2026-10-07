"""Select and check a reproducible, stratified audit of connection rows."""

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path

import yaml

STRATA = (("direct", False), ("direct", True),
          ("approximate", False), ("approximate", True))


def select_sample(report: dict, seed: str, per_stratum: int) -> dict:
    if not isinstance(seed, str) or not seed:
        raise ValueError("seed must be a nonempty string")
    if not isinstance(per_stratum, int) or isinstance(per_stratum, bool) or per_stratum < 1:
        raise ValueError("per_stratum must be positive")
    rows = report.get("connections")
    if not isinstance(rows, list):
        raise TypeError("connection report has no rows")
    if not all(isinstance(row, dict) for row in rows):
        raise TypeError("connection report rows must be objects")

    canonical = [json.dumps(row, sort_keys=True, separators=(",", ":")) for row in rows]
    population_sha256 = hashlib.sha256("\n".join(sorted(canonical)).encode()).hexdigest()
    duplicates = Counter(canonical)
    occurrences = Counter()
    groups = {stratum: [] for stratum in STRATA}
    for serialized in sorted(canonical):
        row = json.loads(serialized)
        kind = row.get("kind")
        has_source_bits = "source_bits" in row
        has_dest_bits = "dest_bits" in row
        if kind not in ("direct", "approximate") or has_source_bits != has_dest_bits:
            raise ValueError("connection row has unsupported kind or partial bit range")
        occurrence = occurrences[serialized]
        occurrences[serialized] += 1
        payload = seed + ":" + serialized
        if duplicates[serialized] > 1:
            payload += f":{occurrence}"
        digest = hashlib.sha256(payload.encode()).hexdigest()
        groups[(kind, has_source_bits)].append((digest, row))

    selected = []
    population = {}
    for kind, ranged in STRATA:
        name = f"{kind}/{'ranged' if ranged else 'unranged'}"
        group = groups[(kind, ranged)]
        population[name] = len(group)
        if len(group) < per_stratum:
            raise ValueError(f"{name} has fewer than {per_stratum} rows")
        for digest, row in sorted(group, key=lambda item: item[0])[:per_stratum]:
            selected.append({"id": digest[:16], "stratum": name, **row})
    if len({row["id"] for row in selected}) != len(selected):
        raise ValueError("sample IDs collided")
    return {"seed": seed, "per_stratum": per_stratum,
            "population_sha256": population_sha256,
            "population": population, "samples": selected}


def evaluate_annotations(sample: dict, annotations: dict) -> dict:
    if annotations.get("seed") != sample["seed"] or \
            annotations.get("population_sha256") != sample["population_sha256"]:
        raise ValueError("annotations are stale for this sample population")
    labels = annotations.get("labels")
    if not isinstance(labels, list) or len(labels) != len(sample["samples"]):
        raise ValueError("annotations must label every sampled row")
    by_id = {label.get("id"): label for label in labels if isinstance(label, dict)}
    if len(by_id) != len(labels) or set(by_id) != {row["id"] for row in sample["samples"]}:
        raise ValueError("annotation IDs do not match the selected sample")

    totals = {name: {"confirmed": 0, "contradicted": 0, "unresolved": 0}
              for name in sample["population"]}
    for row in sample["samples"]:
        label = by_id[row["id"]]
        verdict = label.get("verdict")
        if verdict not in ("confirmed", "contradicted", "unresolved"):
            raise ValueError(f"invalid verdict for {row['id']}")
        if verdict == "unresolved":
            if not label.get("note"):
                raise ValueError(f"unresolved row {row['id']} needs a note")
        elif not label.get("evidence"):
            raise ValueError(f"adjudicated row {row['id']} needs source evidence")
        totals[row["stratum"]][verdict] += 1
    overall = {verdict: sum(counts[verdict] for counts in totals.values())
               for verdict in ("confirmed", "contradicted", "unresolved")}
    return {"population_sha256": sample["population_sha256"],
            "population": sample["population"],
            "sample_size": len(sample["samples"]), "by_stratum": totals,
            "overall": overall}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--seed", default="svlens-accuracy-v1")
    parser.add_argument("--per-stratum", type=int, default=5)
    parser.add_argument("--annotations", type=Path)
    args = parser.parse_args()
    sample = select_sample(json.loads(args.report.read_text()), args.seed, args.per_stratum)
    if args.annotations:
        annotations = yaml.safe_load(args.annotations.read_text()) or {}
        print(json.dumps(evaluate_annotations(sample, annotations), indent=2))
    else:
        print(json.dumps(sample, indent=2))


if __name__ == "__main__":
    main()
