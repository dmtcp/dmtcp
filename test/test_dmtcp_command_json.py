#!/usr/bin/env python3

import json
import os
import pathlib
import subprocess
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_COMMAND = ROOT / "bin" / "dmtcp_command"
DMTCP_COORDINATOR = ROOT / "bin" / "dmtcp_coordinator"
COMMAND_TIMEOUT = 10


def read_port_file(path):
    deadline = time.time() + 10
    while time.time() < deadline:
        if path.exists():
            data = path.read_text(encoding="utf-8").strip()
            if data:
                return int(data)
        time.sleep(0.05)
    raise RuntimeError("coordinator did not write its port file")


class CoordinatorFixture:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="dmtcp-command-json-")
        self.tmp_path = pathlib.Path(self.tmp.name)
        self.port_file = self.tmp_path / "port"
        self.process = subprocess.Popen(
            [
                str(DMTCP_COORDINATOR),
                "--quiet",
                "--coord-port",
                "0",
                "--port-file",
                str(self.port_file),
                "--timeout",
                "30",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.port = read_port_file(self.port_file)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.tmp.cleanup()


class DmtcpCommandJsonTest(unittest.TestCase):
    def run_command(self, *args, env=None):
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        return subprocess.run(
            [str(DMTCP_COMMAND), *args],
            cwd=str(ROOT),
            env=merged_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=COMMAND_TIMEOUT,
        )

    def test_status_json_reports_coordinator_not_found(self):
        result = self.run_command("--json", "--coord-port", "1", "--status")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = json.loads(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_STATUS")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_NOT_FOUND")
        self.assertEqual(payload["coordinator_host"], "localhost")
        self.assertEqual(payload["coordinator_port"], 1)

    def test_json_escapes_coordinator_host(self):
        host = 'bad"host\nname'

        result = self.run_command("--json", "--coord-host", host,
                                  "--coord-port", "1", "--status")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = json.loads(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_STATUS")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_NOT_FOUND")
        self.assertEqual(payload["coordinator_host"], host)
        self.assertEqual(payload["coordinator_port"], 1)

    def test_invalid_command_json_does_not_print_usage(self):
        result = self.run_command("--json", "--not-a-command")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = json.loads(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_INVALID_COORDINATOR_COMMAND")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_INVALID_COMMAND")

    def test_list_json_reports_reachable_coordinator_workers(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--list")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = DmtcpCommandJson.parse(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["command"], "DMT_LIST")
            self.assertNotIn("phase", payload)
            self.assertNotIn("ok", payload)
            self.assertNotIn("type", payload)
            self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")
            self.assertEqual(payload["coordinator_host"], "localhost")
            self.assertEqual(payload["coordinator_port"], coordinator.port)
            self.assertIsInstance(payload["workers"], str)

    def test_list_json_reports_coordinator_not_found(self):
        result = self.run_command("--json", "--coord-port", "1", "--list")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = json.loads(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_LIST")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_NOT_FOUND")

    def test_help_command_json_reports_success(self):
        result = self.run_command("--json", "--help")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = DmtcpCommandJson.parse(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_HELP")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")

    def test_version_command_json_reports_success(self):
        result = self.run_command("--json", "--version")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = DmtcpCommandJson.parse(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_VERSION")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")
        self.assertIn("version", payload)

    def test_interval_json_reports_updated_checkpoint_interval(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port),
                                      "--interval", "7")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = DmtcpCommandJson.parse(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["command"], "DMT_UPDATE_CKPT_INTERVAL")
            self.assertNotIn("phase", payload)
            self.assertNotIn("ok", payload)
            self.assertNotIn("type", payload)
            self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")
            self.assertEqual(payload["coordinator_host"], "localhost")
            self.assertEqual(payload["coordinator_port"], coordinator.port)
            self.assertEqual(payload["checkpoint_interval"], 7)

    def test_interval_json_reports_coordinator_not_found(self):
        result = self.run_command("--json", "--coord-port", "1",
                                  "--interval", "7")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = DmtcpCommandJson.parse(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["command"], "DMT_UPDATE_CKPT_INTERVAL")
        self.assertNotIn("phase", payload)
        self.assertNotIn("ok", payload)
        self.assertNotIn("type", payload)
        self.assertEqual(payload["command_status"], "DMT_COORD_NOT_FOUND")

    def test_status_json_reports_reachable_coordinator_status(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--status")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["command"], "DMT_STATUS")
            self.assertNotIn("phase", payload)
            self.assertNotIn("ok", payload)
            self.assertNotIn("type", payload)
            self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")
            self.assertEqual(payload["coordinator_host"], "localhost")
            self.assertEqual(payload["coordinator_port"], coordinator.port)
            self.assertIn("num_peers", payload)
            self.assertIn("running", payload)
            self.assertIn("checkpoint_interval", payload)

    def test_checkpoint_json_reports_not_running(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--checkpoint")

            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["command"], "DMT_CHECKPOINT")
            self.assertNotIn("phase", payload)
            self.assertNotIn("ok", payload)
            self.assertNotIn("type", payload)
            self.assertEqual(payload["command_status"], "DMT_COORD_NOT_RUNNING")

    def test_checkpoint_aliases_report_typed_json_command(self):
        cases = [
            ("--bcheckpoint", "DMT_BLOCKING_CKPT"),
            ("-bc", "DMT_BLOCKING_CKPT"),
            ("--kcheckpoint", "DMT_KILL_AFTER_CKPT"),
            ("-kc", "DMT_KILL_AFTER_CKPT"),
            ("-ck", "DMT_KILL_AFTER_CKPT"),
            ("-K", "DMT_KILL_AFTER_CKPT"),
        ]
        with CoordinatorFixture() as coordinator:
            for command, expected_command in cases:
                with self.subTest(command=command):
                    result = self.run_command("--json", "--coord-port",
                                              str(coordinator.port), command)

                    self.assertEqual(result.returncode, 2, result.stdout)
                    self.assertEqual(result.stderr, "")
                    payload = DmtcpCommandJson.parse(result.stdout)
                    self.assertEqual(payload["schema_version"], 1)
                    self.assertEqual(payload["command"], expected_command)
                    self.assertNotIn("phase", payload)
                    self.assertNotIn("ok", payload)
                    self.assertNotIn("type", payload)
                    self.assertEqual(payload["command_status"],
                                     "DMT_COORD_NOT_RUNNING")

    def test_kill_json_reports_success(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--kill")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["command"], "DMT_KILL")
            self.assertNotIn("phase", payload)
            self.assertNotIn("ok", payload)
            self.assertNotIn("type", payload)
            self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")

    def test_quit_json_reports_success_and_stops_coordinator(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--quit")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["command"], "DMT_QUIT")
            self.assertNotIn("phase", payload)
            self.assertNotIn("ok", payload)
            self.assertNotIn("type", payload)
            self.assertEqual(payload["command_status"], "DMT_COORD_SUCCESS")
            coordinator.process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
