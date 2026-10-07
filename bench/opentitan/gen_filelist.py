#!/usr/bin/env python3
"""Parse FuseSoC .core files to generate flat .f filelists for svlens."""

import logging
import os
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML required. Install: pip install pyyaml", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).parent
OT_DIR = SCRIPT_DIR / ".ot-src"
FILELIST_DIR = SCRIPT_DIR / "filelists"
CONFIG_FILE = SCRIPT_DIR / "targets.yaml"


def parse_core_yaml(core_path: Path) -> dict:
    """Parse a FuseSoC .core file, stripping the CAPI=2: header line."""
    with open(core_path) as f:
        lines = f.readlines()
    # Strip CAPI header (not valid YAML)
    if lines and lines[0].startswith("CAPI"):
        lines = lines[1:]
    data = yaml.safe_load("".join(lines))
    return data if isinstance(data, dict) else {}


def find_core_files(ot_root: Path) -> dict:
    """Build map of VLNV core_name -> core_file_path.

    Stores both the full versioned name (lowrisc:ip:uart:0.1) and the
    base name without version (lowrisc:ip:uart) for flexible lookup.
    """
    core_map = {}
    for core_path in sorted(ot_root.rglob("*.core")):
        try:
            data = parse_core_yaml(core_path)
            if "name" not in data:
                continue
            full_name = data["name"]  # e.g., "lowrisc:ip:uart:0.1"
            core_map[full_name] = core_path
            # Also register without version for flexible lookup
            parts = full_name.split(":")
            if len(parts) == 4:
                base_name = ":".join(parts[:3])  # "lowrisc:ip:uart"
                if base_name not in core_map:
                    core_map[base_name] = core_path
        except (OSError, UnicodeError, yaml.YAMLError, AttributeError, TypeError) as exc:
            logging.getLogger(__name__).debug("Skipping core metadata %s: %s", core_path, exc)
            continue
    return core_map


def select_entry(entry: str, flags: set[str]) -> str | None:
    """Evaluate the simple FuseSoC `flag ? (item)` spelling used in this tag."""
    match = re.fullmatch(r"\s*(!?\w+)\s*\?\s*\(([^()]*)\)\s*", entry)
    if not match:
        return entry
    condition, value = match.groups()
    enabled = condition[1:] not in flags if condition.startswith("!") else condition in flags
    return value.strip() if enabled else None


def selected_filesets(data: dict, flags: set[str]) -> list[dict]:
    filesets = data.get("filesets", {})
    default = data.get("targets", {}).get("default", {})
    names = default.get("filesets") if isinstance(default, dict) else None
    if not names:
        names = [name for name, item in filesets.items()
                 if isinstance(item, dict) and item.get("file_type") in
                 ("systemVerilogSource", "verilogSource")]
    if isinstance(names, str):
        names = [names]
    selected = []
    for name in names:
        name = select_entry(name, flags)
        if name is None:
            continue
        if name not in filesets:
            raise ValueError(f"selected fileset {name!r} is missing")
        selected.append(filesets[name])
    return selected


def resolve_deps(core_name: str, core_map: dict, flags: set[str],
                 visited: set[str] | None = None, generated_prims: set[str] | None = None) -> list:
    """Recursively resolve dependencies and collect all SV files."""
    if visited is None:
        visited = set()
    if generated_prims is None:
        generated_prims = set()
    if core_name in visited:
        return []
    visited.add(core_name)

    core_path = core_map.get(core_name)
    if not core_path:
        # Try prefix match: "lowrisc:prim:all" might be stored as "lowrisc:prim:all:0.1"
        for k, v in core_map.items():
            if k.startswith(core_name + ":") or k == core_name:
                core_path = v
                break
    if not core_path:
        raise ValueError(f"unresolved core dependency: {core_name}")

    data = parse_core_yaml(core_path)
    if not data:
        raise ValueError(f"invalid core metadata: {core_path}")

    all_files = []

    # FuseSoC's primgen normally creates a technology-selecting wrapper.
    # For a source-only benchmark use the generic implementation under the
    # wrapper name, and track exactly which wrappers were synthesized here.
    generator = data.get("generate", {}).get("impl", {})
    if generator.get("generator") == "primgen" and core_name.startswith("lowrisc:prim:"):
        if generator.get("parameters", {}).get("action") == "generate_prim_pkg":
            generated_prims.add("__prim_pkg__")
        else:
            primitive = core_name.split(":")[2]
            all_files.extend(resolve_deps("lowrisc:prim_generic:" + primitive,
                                          core_map, flags, visited, generated_prims))
            generated_prims.add(primitive)

    # Resolve dependencies first (depth-first)
    filesets = selected_filesets(data, flags)
    for fs_data in filesets:
        for dep in (fs_data.get("depend") or []):
            selected = select_entry(dep, flags)
            if selected:
                all_files.extend(resolve_deps(selected, core_map, flags,
                                              visited, generated_prims))

    # Then add this core's own files
    for fs_data in filesets:
        for entry in (fs_data.get("files") or []):
            raw = next(iter(entry)) if isinstance(entry, dict) else entry
            selected = select_entry(raw, flags)
            if not selected or not selected.endswith((".sv", ".svh", ".v", ".vh")):
                continue
            full = core_path.parent / selected
            if not full.is_file():
                raise ValueError(f"missing source selected by {core_path}: {selected}")
            all_files.append(str(full.resolve()))
    return all_files


