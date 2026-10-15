"""Bounded RTL-source oracle for simple integral sibling-port connections."""

import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from collections import Counter, defaultdict
from pathlib import Path


def simple_logic_width(type_name: str | None) -> int | None:
    if type_name == "logic":
        return 1
    match = re.fullmatch(r"logic\[(\d+):(\d+)\]", type_name or "")
    if not match:
        return None
    width = abs(int(match[1]) - int(match[2])) + 1
    return width if width <= 4096 else None


def expected_simple_edges(scope: dict, path: str) -> list[tuple[str, str]]:
    """Find unique-output scalar or whole-vector pairs on nets or one direct alias."""
    expected = set()

    def named_values(value):
        if isinstance(value, dict):
            if value.get("kind") == "NamedValue" and isinstance(value.get("symbol"), str):
                yield value["symbol"]
            for child in value.values():
                yield from named_values(child)
        elif isinstance(value, list):
            for child in value:
                yield from named_values(child)

    def assignments(value):
        if isinstance(value, dict):
            if value.get("kind") == "Assignment":
                yield value
            for child in value.values():
                yield from assignments(child)
        elif isinstance(value, list):
            for child in value:
                yield from assignments(child)

    def port_path(scope_path: str, instance: str, port: str, width: int) -> str:
        return f"{scope_path}.{instance}.{port}" + (f"[{width - 1}:0]" if width > 1 else "")

    def visit(node: dict, scope_path: str) -> None:
        body = node.get("body")
        members = body.get("members", []) if isinstance(body, dict) else node.get("members", [])
        if not isinstance(members, list):
            return
        by_symbol = defaultdict(lambda: {"Out": [], "In": []})
        output_writers = defaultdict(int)
        for child in members:
            if not isinstance(child, dict) or child.get("kind") != "Instance":
                continue
            for connection in child.get("connections", []):
                port = connection.get("port") or {}
                expr = connection.get("expr") or {}
                direction = port.get("direction")
                if direction == "Out":
                    output_expr = (expr.get("left") or {}) if expr.get("kind") == "Assignment" else expr
                    for symbol in named_values(output_expr):
                        output_writers[symbol] += 1
                width = simple_logic_width(port.get("type"))
                if width is None or direction not in ("In", "Out"):
                    continue
                if direction == "Out" and expr.get("kind") == "Assignment":
                    expr = expr.get("left") or {}
                if expr.get("kind") != "NamedValue" or simple_logic_width(expr.get("type")) != width:
                    continue
                by_symbol[expr["symbol"]][direction].append((child["name"], port["name"], width))
        for ports in by_symbol.values():
            if len(ports["Out"]) != 1:
                continue
            source_instance, source_port, source_width = ports["Out"][0]
            for dest_instance, dest_port, dest_width in ports["In"]:
                if source_instance != dest_instance and source_width == dest_width:
                    expected.add((port_path(scope_path, source_instance, source_port, source_width),
                                  port_path(scope_path, dest_instance, dest_port, dest_width)))

        writes = defaultdict(int)
        aliases = []
        for member in members:
            if not isinstance(member, dict) or member.get("kind") in ("Instance", "GenerateBlock",
                                                                        "GenerateBlockArray"):
                continue
            for assignment in assignments(member):
                for symbol in named_values(assignment.get("left")):
                    writes[symbol] += 1
            if member.get("kind") != "ContinuousAssign":
                continue
            assignment = member.get("assignment") or {}
            lhs = assignment.get("left") or {}
            rhs = assignment.get("right") or {}
            width = simple_logic_width(lhs.get("type"))
            if (assignment.get("kind") == "Assignment" and lhs.get("kind") == rhs.get("kind") == "NamedValue" and
                    isinstance(lhs.get("symbol"), str) and isinstance(rhs.get("symbol"), str) and width is not None and
                    simple_logic_width(rhs.get("type")) == width and lhs.get("symbol") != rhs.get("symbol")):
                aliases.append((rhs.get("symbol"), lhs.get("symbol"), width))
        for source_symbol, dest_symbol, width in aliases:
            if (writes[dest_symbol] != 1 or writes[source_symbol] or output_writers[source_symbol] != 1 or
                    output_writers[dest_symbol]):
                continue
            sources = by_symbol[source_symbol]["Out"]
            if len(sources) != 1:
                continue
            source_instance, source_port, source_width = sources[0]
            if source_width != width:
                continue
            for dest_instance, dest_port, dest_width in by_symbol[dest_symbol]["In"]:
                if source_instance != dest_instance and dest_width == width:
                    expected.add((port_path(scope_path, source_instance, source_port, width),
                                  port_path(scope_path, dest_instance, dest_port, width)))

        for child in members:
            if not isinstance(child, dict):
                continue
            kind = child.get("kind")
            if kind == "Instance" and child.get("name"):
                visit(child, f"{scope_path}.{child['name']}")
            elif kind == "GenerateBlock":
                suffix = f".{child['name']}" if child.get("name") else ""
                visit(child, scope_path + suffix)
            elif kind == "GenerateBlockArray" and child.get("name"):
                for block in child.get("members", []):
                    index = block.get("constructIndex") if isinstance(block, dict) else None
                    if isinstance(index, int) and block.get("kind") == "GenerateBlock":
                        visit(block, f"{scope_path}.{child['name']}[{index}]")

    visit(scope, path)
    return sorted(expected)


