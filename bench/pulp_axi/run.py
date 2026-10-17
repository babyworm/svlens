"""Run the optional 2x2 PULP AXI-Lite probe against pinned local clones."""

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from evaluate import TOP, evaluate_report


AXI_REV = "70b8e54fd460e3308e58be596ceb3566a6e3576e"
COMMON_REV = "e73baaec2ca665cd80c3c384e9258e35242b829c"
FIXTURE_SHA256 = "c626f223ab39988be03e638e79276e604e5e7fc4654dcfd69956781531b106af"
AXI_SOURCES = (
    "src/axi_pkg.sv", "src/axi_intf.sv", "src/axi_err_slv.sv",
    "src/axi_lite_to_axi.sv", "src/axi_lite_demux.sv", "src/axi_lite_mux.sv",
    "src/axi_lite_xbar.sv",
)
COMMON_SOURCES = (
    "src/cc_pkg.sv", "src/assert_rpt_pkg.sv", "src/cc_addr_decode.sv",
    "src/cc_addr_decode_dync.sv", "src/cc_counter.sv", "src/cc_delta_counter.sv",
    "src/cc_fifo.sv", "src/cc_lzc.sv", "src/cc_rr_arb_tree.sv",
    "src/cc_spill_register.sv", "src/cc_spill_register_flushable.sv",
)


def check_clone(root: Path, expected: str) -> None:
    try:
        head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
        changed = subprocess.check_output(["git", "-C", str(root), "status", "--porcelain",
                                           "--untracked-files=no"], text=True).strip()
    except subprocess.CalledProcessError as error:
        raise ValueError(f"not a readable git clone: {root}") from error
    if head != expected or changed:
        raise ValueError(f"clone must be clean at {expected}: {root} (HEAD {head})")


def run(axi_root: Path, common_root: Path, svlens: Path, output: Path) -> dict:
    axi_root = axi_root.resolve()
    common_root = common_root.resolve()
    svlens = svlens.resolve()
    output = output.resolve()
    check_clone(axi_root, AXI_REV)
    check_clone(common_root, COMMON_REV)
    if not svlens.is_file():
        raise FileNotFoundError(f"svlens binary not found: {svlens}")
    report_path = output / "connect_report.json"
    if report_path.exists():
        raise FileExistsError(f"refusing to reuse an existing report: {report_path}")
    fixture = Path(__file__).resolve().parent / "xbar_probe.sv"
    fixture_hash = hashlib.sha256(fixture.read_bytes()).hexdigest()
    if fixture_hash != FIXTURE_SHA256:
        raise ValueError("AXI fixture changed; re-adjudicate expected source paths")
    sources = [*(common_root / name for name in COMMON_SOURCES),
               *(axi_root / name for name in AXI_SOURCES), fixture]
    missing = [str(path) for path in sources if not path.is_file()]
    if missing:
        raise FileNotFoundError("probe sources missing: " + ", ".join(missing))

    output.mkdir(parents=True, exist_ok=True)
    command = [str(svlens), "conn", f"-I{axi_root / 'include'}", f"-I{common_root / 'include'}",
               *(str(path) for path in sources), "--top", TOP, "--format", "json", "-o", str(output)]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
    if completed.returncode < 0 or not report_path.is_file():
        raise RuntimeError(f"AXI probe did not produce a report (exit {completed.returncode}): "
                           f"{completed.stderr[-1000:]}")
    result = evaluate_report(json.loads(report_path.read_text()))
    result.update({"axi_revision": AXI_REV, "common_cells_revision": COMMON_REV,
                   "fixture_sha256": fixture_hash,
                   "svlens_exit_code": completed.returncode})
    (output / "evaluation.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--axi-root", required=True, type=Path)
    parser.add_argument("--common-cells-root", required=True, type=Path)
    parser.add_argument("--svlens", type=Path, default=Path(__file__).resolve().parents[2] / "build" / "svlens")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(run(args.axi_root, args.common_cells_root, args.svlens, args.output), indent=2))


if __name__ == "__main__":
    main()
