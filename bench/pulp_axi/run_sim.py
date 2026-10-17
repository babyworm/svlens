"""Optionally simulate both AXI-Lite address regions with existing Verilator."""

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from run import AXI_REV, AXI_SOURCES, COMMON_REV, COMMON_SOURCES, FIXTURE_SHA256, check_clone


SUCCESS_MARKER = "PULP AXI 2x2 route simulation passed"
TESTBENCH_SHA256 = "5a7b694f58dbd264765e34bb4d5834a5f7bb8375b900882bfef00e4d4d379bbd"


def run(axi_root: Path, common_root: Path, output: Path, verilator: Path | None = None) -> dict:
    axi_root = axi_root.resolve()
    common_root = common_root.resolve()
    output = output.resolve()
    check_clone(axi_root, AXI_REV)
    check_clone(common_root, COMMON_REV)
    binary = verilator or shutil.which("verilator")
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
    output.mkdir(parents=True, exist_ok=True)
    obj_dir = output / "obj"
    simulator = obj_dir / "Vtb_xbar_probe"
    if simulator.exists():
        raise FileExistsError(f"refusing to reuse an existing simulator: {simulator}")
    command = [str(binary), "--binary", "--timing", "--assert", "-Wno-fatal",
               "--top-module", "tb_xbar_probe", "--Mdir", str(obj_dir),
               f"-I{axi_root / 'include'}", f"-I{common_root / 'include'}",
               *(str(path) for path in sources)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=300, check=False)
    if built.returncode or not simulator.is_file():
        raise RuntimeError(f"Verilator build failed: {built.stderr[-1600:]}")
    executed = subprocess.run([str(simulator)], capture_output=True, text=True, timeout=30, check=False)
    if executed.returncode or SUCCESS_MARKER not in executed.stdout:
        raise RuntimeError(f"AXI route simulation failed: {executed.stdout[-1000:]} "
                           f"{executed.stderr[-1000:]}")
    result = {"axi_revision": AXI_REV, "common_cells_revision": COMMON_REV,
              "fixture_sha256": fixture_hash, "testbench_sha256": tb_hash,
              "two_route_cases_passed": 2, "stdout": SUCCESS_MARKER}
    (output / "simulation.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--axi-root", required=True, type=Path)
    parser.add_argument("--common-cells-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verilator", type=Path)
    args = parser.parse_args()
    print(json.dumps(run(args.axi_root, args.common_cells_root, args.output, args.verilator), indent=2))


if __name__ == "__main__":
    main()
