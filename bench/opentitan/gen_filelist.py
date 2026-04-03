#!/usr/bin/env python3
"""Parse FuseSoC .core files to generate flat .f filelists for svlens."""

import os
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


def find_core_files(ot_root: Path) -> dict:
    """Build map of VLNV core_name -> core_file_path."""
    core_map = {}
    for core_path in ot_root.rglob("*.core"):
        try:
            with open(core_path) as f:
                data = yaml.safe_load(f)
            if not data or "name" not in data:
                continue
            core_map[data["name"]] = core_path
        except Exception:
            continue
    return core_map


def extract_sv_files(core_path: Path) -> list:
    """Extract .sv/.svh/.v/.vh file paths from a .core file."""
    with open(core_path) as f:
        data = yaml.safe_load(f)
    if not data:
        return []

    files = []
    filesets = data.get("filesets", {})
    for fs_name, fs_data in filesets.items():
        if not isinstance(fs_data, dict):
            continue
        for entry in fs_data.get("files", []):
            if isinstance(entry, dict):
                fname = list(entry.keys())[0]
            else:
                fname = entry
            if fname.endswith((".sv", ".svh", ".v", ".vh")):
                full = core_path.parent / fname
                if full.exists():
                    files.append(str(full.resolve()))
    return files


def resolve_deps(core_name: str, core_map: dict, visited: set = None) -> list:
    """Recursively resolve dependencies and collect all SV files."""
    if visited is None:
        visited = set()
    if core_name in visited:
        return []
    visited.add(core_name)

    core_path = core_map.get(core_name)
    if not core_path:
        return []

    with open(core_path) as f:
        data = yaml.safe_load(f)
    if not data:
        return []

    all_files = []

    # Resolve dependencies first (depth-first)
    filesets = data.get("filesets", {})
    for fs_name, fs_data in filesets.items():
        if not isinstance(fs_data, dict):
            continue
        for dep in fs_data.get("depend", []):
            all_files.extend(resolve_deps(dep, core_map, visited))

    # Then add this core's own files
    all_files.extend(extract_sv_files(core_path))
    return all_files


def generate_filelist(target: dict, core_map: dict) -> Path:
    """Generate a .f filelist for one benchmark target."""
    name = target["name"]
    core_name = target["core"]

    print(f"  Resolving {name} ({core_name})...")
    files = resolve_deps(core_name, core_map)

    # Deduplicate preserving order
    seen = set()
    unique = []
    for f in files:
        if f not in seen:
            seen.add(f)
            unique.append(f)

    # Collect include directories
    inc_dirs = sorted({os.path.dirname(f) for f in unique})

    out_path = FILELIST_DIR / f"{name}.f"
    with open(out_path, "w") as out:
        for d in inc_dirs:
            out.write(f"+incdir+{d}\n")
        out.write("\n")
        for f in unique:
            out.write(f"{f}\n")

    print(f"  -> {out_path} ({len(unique)} files)")
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
    print(f"Found {len(core_map)} .core files")

    for target in config["targets"]:
        try:
            generate_filelist(target, core_map)
        except Exception as e:
            print(f"  WARNING: {target['name']} failed: {e}", file=sys.stderr)

    print("Done.")


if __name__ == "__main__":
    main()
