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


def read_port_file(path):
    deadline = time.time() + 10
    while time.time() < deadline:
        if path.exists():
            data = path.read_text(encoding="utf-8").strip()
            if data:
                return int(data)
        time.sleep(0.05)
    raise RuntimeError("coordinator did not write its port file")


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
        )

    def test_status_json_reports_coordinator_not_found(self):
        result = self.run_command("--json", "--coord-port", "1", "--status")

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stderr, "")
        payload = json.loads(result.stdout)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["type"], "status")
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["error_code"], "coordinator_not_found")
        self.assertEqual(payload["coordinator_host"], "localhost")
        self.assertEqual(payload["coordinator_port"], 1)

    def test_status_json_reports_reachable_coordinator_status(self):
        with tempfile.TemporaryDirectory(prefix="dmtcp-command-json-") as tmp:
            tmp_path = pathlib.Path(tmp)
            port_file = tmp_path / "port"
            coordinator = subprocess.Popen(
                [
                    str(DMTCP_COORDINATOR),
                    "--quiet",
                    "--coord-port",
                    "0",
                    "--port-file",
                    str(port_file),
                    "--timeout",
                    "30",
                ],
                cwd=str(ROOT),
                text=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            try:
                port = read_port_file(port_file)
                result = self.run_command("--json", "--coord-port", str(port),
                                          "--status")

                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                payload = json.loads(result.stdout)
                self.assertEqual(payload["schema_version"], 1)
                self.assertEqual(payload["type"], "status")
                self.assertTrue(payload["ok"])
                self.assertEqual(payload["coordinator_host"], "localhost")
                self.assertEqual(payload["coordinator_port"], port)
                self.assertIn("num_peers", payload)
                self.assertIn("running", payload)
                self.assertIn("checkpoint_interval", payload)
            finally:
                coordinator.terminate()
                try:
                    coordinator.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    coordinator.kill()
                    coordinator.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
