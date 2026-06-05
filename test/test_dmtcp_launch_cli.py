#!/usr/bin/env python3

import os
import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_LAUNCH = ROOT / "bin" / "dmtcp_launch"
COMMAND_TIMEOUT = 10


class DmtcpLaunchCliTest(unittest.TestCase):
    def run_launch(self, *args, env=None):
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        return subprocess.run(
            [str(DMTCP_LAUNCH), *args],
            cwd=str(ROOT),
            env=merged_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=COMMAND_TIMEOUT,
        )

    def test_rejects_out_of_range_coord_port_option(self):
        result = self.run_launch("--coord-port", "65536", "/bin/true")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid coordinator port", result.stderr)

    def test_rejects_out_of_range_short_coord_port_option(self):
        result = self.run_launch("-p65536", "/bin/true")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid coordinator port", result.stderr)

    def test_rejects_out_of_range_coord_port_env(self):
        result = self.run_launch("/bin/true", env={"DMTCP_COORD_PORT": "65536"})

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid coordinator port", result.stderr)


if __name__ == "__main__":
    unittest.main()
