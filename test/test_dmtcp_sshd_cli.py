#!/usr/bin/env python3

import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_SSHD = ROOT / "bin" / "dmtcp_sshd"
COMMAND_TIMEOUT = 10


class DmtcpSshdCliTest(unittest.TestCase):
    def run_sshd(self, *args):
        return subprocess.run(
            [str(DMTCP_SSHD), *args],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=COMMAND_TIMEOUT,
        )

    def test_rejects_invalid_port_text(self):
        result = self.run_sshd(
            "--host", "127.0.0.1",
            "--port", "12x",
            "/bin/true",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid --port", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
