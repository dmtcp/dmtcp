#!/usr/bin/env python3

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class SourceAuditTest(unittest.TestCase):
    def assert_file_does_not_contain(self, relative_path, forbidden):
        path = ROOT / relative_path
        lines = path.read_text(encoding="utf-8").splitlines()

        matches = [
            f"{relative_path}:{line_number}"
            for line_number, line in enumerate(lines, start=1)
            if forbidden in line
        ]

        self.assertEqual(
            matches,
            [],
            f"old diagnostic token {forbidden!r} remains at {matches}",
        )

    def assert_file_does_not_match(self, relative_path, pattern):
        path = ROOT / relative_path
        text = path.read_text(encoding="utf-8")

        self.assertIsNone(
            re.search(pattern, text),
            f"old source pattern {pattern!r} remains in {relative_path}",
        )

    def test_selected_runtime_paths_use_new_errno_diagnostics(self):
        for relative_path in ("src/writeckpt.cpp", "src/processinfo.cpp"):
            with self.subTest(path=relative_path):
                self.assert_file_does_not_contain(relative_path, "JASSERT_ERRNO")

    def test_checkpoint_serializer_uses_shared_numeric_parsers(self):
        self.assert_file_does_not_contain("src/ckptserializer.cpp", "strtol")

    def test_glibc_version_checks_use_shared_numeric_parsers(self):
        for relative_path in (
            "src/tls.cpp",
            "src/plugin/pid/glibc_pthread.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assert_file_does_not_contain(relative_path, "strtol")

    def test_pid_path_translation_uses_shared_numeric_parsers(self):
        self.assert_file_does_not_contain("src/plugin/pid/pid.cpp", "strtol")

    def test_sshd_cli_uses_shared_numeric_parsers(self):
        self.assert_file_does_not_contain(
            "src/plugin/ssh/dmtcp_sshd.cpp", "atoi")

    def test_dmtcp_launch_java_warning_uses_shared_numeric_parsers(self):
        self.assert_file_does_not_contain("src/dmtcp_launch.cpp", "atol")

    def test_processinfo_rlimit_uses_shared_numeric_parser(self):
        self.assert_file_does_not_contain("src/processinfo.cpp", "atol")

    def test_socket_mpi_spawn_port_uses_shared_numeric_parser(self):
        self.assert_file_does_not_contain(
            "src/plugin/socket/socketconnection.cpp", "atoi")

    def test_processinfo_uses_contains_for_membership_checks(self):
        for pattern in (
            r"_pthreadJoinId\.find\(thread\) != _pthreadJoinId\.end\(\)",
            r"kvmap\.find\(key\) != kvmap\.end\(\)",
        ):
            with self.subTest(pattern=pattern):
                self.assert_file_does_not_match("src/processinfo.cpp",
                                                pattern)

    def test_popen_uses_contains_for_membership_checks(self):
        self.assert_file_does_not_match(
            "src/popen.cpp",
            r"_dmtcpPopenPidMap\.find\(fp\) != _dmtcpPopenPidMap\.end\(\)",
        )

    def test_timerlist_uses_contains_for_timer_info_checks(self):
        self.assert_file_does_not_match(
            "src/plugin/timer/timerlist.cpp",
            r"_timerInfo\.find\([^)]+\) != _timerInfo\.end\(\)",
        )

    def test_restartscript_uses_contains_for_shell_command_checks(self):
        for pattern in (
            r"sshCmdFileNames\.find\(host->first\) != sshCmdFileNames\.end\(\)",
            r"rshCmdFileNames\.find\(host->first\) != rshCmdFileNames\.end\(\)",
        ):
            with self.subTest(pattern=pattern):
                self.assert_file_does_not_match("src/restartscript.cpp",
                                                pattern)

    def test_cli_option_parsing_uses_starts_with_for_prefix_checks(self):
        for relative_path in (
            "src/dmtcp_launch.cpp",
            "src/dmtcprestartinternal.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assert_file_does_not_match(
                    relative_path,
                    r"s\.substr\(0, [12]\) == \"-",
                )

    def test_string_prefix_checks_use_cxx20_helpers(self):
        checks = (
            (
                "src/dmtcp_launch.cpp",
                r"strncmp\(\*argv, \"-Xmx\", sizeof\(\"-Xmx\"\) - 1\) == 0",
            ),
            (
                "src/plugin/file/fileconnection.cpp",
                r"_path\.compare\(0, cwd\.length\(\), cwd\) == 0",
            ),
        )
        for relative_path, pattern in checks:
            with self.subTest(path=relative_path):
                self.assert_file_does_not_match(relative_path, pattern)

    def test_coordinator_clock_gettime_uses_errno_diagnostics(self):
        self.assert_file_does_not_contain(
            "src/dmtcp_coordinator.cpp", "ASSERT_EQ(0, clock_gettime")

    def test_threadsync_wrapper_locks_use_assert_diagnostics(self):
        for forbidden in ("fprintf(stderr", "_exit(DMTCP_FAIL_RC)"):
            with self.subTest(token=forbidden):
                self.assert_file_does_not_contain("src/threadsync.cpp",
                                                  forbidden)

    def test_real_pthread_sigmask_uses_pthread_diagnostics(self):
        for relative_path in (
            "src/signalwrappers.cpp",
            "src/threadwrappers.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assert_file_does_not_contain(relative_path,
                                                  "ASSERT(rc == 0")

    def test_child_thread_signal_set_is_initialized_before_use(self):
        self.assert_file_does_not_match(
            "src/threadwrappers.cpp",
            r"sigset_t set;\n\s*sigaddset\(&set, SigInfo::ckptSignal\(\)\);",
        )

    def test_virtual_pid_env_uses_shared_numeric_parser(self):
        self.assert_file_does_not_contain("src/util_exec.cpp", "sscanf")

    def test_util_assert_avoids_allocation_heavy_formatting(self):
        for relative_path in ("src/util_assert.h", "src/util_assert.cpp"):
            for forbidden in (
                "<format>",
                "<iostream>",
                "<sstream>",
                "std::format",
                "std::ostringstream",
                "std::stringstream",
                "new ",
                "malloc(",
                "free(",
            ):
                with self.subTest(path=relative_path, token=forbidden):
                    self.assert_file_does_not_contain(relative_path,
                                                      forbidden)


if __name__ == "__main__":
    unittest.main()
