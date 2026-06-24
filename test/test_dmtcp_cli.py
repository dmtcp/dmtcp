#!/usr/bin/env python3

import os
import pathlib
import signal
import subprocess
import tempfile
import time
import unittest

from autotest import DmtcpCommandJson


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


def wait_for_port_file(path):
    deadline = time.time() + COMMAND_TIMEOUT
    while time.time() < deadline:
        if path.exists():
            data = path.read_text(encoding="utf-8").strip()
            if data:
                return int(data)
        time.sleep(0.05)
    raise AssertionError(f"port file was not written: {path}")


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

    def test_launch_new_coordinator_writes_port_file(self):
        with tempfile.TemporaryDirectory(
                prefix="dmtcp-launch-cli-", dir=str(ROOT)) as tmp:
            port_file = pathlib.Path(tmp) / "coordinator.port"
            proc = subprocess.Popen(
                [
                    str(BIN / "dmtcp_launch"),
                    "--quiet",
                    "--new-coordinator",
                    "--coord-port",
                    "0",
                    "--port-file",
                    str(port_file),
                    "/bin/sleep",
                    "30",
                ],
                cwd=str(ROOT),
                text=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                preexec_fn=os.setpgrp,
            )
            port = None
            try:
                port = wait_for_port_file(port_file)

                last_status = None
                deadline = time.time() + COMMAND_TIMEOUT
                while time.time() < deadline:
                    result = run_tool(
                        "dmtcp_command", "--json",
                        "--coord-port", str(port),
                        "--status",
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    last_status = DmtcpCommandJson.parse(result.stdout)
                    if last_status.get("num_peers") == 1 and \
                            last_status.get("running"):
                        break
                    time.sleep(0.05)
                else:
                    self.fail(f"worker did not join coordinator: "
                              f"{last_status}")

                self.assertEqual(last_status["coordinator_port"], port)
                self.assertEqual(last_status["num_peers"], 1)
                self.assertTrue(last_status["running"])
            finally:
                if port is not None:
                    run_tool("dmtcp_command", "--coord-port", str(port),
                             "--kill")
                    run_tool("dmtcp_command", "--coord-port", str(port),
                             "--quit")
                if proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait(timeout=5)

    def test_coordinator_daemon_writes_log_file_from_env(self):
        with tempfile.TemporaryDirectory(
                prefix="dmtcp-coord-log-cli-", dir=str(ROOT)) as tmp:
            tmp_path = pathlib.Path(tmp)
            port_file = tmp_path / "coordinator.port"
            log_file = tmp_path / "coordinator.log"
            env = os.environ.copy()
            env["DMTCP_COORD_LOG_FILENAME"] = str(log_file)

            result = subprocess.run(
                [
                    str(BIN / "dmtcp_coordinator"),
                    "--daemon",
                    "--coord-port",
                    "0",
                    "--port-file",
                    str(port_file),
                ],
                cwd=str(ROOT),
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=COMMAND_TIMEOUT,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

            port = wait_for_port_file(port_file)
            try:
                quit_result = run_tool("dmtcp_command", "--coord-port",
                                       str(port), "--quit")
                self.assertEqual(quit_result.returncode, 0,
                                 quit_result.stderr)

                deadline = time.time() + COMMAND_TIMEOUT
                while time.time() < deadline:
                    if log_file.exists():
                        contents = log_file.read_text(encoding="utf-8",
                                                      errors="replace")
                        if "DMTCP coordinator exiting" in contents:
                            break
                    time.sleep(0.05)
                else:
                    self.fail("coordinator log did not record clean exit")
            finally:
                run_tool("dmtcp_command", "--coord-port", str(port), "--quit")


if __name__ == "__main__":
    unittest.main()
