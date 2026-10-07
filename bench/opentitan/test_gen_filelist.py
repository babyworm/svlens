import tempfile
import unittest
from pathlib import Path

from gen_filelist import find_core_files, resolve_deps, select_entry, selected_filesets


class GenFilelistTests(unittest.TestCase):
    def test_core_scan_keeps_valid_entries_when_one_yaml_file_is_malformed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "broken.core").write_text("CAPI=2:\nname: [unterminated\n")
            good = root / "good.core"
            good.write_text("CAPI=2:\nname: vendor:ip:good:0.1\n")
            core_map = find_core_files(root)
            self.assertEqual(core_map["vendor:ip:good:0.1"], good)
            self.assertEqual(core_map["vendor:ip:good"], good)

    def test_flagged_items_only_select_matching_rtl(self):
        self.assertEqual(select_entry("fileset_ip ? (rtl)", {"fileset_ip"}), "rtl")
        self.assertIsNone(select_entry("!fileset_ip ? (sim)", {"fileset_ip"}))
        self.assertEqual(select_entry("!fileset_ip ? (sim)", set()), "sim")
        data = {
            "filesets": {"rtl": {"files": ["rtl.sv"]},
                         "sim": {"files": ["sim.sv"]}},
            "targets": {"default": {"filesets": [
                "fileset_ip ? (rtl)", "!fileset_ip ? (sim)"]}},
        }
        self.assertEqual(selected_filesets(data, {"fileset_ip"}),
                         [data["filesets"]["rtl"]])
        self.assertEqual(selected_filesets(data, set()),
                         [data["filesets"]["sim"]])

    def test_unresolved_dependency_fails_instead_of_partial_filelist(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            core = root / "top.core"
            source = root / "top.sv"
            source.write_text("module top; endmodule\n")
            core.write_text(
                "CAPI=2:\nname: vendor:ip:top:0.1\n"
                "filesets:\n  rtl:\n    file_type: systemVerilogSource\n"
                "    depend: [vendor:ip:missing]\n    files: [top.sv]\n"
                "targets:\n  default:\n    filesets: [rtl]\n")
            with self.assertRaisesRegex(ValueError, "unresolved core dependency"):
                resolve_deps("vendor:ip:top:0.1", {"vendor:ip:top:0.1": core}, set())


if __name__ == "__main__":
    unittest.main()
