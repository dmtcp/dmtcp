#!/usr/bin/env python3

import contextlib
import io
import os
import pathlib
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import autotest_config
import autotest as autotest_module
from autotest import (
    format_list_entry,
    list_header,
    list_groups,
    list_has_notes,
    list_column_widths,
    list_separator,
)
from autotest import (
    DEFAULT_SLOW_PRE_CHECKPOINT_DELAY,
    DmtcpCommandJson,
    DmtcpHarness,
    DmtcpStatus,
    HarnessFailure,
    CommandTestSpec,
    REGISTRY,
    ROOT,
    SLOW_FACTOR_BASE,
    TestContext,
    TestRegistry,
    TestResult,
    TestSpec,
)


def assert_registered_unless_m32_disabled(testcase, names, name):
    if autotest_config.USE_M32 and \
            name in TestRegistry.M32_DISABLED_TESTS:
        testcase.assertNotIn(name, names)
    else:
        testcase.assertIn(name, names)


def assert_registered_when_path_exists(testcase, names, name, path):
    if pathlib.Path(path).exists():
        assert_registered_unless_m32_disabled(testcase, names, name)
    else:
        testcase.assertNotIn(name, names)


def assert_registered_when_repo_file_exists(testcase, names, name, path):
    if (ROOT / path).exists():
        assert_registered_unless_m32_disabled(testcase, names, name)
    else:
        testcase.assertNotIn(name, names)


def assert_registered_unless_aarch64_disabled(testcase, names, name):
    if getattr(autotest_config, "AARCH64_HOST", "no") == "yes":
        testcase.assertNotIn(name, names)
    else:
        assert_registered_unless_m32_disabled(testcase, names, name)


