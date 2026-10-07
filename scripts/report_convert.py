"""Convert svlens JSON reports to SARIF or a PR-ready Markdown summary."""

import argparse
import json
from pathlib import Path

CONN_LEVEL = {"ERROR": "error", "WARN": "warning", "INFO": "note"}
CDC_LEVEL = {"VIOLATION": "error", "CAUTION": "warning",
             "CONVENTION": "note", "INFO": "note"}


def _physical_location(issue: dict, repo_root: Path) -> dict | None:
    filename, line = issue.get("file"), issue.get("line")
    if not filename or not isinstance(line, int) or line < 1:
        return None
    path = Path(filename)
    if path.is_absolute():
        try:
            path = path.resolve().relative_to(repo_root.resolve())
        except ValueError:
            return None
    if ".." in path.parts:
        return None
    region = {"startLine": line}
    if isinstance(issue.get("column"), int) and issue["column"] > 0:
        region["startColumn"] = issue["column"]
    return {"artifactLocation": {"uri": path.as_posix()}, "region": region}


def _conn_findings(report: dict, repo_root: Path) -> list[dict]:
    findings = []
    for issue in report.get("issues", []):
        rule = "CONN/" + (issue.get("rule_id") or issue.get("type", "UNKNOWN"))
        location = {"logicalLocations": [
            {"fullyQualifiedName": issue.get("port", "<unknown port>")}]}
        physical = _physical_location(issue, repo_root)
        if physical:
            location["physicalLocation"] = physical
        findings.append({
            "ruleId": rule,
            "level": CONN_LEVEL.get(issue.get("severity", ""), "warning"),
            "message": {"text": issue.get("detail", rule)},
            "locations": [location],
        })
    return findings


def _cdc_findings(report: dict, repo_root: Path) -> list[dict]:
    findings = []
    for crossing in report.get("crossings", []):
        category = crossing.get("category", "")
        if category not in CDC_LEVEL:
            continue
        rule = "CDC/" + (crossing.get("rule") or "UNKNOWN")
        source = crossing.get("source", "<unknown source>")
        dest = crossing.get("dest", "<unknown destination>")
        detail = crossing.get("recommendation") or crossing.get("rationale") or rule
        endpoint = "dest" if crossing.get("dest_file") and crossing.get("dest_line") else "source"
        location = {"logicalLocations": [{"fullyQualifiedName":
                    dest if endpoint == "dest" else source}]}
        physical = _physical_location({
            "file": crossing.get(f"{endpoint}_file"),
            "line": crossing.get(f"{endpoint}_line"),
            "column": crossing.get(f"{endpoint}_column"),
        }, repo_root)
        if physical:
            location["physicalLocation"] = physical
        findings.append({
            "ruleId": rule,
            "level": CDC_LEVEL[category],
            "message": {"text": f"{source} -> {dest}: {detail}"},
            "locations": [location],
        })
    return findings


def to_sarif(conn: dict | None, cdc: dict | None, repo_root: Path) -> dict:
    runs = []
    for mode, findings in (("conn", _conn_findings(conn, repo_root) if conn else None),
                           ("cdc", _cdc_findings(cdc, repo_root) if cdc else None)):
        if findings is None:
            continue
        rule_ids = sorted({finding["ruleId"] for finding in findings})
        runs.append({
            "tool": {"driver": {"name": "svlens " + mode,
                                "rules": [{"id": rule} for rule in rule_ids]}},
            "results": findings,
        })
    return {"$schema": "https://json.schemastore.org/sarif-2.1.0.json",
            "version": "2.1.0", "runs": runs}


