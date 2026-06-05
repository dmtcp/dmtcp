#!/usr/bin/env python3

import os
import pathlib
import select
import socket
import subprocess
import tempfile
import time
import unittest

from dmtcp_test_harness import parse_dmtcp_command_json


ROOT = pathlib.Path(__file__).resolve().parents[1]
DMTCP_COMMAND = ROOT / "bin" / "dmtcp_command"
DMTCP_COORDINATOR = ROOT / "bin" / "dmtcp_coordinator"
SYNTHETIC_WORKER = ROOT / "test" / "coordinator_synthetic_worker"
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


def read_nonempty_file(path):
    deadline = time.time() + 10
    while time.time() < deadline:
        if path.exists():
            data = path.read_text(encoding="utf-8")
            if data:
                return data
        time.sleep(0.05)
    raise RuntimeError(f"file stayed empty: {path}")


class CoordinatorFixture:
    def __init__(self, extra_args=None):
        self.tmp = tempfile.TemporaryDirectory(prefix="dmtcp-coord-synth-")
        self.tmp_path = pathlib.Path(self.tmp.name)
        self.port_file = self.tmp_path / "port"
        args = [
            str(DMTCP_COORDINATOR),
            "--quiet",
            "--coord-port",
            "0",
            "--port-file",
            str(self.port_file),
            "--timeout",
            "30",
        ]
        if extra_args:
            args.extend(extra_args)
        self.process = subprocess.Popen(
            args,
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
                 expect_kill_after_checkpoint=False,
                 expect_duplicate_checkpoint=False,
                 expect_reject_not_restarting=False,
                 expect_reject_not_running=False,
                 expect_restart_peer_mismatch=False,
                 expect_kvdb=False,
                 expect_invalid_protocol_reject=False,
                 expect_oversized_extra_reject=False,
                 expect_invalid_message_size_reject=False,
                 send_partial_message=False,
                 send_unexpected_message=False,
                 barrier_after_stdin=False,
                 barrier_twice_before_wait=False,
                 restart_worker=False,
                 num_peers=None):
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
        if barrier_after_stdin:
            args.append("--barrier-after-stdin")
        if barrier_twice_before_wait:
            args.append("--send-barrier-twice-before-wait")
        if expect_checkpoint:
            args.append("--expect-checkpoint")
        if expect_kill_after_checkpoint:
            args.append("--expect-kill-after-checkpoint")
        if invalid_comp_group:
            args.append("--invalid-comp-group")
        if expect_duplicate_checkpoint:
            args.append("--expect-duplicate-checkpoint-after-update")
        if expect_reject_not_restarting:
            args.append("--expect-reject-not-restarting")
        if expect_reject_not_running:
            args.append("--expect-reject-not-running")
        if expect_restart_peer_mismatch:
            args.append("--expect-restart-peer-mismatch")
        if expect_kvdb:
            args.append("--expect-kvdb")
        if expect_invalid_protocol_reject:
            args.append("--expect-invalid-protocol-reject")
        if expect_oversized_extra_reject:
            args.append("--expect-oversized-extra-reject")
        if expect_invalid_message_size_reject:
            args.append("--expect-invalid-message-size-reject")
        if send_partial_message:
            args.append("--send-partial-message")
        if send_unexpected_message:
            args.append("--send-unexpected-message")
        if restart_worker:
            args.append("--restart-worker")
        if num_peers is not None:
            args.extend(["--num-peers", str(num_peers)])
        self.process = subprocess.Popen(
            args,
            cwd=str(ROOT),
            stdin=subprocess.PIPE if barrier_after_stdin else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._stdout_buffer = b""

    def _read_stderr(self):
        data = self.process.stderr.read()
        if isinstance(data, bytes):
            return data.decode("utf-8", errors="replace")
        return data

    def _stdout_ready(self, timeout):
        if b"\n" in self._stdout_buffer:
            return True
        readable, _, _ = select.select([self.process.stdout], [], [], timeout)
        return bool(readable)

    def _read_stdout_line(self):
        while b"\n" not in self._stdout_buffer:
            chunk = os.read(self.process.stdout.fileno(), 4096)
            if chunk:
                self._stdout_buffer += chunk
                continue

            stderr = ""
            if self.process.poll() is not None:
                stderr = self._read_stderr()
            raise RuntimeError(f"worker stdout closed early: {stderr}")

        line, self._stdout_buffer = self._stdout_buffer.split(b"\n", 1)
        return line.decode("utf-8", errors="replace").strip()

    def wait_until_accepted(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line.startswith("accepted virtual_pid="):
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
        raise RuntimeError("worker did not complete coordinator handshake")

    def wait_until_killed(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "received DMT_KILL_PEER":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive kill message")

    def wait_until_checkpoint_requested(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "received DMT_DO_CHECKPOINT":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive checkpoint request")

    def wait_until_duplicate_checkpoint_requested(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "received duplicate DMT_DO_CHECKPOINT":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive duplicate checkpoint")

    def wait_until_barrier_released(self, barrier):
        deadline = time.time() + 10
        expected = f"released barrier={barrier}"
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == expected:
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not receive barrier release")

    def send_barrier_from_stdin(self):
        self.process.stdin.write(b"\n")
        self.process.stdin.flush()

    def wait_until_barrier_sent(self, barrier):
        deadline = time.time() + 10
        expected = f"sent barrier={barrier}"
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == expected:
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not send barrier")

    def wait_until_rejected_wrong_computation(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "rejected DMT_REJECT_WRONG_COMP":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker was not rejected for wrong computation")

    def wait_until_rejected_not_restarting(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "rejected DMT_REJECT_NOT_RESTARTING":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError(
            "worker was not rejected for non-restarting coordinator")

    def wait_until_rejected_not_running(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "rejected DMT_REJECT_NOT_RUNNING":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError(
            "worker was not rejected for non-running coordinator")

    def wait_until_rejected_restart_peer_mismatch(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "rejected DMT_REJECT_RESTART_PEER_MISMATCH":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError(
            "worker was not rejected for restart peer-count mismatch")

    def wait_until_kvdb_round_trip(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "kvdb old=0 value=synthetic-value":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not complete KVDB round trip")

    def wait_until_invalid_protocol_rejected(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "rejected invalid protocol":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker was not rejected for invalid protocol")

    def wait_until_oversized_extra_rejected(self):
        return self.wait_until_invalid_protocol_rejected()

    def wait_until_partial_message_sent(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "sent partial protocol message":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not send partial protocol message")

    def wait_until_unexpected_message_rejected(self):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self._stdout_ready(0.1):
                line = self._read_stdout_line()
                if line == "sent unexpected protocol message":
                    return line
                raise RuntimeError(f"unexpected worker output: {line}")
            if self.process.poll() is not None:
                stderr = self._read_stderr()
                raise RuntimeError(f"worker exited early: {stderr}")
        raise RuntimeError("worker did not send unexpected protocol message")

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if self.process.stdin is not None:
            self.process.stdin.close()
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
            timeout=COMMAND_TIMEOUT,
        )

    def run_coordinator(self, *args, env=None, timeout=COMMAND_TIMEOUT):
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)
        return subprocess.run(
            [str(DMTCP_COORDINATOR), *args],
            cwd=str(ROOT),
            env=merged_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=timeout,
        )

    def assert_coordinator_rejects_args(self, *args):
        try:
            result = self.run_coordinator(*args, timeout=1)
        except subprocess.TimeoutExpired:
            self.fail(f"coordinator did not reject invalid args: {args}")

        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("Usage:", result.stderr)

    def test_invalid_coord_port_option_exits_with_usage(self):
        self.assert_coordinator_rejects_args("--coord-port", "12x")

    def test_invalid_coord_port_env_exits_with_error(self):
        try:
            result = self.run_coordinator(env={"DMTCP_COORD_PORT": "65536"},
                                          timeout=1)
        except subprocess.TimeoutExpired:
            self.fail("coordinator did not reject invalid DMTCP_COORD_PORT")

        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("invalid coordinator port", result.stderr)

    def test_invalid_timeout_option_exits_with_usage(self):
        self.assert_coordinator_rejects_args("--timeout", "12x")

    def test_invalid_stale_timeout_option_exits_with_usage(self):
        self.assert_coordinator_rejects_args("--stale-timeout", "12x")

    def test_invalid_interval_option_exits_with_usage(self):
        self.assert_coordinator_rejects_args("--interval", "12x")

    def coordinator_status(self, port):
        result = self.run_command("--json", "--coord-port", str(port),
                                  "--status")
        self.assertEqual(result.returncode, 0, result.stderr)
        return parse_dmtcp_command_json(result.stdout)

    def wait_until_num_peers(self, port, expected, timeout=5):
        deadline = time.time() + timeout
        last_status = None
        while time.time() < deadline:
            last_status = self.coordinator_status(port)
            if last_status["num_peers"] == expected:
                return last_status
            time.sleep(0.05)
        self.fail(f"expected {expected} peers, last status: {last_status}")

    def assert_no_worker_output(self, worker, seconds=0.3):
        if worker._stdout_ready(seconds):
            self.fail(f"unexpected worker output: {worker._read_stdout_line()}")
        self.assertIsNone(worker.process.poll())

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

    def test_status_file_is_written_at_startup(self):
        with tempfile.TemporaryDirectory(prefix="dmtcp-status-file-") as tmp:
            status_file = pathlib.Path(tmp) / "coordinator.status"
            with CoordinatorFixture(
                    extra_args=["--status-file", str(status_file)]):
                contents = read_nonempty_file(status_file)

                self.assertIn("Coordinator started:", contents)
                self.assertIn("Status...", contents)
                self.assertIn("Checkpoint Interval:", contents)

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

    def test_replacement_worker_can_join_after_peer_disconnects(self):
        with CoordinatorFixture() as coordinator:
            remaining = WorkerProcess(coordinator.port)
            departed = WorkerProcess(coordinator.port)
            replacement = None
            try:
                remaining.wait_until_accepted()
                departed.wait_until_accepted()
                status = self.wait_until_num_peers(coordinator.port, 2)
                self.assertTrue(status["running"])

                departed.stop()
                status = self.wait_until_num_peers(coordinator.port, 1)
                self.assertTrue(status["running"])

                replacement = WorkerProcess(coordinator.port)
                replacement.wait_until_accepted()
                status = self.wait_until_num_peers(coordinator.port, 2)

                self.assertTrue(status["ok"])
                self.assertTrue(status["running"])
            finally:
                remaining.stop()
                departed.stop()
                if replacement is not None:
                    replacement.stop()

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
            restart_worker = None
            try:
                running_worker.wait_until_accepted()
                restart_worker = WorkerProcess(
                    coordinator.port, expect_reject_not_restarting=True)
                restart_worker.wait_until_rejected_not_restarting()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertTrue(status["running"])
            finally:
                if restart_worker is not None:
                    restart_worker.stop()
                running_worker.stop()

    def test_two_synthetic_workers_release_same_barrier(self):
        with CoordinatorFixture() as coordinator:
            barrier = "synthetic-barrier"
            workers = [WorkerProcess(coordinator.port, barrier=barrier,
                                     barrier_after_stdin=True)
                       for _ in range(2)]
            try:
                for worker in workers:
                    worker.wait_until_accepted()
                self.wait_until_num_peers(coordinator.port, 2)
                for worker in workers:
                    worker.send_barrier_from_stdin()
                for worker in workers:
                    worker.wait_until_barrier_sent(barrier)
                for worker in workers:
                    worker.wait_until_barrier_released(barrier)
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 2)
                self.assertTrue(status["running"])
            finally:
                for worker in workers:
                    worker.stop()

    def test_restarting_workers_release_barrier_only_after_restart_quorum(self):
        with CoordinatorFixture() as coordinator:
            barrier = "restart-quorum"
            first = WorkerProcess(coordinator.port, barrier=barrier,
                                  restart_worker=True, num_peers=2)
            second = None
            try:
                first.wait_until_accepted()
                self.assert_no_worker_output(first)

                second = WorkerProcess(coordinator.port, barrier=barrier,
                                       restart_worker=True, num_peers=2)
                second.wait_until_accepted()

                first.wait_until_barrier_released(barrier)
                second.wait_until_barrier_released(barrier)
            finally:
                first.stop()
                if second is not None:
                    second.stop()

    def test_restart_worker_with_peer_count_mismatch_is_rejected(self):
        with CoordinatorFixture() as coordinator:
            first = WorkerProcess(coordinator.port, restart_worker=True,
                                  num_peers=2)
            second = None
            try:
                first.wait_until_accepted()

                second = WorkerProcess(
                    coordinator.port,
                    expect_restart_peer_mismatch=True,
                    num_peers=3)
                second.wait_until_rejected_restart_peer_mismatch()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertFalse(status["running"])
            finally:
                first.stop()
                if second is not None:
                    second.stop()

    def test_restart_worker_with_wrong_computation_group_is_rejected(self):
        with CoordinatorFixture() as coordinator:
            first = WorkerProcess(coordinator.port, restart_worker=True,
                                  num_peers=2)
            second = None
            try:
                first.wait_until_accepted()

                second = WorkerProcess(
                    coordinator.port,
                    restart_worker=True,
                    invalid_comp_group=True,
                    num_peers=2)
                second.wait_until_rejected_wrong_computation()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertFalse(status["running"])
            finally:
                first.stop()
                if second is not None:
                    second.stop()

    def test_new_worker_is_rejected_while_restart_is_active(self):
        with CoordinatorFixture() as coordinator:
            restarting = WorkerProcess(coordinator.port, restart_worker=True,
                                       num_peers=2)
            new_worker = None
            try:
                restarting.wait_until_accepted()

                new_worker = WorkerProcess(
                    coordinator.port,
                    expect_reject_not_running=True)
                new_worker.wait_until_rejected_not_running()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertFalse(status["running"])
            finally:
                restarting.stop()
                if new_worker is not None:
                    new_worker.stop()

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

    def test_mismatched_barrier_disconnects_offending_worker(self):
        with CoordinatorFixture() as coordinator:
            waiter = WorkerProcess(coordinator.port, barrier="barrier-a",
                                   barrier_after_stdin=True)
            offender = None
            try:
                waiter.wait_until_accepted()
                offender = WorkerProcess(coordinator.port, barrier="barrier-b",
                                         barrier_after_stdin=True)
                offender.wait_until_accepted()

                waiter.send_barrier_from_stdin()
                waiter.wait_until_barrier_sent("barrier-a")
                self.assert_no_worker_output(waiter)

                offender.send_barrier_from_stdin()
                offender.wait_until_barrier_sent("barrier-b")
                offender.process.wait(timeout=5)
                self.assertNotEqual(offender.process.returncode, 0)

                waiter.wait_until_barrier_released("barrier-a")
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
                self.assertTrue(status["running"])
            finally:
                waiter.stop()
                if offender is not None:
                    offender.stop()

    def test_duplicate_barrier_from_same_worker_does_not_release(self):
        with CoordinatorFixture() as coordinator:
            barrier = "duplicate-barrier"
            repeated = WorkerProcess(coordinator.port, barrier=barrier,
                                     barrier_after_stdin=True,
                                     barrier_twice_before_wait=True)
            peer = WorkerProcess(coordinator.port, barrier=barrier,
                                 barrier_after_stdin=True)
            try:
                repeated.wait_until_accepted()
                peer.wait_until_accepted()
                self.wait_until_num_peers(coordinator.port, 2)

                repeated.send_barrier_from_stdin()
                repeated.wait_until_barrier_sent(barrier)
                repeated.wait_until_barrier_sent(barrier)
                self.assert_no_worker_output(repeated)

                peer.send_barrier_from_stdin()
                peer.wait_until_barrier_sent(barrier)

                repeated.wait_until_barrier_released(barrier)
                peer.wait_until_barrier_released(barrier)
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 2)
                self.assertTrue(status["running"])
            finally:
                repeated.stop()
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
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                self.assertEqual(payload["type"], "checkpoint")
                self.assertEqual(payload["num_peers"], 1)
                worker.wait_until_checkpoint_requested()
            finally:
                worker.stop()

    def test_checkpoint_command_rejects_second_request_while_active(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_checkpoint=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                worker.wait_until_checkpoint_requested()

                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertNotEqual(result.returncode, 0)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertFalse(payload["ok"])
                self.assertEqual(payload["type"], "checkpoint")
                self.assertEqual(payload["error_code"], "not_running")
            finally:
                worker.stop()

    def test_kill_command_during_checkpoint_reaches_worker(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port,
                                   expect_checkpoint=True,
                                   expect_kill_after_checkpoint=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                worker.wait_until_checkpoint_requested()

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

    def test_timeout_exits_during_slow_checkpoint(self):
        with CoordinatorFixture(extra_args=["--timeout", "1"]) as coordinator:
            worker = WorkerProcess(coordinator.port, expect_checkpoint=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                worker.wait_until_checkpoint_requested()

                coordinator.process.wait(timeout=5)
                self.assertNotEqual(coordinator.process.returncode, 0)
            finally:
                worker.stop()

    def test_new_worker_during_checkpoint_receives_checkpoint_request(self):
        with CoordinatorFixture() as coordinator:
            first = WorkerProcess(coordinator.port, expect_checkpoint=True)
            second = None
            try:
                first.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                first.wait_until_checkpoint_requested()

                second = WorkerProcess(coordinator.port,
                                       expect_checkpoint=True)
                second.wait_until_accepted()
                second.wait_until_checkpoint_requested()
                status = self.wait_until_num_peers(coordinator.port, 2)

                self.assertTrue(status["ok"])
            finally:
                first.stop()
                if second is not None:
                    second.stop()

    def test_restart_worker_is_rejected_while_checkpoint_is_active(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_checkpoint=True)
            restart_worker = None
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port),
                                          "--checkpoint")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                worker.wait_until_checkpoint_requested()

                restart_worker = WorkerProcess(
                    coordinator.port, expect_reject_not_restarting=True)
                restart_worker.wait_until_rejected_not_restarting()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 1)
            finally:
                worker.stop()
                if restart_worker is not None:
                    restart_worker.stop()

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
                payload = parse_dmtcp_command_json(result.stdout)

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

    def test_exit_on_last_stops_coordinator_after_worker_disconnect(self):
        with CoordinatorFixture(extra_args=["--exit-on-last"]) as coordinator:
            worker = WorkerProcess(coordinator.port)
            worker.wait_until_accepted()
            worker.stop()

            coordinator.process.wait(timeout=5)
            self.assertEqual(coordinator.process.returncode, 0)

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

    def test_oversized_extra_bytes_worker_is_rejected(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(
                coordinator.port, expect_oversized_extra_reject=True)
            try:
                worker.wait_until_oversized_extra_rejected()
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 0)
                self.assertFalse(status["running"])
            finally:
                worker.stop()

    def test_invalid_message_size_worker_is_rejected(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(
                coordinator.port, expect_invalid_message_size_reject=True)
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

    def test_unexpected_worker_message_disconnects_worker(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port,
                                   send_unexpected_message=True)
            try:
                worker.wait_until_accepted()
                worker.wait_until_unexpected_message_rejected()
                status = self.wait_until_num_peers(coordinator.port, 0)

                self.assertTrue(status["ok"])
                self.assertFalse(status["running"])
            finally:
                worker.stop()

    def test_idle_half_open_connection_does_not_block_status(self):
        with CoordinatorFixture() as coordinator:
            sock = socket.create_connection(("127.0.0.1", coordinator.port),
                                            timeout=1)
            try:
                status = self.coordinator_status(coordinator.port)

                self.assertTrue(status["ok"])
                self.assertEqual(status["num_peers"], 0)
                self.assertFalse(status["running"])
            finally:
                sock.close()

    def test_quit_command_kills_workers_and_stops_coordinator(self):
        with CoordinatorFixture() as coordinator:
            worker = WorkerProcess(coordinator.port, expect_kill=True)
            try:
                worker.wait_until_accepted()
                result = self.run_command("--json", "--coord-port",
                                          str(coordinator.port), "--quit")
                self.assertEqual(result.returncode, 0, result.stderr)
                payload = parse_dmtcp_command_json(result.stdout)

                self.assertTrue(payload["ok"])
                self.assertEqual(payload["type"], "quit")
                worker.wait_until_killed()
                coordinator.process.wait(timeout=5)
            finally:
                worker.stop()


if __name__ == "__main__":
    unittest.main()
