"""Bounded, pinned AXI-Lite crossbar connectivity checks for the 2x2 probe."""

import argparse
import hashlib
import json
from pathlib import Path


TOP = "svlens_axi_xbar_probe"
EXPECTED_SHA256 = "f89dc8640861f95e1f437724635e5a2777f231695a09eb09ca0b2660737a9730"
SIGNALS = (
    ("aw_addr", 8, True), ("aw_prot", 3, True), ("aw_valid", 1, True),
    ("aw_ready", 1, False), ("w_data", 32, True), ("w_strb", 4, True),
    ("w_valid", 1, True), ("w_ready", 1, False), ("b_resp", 2, False),
    ("b_valid", 1, False), ("b_ready", 1, True), ("ar_addr", 8, True),
    ("ar_prot", 3, True), ("ar_valid", 1, True), ("ar_ready", 1, False),
    ("r_data", 32, False), ("r_resp", 2, False), ("r_valid", 1, False),
    ("r_ready", 1, True),
)


def port(path: str, signal: str, width: int) -> str:
    return f"{path}.{signal}" + (f"[{width - 1}:0]" if width > 1 else "")


def row(source: str, dest: str, kind: str, source_low: int, source_high: int,
        dest_low: int, dest_high: int) -> dict:
    return {"source": source, "dest": dest, "kind": kind,
            "source_bits": {"low": source_low, "high": source_high},
            "dest_bits": {"low": dest_low, "high": dest_high}}


def expected_rows() -> dict[str, list[dict]]:
    """Build independent boundary, decoder, and response may-flow frames."""
    boundary = []
    decoder = []
    response = []
    for lane in range(2):
        manager = f"{TOP}.u_m{lane}.bus"
        subordinate = f"{TOP}.u_s{lane}.bus"
        slave_side = f"{TOP}.u_xbar.slv_ports[{lane}]"
        master_side = f"{TOP}.u_xbar.mst_ports[{lane}]"
        for signal, width, forward in SIGNALS:
            manager_port = port(manager, signal, width)
            slave_port = port(slave_side, signal, width)
            master_port = port(master_side, signal, width)
            subordinate_port = port(subordinate, signal, width)
            source, dest = (manager_port, slave_port) if forward else (slave_port, manager_port)
            boundary.append(row(source, dest, "direct", 0, width - 1, 0, width - 1))
            source, dest = (master_port, subordinate_port) if forward else (subordinate_port, master_port)
            boundary.append(row(source, dest, "direct", 0, width - 1, 0, width - 1))
        for channel in ("aw", "ar"):
            source = port(manager, f"{channel}_addr", 8)
            dest = f"{TOP}.u_xbar.i_xbar.gen_slv_port_demux[{lane}].i_axi_{channel}_decode.addr_i[7:0]"
            decoder.append(row(source, dest, "direct", 0, 7, 0, 7))

    # axi_lite_xbar.sv crosses every master response into each slave's
    # response array. The demux's runtime response selector makes these
    # candidate paths approximate, not a claim about a chosen transaction.
    for master in range(2):
        source = (f"{TOP}.u_xbar.i_xbar.gen_mst_port_mux[{master}]."
                  "i_axi_lite_mux.slv_resps_o[81:0]")
        for slave in range(2):
            dest = (f"{TOP}.u_xbar.i_xbar.gen_slv_port_demux[{slave}]."
                    "i_axi_lite_demux.gen_demux.i_r_spill_reg.data_i[33:0]")
            low = 41 * slave + 1
            response.append(row(source, dest, "approximate", low, low + 33, 0, 33))
    return {"boundary": boundary, "decoder": decoder, "response": response}


def evaluate_report(report: dict) -> dict:
    rows = report.get("connections")
    summary = report.get("summary") or {}
    if report.get("top") != TOP or not isinstance(rows, list) or not all(
            isinstance(item, dict) and isinstance(item.get("source"), str) and
            isinstance(item.get("dest"), str) for item in rows):
        raise ValueError("probe report has the wrong top or missing connection rows")
    if summary.get("errors") != 0 or summary.get("connections_analyzed") != len(rows):
        raise ValueError("probe report is incomplete or has analysis errors")

    frames = expected_rows()
    by_pair = {}
    for actual in rows:
        by_pair.setdefault((actual.get("source"), actual.get("dest")), []).append(actual)
    for name, expected in frames.items():
        for want in expected:
            pair = (want["source"], want["dest"])
            matches = by_pair.get(pair, [])
            if len(matches) != 1 or any(matches[0].get(key) != value for key, value in want.items()):
                raise ValueError(f"{name} path missing, duplicated, or changed: {pair}")

    boundary_pairs = {(item["source"], item["dest"]) for item in frames["boundary"]}
    for actual in rows:
        source = actual.get("source", "")
        dest = actual.get("dest", "")
        manager_to_slave = (source.startswith(f"{TOP}.u_m") and dest.startswith(f"{TOP}.u_xbar.slv_ports[")) or (
            source.startswith(f"{TOP}.u_xbar.slv_ports[") and dest.startswith(f"{TOP}.u_m"))
        master_to_subordinate = (source.startswith(f"{TOP}.u_xbar.mst_ports[") and
                                 dest.startswith(f"{TOP}.u_s")) or (
            source.startswith(f"{TOP}.u_s") and dest.startswith(f"{TOP}.u_xbar.mst_ports["))
        if (manager_to_slave or master_to_subordinate) and (source, dest) not in boundary_pairs:
            raise ValueError(f"unexpected boundary path or wrong-lane link: {(source, dest)}")

    for lane in range(2):
        for channel in ("aw", "ar"):
            source = port(f"{TOP}.u_m{lane}.bus", f"{channel}_addr", 8)
            other = 1 - lane
            wrong_dest = (f"{TOP}.u_xbar.i_xbar.gen_slv_port_demux[{other}]."
                          f"i_axi_{channel}_decode.addr_i[7:0]")
            if (source, wrong_dest) in by_pair:
                raise ValueError(f"wrong-lane address decoder path: {(source, wrong_dest)}")

    expected = [item for frame in frames.values() for item in frame]
    fingerprint = hashlib.sha256("\n".join(json.dumps(item, sort_keys=True, separators=(",", ":"))
                                             for item in expected).encode()).hexdigest()
    if fingerprint != EXPECTED_SHA256:
        raise ValueError("AXI expected-pair frame changed; re-adjudicate upstream RTL paths")
    return {"top": TOP, "connection_rows": len(rows), "bit_flow_gaps": summary.get("bit_flow_gap_count"),
            "boundary_found": len(frames["boundary"]), "decoder_found": len(frames["decoder"]),
            "response_found": len(frames["response"]), "expected_sha256": fingerprint}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(json.dumps(evaluate_report(json.loads(args.report.read_text())), indent=2))


if __name__ == "__main__":
    main()