def expected_passthrough_edges(scope: dict, path: str, max_hops: int = 4) -> list[tuple[str, str]]:
    """Bounded sibling-port paths through sole, direct module input-to-output copies."""
    if max_hops < 1:
        raise ValueError("max_hops must be positive")
    expected = set()
    bodies = {}

    def index_bodies(value):
        if isinstance(value, dict):
            if value.get("kind") == "InstanceBody" and isinstance(value.get("addr"), int):
                bodies[str(value["addr"])] = value
            for child in value.values():
                index_bodies(child)
        elif isinstance(value, list):
            for child in value:
                index_bodies(child)

    index_bodies(scope)

    def members_of(node):
        body = node.get("body")
        if isinstance(body, str):
            body = bodies.get(body.split()[0])
        members = body.get("members", []) if isinstance(body, dict) else node.get("members", [])
        return members if isinstance(members, list) else []

    def assignments(value):
        if isinstance(value, dict):
            if value.get("kind") == "Assignment":
                yield value
            for child in value.values():
                yield from assignments(child)
        elif isinstance(value, list):
            for child in value:
                yield from assignments(child)

    def named_values(value):
        if isinstance(value, dict):
            if value.get("kind") == "NamedValue" and isinstance(value.get("symbol"), str):
                yield value["symbol"]
            for child in value.values():
                yield from named_values(child)
        elif isinstance(value, list):
            for child in value:
                yield from named_values(child)

    def binding(connection, width):
        port = connection.get("port") or {}
        expr = connection.get("expr") or {}
        if port.get("direction") == "Out" and expr.get("kind") == "Assignment":
            expr = expr.get("left") or {}
        if (simple_logic_width(port.get("type")) == width and expr.get("kind") == "NamedValue" and
                simple_logic_width(expr.get("type")) == width):
            return expr.get("symbol")
        return None

    def direct_copies(child):
        members = members_of(child)
        ports = {port.get("internalSymbol"): port for port in members if port.get("kind") == "Port"}
        writes = Counter(symbol for assignment in assignments(members)
                         for symbol in named_values(assignment.get("left")))
        connections = {(connection.get("port") or {}).get("name"): connection
                       for connection in child.get("connections", [])}
        for member in members:
            if member.get("kind") != "ContinuousAssign":
                continue
            assignment = member.get("assignment") or {}
            lhs = assignment.get("left") or {}
            rhs = assignment.get("right") or {}
            width = simple_logic_width(lhs.get("type"))
            if (assignment.get("kind") != "Assignment" or lhs.get("kind") != "NamedValue" or
                    rhs.get("kind") != "NamedValue" or width is None or
                    simple_logic_width(rhs.get("type")) != width):
                continue
            output = ports.get(lhs.get("symbol")) or {}
            input_port = ports.get(rhs.get("symbol")) or {}
            if (output.get("direction") != "Out" or input_port.get("direction") != "In" or
                    simple_logic_width(output.get("type")) != width or
                    simple_logic_width(input_port.get("type")) != width or
                    writes[lhs.get("symbol")] != 1 or writes[rhs.get("symbol")]):
                continue
            input_conn = connections.get(input_port.get("name")) or {}
            output_conn = connections.get(output.get("name")) or {}
            source_net = binding(input_conn, width)
            dest_net = binding(output_conn, width)
            if isinstance(source_net, str) and isinstance(dest_net, str) and source_net != dest_net:
                yield source_net, dest_net, width, output.get("name")

    def port_path(scope_path, instance, port, width):
        return f"{scope_path}.{instance}.{port}" + (f"[{width - 1}:0]" if width > 1 else "")

    def visit(node, scope_path):
        members = members_of(node)
        by_symbol = defaultdict(lambda: {"Out": [], "In": []})
        parent_writes = Counter(symbol for member in members if isinstance(member, dict) and
                                member.get("kind") not in ("Instance", "InstanceArray")
                                for assignment in assignments(member)
                                for symbol in named_values(assignment.get("left")))
        copies = defaultdict(list)
        for child in members:
            if not isinstance(child, dict) or child.get("kind") != "Instance" or not child.get("name"):
                continue
            for connection in child.get("connections", []):
                port = connection.get("port") or {}
                direction = port.get("direction")
                width = simple_logic_width(port.get("type"))
                if direction not in ("In", "Out") or width is None:
                    continue
                symbol = binding(connection, width)
                if isinstance(symbol, str):
                    by_symbol[symbol][direction].append((child["name"], port["name"], width))
            for source_net, dest_net, width, output_port in direct_copies(child):
                copies[source_net].append((dest_net, width, child["name"], output_port))

        for start_net, ports in by_symbol.items():
            if len(ports["Out"]) != 1 or parent_writes[start_net]:
                continue
            source_instance, source_port, source_width = ports["Out"][0]
            pending = [(start_net, 0)]
            visited = {start_net}
            while pending:
                current, hops = pending.pop()
                if hops == max_hops:
                    continue
                for next_net, width, bridge_instance, output_port in copies[current]:
                    drivers = by_symbol[next_net]["Out"]
                    if (width != source_width or parent_writes[next_net] or len(drivers) != 1 or
                            drivers[0] != (bridge_instance, output_port, width) or next_net in visited):
                        continue
                    visited.add(next_net)
                    for dest_instance, dest_port, dest_width in by_symbol[next_net]["In"]:
                        if dest_instance not in (source_instance, bridge_instance) and dest_width == width:
                            expected.add((port_path(scope_path, source_instance, source_port, width),
                                          port_path(scope_path, dest_instance, dest_port, width)))
                    pending.append((next_net, hops + 1))

        for child in members:
            if not isinstance(child, dict):
                continue
            kind = child.get("kind")
            if kind == "Instance" and child.get("name"):
                visit(child, f"{scope_path}.{child['name']}")
            elif kind == "GenerateBlock":
                suffix = f".{child['name']}" if child.get("name") else ""
                visit(child, scope_path + suffix)
            elif kind == "GenerateBlockArray" and child.get("name"):
                for block in child.get("members", []):
                    index = block.get("constructIndex") if isinstance(block, dict) else None
                    if isinstance(index, int) and block.get("kind") == "GenerateBlock":
                        visit(block, f"{scope_path}.{child['name']}[{index}]")

    visit(scope, path)
    return sorted(expected)


