#!/usr/bin/env python3

import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_RESTART = ROOT / "bin" / "dmtcp_restart"
COMMAND_TIMEOUT = 10


class DmtcpRestartCliTest(unittest.TestCase):
    def run_restart(self, *args):
        return subprocess.run(
            [str(DMTCP_RESTART), *args],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=COMMAND_TIMEOUT,
        )

    def test_rejects_invalid_gdb_level(self):
        result = self.run_restart("--gdb", "abc")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid gdb debug level", result.stderr)


if __name__ == "__main__":
    unittest.main()
