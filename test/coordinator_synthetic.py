#!/usr/bin/env python3

import json
import os
import pathlib
import select
import subprocess
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_COMMAND = ROOT / "bin" / "dmtcp_command"
DMTCP_COORDINATOR = ROOT / "bin" / "dmtcp_coordinator"
SYNTHETIC_WORKER = ROOT / "test" / "coordinator_synthetic_worker"


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
        self.tmp = tempfile.TemporaryDirectory(prefix="dmtcp-coord-synth-")
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
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
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
        self.process.stdout.close()
        self.process.stderr.close()
        self.tmp.cleanup()


class WorkerProcess:
    def __init__(self, port, expect_kill=False, barrier=None):
        args = [
            str(SYNTHETIC_WORKER),
            "127.0.0.1",
            str(port),
            "--hold-seconds",
            "20",
        ]
        if expect_kill:
            args.append("--expect-kill")
        if barrier:
            args.extend(["--barrier", barrier])
        self.process = subprocess.Popen(
            args,
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def wait_until_accepted(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line.startswith("accepted virtual_pid="):
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
        raise RuntimeError("worker did not complete coordinator handshake")

    def wait_until_killed(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "received DMT_KILL_PEER":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive kill message")

    def wait_until_barrier_released(self, barrier):
        deadline = time.time() + 10
        expected = f"released barrier={barrier}"
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == expected:
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive barrier release")

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process.stdout.close()
        self.process.stderr.close()


class SyntheticCoordinatorWorkerTest(unittest.TestCase):
    def run_command(self, *args):
        return subprocess.run(
            [str(DMTCP_COMMAND), *args],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def coordinator_status(self, port):
        result = self.run_command("--json", "--coord-port", str(port),
                                  "--status")
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_single_synthetic_worker_join_updates_status(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port)
            try:
                worker.wait_until_accepted()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertTrue(status["running"])
            finally:
                worker.stop()

    def test_two_synthetic_workers_join_same_computation(self):
        with CoordinatorFixture() as coordinator:
            workers = [WorkerProcess(coordinator.port) for _ in range(2)]
            try:
                for worker in workers:
                    worker.wait_until_accepted()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 2)
                self.assertTrue(status["running"])
            finally:
                for worker in workers:
                    worker.stop()

    def test_two_synthetic_workers_release_same_barrier(self):
        with CoordinatorFixture() as coordinator:
            barrier = "synthetic-barrier"
            workers = [WorkerProcess(coordinator.port, barrier=barrier)
                       for _ in range(2)]
            try:
                for worker in workers:
                    worker.wait_until_accepted()
                for worker in workers:
                    worker.wait_until_barrier_released(barrier)
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 2)
                self.assertTrue(status["running"])
            finally:
                for worker in workers:
                    worker.stop()

    def test_kill_command_reaches_synthetic_worker(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_kill=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port), "--kill")
                self.assertEqual(result.returncode, 0, result.stderr)

                worker.wait_until_killed()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 0)
                self.assertFalse(status["running"])
            finally:
                worker.stop()


if __name__ == "__main__":
    unittest.main()
