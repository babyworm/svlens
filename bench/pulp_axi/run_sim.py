"""Optionally simulate both AXI-Lite address regions with existing Verilator."""

import argparse
import hashlib
import json
import resource
import shutil
import subprocess
from pathlib import Path

from run import AXI_REV, AXI_SOURCES, COMMON_REV, COMMON_SOURCES, FIXTURE_SHA256, check_clone


SUCCESS_MARKER = "PULP AXI 2x2 route simulation passed: 6 cases, 12 writes, 12 reads"
TESTBENCH_SHA256 = "0d1599cd13ed8500de17872ab236c4695c6648c77f802344a25c92ab7ca6a23c"


def execute(command: list[str], log_path: Path, timeout: int) -> tuple[int, str]:
    with log_path.open("w") as log:
        try:
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                       cwd=log_path.parent, text=True, timeout=timeout, check=False)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"AXI simulation command timed out; see {log_path}") from error
    return completed.returncode, log_path.read_text()


def run(axi_root: Path, common_root: Path, output: Path, verilator: Path | None = None) -> dict:
    axi_root = axi_root.resolve()
    common_root = common_root.resolve()
    output = output.resolve()
    check_clone(axi_root, AXI_REV)
    check_clone(common_root, COMMON_REV)
    binary = str(verilator.resolve()) if verilator else shutil.which("verilator")
    if not binary:
        raise FileNotFoundError("Verilator is required for this optional simulation")
    fixture_dir = Path(__file__).resolve().parent
    fixture = fixture_dir / "xbar_probe.sv"
    fixture_hash = hashlib.sha256(fixture.read_bytes()).hexdigest()
    if fixture_hash != FIXTURE_SHA256:
        raise ValueError("AXI fixture changed; re-adjudicate expected source paths")
    tb = fixture_dir / "tb_xbar_probe.sv"
    tb_hash = hashlib.sha256(tb.read_bytes()).hexdigest()
    if tb_hash != TESTBENCH_SHA256:
        raise ValueError("AXI simulation testbench changed; re-adjudicate route checks")
    sources = [*(common_root / name for name in COMMON_SOURCES),
               *(axi_root / name for name in AXI_SOURCES), fixture, tb]
    missing = [str(path) for path in sources if not path.is_file()]
    if missing:
        raise FileNotFoundError("simulation sources missing: " + ", ".join(missing))
    if output.exists() and any(output.iterdir()):
        raise FileExistsError(f"refusing to reuse a nonempty simulation directory: {output}")
    output.mkdir(parents=True, exist_ok=True)
    obj_dir = output / "obj"
    simulator = obj_dir / "Vtb_xbar_probe"
    command = [str(binary), "--binary", "--timing", "--assert", "-Wno-fatal",
               "--top-module", "tb_xbar_probe", "--Mdir", str(obj_dir),
               f"-I{axi_root / 'include'}", f"-I{common_root / 'include'}",
               *(str(path) for path in sources)]
    code, log = execute(command, output / "build.log", 300)
    if code or not simulator.is_file():
        raise RuntimeError(f"Verilator build failed: {log[-1600:]}")
    code, log = execute([str(simulator)], output / "simulation.log", 30)
    if code or SUCCESS_MARKER not in log:
        raise RuntimeError(f"AXI route simulation failed: {log[-1600:]}")
    code, log = execute([str(simulator), "+SVLENS_AXI_MUTATE_RESPONSE"],
                        output / "negative-control.log", 30)
    if not code or "Assertion failed" not in log or "wrong or duplicate R response" not in log:
        raise RuntimeError(f"AXI negative control did not detect the corrupted response: {log[-1600:]}")
    result = {"axi_revision": AXI_REV, "common_cells_revision": COMMON_REV,
              "fixture_sha256": fixture_hash, "testbench_sha256": tb_hash,
              "route_cases_passed": 6, "contended_cases_passed": 2,
              "write_transactions_passed": 12, "read_transactions_passed": 12,
              "negative_control_detected": True, "stdout": SUCCESS_MARKER}
    (output / "simulation.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--axi-root", required=True, type=Path)
    parser.add_argument("--common-cells-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verilator", type=Path)
    args = parser.parse_args()
    # The intentional assertion failure must not leave a core dump in the report.
    resource.setrlimit(resource.RLIMIT_CORE, (0, resource.getrlimit(resource.RLIMIT_CORE)[1]))
    print(json.dumps(run(args.axi_root, args.common_cells_root, args.output, args.verilator), indent=2))


if __name__ == "__main__":
    main()
