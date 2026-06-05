#!/usr/bin/env python3

import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_COMMAND = ROOT / "bin" / "dmtcp_command"
COMMAND_TIMEOUT = 10


class DmtcpCommandCliTest(unittest.TestCase):
    def run_command(self, *args):
        return subprocess.run(
            [str(DMTCP_COMMAND), *args],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=COMMAND_TIMEOUT,
        )

    def test_rejects_invalid_interval_option(self):
        result = self.run_command("--interval", "12x")

        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("Usage:", result.stderr)

    def test_rejects_invalid_short_interval_option(self):
        result = self.run_command("-i12x")

        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main()
