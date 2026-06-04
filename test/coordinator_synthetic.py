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
    def __init__(self, port, expect_kill=False, barrier=None,
                 expect_checkpoint=False, invalid_comp_group=False,
                 expect_duplicate_checkpoint=False,
                 expect_reject_not_restarting=False,
                 expect_kvdb=False,
                 expect_invalid_protocol_reject=False,
                 send_partial_message=False):
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
        if expect_checkpoint:
            args.append("--expect-checkpoint")
        if invalid_comp_group:
            args.append("--invalid-comp-group")
        if expect_duplicate_checkpoint:
            args.append("--expect-duplicate-checkpoint-after-update")
        if expect_reject_not_restarting:
            args.append("--expect-reject-not-restarting")
        if expect_kvdb:
            args.append("--expect-kvdb")
        if expect_invalid_protocol_reject:
            args.append("--expect-invalid-protocol-reject")
        if send_partial_message:
            args.append("--send-partial-message")
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

    def wait_until_checkpoint_requested(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "received DMT_DO_CHECKPOINT":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive checkpoint request")

    def wait_until_duplicate_checkpoint_requested(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "received duplicate DMT_DO_CHECKPOINT":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive duplicate checkpoint")

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

    def wait_until_rejected_wrong_computation(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "rejected DMT_REJECT_WRONG_COMP":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker was not rejected for wrong computation")

    def wait_until_rejected_not_restarting(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "rejected DMT_REJECT_NOT_RESTARTING":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError(
            "worker was not rejected for non-restarting coordinator")

    def wait_until_kvdb_round_trip(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "kvdb old=0 value=synthetic-value":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not complete KVDB round trip")

    def wait_until_invalid_protocol_rejected(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "rejected invalid protocol":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker was not rejected for invalid protocol")

    def wait_until_partial_message_sent(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            readable, _, _ = select.select([self.process.stdout], [], [], 0.1)
            if readable:
                line = self.process.stdout.readline().strip()
                if line == "sent partial protocol message":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self.process.stderr.read()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not send partial protocol message")

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

    def test_new_worker_with_existing_computation_group_is_rejected(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, invalid_comp_group=True)
            try:
                worker.wait_until_rejected_wrong_computation()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 0)
                self.assertFalse(status["running"])
            finally:
                worker.stop()

    def test_restart_worker_is_rejected_while_computation_is_running(self):
        with CoordinatorFixture() as coordinator:
            running_worker = WorkerProcess(coordinator.port)
            restart_worker = WorkerProcess(
                coordinator.port, expect_reject_not_restarting=True)
            try:
                running_worker.wait_until_accepted()
                restart_worker.wait_until_rejected_not_restarting()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertTrue(status["running"])
            finally:
                restart_worker.stop()
                running_worker.stop()

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

    def test_barrier_waiter_releases_when_peer_disconnects(self):
        with CoordinatorFixture() as coordinator:
            barrier = "disconnect-barrier"
            waiter = WorkerProcess(coordinator.port, barrier=barrier)
            peer = WorkerProcess(coordinator.port)
            try:
                waiter.wait_until_accepted()
                peer.wait_until_accepted()

                peer.stop()
                waiter.wait_until_barrier_released(barrier)
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertTrue(status["running"])
            finally:
                waiter.stop()
                peer.stop()

    def test_checkpoint_command_reaches_synthetic_worker(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_checkpoint=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = json.loads(result.stdout)

                self.assertTrue(payload["ok"])
                self.assertEqual(payload["type"], "checkpoint")
                self.assertEqual(payload["num_peers"], 1)
                worker.wait_until_checkpoint_requested()
            finally:
                worker.stop()

    def test_worker_update_during_checkpoint_gets_duplicate_request(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port,
                                   expect_duplicate_checkpoint=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = json.loads(result.stdout)

                self.assertTrue(payload["ok"])
                self.assertEqual(payload["type"], "checkpoint")
                worker.wait_until_duplicate_checkpoint_requested()
            finally:
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

    def test_kvdb_request_round_trip(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_kvdb=True)
            try:
                worker.wait_until_accepted()
                worker.wait_until_kvdb_round_trip()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertTrue(status["running"])
            finally:
                worker.stop()

    def test_invalid_magic_worker_is_rejected(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(
                coordinator.port, expect_invalid_protocol_reject=True)
            try:
                worker.wait_until_invalid_protocol_rejected()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 0)
                self.assertFalse(status["running"])
            finally:
                worker.stop()

    def test_partial_worker_message_does_not_start_computation(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, send_partial_message=True)
            try:
                worker.wait_until_partial_message_sent()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 0)
                self.assertFalse(status["running"])
            finally:
                worker.stop()

    def test_quit_command_kills_workers_and_stops_coordinator(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_kill=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port), "--quit")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = json.loads(result.stdout)

                self.assertTrue(payload["ok"])
                self.assertEqual(payload["type"], "quit")
                worker.wait_until_killed()
                coordinator.process.wait(timeout=5)
            finally:
                worker.stop()


if __name__ == "__main__":
    unittest.main()
