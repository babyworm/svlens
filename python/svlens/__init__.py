"""Small Python API around the svlens executable and JSON reports."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from collections.abc import Sequence
from pathlib import Path


class SvlensError(RuntimeError):
    """Analysis did not produce a readable JSON report."""


def _binary(binary: str | os.PathLike[str] | None) -> str:
    candidate = binary or os.environ.get("SVLENS_BINARY") or shutil.which("svlens")
    if candidate is None:
        raise SvlensError("svlens binary not found; set SVLENS_BINARY or pass binary=")
    path = Path(candidate)
    if not path.is_file() and path.parent == Path("."):
        resolved = shutil.which(str(candidate))
        if resolved:
            path = Path(resolved)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise SvlensError(f"svlens binary is not executable: {path}")
    return str(path.resolve())


def _run(mode: str, files: Sequence[str | os.PathLike[str]] | None, *,
         filelist: str | os.PathLike[str] | None, top: str,
         binary: str | os.PathLike[str] | None,
         extra_args: Sequence[str], timeout: float | None) -> dict:
    if bool(files) == bool(filelist):
        raise ValueError("pass exactly one of files or filelist")
    if not top:
        raise ValueError("top must be non-empty")

    with tempfile.TemporaryDirectory(prefix="svlens-python-") as output:
        command = [_binary(binary), mode, "--top", top, "-o", output]
        if mode == "all":
            command += ["--conn-format", "json", "--cdc-format", "json"]
        else:
            command += ["--format", "json"]
        command += list(extra_args)
        if filelist:
            command += ["-F", str(filelist)]
        else:
            command += [str(path) for path in files or ()]

        try:
            process = subprocess.run(command, capture_output=True, text=True,
                                     check=False, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise SvlensError(f"svlens {mode} timed out") from error

        root = Path(output)
        if mode == "all":
            summary_path = root / "svlens_summary.json"
            if not summary_path.is_file():
                raise SvlensError(process.stderr.strip() or "svlens all produced no summary")
            reports = {"summary": summary_path,
                       "conn": root / "conn" / "connect_report.json",
                       "cdc": root / "cdc" / "cdc_report.json",
                       "metrics": root / "metrics" / "metrics_report.json"}
            missing = [name for name, path in reports.items() if not path.is_file()]
            if missing:
                raise SvlensError(f"svlens all omitted reports: {', '.join(missing)}")
            return {name: json.loads(path.read_text()) for name, path in reports.items()}

        report_name = {"conn": "connect_report.json", "cdc": "cdc_report.json",
                       "metrics": "metrics_report.json"}[mode]
        report_path = root / report_name
        if not report_path.is_file():
            raise SvlensError(process.stderr.strip() or
                              f"svlens {mode} produced no {report_name} (exit {process.returncode})")
        return json.loads(report_path.read_text())


def conn(files: Sequence[str | os.PathLike[str]] | None = None, *,
         filelist: str | os.PathLike[str] | None = None, top: str,
         binary: str | os.PathLike[str] | None = None,
         extra_args: Sequence[str] = (), timeout: float | None = None) -> dict:
    """Return connect_report.json as a dict, including findings on nonzero exit."""
    return _run("conn", files, filelist=filelist, top=top, binary=binary,
                extra_args=extra_args, timeout=timeout)


def cdc(files: Sequence[str | os.PathLike[str]] | None = None, *,
        filelist: str | os.PathLike[str] | None = None, top: str,
        binary: str | os.PathLike[str] | None = None,
        extra_args: Sequence[str] = (), timeout: float | None = None) -> dict:
    """Return cdc_report.json as a dict."""
    return _run("cdc", files, filelist=filelist, top=top, binary=binary,
                extra_args=extra_args, timeout=timeout)


def metrics(files: Sequence[str | os.PathLike[str]] | None = None, *,
            filelist: str | os.PathLike[str] | None = None, top: str,
            binary: str | os.PathLike[str] | None = None,
            extra_args: Sequence[str] = (), timeout: float | None = None) -> dict:
    """Return metrics_report.json as a dict."""
    return _run("metrics", files, filelist=filelist, top=top, binary=binary,
                extra_args=extra_args, timeout=timeout)


def all_modes(files: Sequence[str | os.PathLike[str]] | None = None, *,
              filelist: str | os.PathLike[str] | None = None, top: str,
              binary: str | os.PathLike[str] | None = None,
              extra_args: Sequence[str] = (), timeout: float | None = None) -> dict:
    """Return the all-mode summary and three reports as dicts."""
    return _run("all", files, filelist=filelist, top=top, binary=binary,
                extra_args=extra_args, timeout=timeout)
