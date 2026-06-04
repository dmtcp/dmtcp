#!/usr/bin/env python3

import os
import platform
import unittest

import autotest_config
from dmtcp_test_cases import get_test, iter_tests
from dmtcp_test_harness import (
    DmtcpStatus,
    ROOT,
    TestResult,
    TestSpec,
    checkpoint_payload_succeeded,
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
            "gettid", "file1", "file3", "stat", "mmap1", "mremap",
            "gettimeofday", "sigchild", "rlimit-restore", "poll", "environ",
            "realpath", "pthread1", "pthread2", "pthread4", "pthread5",
            "pthread6", "mutex1", "mutex2", "mutex3", "mutex4", "timer1",
            "clock", "dlopen1",
            "dmtcp5", "shared-fd1", "shared-fd2", "stale-fd",
            "rlimit-nofile", "procfd1", "epoll1", "forkexec",
            "client-server", "seqpacket", "shared-memory1", "shared-memory2",
            "sysv-shm1", "sysv-shm2", "sysv-sem", "sysv-msg",
            "syscall-tester", "file2", "presuspend", "plugin-sleep2",
            "plugin-init", "popen1", "poll-disable-event-plugin", "pthread3",
            "restartdir", "pty1", "pty2", "vfork1", "vfork2", "frisbee",
        ]:
            self.assertIn(name, names)

        syscall_tester = get_test("syscall-tester")
        self.assertEqual(syscall_tester.checkpoint_command, "--kcheckpoint")
        self.assertEqual(syscall_tester.commands,
                         ["--checkpoint-open-files ./test/syscall-tester"])
        restartdir = get_test("restartdir")
        self.assertTrue(restartdir.restart_uses_directory)
        frisbee = get_test("frisbee")
        self.assertEqual(len(frisbee.commands), 3)
        self.assertEqual(frisbee.env["DMTCP_GZIP"], "1")

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


if __name__ == "__main__":
    unittest.main()
