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
        self.assertEqual(payload["type"], "status")
        self.assertEqual(payload["phase"], "status")
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["error_code"], "coordinator_not_found")
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
        self.assertEqual(payload["type"], "status")
        self.assertEqual(payload["phase"], "status")
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["coordinator_host"], host)
        self.assertEqual(payload["coordinator_port"], 1)

    def test_status_json_reports_reachable_coordinator_status(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--status")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["type"], "status")
            self.assertEqual(payload["phase"], "status")
            self.assertTrue(payload["ok"])
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
            self.assertEqual(payload["type"], "checkpoint")
            self.assertEqual(payload["phase"], "checkpoint")
            self.assertFalse(payload["ok"])
            self.assertEqual(payload["error_code"], "not_running")

    def test_kill_json_reports_success(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--kill")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["type"], "kill")
            self.assertEqual(payload["phase"], "kill")
            self.assertTrue(payload["ok"])

    def test_quit_json_reports_success_and_stops_coordinator(self):
        with CoordinatorFixture() as coordinator:
            result = self.run_command("--json", "--coord-port",
                                      str(coordinator.port), "--quit")

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            payload = json.loads(result.stdout)
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["type"], "quit")
            self.assertEqual(payload["phase"], "quit")
            self.assertTrue(payload["ok"])
            coordinator.process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
