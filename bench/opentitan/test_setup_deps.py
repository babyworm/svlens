import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SETUP = ROOT / "scripts" / "setup-deps.sh"


class SetupDepsTests(unittest.TestCase):
    def test_tool_enabled_prefix_requires_the_slang_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            config = prefix / "lib" / "cmake" / "slang" / "slangConfig.cmake"
            config.parent.mkdir(parents=True)
            config.write_text("# test install\n")
            command = ["bash", str(SETUP), "--prefix", str(prefix), "--offline", "--with-tools"]
            missing = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("has no slang CLI", missing.stdout)

            binary = prefix / "bin" / "slang"
            binary.parent.mkdir()
            binary.write_text("#!/bin/sh\nexit 0\n")
            binary.chmod(0o755)
            present = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(present.returncode, 0, present.stdout + present.stderr)
            self.assertIn("slang already installed", present.stdout)


if __name__ == "__main__":
    unittest.main()
