#!/usr/bin/env python3

import gzip
import os
import pathlib
import platform
import signal
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import autotest_config
import autotest as autotest_module
import dmtcp_test_harness as harness_module
from autotest import filter_tests_by_metadata, format_list_entry
from dmtcp_test_cases import get_test, iter_tests
from dmtcp_test_harness import (
    DMTCP_CKPT_HEADER_PADDING_OFFSET,
    DmtcpHarness,
    DmtcpStatus,
    HarnessFailure,
    ROOT,
    TestContext,
    TestResult,
    TestSpec,
    checkpoint_image_is_gzip,
    checkpoint_payload_succeeded,
    parse_dmtcp_command_json,
    validate_checkpoint_bootstrap_headers,
)


def processor_is_arm() -> bool:
    processor = platform.processor() or os.uname().machine
    return processor.startswith("arm")


def machine_is_armv7() -> bool:
    return os.uname().machine in ["armv7", "armv7l"]


class DmtcpTestHarnessUnitTest(unittest.TestCase):
    def test_status_from_json_payload(self):
        status = DmtcpStatus.from_json(
            {
                "schema_version": 1,
                "type": "status",
                "ok": True,
                "num_peers": 2,
                "running": True,
                "checkpoint_interval": 0,
            }
        )

        self.assertEqual(status.num_peers, 2)
        self.assertTrue(status.running)
        self.assertEqual(status.checkpoint_interval, 0)

    def test_parse_dmtcp_command_json_rejects_non_object(self):
        with self.assertRaises(ValueError) as caught:
            parse_dmtcp_command_json("[]")

        self.assertIn("must be an object", str(caught.exception))

    def test_parse_dmtcp_command_json_rejects_unsupported_schema(self):
        with self.assertRaises(ValueError) as caught:
            parse_dmtcp_command_json(
                '{"schema_version": 2, "type": "status", "ok": true}'
            )

        self.assertIn("unsupported dmtcp_command JSON schema",
                      str(caught.exception))

    def test_result_records_failure_phase(self):
        result = TestResult.fail("dmtcp1", "checkpoint", "no ckpt image")

        self.assertFalse(result.passed)
        self.assertEqual(result.name, "dmtcp1")
        self.assertEqual(result.phase, "checkpoint")
        self.assertEqual(result.message, "no ckpt image")

    def test_spec_accepts_multiple_peer_counts(self):
        spec = TestSpec("popen1", [1, 2], ["./test/popen1"])

        self.assertEqual(spec.peer_counts(), [1, 2])

    def test_spec_records_pre_checkpoint_delay(self):
        spec = TestSpec("file2", 1, ["./test/file2"],
                        pre_checkpoint_delay=3.0)

        self.assertEqual(spec.pre_checkpoint_delay, 3.0)

    def test_spec_default_timeout_allows_slow_checkpoint_completion(self):
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])

        self.assertEqual(spec.timeout, 30.0)

    def test_json_command_timeout_becomes_harness_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("timeout", 1, ["./test/dmtcp1"], timeout=0.01)
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            def timeout_run(*args, **kwargs):
                raise subprocess.TimeoutExpired(
                    args[0],
                    kwargs.get("timeout"),
                    output="partial stdout",
                    stderr="partial stderr",
                )

            with mock.patch.object(harness_module.subprocess, "run",
                                   timeout_run):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--status", "status",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "status")
            self.assertIn("timed out", caught.exception.message)
            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("$ dmtcp_command --json --status", transcript)
            self.assertIn("timeout=0.01", transcript)
            self.assertIn("partial stdout", transcript)
            self.assertIn("partial stderr", transcript)

    def test_json_command_schema_error_becomes_harness_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("schema", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout='{"schema_version": 2, "type": "status", "ok": true}',
                stderr="",
            )

            with mock.patch.object(harness_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--status", "status",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "status")
            self.assertIn("unsupported dmtcp_command JSON schema",
                          caught.exception.message)

    def test_start_coordinator_uses_process_group(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("process-group", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(harness_module.subprocess, "Popen") as popen, \
                 mock.patch.object(harness_module.TestContext,
                                   "_read_port_file",
                                   lambda self: 12345):
                context._start_coordinator()

            self.assertEqual(popen.call_args.kwargs["preexec_fn"], os.setpgrp)

    def test_start_coordinator_passes_extra_args(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("exit-on-last", 1, ["./test/dmtcp1"],
                            coordinator_args=["--exit-on-last"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(harness_module.subprocess, "Popen") as popen, \
                 mock.patch.object(harness_module.TestContext,
                                   "_read_port_file",
                                   lambda self: 12345):
                context._start_coordinator()

            self.assertIn("--exit-on-last", popen.call_args.args[0])

    def test_start_coordinator_records_command_transcript(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("transcript", 1, ["./test/dmtcp1"],
                            coordinator_args=["--exit-on-last"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(harness_module.subprocess, "Popen"), \
                 mock.patch.object(harness_module.TestContext,
                                   "_read_port_file",
                                   lambda self: 12345):
                context._start_coordinator()

            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=start-coordinator", transcript)
            self.assertIn("dmtcp_coordinator", transcript)
            self.assertIn("--exit-on-last", transcript)
            self.assertIn(str(work.port_file), transcript)

    def test_launch_processes_records_command_transcript(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("launch-transcript", 1, ["/bin/true"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(harness_module.subprocess, "Popen"):
                context._launch_processes()

            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=launch-worker-0", transcript)
            self.assertIn("dmtcp_launch /bin/true", transcript)

    def test_restart_records_command_transcript(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            ckpt = work.ckpt_dir / "ckpt_test.dmtcp"
            ckpt.write_bytes(b"checkpoint")
            spec = TestSpec("restart-transcript", 1, ["/bin/true"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(harness_module.subprocess, "Popen"), \
                 mock.patch.object(context, "_wait_for_status",
                                   lambda peers, running, phase: None), \
                 mock.patch.object(context, "_clear_checkpoint_dir",
                                   lambda: None):
                context._restart()

            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=restart-worker-0", transcript)
            self.assertIn("dmtcp_restart --quiet", transcript)
            self.assertIn(str(ckpt), transcript)

    def test_checkpoint_records_image_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            ckpt = work.ckpt_dir / "ckpt_test.dmtcp"
            ckpt.write_bytes(b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0")
            spec = TestSpec("checkpoint-images", 1, ["/bin/true"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(context, "_run_json_command",
                                   lambda command, phase, allow_error:
                                   {"ok": True, "type": "checkpoint"}), \
                 mock.patch.object(context, "_wait_for",
                                   lambda predicate, phase, message: None), \
                 mock.patch.object(context, "_wait_for_status",
                                   lambda peers, running, phase: None):
                context._checkpoint()

            image_log = (tmp_path / "checkpoint-images.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=checkpoint", image_log)
            self.assertIn(str(ckpt), image_log)
            self.assertIn("gzip=False", image_log)

    def test_complete_supports_kill_exit_on_last(self):
        spec = TestSpec("exit-on-last", 1, ["./test/dmtcp1"],
                        completion_command="--kill-exit-on-last")
        context = TestContext(DmtcpHarness(ROOT), spec, mock.Mock())

        with mock.patch.object(context,
                               "_kill_workers_and_wait_for_coordinator_exit") \
             as complete:
            context._complete_test()

        complete.assert_called_once_with()

    def test_cleanup_signals_worker_process_group(self):
        class FakeProcess:
            pid = 4321

            def __init__(self):
                self.waited = False

            def poll(self):
                return None if not self.waited else 0

            def wait(self, timeout):
                self.waited = True
                return 0

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("process-group", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            context.processes.append(FakeProcess())

            with mock.patch.object(context, "_kill_workers",
                                   lambda best_effort=False: None), \
                 mock.patch.object(context, "_quit_coordinator",
                                   lambda: None), \
                 mock.patch.object(harness_module.os, "killpg") as killpg:
                context.cleanup()

            self.assertEqual(killpg.call_args.args,
                             (4321, signal.SIGTERM))

    def test_unexpected_harness_exception_records_failure(self):
        def fail_run(self):
            raise ValueError("unexpected failure")

        with mock.patch.object(harness_module.TestContext, "run", fail_run), \
             mock.patch.object(harness_module.TestContext, "cleanup",
                               lambda self: None):
            result = DmtcpHarness(ROOT).run(
                TestSpec("unexpected", 1, ["./test/dmtcp1"]))

        try:
            self.assertFalse(result.passed)
            self.assertEqual(result.phase, "harness")
            self.assertIn("unexpected failure", result.message)
            self.assertIsNotNone(result.artifact_dir)
            log = (result.artifact_dir / "harness-error.log").read_text(
                encoding="utf-8")
            self.assertIn("ValueError: unexpected failure", log)
        finally:
            if result.artifact_dir is not None:
                shutil.rmtree(result.artifact_dir, ignore_errors=True)

    def test_harness_failure_records_result_summary(self):
        def fail_run(self):
            raise HarnessFailure("checkpoint", "checkpoint image missing")

        with mock.patch.object(harness_module.TestContext, "run", fail_run), \
             mock.patch.object(harness_module.TestContext, "cleanup",
                               lambda self: None):
            result = DmtcpHarness(ROOT).run(
                TestSpec("summary-failure", 1, ["./test/dmtcp1"]))

        try:
            summary = (result.artifact_dir / "result.log").read_text(
                encoding="utf-8")
            self.assertIn("name=summary-failure", summary)
            self.assertIn("passed=False", summary)
            self.assertIn("phase=checkpoint", summary)
            self.assertIn("message=checkpoint image missing", summary)
        finally:
            if result.artifact_dir is not None:
                shutil.rmtree(result.artifact_dir, ignore_errors=True)

    def test_cleanup_exception_records_cleanup_failure_after_pass(self):
        def cleanup_fails(self):
            raise RuntimeError("cleanup failed")

        with mock.patch.object(harness_module.TestContext, "run",
                               lambda self: None), \
             mock.patch.object(harness_module.TestContext, "cleanup",
                               cleanup_fails):
            result = DmtcpHarness(ROOT).run(
                TestSpec("cleanup-pass", 1, ["./test/dmtcp1"]))

        try:
            self.assertFalse(result.passed)
            self.assertEqual(result.phase, "cleanup")
            self.assertIn("cleanup failed", result.message)
            self.assertIsNotNone(result.artifact_dir)
            summary = (result.artifact_dir / "result.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=cleanup", summary)
            self.assertIn("message=cleanup failed", summary)
            log = (result.artifact_dir / "cleanup-error.log").read_text(
                encoding="utf-8")
            self.assertIn("RuntimeError: cleanup failed", log)
        finally:
            if result.artifact_dir is not None:
                shutil.rmtree(result.artifact_dir, ignore_errors=True)

    def test_cleanup_exception_keeps_original_failure_phase(self):
        def run_fails(self):
            raise HarnessFailure("checkpoint", "checkpoint failed")

        def cleanup_fails(self):
            raise RuntimeError("cleanup failed")

        with mock.patch.object(harness_module.TestContext, "run", run_fails), \
             mock.patch.object(harness_module.TestContext, "cleanup",
                               cleanup_fails):
            result = DmtcpHarness(ROOT).run(
                TestSpec("cleanup-fail", 1, ["./test/dmtcp1"]))

        try:
            self.assertFalse(result.passed)
            self.assertEqual(result.phase, "checkpoint")
            self.assertEqual(result.message, "checkpoint failed")
            self.assertIsNotNone(result.artifact_dir)
            log = (result.artifact_dir / "cleanup-error.log").read_text(
                encoding="utf-8")
            self.assertIn("RuntimeError: cleanup failed", log)
        finally:
            if result.artifact_dir is not None:
                shutil.rmtree(result.artifact_dir, ignore_errors=True)

    def test_spec_records_environment_and_launch_delay(self):
        spec = TestSpec("gzip", 1, ["./test/dmtcp1"],
                        env={"DMTCP_GZIP": "1"},
                        post_launch_delay=2.0)

        self.assertEqual(spec.env["DMTCP_GZIP"], "1")
        self.assertEqual(spec.post_launch_delay, 2.0)

    def test_spec_records_library_path_appends(self):
        spec = TestSpec("pthread_atfork1", 2, ["./test/pthread_atfork1"],
                        library_paths=["./test"])

        self.assertEqual(spec.library_paths, ["./test"])

    def test_spec_records_restart_directory_mode(self):
        spec = TestSpec("restartdir", 1, ["./test/dmtcp1"],
                        restart_uses_directory=True)

        self.assertTrue(spec.restart_uses_directory)

    def test_spec_records_extra_coordinator_args(self):
        spec = TestSpec("exit-on-last", 1, ["./test/dmtcp1"],
                        coordinator_args=["--exit-on-last"])

        self.assertEqual(spec.coordinator_args, ["--exit-on-last"])

    def test_spec_records_machine_readable_metadata(self):
        spec = TestSpec("checkpoint-header", 1, ["./test/dmtcp1"],
                        tags=["checkpoint-header"],
                        requirements=["plain-checkpoint-image"],
                        limits=["cycles=1"])

        self.assertEqual(spec.tags, ["checkpoint-header"])
        self.assertEqual(spec.requirements, ["plain-checkpoint-image"])
        self.assertEqual(spec.limits, ["cycles=1"])

    def test_autotest_list_entry_includes_machine_readable_metadata(self):
        spec = TestSpec("checkpoint-header", [1, 2], ["./test/dmtcp1"],
                        cycles=1,
                        tags=["checkpoint-header"],
                        requirements=["plain-checkpoint-image"],
                        limits=["cycles=1"])

        self.assertEqual(
            format_list_entry(spec),
            "checkpoint-header\tpeers=1,2\tcycles=1\t"
            "tags=checkpoint-header\trequires=plain-checkpoint-image\t"
            "limits=cycles=1",
        )

    def test_autotest_filters_tests_by_metadata(self):
        tests = [
            TestSpec("plain", 1, ["./test/dmtcp1"]),
            TestSpec("header", 1, ["./test/dmtcp1"],
                     tags=["checkpoint-header"],
                     requirements=["plain-checkpoint-image"]),
            TestSpec("gzip", 1, ["./test/dmtcp1"],
                     tags=["checkpoint-header", "gzip"],
                     requirements=["gzip-launcher"]),
        ]

        selected = filter_tests_by_metadata(
            tests,
            tags=["checkpoint-header"],
            requirements=["gzip-launcher"],
        )

        self.assertEqual([spec.name for spec in selected], ["gzip"])

    def test_autotest_run_rejects_empty_selection(self):
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "test" / "autotest.py"),
                "--tag",
                "missing-test-tag",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("No tests selected", result.stderr)

    def test_autotest_can_retain_success_artifacts(self):
        captured = {}
        output = []
        spec = TestSpec("retain-success", 1, ["./test/dmtcp1"])
        artifact_dir = pathlib.Path("/tmp/dmtcp-retain-success")

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False):
                captured["verbose"] = verbose
                captured["retain_success_artifacts"] = (
                    retain_success_artifacts)

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            self.assertEqual(selected_spec, spec)
            self.assertFalse(retry_once)
            return TestResult.pass_(selected_spec.name, artifact_dir)

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--retain-success-artifacts",
                                "retain-success"]), \
             mock.patch.object(autotest_module, "select_tests",
                               lambda names, tags, requirements: [spec]), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(autotest_module, "report", output.append):
            rc = autotest_module.main()

        self.assertEqual(rc, 0)
        self.assertFalse(captured["verbose"])
        self.assertTrue(captured["retain_success_artifacts"])
        self.assertIn(f"{spec.name}: artifacts={artifact_dir}", output)

    def test_spec_records_checkpoint_header_validation(self):
        spec = TestSpec("checkpoint-header", 1, ["./test/dmtcp1"],
                        validate_checkpoint_headers=True)

        self.assertTrue(spec.validate_checkpoint_headers)

    def test_spec_records_checkpoint_gzip_expectation(self):
        spec = TestSpec("gzip-invalid-env", 1, ["./test/dmtcp1"],
                        expect_checkpoint_gzip=False)

        self.assertFalse(spec.expect_checkpoint_gzip)

    def test_checkpoint_image_is_gzip_detects_magic(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "ckpt.dmtcp"
            image.write_bytes(b"\x1f\x8bpayload")

            self.assertTrue(checkpoint_image_is_gzip(image))

    def test_checkpoint_image_is_gzip_rejects_plain_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "ckpt.dmtcp"
            image.write_bytes(b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0")

            self.assertFalse(checkpoint_image_is_gzip(image))

    def test_validate_checkpoint_bootstrap_headers_accepts_matching_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "ckpt.dmtcp"
            header = bytearray(4096)
            signature = b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0"
            header[:len(signature)] = signature
            struct.pack_into("=IIII", header, 32, 4096, 1, 8, 0x01020304)
            image.write_bytes(bytes(header) + bytes(header) + b"payload")

            validate_checkpoint_bootstrap_headers(image)

    def test_validate_checkpoint_bootstrap_headers_accepts_gzip_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "ckpt.dmtcp"
            header = bytearray(4096)
            signature = b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0"
            header[:len(signature)] = signature
            struct.pack_into("=IIII", header, 32, 4096, 1, 8, 0x01020304)
            with gzip.open(image, "wb") as out:
                out.write(bytes(header) + bytes(header) + b"payload")

            validate_checkpoint_bootstrap_headers(image)

    def test_validate_checkpoint_bootstrap_headers_rejects_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "ckpt.dmtcp"
            header = bytearray(4096)
            signature = b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0"
            header[:len(signature)] = signature
            struct.pack_into("=IIII", header, 32, 4096, 1, 8, 0x01020304)
            second = bytearray(header)
            second[128] = 1
            image.write_bytes(bytes(header) + bytes(second) + b"payload")

            with self.assertRaises(HarnessFailure) as caught:
                validate_checkpoint_bootstrap_headers(image)

            self.assertEqual(caught.exception.phase, "checkpoint-header")
            self.assertIn("bootstrap records differ", caught.exception.message)

    def test_validate_checkpoint_bootstrap_headers_rejects_reserved_padding(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = pathlib.Path(tmp) / "ckpt.dmtcp"
            header = bytearray(4096)
            signature = b"DMTCP_CHECKPOINT_IMAGE_v5.0\n\0"
            header[:len(signature)] = signature
            struct.pack_into("=IIII", header, 32, 4096, 1, 8, 0x01020304)
            header[DMTCP_CKPT_HEADER_PADDING_OFFSET] = 1
            image.write_bytes(bytes(header) + bytes(header) + b"payload")

            with self.assertRaises(HarnessFailure) as caught:
                validate_checkpoint_bootstrap_headers(image)

            self.assertEqual(caught.exception.phase, "checkpoint-header")
            self.assertIn("reserved padding", caught.exception.message)

    def test_kcheckpoint_accepts_not_running_after_kill(self):
        spec = TestSpec("syscall-tester", 1, ["./test/syscall-tester"],
                        checkpoint_command="--kcheckpoint")

        self.assertTrue(checkpoint_payload_succeeded(
            spec,
            {"ok": False, "error_code": "not_running"},
        ))

    def test_registry_contains_dmtcp1(self):
        tests = list(iter_tests())

        self.assertIn("dmtcp1", [test.name for test in tests])
        dmtcp1 = get_test("dmtcp1")
        self.assertEqual(dmtcp1.peers, 1)
        self.assertEqual(dmtcp1.commands, ["./test/dmtcp1"])

    def test_registry_contains_default_flow_smoke_tests(self):
        names = [test.name for test in iter_tests()]

        for name in [
            "dmtcp2", "dmtcp3", "dmtcp4", "alarm", "sched_test",
            "coordinator-barrier", "gettid", "file1", "file3", "stat",
            "mmap1", "mremap", "gettimeofday", "sigchild",
            "rlimit-restore", "poll", "environ", "realpath", "pthread1",
            "pthread2", "pthread4", "pthread5", "pthread6", "mutex1",
            "mutex2", "mutex3", "mutex4", "timer1", "clock", "dlopen1",
            "dmtcp5", "shared-fd1", "shared-fd2", "stale-fd",
            "rlimit-nofile", "procfd1", "epoll1", "forkexec",
            "client-server", "seqpacket", "shared-memory1", "shared-memory2",
            "sysv-shm1", "sysv-shm2", "sysv-sem", "sysv-msg",
            "syscall-tester", "file2", "presuspend", "plugin-sleep2",
            "plugin-init", "popen1", "poll-disable-event-plugin", "pthread3",
            "restartdir", "pty1", "pty2", "vfork1", "vfork2", "frisbee",
            "nocheckpoint", "checkpoint-header", "gzip-invalid-env",
            "coordinator-exit-on-last", "command-json-bcheckpoint",
        ]:
            self.assertIn(name, names)

        syscall_tester = get_test("syscall-tester")
        self.assertEqual(syscall_tester.checkpoint_command, "--kcheckpoint")
        self.assertEqual(syscall_tester.commands,
                         ["--checkpoint-open-files ./test/syscall-tester"])
        coordinator_barrier = get_test("coordinator-barrier")
        self.assertEqual(coordinator_barrier.peers, 2)
        self.assertEqual(coordinator_barrier.cycles, 1)
        self.assertIn("coordinator", coordinator_barrier.tags)
        self.assertIn("barrier", coordinator_barrier.tags)
        self.assertIn("real-worker", coordinator_barrier.requirements)
        self.assertIn("cycles=1", coordinator_barrier.limits)
        restartdir = get_test("restartdir")
        self.assertTrue(restartdir.restart_uses_directory)
        frisbee = get_test("frisbee")
        self.assertEqual(len(frisbee.commands), 3)
        self.assertEqual(frisbee.env["DMTCP_GZIP"], "1")
        nocheckpoint = get_test("nocheckpoint")
        self.assertEqual(nocheckpoint.cycles, 1)
        checkpoint_header = get_test("checkpoint-header")
        self.assertTrue(checkpoint_header.validate_checkpoint_headers)
        self.assertEqual(checkpoint_header.env["DMTCP_GZIP"], "0")
        self.assertFalse(checkpoint_header.expect_checkpoint_gzip)
        self.assertIn("checkpoint-header", checkpoint_header.tags)
        self.assertIn("cycles=1", checkpoint_header.limits)
        invalid_gzip = get_test("gzip-invalid-env")
        self.assertEqual(invalid_gzip.cycles, 1)
        self.assertEqual(invalid_gzip.env["DMTCP_GZIP"], "12x")
        self.assertFalse(invalid_gzip.expect_checkpoint_gzip)
        self.assertIn("cycles=1", invalid_gzip.limits)
        exit_on_last = get_test("coordinator-exit-on-last")
        self.assertEqual(exit_on_last.cycles, 0)
        self.assertEqual(exit_on_last.coordinator_args, ["--exit-on-last"])
        self.assertEqual(exit_on_last.completion_command,
                         "--kill-exit-on-last")
        self.assertIn("coordinator", exit_on_last.tags)
        self.assertIn("exit-on-last", exit_on_last.tags)
        self.assertIn("real-worker", exit_on_last.requirements)
        self.assertIn("cycles=0", exit_on_last.limits)
        bcheckpoint = get_test("command-json-bcheckpoint")
        self.assertEqual(bcheckpoint.checkpoint_command, "--bcheckpoint")
        self.assertEqual(bcheckpoint.cycles, 1)
        self.assertIn("command-json", bcheckpoint.tags)
        self.assertIn("real-worker", bcheckpoint.requirements)
        self.assertIn("cycles=1", bcheckpoint.limits)

    def test_registry_contains_configured_optional_tests(self):
        names = [test.name for test in iter_tests()]

        if autotest_config.HAS_EPOLL_CREATE1 == "yes":
            self.assertIn("epoll2", names)
        if not autotest_config.USE_M32:
            self.assertIn("dlopen2", names)
        if autotest_config.HAS_CMA == "yes":
            self.assertIn("cma", names)
        if not processor_is_arm() and autotest_config.TEST_POSIX_MQ == "yes":
            self.assertIn("posix-mq1", names)
        if not processor_is_arm() and autotest_config.HAS_SYS_MQ_OPEN == "yes":
            self.assertIn("posix-mq-close-untracked", names)
        if not machine_is_armv7():
            self.assertIn("pthread_atfork1", names)
            self.assertIn("pthread_atfork2", names)
            atfork = get_test("pthread_atfork1")
            self.assertIn(f"{ROOT}/test", atfork.library_paths)
        if not autotest_config.USE_M32:
            self.assertIn("waitpid", names)
            self.assertIn("waitid-syscall", names)
            self.assertIn("gzip", names)
            self.assertIn("perl", names)
            self.assertIn("bash", names)
            if (autotest_config.HAS_PYTHON == "yes" or
                    autotest_config.HAS_PYTHON3 == "yes"):
                self.assertIn("python", names)
            if autotest_config.HAS_DASH == "yes":
                self.assertIn("dash", names)
            if autotest_config.HAS_ZSH == "yes":
                self.assertIn("zsh", names)
            gzip = get_test("gzip")
            self.assertEqual(gzip.env["DMTCP_GZIP"], "1")
            self.assertTrue(gzip.validate_checkpoint_headers)
        if autotest_config.HAS_JAVA == "yes" and autotest_config.HAS_JAVAC == "yes":
            self.assertIn("java1", names)
            java1 = get_test("java1")
            self.assertEqual(java1.env["CLASSPATH"], "./test")
        if autotest_config.HAS_OPENMP == "yes":
            self.assertIn("openmp-1", names)
            self.assertIn("openmp-2", names)
            self.assertEqual(get_test("openmp-1").cycles, 1)
            self.assertEqual(get_test("openmp-2").cycles, 1)

    def test_parity_ledger_mentions_authoritative_registry_tests(self):
        ledger = (ROOT / "test" / "autotest-parity.md").read_text(
            encoding="utf-8")

        missing = [
            test.name for test in iter_tests()
            if f"`{test.name}`" not in ledger
        ]

        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
