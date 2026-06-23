#!/usr/bin/env python3

import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
COMMAND_TIMEOUT = 10


def run_tool(tool, *args):
    return subprocess.run(
        [str(BIN / tool), *args],
        cwd=str(ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=COMMAND_TIMEOUT,
    )


def assert_contains_all(testcase, text, needles):
    for needle in needles:
        testcase.assertIn(needle, text)


class DmtcpCliTest(unittest.TestCase):
    def test_launch_help_lists_common_runtime_flags(self):
        result = run_tool("dmtcp_launch", "--help")

        self.assertEqual(result.returncode, 0, result.stderr)
        assert_contains_all(
            self,
            result.stdout,
            [
                "--checkpoint-open-files",
                "--allow-file-overwrite",
                "--ckpt-signal",
                "--no-gzip",
                "--ckptdir",
                "--coord-host",
                "--coord-port",
                "--port-file",
                "--tmpdir",
                "--coord-logfile",
            ],
        )

    def test_restart_help_lists_common_runtime_flags(self):
        result = run_tool("dmtcp_restart", "--help")

        self.assertEqual(result.returncode, 0, result.stderr)
        assert_contains_all(
            self,
            result.stdout,
            [
                "--no-strict-checking",
                "--ckptdir",
                "--restartdir",
                "--tmpdir",
                "--quiet",
                "--coord-logfile",
                "--debug-restart-pause",
                "--version",
            ],
        )

    def test_coordinator_help_lists_timeout_flags(self):
        result = run_tool("dmtcp_coordinator", "--help")

        self.assertEqual(result.returncode, 0, result.stderr)
        assert_contains_all(
            self,
            result.stdout,
            [
                "--timeout",
                "--stale-timeout",
                "--coord-port",
                "--port-file",
                "--coord-logfile",
            ],
        )

    def test_restart_version_reports_dmtcp_version(self):
        result = run_tool("dmtcp_restart", "--version")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"dmtcp_restart \(DMTCP\) \d+\.\d+")


if __name__ == "__main__":
    unittest.main()
