"""Run the OpenTitan benchmark on macOS or Linux."""

import hashlib
import json
import os
import signal
import subprocess
import time
from pathlib import Path

import yaml

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
SVLENS = REPO_ROOT / "build" / "svlens"
RESULTS_DIR = SCRIPT_DIR / "results"
FILELIST_DIR = SCRIPT_DIR / "filelists"


def run_mode(mode: str, filelist: Path, top: str, timeout_sec: int, out_dir: Path,
             sdc: Path | None = None, emit_sva: bool = False) -> dict:
    if emit_sva and mode != "cdc":
        raise ValueError("SVA emission is supported only for CDC runs")
    out_dir.mkdir(parents=True, exist_ok=True)
    command = [str(SVLENS), mode, "--single-unit", "-F", str(filelist), "--top", top,
               "--format", "json", "-o", str(out_dir)]
    if sdc is not None:
        if mode != "cdc":
            raise ValueError("SDC context is supported only for CDC runs")
        command.extend(["--sdc", str(sdc)])
    sva_path = out_dir / "cdc_assertions.sva" if emit_sva else None
    if sva_path is not None:
        command.extend(["--emit-sva", str(sva_path)])
    start = time.monotonic_ns()
    report_name = "connect_report.json" if mode == "conn" else "cdc_report.json"
    report_path = out_dir / report_name
    previous_mtime = report_path.stat().st_mtime_ns if report_path.exists() else None
    previous_sva_mtime = sva_path.stat().st_mtime_ns if sva_path and sva_path.exists() else None
    timed_out = False
    with (out_dir.parent / f"{out_dir.name}_stdout.log").open("w") as stdout, \
         (out_dir.parent / f"{out_dir.name}_stderr.log").open("w") as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                   start_new_session=True)
        try:
            exit_code = process.wait(timeout=timeout_sec)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            exit_code = 124

    current_mtime = report_path.stat().st_mtime_ns if report_path.exists() else None
    fresh_report = current_mtime is not None and current_mtime != previous_mtime
    current_sva_mtime = sva_path.stat().st_mtime_ns if sva_path and sva_path.exists() else None
    fresh_sva = sva_path is None or (current_sva_mtime is not None and
                                    current_sva_mtime != previous_sva_mtime)
    return {
        "exit": exit_code,
        "ms": round((time.monotonic_ns() - start) / 1_000_000),
        "status": "timeout" if timed_out else (
            "reported" if fresh_report and fresh_sva else "no_report"),
        # Peak per-process RSS is not portable through Python's subprocess API.
        "rss_kb": None,
    }


def main() -> None:
    if not SVLENS.is_file():
        raise SystemExit(f"svlens not found at {SVLENS}; run 'make build' first")

    config = yaml.safe_load((SCRIPT_DIR / "targets.yaml").read_text())
    missing_filelists = [
        target["name"] for target in config["targets"]
        if not (FILELIST_DIR / f"{target['name']}.f").is_file()
    ]
    if missing_filelists:
        raise SystemExit("Missing benchmark filelists: " + ", ".join(missing_filelists)
                         + "; run gen_filelist.py first")
    missing_sdcs = [
        target["period_sdc"] for target in config["targets"]
        if target.get("period_sdc") and not (SCRIPT_DIR / target["period_sdc"]).is_file()
    ]
    if missing_sdcs:
        raise SystemExit("Missing benchmark period SDC: " + ", ".join(missing_sdcs))
    RESULTS_DIR.mkdir(exist_ok=True)
    commit = subprocess.check_output(
        ["git", "-C", str(SCRIPT_DIR / ".ot-src"), "rev-parse", "HEAD"],
        text=True).strip()
    svlens_version = subprocess.check_output([str(SVLENS), "--version"], text=True).strip()
    svlens_binary_sha256 = hashlib.sha256(SVLENS.read_bytes()).hexdigest()
    svlens_commit = subprocess.check_output(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"], text=True).strip()
    svlens_dirty = bool(subprocess.check_output(
        ["git", "-C", str(REPO_ROOT), "status", "--porcelain"], text=True).strip())

    for target in config["targets"]:
        name = target["name"]
        filelist = FILELIST_DIR / f"{name}.f"
        target_dir = RESULTS_DIR / name
        metrics = {
            "name": name,
            "level": target["level"],
            "top": target["top_module"],
            "opentitan_tag": config["opentitan_tag"],
            "opentitan_commit": commit,
            "svlens_version": svlens_version,
            "svlens_binary_sha256": svlens_binary_sha256,
            "svlens_commit": svlens_commit,
            "svlens_dirty": svlens_dirty,
        }
        for mode in ("conn", "cdc"):
            result = run_mode(mode, filelist, target["top_module"],
                              target["timeout_sec"], target_dir / mode)
            metrics[f"{mode}_exit"] = result["exit"]
            metrics[f"{mode}_ms"] = result["ms"]
            metrics[f"{mode}_status"] = result["status"]
            metrics[f"{mode}_rss_kb"] = result["rss_kb"]
            print(f"{name}/{mode}: {result['status']}, exit={result['exit']}, "
                  f"{result['ms']}ms")
        if target.get("sva_probe"):
            result = run_mode("cdc", filelist, target["top_module"],
                              target["timeout_sec"], target_dir / "cdc_sva", emit_sva=True)
            for key in ("exit", "ms", "status", "rss_kb"):
                metrics[f"cdc_sva_{key}"] = result[key]
            print(f"{name}/cdc_sva: {result['status']}, exit={result['exit']}, "
                  f"{result['ms']}ms")
        if target.get("period_sdc"):
            sdc_path = SCRIPT_DIR / target["period_sdc"]
            result = run_mode("cdc", filelist, target["top_module"],
                              target["timeout_sec"], target_dir / "cdc_periods", sdc=sdc_path)
            for key in ("exit", "ms", "status", "rss_kb"):
                metrics[f"cdc_periods_{key}"] = result[key]
            metrics["cdc_periods_sdc"] = target["period_sdc"]
            metrics["cdc_periods_sdc_sha256"] = hashlib.sha256(sdc_path.read_bytes()).hexdigest()
            print(f"{name}/cdc_periods: {result['status']}, exit={result['exit']}, "
                  f"{result['ms']}ms")
        (target_dir / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")



if __name__ == "__main__":
    main()