class DmtcpTestHarnessUnitTest(unittest.TestCase):
    def test_status_from_json_payload(self):
        status = DmtcpStatus.from_json(
            {
                "schema_version": 1,
                "command": "DMT_STATUS",
                "command_status": "DMT_COORD_SUCCESS",
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
            DmtcpCommandJson.parse("[]")

        self.assertIn("must be an object", str(caught.exception))

    def test_parse_dmtcp_command_json_rejects_unsupported_schema(self):
        with self.assertRaises(ValueError) as caught:
            DmtcpCommandJson.parse(
                '{"schema_version": 2, "command": "DMT_STATUS"}'
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

    def test_spec_can_request_worker_replacement(self):
        spec = TestSpec("coordinator-replacement-worker", 2,
                        ["./test/dmtcp1", "./test/dmtcp1"],
                        cycles=0,
                        replace_worker_index=0)

        self.assertEqual(spec.replace_worker_index, 0)

    def test_spec_can_request_restart_rejection_while_running(self):
        spec = TestSpec("coordinator-reject-restart-while-running", 1,
                        ["./test/dmtcp1"], cycles=0,
                        reject_restart_while_running=True)

        self.assertTrue(spec.reject_restart_while_running)

    def test_spec_can_request_pty_launch_mode(self):
        spec = TestSpec("emacs", 1, ["/usr/bin/emacs -nw"],
                        launch_mode="pty")

        self.assertEqual(spec.launch_mode, "pty")

    def test_spec_can_create_private_environment_directories(self):
        spec = TestSpec("screen", 1, ["/usr/bin/screen"],
                        private_env_dirs={"SCREENDIR": "screen"})

        self.assertEqual(spec.private_env_dirs, {"SCREENDIR": "screen"})

    def test_spec_can_record_configure_flags_and_required_files(self):
        spec = TestSpec("java1", 1, ["java -Xmx5M java1"],
                        configure_flags=["HAS_JAVA", "HAS_JAVAC"],
                        blocked_configure_flags=["ARM_HOST"],
                        required_files=["test/java1.class"])

        self.assertEqual(spec.configure_flags, ["HAS_JAVA", "HAS_JAVAC"])
        self.assertEqual(spec.blocked_configure_flags, ["ARM_HOST"])
        self.assertEqual(spec.required_files, ["test/java1.class"])

    def test_registry_class_exposes_filtered_specs(self):
        registry = TestRegistry()

        self.assertIn("dmtcp1", [test.name for test in registry.select()])
        self.assertEqual(registry.get_test("dmtcp1").commands,
                         ["./test/dmtcp1"])

    def test_replacement_worker_spec_is_registered(self):
        spec = REGISTRY.get_test("coordinator-replacement-worker")

        self.assertEqual(spec.replace_worker_index, 0)
        self.assertIn("coordinator", spec.tags)
        self.assertIn("replacement-worker", spec.tags)
        self.assertIn("real-worker", spec.requirements)
        self.assertIn("cycles=0", spec.limits)

    def test_restart_rejection_spec_is_registered(self):
        spec = REGISTRY.get_test("coordinator-reject-restart-while-running")

        self.assertTrue(spec.reject_restart_while_running)
        self.assertEqual(spec.cycles, 0)
        self.assertIn("coordinator", spec.tags)
        self.assertIn("restart-rejection", spec.tags)
        self.assertIn("real-worker", spec.requirements)
        self.assertIn("cycles=0", spec.limits)

    def test_spec_default_timeout_allows_slow_checkpoint_completion(self):
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])

        self.assertEqual(spec.timeout, 30.0)

    def test_harness_slow_count_scales_timing_without_mutating_spec(self):
        spec = TestSpec("file2", 1, ["./test/file2"],
                        timeout=4.0,
                        pre_checkpoint_delay=3.0)
        harness = DmtcpHarness(ROOT, slow_count=1)

        scaled = harness._scaled_spec(spec)

        self.assertEqual(harness.slow_factor, SLOW_FACTOR_BASE)
        self.assertEqual(scaled.timeout, 20.0)
        self.assertEqual(scaled.pre_checkpoint_delay, 15.0)
        self.assertEqual(spec.timeout, 4.0)
        self.assertEqual(spec.pre_checkpoint_delay, 3.0)

    def test_harness_slow_count_adds_default_checkpoint_delay(self):
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])
        harness = DmtcpHarness(ROOT, slow_count=1)

        scaled = harness._scaled_spec(spec)

        self.assertEqual(scaled.pre_checkpoint_delay,
                         DEFAULT_SLOW_PRE_CHECKPOINT_DELAY * SLOW_FACTOR_BASE)

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

            with mock.patch.object(autotest_module.subprocess, "run",
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
                stdout='{"schema_version": 2, "command": "DMT_STATUS"}',
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--status", "status",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "status")
            self.assertIn("unsupported dmtcp_command JSON schema",
                          caught.exception.message)

    def test_json_command_mixed_output_becomes_harness_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("mixed-json", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout=(
                    "human status line\n"
                    '{"schema_version": 1, "command": "DMT_STATUS", '
                    '"command_status": "DMT_COORD_SUCCESS"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--status", "status",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "status")
            self.assertIn("mixed human/JSON output", caught.exception.message)
            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("human status line", transcript)
            self.assertIn('"schema_version": 1', transcript)

    def test_json_command_wrong_command_becomes_phase_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("wrong-command", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_STATUS", '
                    '"command_status": "DMT_COORD_SUCCESS"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--checkpoint", "checkpoint",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "checkpoint")
            self.assertIn("expected JSON command 'DMT_CHECKPOINT'",
                          caught.exception.message)

    def test_json_command_rejects_redundant_phase_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("wrong-phase", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_KILL", '
                    '"phase": "DMT_CHECKPOINT", '
                    '"command_status": "DMT_COORD_SUCCESS"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--kill", "kill",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "kill")
            self.assertIn("field phase is redundant",
                          caught.exception.message)

    def test_json_command_rejects_ok_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("stale-ok", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_KILL", '
                    '"command_status": "DMT_COORD_SUCCESS", '
                    '"ok": true}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--kill", "kill",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "kill")
            self.assertIn("field ok is obsolete", caught.exception.message)

    def test_json_command_rejects_type_field(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("stale-type", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_KILL", '
                    '"command_status": "DMT_COORD_SUCCESS", '
                    '"type": "DMT_KILL"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--kill", "kill",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "kill")
            self.assertIn("field type has been replaced by command",
                          caught.exception.message)

    def test_json_command_payload_requires_command_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("missing-command-status", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 2,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_CHECKPOINT"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--checkpoint", "checkpoint",
                                              allow_error=True)

            self.assertEqual(caught.exception.phase, "checkpoint")
            self.assertIn("command_status", caught.exception.message)

    def test_json_command_rejects_non_string_command_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("bad-command-status", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 2,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_CHECKPOINT", '
                    '"command_status": 17}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--checkpoint", "checkpoint",
                                              allow_error=True)

            self.assertEqual(caught.exception.phase, "checkpoint")
            self.assertIn("command_status", caught.exception.message)

    def test_json_command_disallows_error_payload_when_not_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("disallow-error", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 0,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_KILL", '
                    '"command_status": "DMT_COORD_NOT_RUNNING"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--kill", "kill",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "kill")
            self.assertIn("DMT_COORD_NOT_RUNNING", caught.exception.message)

    def test_json_command_rejects_nonzero_exit_with_success_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("nonzero-success", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            result = subprocess.CompletedProcess(
                ["dmtcp_command"], 17,
                stdout=(
                    '{"schema_version": 1, "command": "DMT_STATUS", '
                    '"command_status": "DMT_COORD_SUCCESS"}'
                ),
                stderr="",
            )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   return_value=result):
                with self.assertRaises(HarnessFailure) as caught:
                    context._run_json_command("--status", "status",
                                              allow_error=False)

            self.assertEqual(caught.exception.phase, "status")
            self.assertIn("exit code 17 despite DMT_COORD_SUCCESS",
                          caught.exception.message)
            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("exit=17", transcript)

    def test_json_command_rejects_non_string_coordinator_host(self):
        with self.assertRaises(ValueError) as caught:
            DmtcpCommandJson.validate_result(
                {
                    "schema_version": 1,
                    "command": "DMT_STATUS",
                    "command_status": "DMT_COORD_SUCCESS",
                    "coordinator_host": 123,
                },
                "DMT_STATUS",
            )

        self.assertIn("coordinator_host must be a string",
                      str(caught.exception))

    def test_json_command_rejects_non_integer_coordinator_port(self):
        with self.assertRaises(ValueError) as caught:
            DmtcpCommandJson.validate_result(
                {
                    "schema_version": 1,
                    "command": "DMT_STATUS",
                    "command_status": "DMT_COORD_SUCCESS",
                    "coordinator_port": "7779",
                },
                "DMT_STATUS",
            )

        self.assertIn("coordinator_port must be an integer",
                      str(caught.exception))

    def test_status_payload_error_becomes_status_phase_failure(self):
        context = TestContext(
            DmtcpHarness(ROOT),
            TestSpec("status-payload", 1, ["./test/dmtcp1"]),
            mock.Mock(),
        )

        with mock.patch.object(context, "_run_json_command",
                               return_value={
                                   "schema_version": 1,
                                   "command": "DMT_STATUS",
                                   "command_status": "DMT_COORD_SUCCESS",
                               }):
            with self.assertRaises(HarnessFailure) as caught:
                context._status()

        self.assertEqual(caught.exception.phase, "status")
        self.assertIn("missing status JSON field", caught.exception.message)

    def test_status_payload_rejects_non_boolean_running_field(self):
        context = TestContext(
            DmtcpHarness(ROOT),
            TestSpec("status-payload", 1, ["./test/dmtcp1"]),
            mock.Mock(),
        )

        with mock.patch.object(context, "_run_json_command",
                               return_value={
                                   "schema_version": 1,
                                   "command": "DMT_STATUS",
                                   "command_status": "DMT_COORD_SUCCESS",
                                   "num_peers": 1,
                                   "running": "false",
                                   "checkpoint_interval": 0,
                               }):
            with self.assertRaises(HarnessFailure) as caught:
                context._status()

        self.assertEqual(caught.exception.phase, "status")
        self.assertIn("running must be a boolean", caught.exception.message)

    def test_status_payload_rejects_non_integer_count_fields(self):
        for field_name in ("num_peers", "checkpoint_interval"):
            with self.subTest(field=field_name):
                payload = {
                    "schema_version": 1,
                    "command": "DMT_STATUS",
                    "command_status": "DMT_COORD_SUCCESS",
                    "num_peers": 1,
                    "running": True,
                    "checkpoint_interval": 0,
                }
                payload[field_name] = "1"
                context = TestContext(
                    DmtcpHarness(ROOT),
                    TestSpec("status-payload", 1, ["./test/dmtcp1"]),
                    mock.Mock(),
                )

                with mock.patch.object(context, "_run_json_command",
                                       return_value=payload):
                    with self.assertRaises(HarnessFailure) as caught:
                        context._status()

                self.assertEqual(caught.exception.phase, "status")
                self.assertIn(f"{field_name} must be an integer",
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

            with mock.patch.object(autotest_module.subprocess, "Popen") as popen, \
                 mock.patch.object(autotest_module.TestContext,
                                   "_read_port_file",
                                   lambda self: 12345):
                context._start_coordinator()

            self.assertEqual(popen.call_args.kwargs["preexec_fn"], os.setpgrp)

    def test_start_coordinator_records_command_transcript(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("transcript", 1, ["./test/dmtcp1"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(autotest_module.subprocess, "Popen"), \
                 mock.patch.object(autotest_module.TestContext,
                                   "_read_port_file",
                                   lambda self: 12345):
                context._start_coordinator()

            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=start-coordinator", transcript)
            self.assertIn("dmtcp_coordinator", transcript)
            self.assertIn(str(work.port_file), transcript)

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

            with mock.patch.object(autotest_module.subprocess, "Popen") as popen, \
                 mock.patch.object(autotest_module.TestContext,
                                   "_read_port_file",
                                   lambda self: 12345):
                context._start_coordinator()

            self.assertIn("--exit-on-last", popen.call_args.args[0])

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

            with mock.patch.object(autotest_module.subprocess, "Popen"):
                context._launch_processes()

            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=launch-worker-0", transcript)
            self.assertIn("dmtcp_launch /bin/true", transcript)

    def test_launch_processes_expands_workdir_placeholder(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("workdir-template", 1,
                            ["/bin/true {workdir}/output.txt"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            with mock.patch.object(autotest_module.subprocess, "Popen"):
                context._launch_processes()

            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn(f"{tmp_path}/output.txt", transcript)
            self.assertNotIn("{workdir}", transcript)

    def test_launch_processes_can_use_a_pty(self):
        class FakeProcess:
            pid = 1234

            def poll(self):
                return None

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("pty-launch", 1, ["/bin/cat"],
                            launch_mode="pty")
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            fake_process = FakeProcess()

            with mock.patch.object(context, "_spawn_pty_process",
                                   return_value=fake_process) as spawn, \
                 mock.patch.object(autotest_module.subprocess, "Popen") \
                 as popen:
                context._launch_processes()

            popen.assert_not_called()
            spawn.assert_called_once_with(
                [str(context.harness.launch), "/bin/cat"],
                tmp_path / "worker-0.out",
                "launch-worker-0",
            )
            self.assertEqual(context.processes, [fake_process])

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

            with mock.patch.object(autotest_module.subprocess, "Popen"), \
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

    def test_restart_can_use_a_pty(self):
        class FakeProcess:
            pid = 5678

            def poll(self):
                return None

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            ckpt = work.ckpt_dir / "ckpt_test.dmtcp"
            ckpt.write_bytes(b"checkpoint")
            spec = TestSpec("pty-restart", 1, ["/bin/cat"],
                            launch_mode="pty")
            context = TestContext(DmtcpHarness(ROOT), spec, work)
            fake_process = FakeProcess()

            with mock.patch.object(context, "_spawn_pty_process",
                                   return_value=fake_process) as spawn, \
                 mock.patch.object(autotest_module.subprocess, "Popen") \
                 as popen, \
                 mock.patch.object(context, "_wait_for_status",
                                   lambda peers, running, phase: None), \
                 mock.patch.object(context, "_clear_checkpoint_dir",
                                   lambda: None):
                context._restart()

            popen.assert_not_called()
            spawn.assert_called_once_with(
                [str(context.harness.restart), "--quiet", str(ckpt)],
                tmp_path / "restart-0.out",
                "restart-worker-0",
            )
            self.assertEqual(context.processes, [fake_process])

    def test_restart_while_running_records_rejected_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            ckpt = work.ckpt_dir / "ckpt_test.dmtcp"
            ckpt.write_bytes(b"checkpoint")
            spec = TestSpec("restart-while-running", 1, ["/bin/true"])
            context = TestContext(DmtcpHarness(ROOT), spec, work)

            def rejected_restart(*args, **kwargs):
                return subprocess.CompletedProcess(
                    args[0],
                    1,
                    stdout="",
                    stderr="restart rejected while computation is running\n",
                )

            with mock.patch.object(autotest_module.subprocess, "run",
                                   rejected_restart), \
                 mock.patch.object(context, "_assert_status") as status:
                context._assert_restart_rejected_while_running()

            status.assert_called_once_with([1], True,
                                           "restart-while-running")
            transcript = (tmp_path / "commands.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=restart-while-running", transcript)
            self.assertIn("exit=1", transcript)
            self.assertIn("dmtcp_restart --quiet", transcript)
            self.assertIn("restart rejected while computation is running",
                          transcript)

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
                                   lambda command, phase, allow_error,
                                   expected_command=None:
                                   {
                                       "command": "DMT_CHECKPOINT",
                                       "command_status": "DMT_COORD_SUCCESS",
                                   }), \
                 mock.patch.object(context, "_wait_for",
                                   lambda predicate, phase, message: None), \
                 mock.patch.object(context, "_wait_for_status",
                                   lambda peers, running, phase: None):
                context._checkpoint()

            image_log = (tmp_path / "checkpoint-images.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=checkpoint", image_log)
            self.assertIn(str(ckpt), image_log)

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
                 mock.patch.object(autotest_module.os, "killpg") as killpg:
                context.cleanup()

            self.assertEqual(killpg.call_args.args,
                             (4321, signal.SIGTERM))

    def test_unexpected_harness_exception_records_failure(self):
        def fail_run(self):
            raise ValueError("unexpected failure")

        with mock.patch.object(autotest_module.TestContext, "run", fail_run), \
             mock.patch.object(autotest_module.TestContext, "cleanup",
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

        with mock.patch.object(autotest_module.TestContext, "run", fail_run), \
             mock.patch.object(autotest_module.TestContext, "cleanup",
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

        with mock.patch.object(autotest_module.TestContext, "run",
                               lambda self: None), \
             mock.patch.object(autotest_module.TestContext, "cleanup",
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

        with mock.patch.object(autotest_module.TestContext, "run", run_fails), \
             mock.patch.object(autotest_module.TestContext, "cleanup",
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

    def test_failed_run_records_process_status_around_cleanup(self):
        class FakeProcess:
            def __init__(self, pid, returncode):
                self.pid = pid
                self.returncode = returncode

            def poll(self):
                return self.returncode

        def run_fails(self):
            self.coordinator_proc = FakeProcess(1234, None)
            self.processes.append(FakeProcess(2345, 7))
            raise HarnessFailure("checkpoint", "checkpoint failed")

        with mock.patch.object(autotest_module.TestContext, "run", run_fails), \
             mock.patch.object(autotest_module.TestContext, "cleanup",
                               lambda self: None):
            result = DmtcpHarness(ROOT).run(
                TestSpec("process-status", 1, ["./test/dmtcp1"]))

        try:
            self.assertFalse(result.passed)
            self.assertIsNotNone(result.artifact_dir)
            status = (result.artifact_dir / "processes.log").read_text(
                encoding="utf-8")
            self.assertIn("phase=before-cleanup", status)
            self.assertIn("phase=after-cleanup", status)
            self.assertIn(
                "role=coordinator pid=1234 state=running returncode=None",
                status)
            self.assertIn(
                "role=worker index=0 pid=2345 state=exited returncode=7",
                status)
        finally:
            if result.artifact_dir is not None:
                shutil.rmtree(result.artifact_dir, ignore_errors=True)

    def test_spec_records_environment_and_launch_delay(self):
        spec = TestSpec("gzip", 1, ["./test/dmtcp1"],
                        env={"DMTCP_GZIP": "1"},
                        post_launch_delay=2.0)

        self.assertEqual(spec.env["DMTCP_GZIP"], "1")
        self.assertEqual(spec.post_launch_delay, 2.0)

    def test_make_env_creates_private_environment_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec("screen", 1, ["/usr/bin/screen"],
                            private_env_dirs={"SCREENDIR": "screen"})

            context = TestContext(DmtcpHarness(ROOT), spec, work)

            screen_dir = tmp_path / "screen"
            self.assertEqual(context.env["SCREENDIR"], str(screen_dir))
            self.assertTrue(screen_dir.is_dir())
            self.assertEqual(screen_dir.stat().st_mode & 0o777, 0o700)

    def test_make_env_expands_workname_for_private_environment_directories(self):
        with tempfile.TemporaryDirectory() as tmp, \
             tempfile.TemporaryDirectory() as private_tmp:
            tmp_path = pathlib.Path(tmp) / "dmtcp-screen-test"
            tmp_path.mkdir()
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            spec = TestSpec(
                "screen", 1, ["/usr/bin/screen"],
                private_env_dirs={
                    "SCREENDIR": f"{private_tmp}/{{workname}}-screen",
                },
            )

            context = TestContext(DmtcpHarness(ROOT), spec, work)
            expected = pathlib.Path(private_tmp) / "dmtcp-screen-test-screen"

            self.assertEqual(context.env["SCREENDIR"], str(expected))
            self.assertTrue(expected.is_dir())
            context.cleanup()
            self.assertFalse(expected.exists())

    def test_make_env_rejects_private_environment_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            work = mock.Mock()
            work.path = tmp_path
            work.ckpt_dir = tmp_path / "ckpt"
            work.ckpt_dir.mkdir()
            work.port_file = tmp_path / "port"
            target = tmp_path / "target"
            target.mkdir()
            link = tmp_path / "screen"
            link.symlink_to(target)
            spec = TestSpec("screen", 1, ["/usr/bin/screen"],
                            private_env_dirs={"SCREENDIR": str(link)})

            with self.assertRaisesRegex(HarnessFailure, "symlink"):
                TestContext(DmtcpHarness(ROOT), spec, work)

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

    def test_autotest_list_entry_includes_user_facing_notes(self):
        spec = TestSpec("checkpoint-header", [1, 2], ["./test/dmtcp1"],
                        cycles=1,
                        category="Checkpoint mechanics",
                        tags=["checkpoint-header", "slow", "pty"],
                        requirements=["plain-checkpoint-image"],
                        limits=["cycles=1"])
        widths = list_column_widths([spec], include_notes=True)

        self.assertEqual(
            format_list_entry(list_header(include_notes=True), widths,
                              include_notes=True),
            "Test               Notes",
        )
        self.assertEqual(
            list_separator(widths),
            "-----------------  -----------------------",
        )
        self.assertEqual(
            format_list_entry(spec, widths, include_notes=True, indent="  "),
            "  checkpoint-header  1 checkpoint, slow, pty",
        )

    def test_autotest_list_omits_notes_column_when_no_tests_have_notes(self):
        specs = [
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
        ]
        widths = list_column_widths(specs, include_notes=False)

        self.assertFalse(list_has_notes(specs))
        self.assertEqual(
            format_list_entry(list_header(include_notes=False), widths,
                              include_notes=False),
            "Test",
        )
        self.assertEqual(
            format_list_entry(specs[0], widths, include_notes=False,
                              indent="  "),
            "  dmtcp1",
        )

    def test_autotest_list_groups_and_sorts_tests_by_category(self):
        tests = [
            TestSpec("zsh", 1, ["/bin/zsh"], category="Interactive shells"),
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"],
                     category="Single-process programs"),
            TestSpec("bash", 1, ["/bin/bash"],
                     category="Interactive shells"),
        ]

        groups = list_groups(tests)

        self.assertEqual(
            [(category, [test.name for test in grouped_tests])
             for category, grouped_tests in groups],
            [
                ("Single-process programs", ["dmtcp1"]),
                ("Interactive shells", ["bash", "zsh"]),
            ],
        )

    def test_integration_progress_printer_streams_activity_status(self):
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])
        progress = autotest_module.IntegrationProgressPrinter()
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            progress.start_test(spec, verbose=False)
            progress.event("ckpt-start")
            progress.event("ckpt-passed")
            progress.event("rstr-start")
            progress.event("rstr-passed")
            progress.event("cycle-separator")
            progress.event("ckpt-start")
            progress.event("ckpt-passed")
            progress.event("rstr-start")
            progress.event("rstr-passed")
            progress.finish_result(spec, TestResult.pass_(spec.name))

        self.assertEqual(
            stdout.getvalue(),
            "  dmtcp1         ckpt:PASSED; rstr:PASSED -> "
            "ckpt:PASSED; rstr:PASSED\n",
        )

    def test_integration_progress_printer_reports_zero_cycle_pass(self):
        spec = TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"],
                        cycles=0,
                        category=autotest_module.COORDINATOR_PROTOCOL_CATEGORY)
        progress = autotest_module.IntegrationProgressPrinter(
            name_width=len("exit-on-last"))
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            progress.start_test(spec, verbose=False)
            progress.finish_result(spec, TestResult.pass_(spec.name))

        self.assertEqual(stdout.getvalue(),
                         "  exit-on-last run:PASSED\n")

    def test_integration_progress_printer_reports_active_activity_failure(self):
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])
        progress = autotest_module.IntegrationProgressPrinter()
        stdout = io.StringIO()

        with mock.patch.object(autotest_module, "COLOR_MODE", "always"), \
             contextlib.redirect_stdout(stdout):
            progress.start_test(spec, verbose=False)
            progress.event("ckpt-start")
            progress.finish_result(
                spec,
                TestResult.fail("dmtcp1", "checkpoint", "no ckpt image"),
            )

        self.assertEqual(
            stdout.getvalue(),
            "  dmtcp1         ckpt:\033[0;31mFAILED phase=checkpoint "
            "msg=no ckpt image\033[0m\n",
        )

    def test_autotest_failed_run_status_is_red(self):
        result = TestResult.fail("dmtcp1", "checkpoint", "no ckpt image")

        with mock.patch.object(autotest_module, "COLOR_MODE", "always"):
            self.assertEqual(
                autotest_module.format_failed_run_status(result),
                "\033[0;31mckpt:FAILED phase=checkpoint "
                "msg=no ckpt image\033[0m",
            )

    def test_autotest_color_never_omits_red_escape_codes(self):
        result = TestResult.fail("dmtcp1", "checkpoint", "no ckpt image")

        with mock.patch.object(autotest_module, "COLOR_MODE", "never"):
            self.assertEqual(
                autotest_module.format_failed_run_status(result),
                "ckpt:FAILED phase=checkpoint msg=no ckpt image",
            )

    def test_autotest_auto_color_honors_plain_output_environment(self):
        with mock.patch.object(autotest_module, "COLOR_MODE", "auto"):
            with mock.patch.dict(os.environ, {"NO_COLOR": "1"}):
                self.assertEqual(autotest_module.red_text("FAIL"), "FAIL")
            with mock.patch.dict(os.environ, {"TERM": "dumb"}, clear=True):
                self.assertEqual(autotest_module.red_text("FAIL"), "FAIL")

    def test_command_run_status_uses_sentence_style(self):
        passed = TestResult.pass_("autotest-unit", message="Ran 99 tests")
        failed = TestResult.fail("autotest-unit", "run", "exit=1")

        with mock.patch.object(autotest_module, "COLOR_MODE", "never"):
            self.assertEqual(
                autotest_module.format_command_run_status(passed),
                "PASS. Ran 99 tests",
            )
            self.assertEqual(
                autotest_module.format_command_run_status(failed),
                "FAIL. exit=1",
            )

    def test_command_failure_details_are_capped_and_actionable(self):
        spec = CommandTestSpec(
            "autotest-unit",
            "Harness tests",
            "harness",
            [sys.executable, "./autotest_test.py"],
        )
        result = TestResult.fail(
            spec.name,
            "run",
            "exit=1",
            details="\n".join(f"line {index}" for index in range(25)),
        )

        lines = autotest_module.command_failure_detail_lines(
            spec, result, max_output_lines=3)

        self.assertEqual(lines[:3], [
            f"command: {sys.executable} ./autotest_test.py",
            f"cwd: {ROOT / 'test'}",
            "output tail (last 3 lines):",
        ])
        self.assertEqual(lines[3:], ["  line 22", "  line 23", "  line 24"])

    def test_summary_groups_skipped_reasons(self):
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            autotest_module.print_summary(
                passed=3,
                total=4,
                disabled_counts={
                    "missing /bin/zsh": 2,
                    "requires HAS_OPENMPI": 1,
                },
            )

        self.assertEqual(
            stdout.getvalue().splitlines(),
            [
                "== Summary ==",
                "=============",
                "test groups: pass=3 fail=1 skipped=3 total=7",
                "skipped by reason:",
                "  2 missing /bin/zsh",
                "  1 requires HAS_OPENMPI",
                "run ./test/autotest.py --list for disabled-test details",
            ],
        )

    def test_autotest_run_entry_uses_padded_test_name(self):
        self.assertEqual(
            autotest_module.format_run_entry(
                TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
                "ckpt:PASSED; rstr:PASSED -> ckpt:PASSED; rstr:PASSED",
            ),
            "dmtcp1         ckpt:PASSED; rstr:PASSED -> "
            "ckpt:PASSED; rstr:PASSED",
        )

    def test_autotest_run_entry_aligns_to_requested_name_width(self):
        self.assertEqual(
            autotest_module.format_run_entry(
                TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
                "PASS",
                name_width=len("exit-on-last"),
            ),
            "dmtcp1      PASS",
        )
        self.assertEqual(
            autotest_module.format_run_entry(
                TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"],
                         category=autotest_module.COORDINATOR_PROTOCOL_CATEGORY),
                "PASS",
                name_width=len("exit-on-last"),
            ),
            "exit-on-last PASS",
        )

    def test_run_name_width_keeps_default_alignment(self):
        selected_specs = [
            TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"],
                     cycles=0),
            TestSpec("posix-mq-close-untracked", 1,
                     ["./test/posix-mq-close-untracked"]),
        ]

        width = autotest_module.run_name_width(["all"], selected_specs)

        self.assertEqual(width, autotest_module.RUN_TESTNAME_WIDTH)

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

        selected = TestRegistry(tests).select(
            tags=["checkpoint-header"],
            requirements=["gzip-launcher"],
        )

        self.assertEqual([spec.name for spec in selected], ["gzip"])

    def test_registry_lists_disabled_tests_with_reasons(self):
        tests = [
            TestSpec("available", 1, ["./test/autotest.py"]),
            TestSpec("missing-command", 1,
                     ["/definitely/missing/dmtcp-test-command"],
                     category="Interactive shells"),
            TestSpec("missing-config", 1, ["./test/autotest.py"],
                     configure_flags=["MISSING_AUTOTEST_FLAG"]),
            TestSpec("missing-config-and-command", 1,
                     ["/definitely/missing/dmtcp-test-command"],
                     configure_flags=["MISSING_AUTOTEST_FLAG"]),
        ]
        registry = TestRegistry(tests)

        self.assertEqual([spec.name for spec in registry.select()],
                         ["available"])
        listed = registry.select_for_listing()
        self.assertEqual([spec.name for spec in listed],
                         [
                             "available",
                             "missing-command",
                             "missing-config",
                             "missing-config-and-command",
                         ])

        disabled = [spec for spec in listed
                    if spec.category == "Disabled tests"]
        self.assertEqual(
            [(spec.name, spec.list_notes) for spec in disabled],
            [
                ("missing-command",
                 ["missing /definitely/missing/dmtcp-test-command"]),
                ("missing-config", ["requires MISSING_AUTOTEST_FLAG"]),
                ("missing-config-and-command",
                 ["requires MISSING_AUTOTEST_FLAG"]),
            ],
        )

    def test_registry_counts_disabled_reasons_for_summary(self):
        tests = [
            TestSpec("available", 1, ["./test/autotest.py"]),
            TestSpec("missing-command", 1,
                     ["/definitely/missing/dmtcp-test-command"],
                     tags=["slow"]),
            TestSpec("missing-config", 1, ["./test/autotest.py"],
                     configure_flags=["MISSING_AUTOTEST_FLAG"]),
            TestSpec("missing-config-and-command", 1,
                     ["/definitely/missing/dmtcp-test-command"],
                     configure_flags=["MISSING_AUTOTEST_FLAG"],
                     tags=["slow"]),
        ]
        registry = TestRegistry(tests)

        self.assertEqual(
            registry.disabled_reason_counts(),
            {
                "missing /definitely/missing/dmtcp-test-command": 1,
                "requires MISSING_AUTOTEST_FLAG": 2,
            },
        )
        self.assertEqual(
            registry.disabled_reason_counts(tags=["slow"]),
            {
                "missing /definitely/missing/dmtcp-test-command": 1,
                "requires MISSING_AUTOTEST_FLAG": 1,
            },
        )

    def test_registry_blocks_vim_on_aarch64(self):
        tests = [
            TestSpec("vim", 1, ["/usr/bin/vim"],
                     configure_flags=["HAS_VIM"],
                     blocked_configure_flags=["AARCH64_HOST"]),
        ]

        with mock.patch.object(
                TestRegistry, "_config_yes",
                staticmethod(lambda flag: flag in {
                    "HAS_VIM", "AARCH64_HOST"
                })):
            registry = TestRegistry(tests)

        self.assertEqual(registry.select(), [])
        listed = registry.select_for_listing(["vim"])
        self.assertEqual(len(listed), 1)
        self.assertEqual(listed[0].category, "Disabled tests")
        self.assertEqual(listed[0].list_notes,
                         ["blocked by AARCH64_HOST"])

    def test_registry_listing_can_select_disabled_tests_by_name(self):
        tests = [
            TestSpec("available", 1, ["./test/autotest.py"]),
            TestSpec("missing-command", 1,
                     ["/definitely/missing/dmtcp-test-command"]),
        ]
        registry = TestRegistry(tests)

        selected = registry.select_for_listing(["missing-command"])

        self.assertEqual(len(selected), 1)
        self.assertEqual(selected[0].category, "Disabled tests")
        self.assertEqual(selected[0].list_notes,
                         ["missing /definitely/missing/dmtcp-test-command"])

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

    def test_autotest_list_rejects_unknown_test(self):
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "test" / "autotest.py"),
                "--list",
                "definitely-not-a-test",
            ],
            cwd=str(ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
        )

        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("Unknown test: definitely-not-a-test", result.stderr)

    def test_autotest_list_rejects_empty_selection(self):
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "test" / "autotest.py"),
                "--list",
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
        self.assertEqual(result.stdout, "")
        self.assertIn("No tests selected", result.stderr)

    def test_autotest_can_retain_success_artifacts(self):
        captured = {}
        stdout = io.StringIO()
        spec = TestSpec("retain-success", 1, ["./test/dmtcp1"])
        artifact_dir = pathlib.Path("/tmp/dmtcp-retain-success")

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                captured["verbose"] = verbose
                captured["retain_success_artifacts"] = (
                    retain_success_artifacts)
                captured["slow_count"] = slow_count

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            self.assertEqual(selected_spec, spec)
            self.assertFalse(retry_once)
            return TestResult.pass_(selected_spec.name, artifact_dir)

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--retain-success-artifacts",
                                "retain-success"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements: [spec]), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(REGISTRY, "disabled_reason_counts",
                               lambda tags, requirements: {}), \
             mock.patch.object(autotest_module.time, "monotonic",
                               lambda: 0.0), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 0)
        self.assertFalse(captured["verbose"])
        self.assertTrue(captured["retain_success_artifacts"])
        self.assertEqual(captured["slow_count"], 0)
        self.assertIn(f"  {spec.name} artifacts={artifact_dir}",
                      stdout.getvalue().splitlines())

    def test_autotest_run_groups_output_by_category(self):
        stdout = io.StringIO()
        selected_specs = [
            TestSpec("zsh", 1, ["/bin/zsh"],
                     category="Interactive shells"),
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"],
                     category="Single-process programs"),
            TestSpec("bash", 1, ["/bin/bash"],
                     category="Interactive shells"),
            TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"],
                     cycles=0,
                     category="Coordinator protocol tests"),
        ]
        run_order = []

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                pass

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            run_order.append(selected_spec.name)
            if selected_spec.cycles == 0:
                harness.progress("run-passed")
            else:
                for cycle in range(selected_spec.cycles):
                    if cycle:
                        harness.progress("cycle-separator")
                    harness.progress("ckpt-start")
                    harness.progress("ckpt-passed")
                    harness.progress("rstr-start")
                    harness.progress("rstr-passed")
            return TestResult.pass_(selected_spec.name)

        with mock.patch.object(sys, "argv", ["autotest.py"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements:
                               selected_specs), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(REGISTRY, "disabled_reason_counts",
                               lambda tags, requirements: {}), \
             mock.patch.object(autotest_module.time, "monotonic",
                               lambda: 0.0), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 0)
        lines = stdout.getvalue().splitlines()
        self.assertEqual(
            lines,
            [
                "== Tests ==",
                "===========",
                "",
                "Single-process programs",
                "-----------------------",
                "  dmtcp1         ckpt:PASSED; rstr:PASSED -> "
                "ckpt:PASSED; rstr:PASSED (0.0s)",
                "",
                "Interactive shells",
                "------------------",
                "  bash           ckpt:PASSED; rstr:PASSED -> "
                "ckpt:PASSED; rstr:PASSED (0.0s)",
                "  zsh            ckpt:PASSED; rstr:PASSED -> "
                "ckpt:PASSED; rstr:PASSED (0.0s)",
                "",
                "Coordinator protocol tests",
                "--------------------------",
                "  exit-on-last   run:PASSED (0.0s)",
                "== Summary ==",
                "=============",
                "test groups: pass=4 fail=0 skipped=0 total=4",
            ],
        )
        self.assertEqual(run_order, [
            "dmtcp1",
            "bash",
            "zsh",
            "coordinator-exit-on-last",
        ])

    def test_autotest_run_prints_failures_in_red(self):
        stdout = io.StringIO()
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                pass

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            return TestResult.fail(selected_spec.name, "restart",
                                   "restart failed",
                                   pathlib.Path("/tmp/dmtcp-failed"))

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--color", "always"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements: [spec]), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(REGISTRY, "disabled_reason_counts",
                               lambda tags, requirements: {}), \
             mock.patch.object(autotest_module.time, "monotonic",
                               lambda: 0.0), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 1)
        self.assertIn(
            "  dmtcp1         \033[0;31mrstr:FAILED phase=restart "
            "msg=restart failed\033[0m (0.0s)",
            stdout.getvalue().splitlines(),
        )
        self.assertIn("  dmtcp1         artifacts=/tmp/dmtcp-failed",
                      stdout.getvalue().splitlines())
        self.assertIn("== Failures ==", stdout.getvalue().splitlines())
        self.assertIn(
            "  dmtcp1         phase=restart msg=restart failed",
            stdout.getvalue().splitlines(),
        )
        self.assertIn(
            "    artifacts: /tmp/dmtcp-failed",
            stdout.getvalue().splitlines(),
        )
        self.assertIn(
            "    logs: result.log commands.log processes.log "
            "worker-*.out restart-*.out",
            stdout.getvalue().splitlines(),
        )

    def test_autotest_suite_all_runs_command_suites_then_integration(self):
        stdout = io.StringIO()
        selected_specs = [
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
            TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"],
                     cycles=0,
                     category="Coordinator protocol tests"),
        ]
        command_calls = []
        integration_calls = []

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                pass

        def fake_run_command_test(command_spec):
            command_calls.append(command_spec.name)
            return TestResult.pass_(command_spec.name, message="mocked")

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            integration_calls.append(selected_spec.name)
            if selected_spec.cycles == 0:
                harness.progress("run-passed")
            else:
                harness.progress("ckpt-start")
                harness.progress("ckpt-passed")
                harness.progress("rstr-start")
                harness.progress("rstr-passed")
                harness.progress("cycle-separator")
                harness.progress("ckpt-start")
                harness.progress("ckpt-passed")
                harness.progress("rstr-start")
                harness.progress("rstr-passed")
            return TestResult.pass_(selected_spec.name)

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--suite", "all",
                                "dmtcp1"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements:
                               selected_specs), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_command_test",
                               fake_run_command_test), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(REGISTRY, "disabled_reason_counts",
                               lambda tags, requirements: {}), \
             mock.patch.object(autotest_module.time, "monotonic",
                               lambda: 0.0), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 0)
        self.assertEqual(command_calls, [
            "dmtcp-unit",
            "autotest-unit",
            "coordinator-synthetic",
            "command-json",
        ])
        self.assertEqual(integration_calls, [
            "coordinator-exit-on-last",
            "dmtcp1",
        ])
        lines = stdout.getvalue().splitlines()
        for heading in [
            "Unit tests",
            "Harness tests",
            "Coordinator protocol tests",
            "Checkpoint/restart integration tests",
        ]:
            self.assertIn(heading, lines)
        self.assertEqual(lines.count("Coordinator protocol tests"), 1)
        self.assertIn("  dmtcp-unit     PASS. mocked", lines)
        self.assertIn("  exit-on-last   run:PASSED (0.0s)", lines)
        self.assertIn("  dmtcp1         ckpt:PASSED; rstr:PASSED -> "
                      "ckpt:PASSED; rstr:PASSED (0.0s)", lines)
        self.assertIn("test groups: pass=6 fail=0 skipped=0 total=6", lines)

    def test_autotest_passes_slow_count_to_harness(self):
        captured = {}
        stdout = io.StringIO()
        spec = TestSpec("slow", 1, ["./test/dmtcp1"])

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                captured["verbose"] = verbose
                captured["retain_success_artifacts"] = (
                    retain_success_artifacts)
                captured["slow_count"] = slow_count

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            self.assertEqual(selected_spec, spec)
            self.assertFalse(retry_once)
            return TestResult.pass_(selected_spec.name)

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--slow", "--slow",
                                "slow"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements: [spec]), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 0)
        self.assertFalse(captured["verbose"])
        self.assertFalse(captured["retain_success_artifacts"])
        self.assertEqual(captured["slow_count"], 2)

    def test_autotest_accepts_jobs_argument(self):
        with mock.patch.object(sys, "argv", ["autotest.py", "--jobs", "4"]):
            args = autotest_module.parse_args()

        self.assertEqual(args.jobs, 4)

    def test_autotest_rejects_non_positive_jobs_argument(self):
        stderr = io.StringIO()
        with mock.patch.object(sys, "argv", ["autotest.py", "--jobs", "0"]), \
             contextlib.redirect_stderr(stderr):
            with self.assertRaises(SystemExit):
                autotest_module.parse_args()

        self.assertIn("expected a positive integer", stderr.getvalue())

    def test_autotest_parallel_integration_uses_requested_jobs(self):
        stdout = io.StringIO()
        selected_specs = [
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
            TestSpec("dmtcp2", 1, ["./test/dmtcp2"]),
        ]
        harnesses = []
        run_order = []

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                self.progress = lambda event: None
                harnesses.append(self)

        class ImmediateFuture:
            def __init__(self, value):
                self._value = value

            def result(self):
                return self._value

        class RecordingExecutor:
            created_max_workers = []

            def __init__(self, max_workers):
                self.created_max_workers.append(max_workers)

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc_value, traceback):
                return False

            def submit(self, fn, *args):
                return ImmediateFuture(fn(*args))

        def fake_as_completed(futures):
            return list(futures)

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            self.assertIsInstance(harness, FakeHarness)
            run_order.append(selected_spec.name)
            harness.progress("ckpt-start")
            harness.progress("ckpt-passed")
            harness.progress("rstr-start")
            harness.progress("rstr-passed")
            return TestResult.pass_(selected_spec.name)

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--jobs", "4"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements:
                               selected_specs), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(autotest_module.concurrent.futures,
                               "ThreadPoolExecutor", RecordingExecutor), \
             mock.patch.object(autotest_module.concurrent.futures,
                               "as_completed", fake_as_completed), \
             mock.patch.object(REGISTRY, "disabled_reason_counts",
                               lambda tags, requirements: {}), \
             mock.patch.object(autotest_module.time, "monotonic",
                               lambda: 0.0), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 0)
        self.assertEqual(RecordingExecutor.created_max_workers, [4])
        self.assertEqual(len(harnesses), 2)
        self.assertEqual(run_order, ["dmtcp1", "dmtcp2"])
        self.assertIn("  dmtcp1         ckpt:PASSED; rstr:PASSED (0.0s)",
                      stdout.getvalue().splitlines())
        self.assertIn("  dmtcp2         ckpt:PASSED; rstr:PASSED (0.0s)",
                      stdout.getvalue().splitlines())

    def test_autotest_parallel_integration_reports_failures(self):
        stdout = io.StringIO()
        spec = TestSpec("dmtcp1", 1, ["./test/dmtcp1"])
        artifact_dir = pathlib.Path("/tmp/dmtcp-parallel-failed")

        class FakeHarness:
            def __init__(self, verbose=False,
                         retain_success_artifacts=False,
                         slow_count=0):
                self.progress = lambda event: None

        class ImmediateFuture:
            def __init__(self, value):
                self._value = value

            def result(self):
                return self._value

        class RecordingExecutor:
            def __init__(self, max_workers):
                pass

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc_value, traceback):
                return False

            def submit(self, fn, *args):
                return ImmediateFuture(fn(*args))

        def fake_as_completed(futures):
            return list(futures)

        def fake_run_with_optional_retry(harness, selected_spec, retry_once):
            harness.progress("ckpt-start")
            return TestResult.fail(selected_spec.name, "checkpoint",
                                   "no ckpt image", artifact_dir)

        with mock.patch.object(sys, "argv",
                               ["autotest.py", "--jobs", "4",
                                "--color", "always"]), \
             mock.patch.object(REGISTRY, "select",
                               lambda names, tags, requirements: [spec]), \
             mock.patch.object(autotest_module, "DmtcpHarness",
                               FakeHarness), \
             mock.patch.object(autotest_module, "run_with_optional_retry",
                               fake_run_with_optional_retry), \
             mock.patch.object(autotest_module.concurrent.futures,
                               "ThreadPoolExecutor", RecordingExecutor), \
             mock.patch.object(autotest_module.concurrent.futures,
                               "as_completed", fake_as_completed), \
             mock.patch.object(REGISTRY, "disabled_reason_counts",
                               lambda tags, requirements: {}), \
             mock.patch.object(autotest_module.time, "monotonic",
                               lambda: 0.0), \
             contextlib.redirect_stdout(stdout):
            rc = autotest_module.main()

        self.assertEqual(rc, 1)
        self.assertIn(
            "  dmtcp1         ckpt:\033[0;31mFAILED phase=checkpoint "
            "msg=no ckpt image\033[0m (0.0s)",
            stdout.getvalue().splitlines(),
        )
        self.assertIn("  dmtcp1         artifacts=/tmp/dmtcp-parallel-failed",
                      stdout.getvalue().splitlines())
        self.assertIn(
            "  dmtcp1         phase=checkpoint msg=no ckpt image",
            stdout.getvalue().splitlines(),
        )

    def test_retry_once_reports_failed_attempt_artifacts(self):
        spec = TestSpec("retry-artifacts", 1, ["./test/dmtcp1"])
        failure_dir = pathlib.Path("/tmp/dmtcp-retry-artifacts")

        class FakeHarness:
            def __init__(self):
                self.calls = 0

            def run(self, selected_spec):
                self.calls += 1
                if self.calls == 1:
                    return TestResult.fail(selected_spec.name, "checkpoint",
                                           "checkpoint failed", failure_dir)
                return TestResult.pass_(selected_spec.name)

        stdout = io.StringIO()
        harness = FakeHarness()
        with contextlib.redirect_stdout(stdout):
            result = autotest_module.run_with_optional_retry(
                harness, spec, retry_once=True)

        self.assertTrue(result.passed)
        self.assertEqual(harness.calls, 2)
        self.assertIn(
            f"{spec.name}: retrying after phase=checkpoint "
            f"msg=checkpoint failed artifacts={failure_dir}",
            stdout.getvalue().splitlines(),
        )
        self.assertIn(f"{spec.name}: PASSED on retry",
                      stdout.getvalue().splitlines())

    def test_registry_contains_dmtcp1(self):
        tests = list(REGISTRY.select())

        self.assertIn("dmtcp1", [test.name for test in tests])
        dmtcp1 = REGISTRY.get_test("dmtcp1")
        self.assertEqual(dmtcp1.peers, 1)
        self.assertEqual(dmtcp1.commands, ["./test/dmtcp1"])

    def test_root_makefile_runs_unified_autotest_check(self):
        makefile = (ROOT / "Makefile.in").read_text(encoding="utf-8")

        self.assertRegex(makefile, r"(?m)^check: tests$")
        self.assertNotRegex(makefile, r"(?m)^check-all:")
        self.assertIn("cd test && $(MAKE) --no-print-directory check-prereqs",
                      makefile)
        self.assertIn("autotest.py --suite all ${AUTOTEST}", makefile)
        self.assertIn("autotest.py --suite integration ${AUTOTEST}",
                      makefile)
        self.assertNotIn("check-all", makefile)

    def test_test_makefile_suite_targets_delegate_to_autotest(self):
        makefile = (ROOT / "test" / "Makefile.in").read_text(
            encoding="utf-8")

        self.assertRegex(makefile, r"(?m)^check-prereqs: "
                         r"\$\(UNIT_TESTS\) "
                         r"\$\(COORDINATOR_SYNTHETIC_WORKER\)$")
        for suite in ["unit", "harness", "coordinator"]:
            self.assertIn(f"autotest.py --suite {suite}", makefile)

    def test_registry_contains_default_flow_smoke_tests(self):
        names = [test.name for test in REGISTRY.select()]

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
            "shared-memory3", "sysv-shm1", "sysv-shm2", "sysv-sem", "sysv-msg",
            "syscall-tester", "file2", "presuspend", "plugin-sleep2",
            "plugin-init", "popen1", "poll-disable-event-plugin", "pthread3",
            "restartdir", "pty1", "pty2", "vfork1", "vfork2", "frisbee",
            "nocheckpoint", "checkpoint-header",
            "coordinator-exit-on-last", "command-json-bcheckpoint",
            "coordinator-reject-restart-while-running",
        ]:
            self.assertIn(name, names)

        syscall_tester = REGISTRY.get_test("syscall-tester")
        self.assertEqual(syscall_tester.checkpoint_command, "--kcheckpoint")
        self.assertEqual(syscall_tester.commands,
                         ["--checkpoint-open-files ./test/syscall-tester"])
        coordinator_barrier = REGISTRY.get_test("coordinator-barrier")
        self.assertEqual(coordinator_barrier.category,
                         "Coordinator protocol tests")
        self.assertEqual(coordinator_barrier.peers, 2)
        self.assertEqual(coordinator_barrier.cycles, 1)
        self.assertIn("coordinator", coordinator_barrier.tags)
        self.assertIn("barrier", coordinator_barrier.tags)
        self.assertIn("real-worker", coordinator_barrier.requirements)
        self.assertIn("cycles=1", coordinator_barrier.limits)
        restartdir = REGISTRY.get_test("restartdir")
        self.assertTrue(restartdir.restart_uses_directory)
        frisbee = REGISTRY.get_test("frisbee")
        self.assertEqual(frisbee.category, "Multi-process coordination")
        self.assertEqual(len(frisbee.commands), 3)
        self.assertEqual(frisbee.env["DMTCP_GZIP"], "1")
        nocheckpoint = REGISTRY.get_test("nocheckpoint")
        self.assertEqual(nocheckpoint.cycles, 1)
        self.assertIn("cycles=1", nocheckpoint.limits)
        checkpoint_header = REGISTRY.get_test("checkpoint-header")
        self.assertTrue(checkpoint_header.validate_checkpoint_headers)
        self.assertEqual(checkpoint_header.env["DMTCP_GZIP"], "0")
        self.assertIn("checkpoint-header", checkpoint_header.tags)
        self.assertIn("cycles=1", checkpoint_header.limits)
        exit_on_last = REGISTRY.get_test("coordinator-exit-on-last")
        self.assertEqual(exit_on_last.cycles, 0)
        self.assertEqual(exit_on_last.coordinator_args, ["--exit-on-last"])
        self.assertEqual(exit_on_last.completion_command,
                         "--kill-exit-on-last")
        self.assertIn("coordinator", exit_on_last.tags)
        self.assertIn("exit-on-last", exit_on_last.tags)
        self.assertIn("real-worker", exit_on_last.requirements)
        self.assertIn("cycles=0", exit_on_last.limits)
        bcheckpoint = REGISTRY.get_test("command-json-bcheckpoint")
        self.assertEqual(bcheckpoint.checkpoint_command, "--bcheckpoint")
        self.assertEqual(bcheckpoint.cycles, 1)
        self.assertIn("command-json", bcheckpoint.tags)
        self.assertIn("real-worker", bcheckpoint.requirements)
        self.assertIn("cycles=1", bcheckpoint.limits)
        shared_memory3 = REGISTRY.get_test("shared-memory3")
        self.assertEqual(shared_memory3.category, "IPC and shared resources")
        self.assertEqual(shared_memory3.pre_checkpoint_delay, 3.0)
        self.assertIn("slow", shared_memory3.tags)

    def test_registry_contains_example_db_plugin_test(self):
        spec = REGISTRY.get_test("plugin-example-db")

        self.assertEqual(spec.peers, 2)
        self.assertEqual(len(spec.commands), 2)
        self.assertIn("libdmtcp_example-db.so", spec.commands[0])
        self.assertIn("EXAMPLE_DB_KEY=1", spec.commands[0])
        self.assertIn("EXAMPLE_DB_KEY_OTHER=2", spec.commands[0])
        self.assertIn("EXAMPLE_DB_KEY=2", spec.commands[1])
        self.assertIn("EXAMPLE_DB_KEY_OTHER=1", spec.commands[1])

    def test_registry_marks_old_slow_timing_tests(self):
        names = {test.name for test in REGISTRY.select()}
        expected = {
            "file2",
            "shared-memory1",
            "shared-memory2",
            "shared-memory3",
        }
        expected.update({"waitpid", "waitid-syscall"})
        if pathlib.Path("/bin/zsh").exists():
            expected.add("zsh")
        if autotest_config.HAS_JAVA == "yes" and \
                autotest_config.HAS_JAVAC == "yes" and \
                (ROOT / "test/java1.class").exists():
            expected.add("java1")
        if (ROOT / "test/openmp-1").exists() and \
                getattr(autotest_config, "AARCH64_HOST", "no") != "yes":
            expected.add("openmp-1")
        if (ROOT / "test/openmp-2").exists() and \
                getattr(autotest_config, "AARCH64_HOST", "no") != "yes":
            expected.add("openmp-2")
        if autotest_config.HAS_SSH_LOCALHOST == "yes":
            expected.add("ssh1")
        if pathlib.Path("/usr/bin/script").exists():
            expected.add("script")
        if autotest_config.HAS_MATLAB == "yes":
            expected.add("matlab-nodisplay")
        if autotest_config.HAS_OPENMPI == "yes" and \
                (ROOT / "test/openmpi").exists():
            expected.add("openmpi")
        if pathlib.Path("/usr/bin/emacs").exists():
            expected.add("emacs")
        if pathlib.Path("/usr/bin/screen").exists():
            expected.add("screen")

        for name in sorted(expected):
            with self.subTest(name=name):
                assert_registered_unless_m32_disabled(self, names, name)
                if name in names:
                    self.assertIn("slow", REGISTRY.get_test(name).tags)

    def test_registry_contains_configured_optional_tests(self):
        names = [test.name for test in REGISTRY.select()]

        if autotest_config.HAS_EPOLL_CREATE1 == "yes":
            self.assertIn("epoll2", names)
            self.assertEqual(REGISTRY.get_test("epoll2").configure_flags,
                             ["HAS_EPOLL_CREATE1"])
        assert_registered_unless_m32_disabled(self, names, "dlopen2")
        assert_registered_when_repo_file_exists(self, names, "cma",
                                                "test/cma")
        if autotest_config.ARM_HOST != "yes" and \
                autotest_config.TEST_POSIX_MQ == "yes":
            self.assertIn("posix-mq1", names)
            self.assertEqual(REGISTRY.get_test("posix-mq1").configure_flags,
                             ["TEST_POSIX_MQ"])
            self.assertEqual(REGISTRY.get_test("posix-mq1").blocked_configure_flags,
                             ["ARM_HOST"])
        if autotest_config.ARM_HOST == "yes":
            self.assertNotIn("posix-mq1", names)
        if autotest_config.ARM_HOST != "yes" and \
                autotest_config.TEST_POSIX_MQ == "yes" and \
                autotest_config.HAS_SYS_MQ_OPEN == "yes":
            self.assertIn("posix-mq-close-untracked", names)
            self.assertEqual(
                REGISTRY.get_test("posix-mq-close-untracked").configure_flags,
                ["TEST_POSIX_MQ", "HAS_SYS_MQ_OPEN"],
            )
            self.assertEqual(
                REGISTRY.get_test("posix-mq-close-untracked").blocked_configure_flags,
                ["ARM_HOST"],
            )
        if autotest_config.ARM_HOST == "yes":
            self.assertNotIn("posix-mq-close-untracked", names)
        assert_registered_when_repo_file_exists(self, names,
                                                "pthread_atfork1",
                                                "test/pthread_atfork1")
        assert_registered_when_repo_file_exists(self, names,
                                                "pthread_atfork2",
                                                "test/pthread_atfork2")
        if "pthread_atfork1" in names:
            atfork = REGISTRY.get_test("pthread_atfork1")
            self.assertIn(f"{ROOT}/test", atfork.library_paths)
        for name in ("waitpid", "waitid-syscall", "gzip", "perl", "bash"):
            assert_registered_unless_m32_disabled(self, names, name)
        assert_registered_unless_m32_disabled(self, names, "python")
        if "python" in names:
            self.assertEqual(REGISTRY.get_test("python").commands, [sys.executable])
        assert_registered_when_path_exists(self, names, "dash", "/bin/dash")
        assert_registered_when_path_exists(self, names, "zsh", "/bin/zsh")
        if "gzip" in names:
            gzip = REGISTRY.get_test("gzip")
            self.assertEqual(gzip.env["DMTCP_GZIP"], "1")
        if autotest_config.HAS_JAVA == "yes" and \
                autotest_config.HAS_JAVAC == "yes" and \
                (ROOT / "test/java1.class").exists():
            self.assertIn("java1", names)
            java1 = REGISTRY.get_test("java1")
            self.assertEqual(java1.env["CLASSPATH"], "./test")
            self.assertEqual(java1.configure_flags,
                             ["HAS_JAVA", "HAS_JAVAC"])
            self.assertEqual(java1.required_files, ["test/java1.class"])
        if (ROOT / "test/openmp-1").exists():
            assert_registered_unless_aarch64_disabled(self, names, "openmp-1")
        if (ROOT / "test/openmp-2").exists():
            assert_registered_unless_aarch64_disabled(self, names, "openmp-2")
        if "openmp-1" in names:
            self.assertEqual(REGISTRY.get_test("openmp-1").cycles, 2)
            self.assertEqual(REGISTRY.get_test("openmp-1").limits, [])
            self.assertEqual(REGISTRY.get_test("openmp-1").pre_checkpoint_delay, 0.9)
            self.assertEqual(REGISTRY.get_test("openmp-1").blocked_configure_flags,
                             ["AARCH64_HOST"])
        if "openmp-2" in names:
            self.assertEqual(REGISTRY.get_test("openmp-2").cycles, 2)
            self.assertEqual(REGISTRY.get_test("openmp-2").limits, [])
            self.assertEqual(REGISTRY.get_test("openmp-2").pre_checkpoint_delay, 0.9)
            self.assertEqual(REGISTRY.get_test("openmp-2").blocked_configure_flags,
                             ["AARCH64_HOST"])
        assert_registered_when_repo_file_exists(self, names, "dmtcp1-m32",
                                                "test/dmtcp1-m32")
        assert_registered_when_repo_file_exists(self, names, "selinux1",
                                                "test/selinux1")
        if autotest_config.HAS_SSH_LOCALHOST == "yes":
            assert_registered_unless_m32_disabled(self, names, "ssh1")
            self.assertEqual(REGISTRY.get_test("ssh1").configure_flags,
                             ["HAS_SSH_LOCALHOST"])
        assert_registered_when_repo_file_exists(self, names, "readline",
                                                "test/readline")
        assert_registered_when_path_exists(self, names, "tcsh", "/bin/tcsh")
        assert_registered_when_path_exists(self, names, "script",
                                           "/usr/bin/script")
        if "script" in names:
            self.assertEqual(REGISTRY.get_test("script").env["SHELL"], "/bin/bash")
        assert_registered_when_path_exists(self, names, "emacs",
                                           "/usr/bin/emacs")
        if "emacs" in names:
            emacs = REGISTRY.get_test("emacs")
            self.assertEqual(emacs.launch_mode, "pty")
            self.assertEqual(emacs.commands,
                             ["/usr/bin/emacs -nw -Q /etc/passwd"])
            self.assertEqual(emacs.env["TERM"], "vt100")
            self.assertIn("pty", emacs.tags)
        assert_registered_when_path_exists(self, names, "screen",
                                           "/usr/bin/screen")
        if "screen" in names:
            screen = REGISTRY.get_test("screen")
            self.assertEqual(screen.launch_mode, "pty")
            self.assertEqual(screen.env["TERM"], "vt100")
            self.assertEqual(screen.private_env_dirs["SCREENDIR"],
                             "/tmp/{workname}-screen")
            self.assertIn("pty", screen.tags)
        assert_registered_when_repo_file_exists(self, names, "cilk1",
                                                "test/cilk1")
        if autotest_config.HAS_MATLAB == "yes":
            assert_registered_unless_m32_disabled(self, names,
                                                  "matlab-nodisplay")
            self.assertEqual(REGISTRY.get_test("matlab-nodisplay").configure_flags,
                             ["HAS_MATLAB"])
        if autotest_config.HAS_MPICH == "yes" and \
                (ROOT / "test/hellompich").exists():
            assert_registered_unless_m32_disabled(self, names,
                                                  "hellompich-n1")
            assert_registered_unless_m32_disabled(self, names,
                                                  "hellompich-n2")
            self.assertEqual(REGISTRY.get_test("hellompich-n1").configure_flags,
                             ["HAS_MPICH"])
            self.assertEqual(REGISTRY.get_test("hellompich-n2").configure_flags,
                             ["HAS_MPICH"])
        if autotest_config.HAS_OPENMPI == "yes" and \
                (ROOT / "test/openmpi").exists():
            assert_registered_unless_m32_disabled(self, names, "openmpi")
            if "openmpi" in names:
                self.assertEqual(REGISTRY.get_test("openmpi").peers, [5, 6])
                self.assertEqual(REGISTRY.get_test("openmpi").configure_flags,
                                 ["HAS_OPENMPI"])

    def test_m32_disabled_tests_are_filtered_by_name(self):
        tests = [
            TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
            TestSpec("gzip", 1, ["./test/dmtcp1"]),
            TestSpec("bash", 1, ["/bin/bash"]),
        ]

        with mock.patch.object(TestRegistry, "_use_m32",
                               staticmethod(lambda: True)):
            filtered = TestRegistry._filter_m32_disabled_tests(tests)

        self.assertEqual([test.name for test in filtered], ["dmtcp1"])

    def test_unavailable_absolute_command_tests_are_filtered_by_path(self):
        tests = [
            TestSpec("missing", 1, ["/definitely/missing/dmtcp-test-binary"]),
            TestSpec("present", 1, ["/bin/sh -c true"]),
            TestSpec("relative", 1, ["./test/dmtcp1"]),
        ]

        filtered = TestRegistry._filter_unavailable_command_executables(tests)

        self.assertEqual([test.name for test in filtered],
                         ["present", "relative"])

    def test_unavailable_absolute_command_filter_checks_all_commands(self):
        tests = [
            TestSpec("mixed", 2,
                     ["/bin/sh -c true",
                      "/definitely/missing/dmtcp-test-binary"]),
            TestSpec("all-present", 2, ["/bin/sh -c true", "/bin/sh -c true"]),
        ]

        filtered = TestRegistry._filter_unavailable_command_executables(tests)

        self.assertEqual([test.name for test in filtered], ["all-present"])

    def test_unavailable_command_filter_checks_in_tree_test_binaries(self):
        tests = [
            TestSpec("missing", 1,
                     ["./test/definitely-missing-dmtcp-test-binary"]),
            TestSpec("present", 1, ["./test/dmtcp1"]),
            TestSpec("option-present", 1,
                     ["--checkpoint-open-files ./test/syscall-tester"]),
        ]

        filtered = TestRegistry._filter_unavailable_command_executables(tests)

        self.assertEqual([test.name for test in filtered],
                         ["present", "option-present"])

    def test_unavailable_required_file_filter_checks_artifacts(self):
        tests = [
            TestSpec("missing", 1, ["/bin/sh"],
                     required_files=["test/definitely-missing-artifact"]),
            TestSpec("present", 1, ["/bin/sh"],
                     required_files=["test/autotest.py"]),
        ]

        filtered = TestRegistry._filter_unavailable_required_files(tests)

        self.assertEqual([test.name for test in filtered], ["present"])

    def test_unavailable_configure_flag_filter_checks_config(self):
        tests = [
            TestSpec("missing", 1, ["/bin/sh"],
                     configure_flags=["MISSING_FLAG"]),
            TestSpec("present", 1, ["/bin/sh"],
                     configure_flags=["AVAILABLE_FLAG"]),
            TestSpec("blocked", 1, ["/bin/sh"],
                     configure_flags=["AVAILABLE_FLAG"],
                     blocked_configure_flags=["BLOCKED_FLAG"]),
            TestSpec("unblocked", 1, ["/bin/sh"],
                     configure_flags=["AVAILABLE_FLAG"],
                     blocked_configure_flags=["MISSING_FLAG"]),
        ]

        with mock.patch.object(
                TestRegistry, "_config_yes",
                staticmethod(lambda flag: flag in {
                    "AVAILABLE_FLAG", "BLOCKED_FLAG"
                })):
            filtered = TestRegistry._filter_unavailable_configure_flags(tests)

        self.assertEqual([test.name for test in filtered],
                         ["present", "unblocked"])

    def test_registry_keeps_deferred_old_runtime_tests_out_of_make_check(self):
        names = {test.name for test in REGISTRY.select()}

        self.assertNotIn("gcl", names)

    def test_parity_ledger_mentions_authoritative_registry_tests(self):
        ledger = (ROOT / "test" / "autotest-parity.md").read_text(
            encoding="utf-8")

        missing = [
            test.name for test in REGISTRY.select()
            if f"`{test.name}`" not in ledger
        ]

        self.assertEqual(missing, [])

    def test_parity_ledger_classifies_old_harness_run_tests(self):
        old_runner = (ROOT / "test" / "autotest_old.py").read_text(
            encoding="utf-8")
        ledger = (ROOT / "test" / "autotest-parity.md").read_text(
            encoding="utf-8")
        old_names = set()
        for line in old_runner.splitlines():
            if line.strip().startswith("#"):
                continue
            match = re.search(r'\brunTest\("([^"]+)"', line)
            if match is not None:
                old_names.add(match.group(1))

        missing = [
            name for name in sorted(old_names)
            if f"`{name}`" not in ledger
        ]

        self.assertEqual(missing, [])

    def test_registry_cycle_limits_are_explicit(self):
        missing = [
            test.name for test in REGISTRY.select()
            if test.cycles != 2 and f"cycles={test.cycles}" not in test.limits
        ]

        self.assertEqual(missing, [])

    def test_parity_ledger_mentions_registry_limits(self):
        ledger = (ROOT / "test" / "autotest-parity.md").read_text(
            encoding="utf-8")

        missing = []
        for test in REGISTRY.select():
            for limit in test.limits:
                if not any(
                    f"`{test.name}`" in line and limit in line
                    for line in ledger.splitlines()
                ):
                    missing.append(f"{test.name}:{limit}")

        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