def generate_filelist(target: dict, core_map: dict) -> Path:
    """Generate a .f filelist for one benchmark target."""
    name = target["name"]
    core_name = target["core"]

    print(f"  Resolving {name} ({core_name})...")
    flags = {"fileset_top"} if name == "top_earlgrey" else {"fileset_ip"}
    generated_prims = set()
    files = resolve_deps(core_name, core_map, flags, generated_prims=generated_prims)

    # Deduplicate preserving order
    seen = set()
    unique = []
    for f in files:
        if f not in seen:
            seen.add(f)
            unique.append(f)

    # Collect include directories
    inc_dirs = sorted({os.path.dirname(f) for f in unique})
    sources = [f for f in unique if f.endswith((".sv", ".v"))]
    header_count = len(unique) - len(sources)
    generated_dir = FILELIST_DIR / "generated" / name
    generated_dir.mkdir(parents=True, exist_ok=True)
    if "__prim_pkg__" in generated_prims:
        package = generated_dir / "prim_pkg.sv"
        package.write_text("package prim_pkg;\n"
                           "  typedef enum integer { ImplGeneric } impl_e;\n"
                           "endpackage : prim_pkg\n")
        sources.insert(0, str(package.resolve()))
    for primitive in sorted(generated_prims):
        if primitive == "__prim_pkg__":
            continue
        generic = OT_DIR / "hw" / "ip" / "prim_generic" / "rtl" / f"prim_generic_{primitive}.sv"
        if not generic.is_file():
            raise ValueError(f"primgen needs a generic implementation for {primitive}")
        module_decl = re.compile(r"\bmodule\s+prim_generic_" + re.escape(primitive) + r"\b")
        content, count = module_decl.subn(f"module prim_{primitive}", generic.read_text(), count=1)
        if count != 1:
            raise ValueError(f"module declaration missing in {generic}")
        content = re.sub(r"\bendmodule\s*:\s*prim_generic_" + re.escape(primitive) + r"\b",
                         f"endmodule : prim_{primitive}", content)
        generated = generated_dir / f"prim_{primitive}.sv"
        generated.write_text(content)
        sources.append(str(generated.resolve()))

    out_path = FILELIST_DIR / f"{name}.f"
    with open(out_path, "w") as out:
        out.write("+define+SYNTHESIS\n")
        out.writelines(f"+incdir+{d}\n" for d in inc_dirs)
        out.write("\n")
        out.writelines(f"{source}\n" for source in sources)

    print(f"  -> {out_path} ({len(sources)} sources, {header_count} headers)")
    return out_path


def main():
    if not OT_DIR.exists():
        print("ERROR: OpenTitan not found. Run fetch.sh first.", file=sys.stderr)
        sys.exit(1)

    with open(CONFIG_FILE) as f:
        config = yaml.safe_load(f)

    FILELIST_DIR.mkdir(exist_ok=True)

    print("=== Generating Filelists ===")
    print(f"OpenTitan root: {OT_DIR}")
    print("Scanning .core files...")
    core_map = find_core_files(OT_DIR)
    print(f"Found {len(core_map)} core entries ({sum(1 for p in OT_DIR.rglob('*.core'))} .core files)")

    failures = []
    for target in config["targets"]:
        try:
            generate_filelist(target, core_map)
        except (OSError, ValueError, KeyError, TypeError, AttributeError, yaml.YAMLError) as e:
            failures.append(target["name"])
            print(f"  ERROR: {target['name']} failed: {e}", file=sys.stderr)

    if failures:
        raise SystemExit("Filelist generation failed: " + ", ".join(failures))
    print("Done.")


if __name__ == "__main__":
    main()