def to_pr_comment(conn: dict | None, cdc: dict | None, max_items: int = 50) -> str:
    rows = []
    if conn:
        for issue in conn.get("issues", []):
            position = issue.get("port", "<unknown port>")
            if issue.get("file") and issue.get("line"):
                position = f"{issue['file']}:{issue['line']} ({position})"
            rows.append(("conn", issue.get("severity", "WARN"),
                         issue.get("rule_id") or issue.get("type", "UNKNOWN"), position,
                         issue.get("detail", "")))
    if cdc:
        for crossing in cdc.get("crossings", []):
            if crossing.get("category") == "WAIVED":
                continue
            position = crossing.get("dest", "<unknown destination>")
            if crossing.get("dest_file") and crossing.get("dest_line"):
                position = f"{crossing['dest_file']}:{crossing['dest_line']} ({position})"
            rows.append(("cdc", crossing.get("category", "INFO"),
                         crossing.get("rule", "UNKNOWN"),
                         position,
                         crossing.get("recommendation", "")))

    def cell(value: str) -> str:
        return str(value).replace("|", "\\|").replace("\n", " ")

    lines = ["## svlens report", "",
             f"{len(rows)} findings; showing the first {min(len(rows), max_items)}.", "",
             "| Mode | Severity | Rule | Signal / location | Detail |",
             "|---|---|---|---|---|"]
    for row in rows[:max_items]:
        lines.append("| " + " | ".join(cell(value) for value in row) + " |")
    lines += ["", "Inline PR comments require a source file and line;",
              "findings without them are included in this summary only.", ""]
    return "\n".join(lines)


def to_pr_comments(conn: dict | None, cdc: dict | None,
                   repo_root: Path, max_items: int = 50) -> list[dict]:
    """Return GitHub review-comment candidates; callers must check the PR diff."""
    comments = []
    if conn:
        for issue in conn.get("issues", []):
            physical = _physical_location(issue, repo_root)
            if not physical:
                continue
            comments.append({
                "path": physical["artifactLocation"]["uri"],
                "line": physical["region"]["startLine"],
                "side": "RIGHT",
                "body": f"svlens {issue.get('rule_id') or issue.get('type', 'CONN')} "
                        f"({issue.get('severity', 'WARN')}): {issue.get('detail', '')}",
            })
    if cdc:
        for crossing in cdc.get("crossings", []):
            if crossing.get("category") == "WAIVED":
                continue
            endpoint = "dest" if crossing.get("dest_file") and crossing.get("dest_line") else "source"
            physical = _physical_location({
                "file": crossing.get(f"{endpoint}_file"),
                "line": crossing.get(f"{endpoint}_line"),
            }, repo_root)
            if not physical:
                continue
            comments.append({
                "path": physical["artifactLocation"]["uri"],
                "line": physical["region"]["startLine"],
                "side": "RIGHT",
                "body": f"svlens {crossing.get('rule') or 'CDC'} "
                        f"({crossing.get('category', 'INFO')}): "
                        f"{crossing.get('source', '?')} → {crossing.get('dest', '?')}. "
                        f"{crossing.get('recommendation', '')}",
            })
    return comments[:max_items]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--conn", type=Path, help="connect_report.json")
    parser.add_argument("--cdc", type=Path, help="cdc_report.json")
    parser.add_argument("--format", choices=("sarif", "pr-comment", "pr-comments-json"), default="sarif")
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--max-items", type=int, default=50)
    parser.add_argument("--output", type=Path, help="write to file instead of stdout")
    args = parser.parse_args()
    if not args.conn and not args.cdc:
        parser.error("at least one of --conn or --cdc is required")
    if args.max_items < 0:
        parser.error("--max-items must be non-negative")

    conn = json.loads(args.conn.read_text()) if args.conn else None
    cdc = json.loads(args.cdc.read_text()) if args.cdc else None
    if args.format == "sarif":
        output = json.dumps(to_sarif(conn, cdc, args.repo_root), indent=2) + "\n"
    elif args.format == "pr-comments-json":
        output = json.dumps(to_pr_comments(conn, cdc, args.repo_root,
                                           args.max_items), indent=2) + "\n"
    else:
        output = to_pr_comment(conn, cdc, args.max_items)
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