def compare_report(report: dict, by_scope: dict[str, list[tuple[str, str]]]) -> dict:
    direct = {(row["source"], row["dest"]) for row in report.get("connections", [])
              if row.get("kind") == "direct"}
    approximate = {(row["source"], row["dest"]) for row in report.get("connections", [])
                   if row.get("kind") == "approximate"}
    expected = sorted({pair for pairs in by_scope.values() for pair in pairs})
    approximate_only = [pair for pair in expected if pair not in direct and pair in approximate]
    missing = [pair for pair in expected if pair not in direct and pair not in approximate]
    scope_results = {}
    for scope, pairs in by_scope.items():
        unique = set(pairs)
        direct_count = len(unique & direct)
        approximate_count = len((unique - direct) & approximate)
        scope_results[scope] = {"expected": len(unique), "found_direct": direct_count,
                                "approximate_only": approximate_count,
                                "missing": len(unique) - direct_count - approximate_count}
    fingerprint = hashlib.sha256("\n".join(" -> ".join(pair) for pair in expected).encode()).hexdigest()
    return {"scope_results": scope_results,
            "expected": len(expected), "found_direct": len(expected) - len(approximate_only) - len(missing),
            "approximate_only": approximate_only, "missing": missing,
            "expected_pairs": [{"source": source, "dest": dest} for source, dest in expected],
            "expected_sha256": fingerprint}


def audit_source_recall(report: dict, filelist: Path, top: str, scopes: list[str],
                        slang_binary: str | None = None) -> dict:
    binary = slang_binary or os.environ.get("SVLENS_SLANG") or shutil.which("slang")
    if not binary:
        raise FileNotFoundError("slang CLI not found; set SVLENS_SLANG")
    if not scopes or len(set(scopes)) != len(scopes):
        raise ValueError("source-recall scopes must be nonempty and distinct")
    by_scope = {}
    with tempfile.TemporaryDirectory(prefix="svlens-source-recall-") as directory:
        for index, scope in enumerate(scopes):
            ast_path = Path(directory) / f"scope-{index}.json"
            command = [binary, "--single-unit", "-F", str(filelist.resolve()), "--top", top,
                       "--ast-json-scope", scope, "--ast-json", str(ast_path)]
            completed = subprocess.run(command, cwd=filelist.parent, capture_output=True,
                                       text=True, timeout=120, check=False)
            if completed.returncode or not ast_path.is_file():
                raise RuntimeError(f"slang could not elaborate source-recall scope {scope}: "
                                   f"{completed.stderr[-800:]}")
            ast = json.loads(ast_path.read_text())
            if ast.get("kind") != "Instance" or ast.get("name") != scope.rsplit(".", 1)[-1]:
                raise ValueError(f"source-recall scope {scope} was not found")
            by_scope[scope] = expected_simple_edges(ast, scope)
    result = compare_report(report, by_scope)
    if not result["expected"]:
        raise ValueError("source-recall oracle found no eligible paths")
    return result
