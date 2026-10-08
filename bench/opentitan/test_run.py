import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run as benchmark_run


class RunTests(unittest.TestCase):
    def test_missing_filelist_fails_before_old_metrics_can_be_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "svlens"
            binary.touch()
            with (mock.patch.object(benchmark_run, "SVLENS", binary),
                  mock.patch.object(benchmark_run, "FILELIST_DIR", root),
                  self.assertRaisesRegex(SystemExit, "Missing benchmark filelists")):
                benchmark_run.main()

    def test_missing_period_sdc_fails_before_old_reports_can_be_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "svlens"
            binary.touch()
            filelists = root / "filelists"
            filelists.mkdir()
            (filelists / "soc.f").write_text("soc.sv\n")
            (root / "targets.yaml").write_text(
                "targets:\n  - name: soc\n    period_sdc: constraints/missing.sdc\n")
            with (mock.patch.object(benchmark_run, "SVLENS", binary),
                  mock.patch.object(benchmark_run, "FILELIST_DIR", filelists),
                  mock.patch.object(benchmark_run, "SCRIPT_DIR", root),
                  self.assertRaisesRegex(SystemExit, "Missing benchmark period SDC")):
                benchmark_run.main()

    def test_existing_report_is_not_counted_when_process_does_not_update_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out_dir = root / "conn"
            out_dir.mkdir()
            (out_dir / "connect_report.json").write_text("{}\n")
            with mock.patch.object(benchmark_run, "SVLENS", Path(shutil.which("true"))):
                result = benchmark_run.run_mode("conn", root / "test.f", "top", 2, out_dir)
            self.assertEqual(result["exit"], 0)
            self.assertEqual(result["status"], "no_report")

    def test_period_probe_passes_sdc_without_overwriting_primary_cdc_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            out_dir = root / "cdc_periods"
            sdc = root / "periods.sdc"
            sdc.write_text("create_clock -period 8 [get_ports clk_i]\n")
            commands = []

            def launch(command, **_kwargs):
                commands.append(command)
                (out_dir / "cdc_report.json").write_text("{}\n")
                process = mock.Mock()
                process.wait.return_value = 0
                return process

            with mock.patch.object(benchmark_run.subprocess, "Popen", side_effect=launch):
                result = benchmark_run.run_mode("cdc", root / "test.f", "top", 2, out_dir, sdc=sdc)
            self.assertEqual(result["status"], "reported")
            self.assertEqual(commands[0][-2:], ["--sdc", str(sdc)])
            self.assertTrue((root / "cdc_periods_stdout.log").exists())
            self.assertTrue((root / "cdc_periods_stderr.log").exists())

    def test_sva_probe_requires_fresh_json_and_assertion_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            commands = []

            def launch(command, **_kwargs):
                commands.append(command)
                out_dir = Path(command[command.index("-o") + 1])
                (out_dir / "cdc_report.json").write_text("{}\n")
                if out_dir.name in ("complete", "sva_only"):
                    (out_dir / "cdc_assertions.sva").write_text("module svlens_cdc_assertions; endmodule\n")
                if out_dir.name == "sva_only":
                    (out_dir / "cdc_report.json").unlink()
                process = mock.Mock()
                process.wait.return_value = 0
                return process

            with mock.patch.object(benchmark_run.subprocess, "Popen", side_effect=launch):
                missing = benchmark_run.run_mode("cdc", root / "test.f", "top", 2,
                                                 root / "missing", emit_sva=True)
                complete = benchmark_run.run_mode("cdc", root / "test.f", "top", 2,
                                                  root / "complete", emit_sva=True)
                stale_dir = root / "stale"
                stale_dir.mkdir()
                (stale_dir / "cdc_assertions.sva").write_text("old assertion\n")
                stale = benchmark_run.run_mode("cdc", root / "test.f", "top", 2,
                                               stale_dir, emit_sva=True)
                sva_only = benchmark_run.run_mode("cdc", root / "test.f", "top", 2,
                                                  root / "sva_only", emit_sva=True)
            self.assertEqual(missing["status"], "no_report")
            self.assertEqual(complete["status"], "reported")
            self.assertEqual(stale["status"], "no_report")
            self.assertEqual(sva_only["status"], "no_report")
            self.assertEqual(commands[0][-2:], ["--emit-sva", str(root / "missing" / "cdc_assertions.sva")])
            self.assertEqual(commands[1][-2:], ["--emit-sva", str(root / "complete" / "cdc_assertions.sva")])
            with self.assertRaisesRegex(ValueError, "only for CDC"):
                benchmark_run.run_mode("conn", root / "test.f", "top", 2, root / "invalid", emit_sva=True)


if __name__ == "__main__":
    unittest.main()
