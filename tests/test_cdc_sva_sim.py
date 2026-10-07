"""Optional Verilator execution checks for generated CDC assertions."""

import json
import resource
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SVLENS = ROOT / "build" / "svlens"
FIXTURES = ROOT / "tests" / "cdc" / "basic"
CASES = (
    ("two_ff_sync", "03_two_ff_sync.sv", "two_ff_sva_tb.sv", "two_ff_sva_tb",
     "SVLENS_SVA_MUTATE", "_2ff"),
    ("fifo_transfer_sva_top", "fifo_transfer_sva.sv", "fifo_transfer_sva_tb.sv",
     "fifo_transfer_sva_tb", "SVLENS_FIFO_SVA_MUTATE", "_fifo_no_step"),
    ("reqack_data_forward_sva_top", "reqack_data_sva.sv", "reqack_data_sva_tb.sv",
     "reqack_data_sva_tb", "SVLENS_REQACK_SVA_MUTATE", "_data_hold_src2dst"),
)


def require_success(command: list[str], timeout: int) -> None:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            timeout=timeout, check=False)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command[0]}\n"
                           + (result.stdout + result.stderr)[-2000:])


def main() -> None:
    verilator = shutil.which("verilator")
    if not verilator:
        raise SystemExit("Verilator is required for this optional SVA simulation test")
    if not SVLENS.is_file():
        raise SystemExit("Build svlens before running the SVA simulation test")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    with tempfile.TemporaryDirectory(prefix="svlens-sva-sim-") as directory:
        for top, source, testbench, bench_top, mutation, suffix in CASES:
            case_dir = Path(directory) / top
            case_dir.mkdir()
            sva_path = case_dir / "cdc_assertions.sva"
            source_path = FIXTURES / source
            require_success([str(SVLENS), "cdc", str(source_path), "--top", top,
                             "--format", "json", "-o", str(case_dir),
                             "--emit-sva", str(sva_path)], 30)
            report = json.loads((case_dir / "cdc_report.json").read_text())
            ids = [label for crossing in report["crossings"]
                   for label in crossing.get("sva_assertion_ids",
                                             [crossing["sva_assertion_id"]]
                                             if "sva_assertion_id" in crossing else [])]
            if not any(label.endswith(suffix) for label in ids):
                raise RuntimeError(f"{top} did not link a {suffix} assertion")

            for mutated in (False, True):
                obj_dir = case_dir / ("mutated" if mutated else "normal")
                command = [verilator, "--binary", "--timing", "--assert", "-Wno-fatal",
                           "--top-module", bench_top, "--Mdir", str(obj_dir)]
                if mutated:
                    command.append(f"-D{mutation}")
                command.extend([str(source_path), str(sva_path), str(FIXTURES / testbench)])
                require_success(command, 120)
                simulation = subprocess.run([str(obj_dir / f"V{bench_top}")], cwd=ROOT,
                                            capture_output=True, text=True, timeout=30, check=False)
                output = simulation.stdout + simulation.stderr
                if mutated:
                    if simulation.returncode == 0 or "Assertion failed" not in output or suffix not in output:
                        raise RuntimeError(f"{top} mutation did not trigger {suffix}:\n{output[-2000:]}")
                elif simulation.returncode or "Verilog $finish" not in output:
                    raise RuntimeError(f"{top} valid simulation failed:\n{output[-2000:]}")
            print(f"{top}: valid run passed; {suffix} mutation detected")


if __name__ == "__main__":
    main()
