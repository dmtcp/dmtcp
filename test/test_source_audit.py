#!/usr/bin/env python3

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class SourceAuditTest(unittest.TestCase):
    def read_text(self, relative_path):
        return (ROOT / relative_path).read_text(encoding="utf-8")

    def extract_function_body(self, relative_path, function_name):
        text = self.read_text(relative_path)
        match = re.search(rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*\{{",
                          text)
        self.assertIsNotNone(match,
                             f"could not find function {function_name} in "
                             f"{relative_path}")

        body_start = match.end()
        depth = 1
        index = body_start
        while index < len(text) and depth > 0:
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
            index += 1

        self.assertEqual(depth, 0,
                         f"could not find end of {function_name} in "
                         f"{relative_path}")
        return text[body_start:index - 1]

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

    def assert_patterns_in_order(self, text, patterns):
        position = 0
        for pattern in patterns:
            match = re.search(pattern, text[position:], re.MULTILINE)
            self.assertIsNotNone(match,
                                 f"pattern {pattern!r} not found in order")
            position += match.end()

    def strip_comments(self, text):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
        return re.sub(r"//.*?$", "", text, flags=re.MULTILINE)

    def old_jalib_diagnostic_files(self):
        diagnostic_pattern = re.compile(
            r"\bJ(?:ASSERT|WARNING|TRACE|NOTE)\b|"
            r"\bJASSERT_(?:STDERR|SET_LOG|CLOSE_STDERR|ERRNO)\b"
        )
        source_suffixes = {".c", ".cc", ".cpp", ".h", ".hpp"}
        files = []
        for root_name in ("include", "src"):
            for path in (ROOT / root_name).rglob("*"):
                if path.suffix not in source_suffixes:
                    continue
                relative_path = path.relative_to(ROOT).as_posix()
                text = path.read_text(encoding="utf-8")
                if diagnostic_pattern.search(text):
                    files.append(relative_path)
        return sorted(files)

    def read_diagnostic_migration_allowlist(self):
        path = ROOT / "test" / "diagnostic_migration_allowlist.txt"
        self.assertTrue(path.exists(),
                        "diagnostic migration allowlist is missing")

        allowed_categories = {
            "coordinator",
            "core-runtime",
            "launcher",
            "plugin",
            "restart",
            "utility",
        }
        allowed_files = set()
        for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), start=1):
            line = line.strip()
            if line == "" or line.startswith("#"):
                continue
            fields = line.split("|", 2)
            self.assertEqual(len(fields), 3,
                             f"{path}:{line_number} must use "
                             "category|path|note")
            category, relative_path, note = (field.strip()
                                             for field in fields)
            self.assertIn(category, allowed_categories,
                          f"{path}:{line_number} has unknown category")
            self.assertTrue(relative_path.startswith(("include/", "src/")),
                            f"{path}:{line_number} has non-source path")
            self.assertNotEqual(note, "",
                                f"{path}:{line_number} needs a note")
            allowed_files.add(relative_path)
        return sorted(allowed_files)

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

    def test_plugin_maps_use_contains_for_membership_checks(self):
        checks = (
            (
                "src/plugin/connectionlist.cpp",
                r"_(?:connections|fdToCon)\.find\([^)]+\) "
                r"(?:==|!=) _(?:connections|fdToCon)\.end\(\)",
            ),
            (
                "src/plugin/svipc/sysvipc.cpp",
                r"_(?:keyMap|map|shmaddrToFlag)\.find\([^)]+\) "
                r"(?:==|!=) _(?:keyMap|map|shmaddrToFlag)\.end\(\)",
            ),
            (
                "src/plugin/socket/kernelbufferdrainer.cpp",
                r"_(?:isSeqpacket|disconnectedSockets)\.find\([^)]+\) "
                r"!= _(?:isSeqpacket|disconnectedSockets)\.end\(\)",
            ),
        )
        for relative_path, pattern in checks:
            with self.subTest(path=relative_path):
                self.assert_file_does_not_match(relative_path, pattern)

    def test_sysvipc_shmaddr_membership_uses_contains(self):
        self.assert_file_does_not_match(
            "src/plugin/svipc/sysvipc.cpp",
            r"_shmaddrToFlag\.find\(\(void \*\)shmaddr\) "
            r"!= _shmaddrToFlag\.end\(\)",
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

    def test_utility_string_equality_uses_cxx20_helpers(self):
        checks = (
            ("src/util_misc.cpp", r"strcmp\(value, \"1\"\) == 0"),
            ("src/util_misc.cpp", r"strcmp\(value, \"0\"\) == 0"),
            ("src/util_misc.cpp", r"strcmp\(path, \"/dev/ptmx\"\) == 0"),
            ("src/util_misc.cpp", r"strcmp\(path_env, stdpath\) == 0"),
            ("src/util_exec.cpp", r"strcmp\(cmd, \"mtcp_restart-32\"\) == 0"),
            ("src/util_exec.cpp", r"strcmp\(compression, \"1\"\) == 0"),
            ("src/util_exec.cpp", r"strcmp\(allocPlugin, \"0\"\) == 0"),
            ("src/util_exec.cpp", r"strcmp\(disableAllPlugins, \"1\"\) == 0"),
            ("src/dmtcp_launch.cpp",
             r"strcmp\(getenv\(ENV_VAR_COMPRESSION\), \"1\"\) == 0"),
            ("src/dmtcp_launch.cpp", r"strcmp\(filename, \"matlab\"\) == 0"),
            ("src/dmtcp_launch.cpp", r"strcmp\(argv\[0\], \"java\"\) == 0"),
            ("src/dmtcp_launch.cpp", r"strcmp\(filename, \"screen\"\) != 0"),
        )
        for relative_path, pattern in checks:
            with self.subTest(path=relative_path, pattern=pattern):
                self.assert_file_does_not_match(relative_path, pattern)

    def test_pty_path_equality_uses_cxx20_helpers(self):
        checks = (
            r"strcmp\(path, \"/dev/tty\"\) == 0",
            r"strcmp\(path, \"/dev/pty\"\) == 0",
            r"strcmp\(path, \"/dev/ptmx\"\) == 0",
            r"strcmp\(path, \"/dev/pts/ptmx\"\) == 0",
        )
        for pattern in checks:
            with self.subTest(pattern=pattern):
                self.assert_file_does_not_match(
                    "src/plugin/file/ptyconnlist.cpp", pattern)

    def test_runtime_string_equality_uses_cxx20_helpers(self):
        checks = (
            ("src/writeckpt.cpp", r"strcmp\(area\.name, \"\[vsyscall\]\"\)"),
            ("src/writeckpt.cpp", r"strcmp\(area\.name, \"\[vectors\]\"\)"),
            ("src/writeckpt.cpp", r"strcmp\(area\.name, \"\[vvar\]\"\)"),
            ("src/writeckpt.cpp",
             r"strcmp\(area\.name, \"\[vvar_vclock\]\"\)"),
            ("src/processinfo.cpp", r"strcmp\(area\.name, \"\[heap\]\"\)"),
            ("src/processinfo.cpp", r"strcmp\(area\.name, \"\[vdso\]\"\)"),
            ("src/processinfo.cpp", r"strcmp\(area\.name, \"\[vvar\]\"\)"),
            ("src/processinfo.cpp",
             r"strcmp\(area\.name, \"\[vvar_vclock\]\"\)"),
            ("src/shareddata.cpp", r"strcmp\(virt, .*\.virt\)"),
            ("src/shareddata.cpp", r"strcmp\(real, .*\.real\)"),
            ("src/plugin/file/ptyconnection.cpp",
             r'_ptsName\.compare\("\?"\) != 0'),
            ("src/pluginmanager.cpp",
             r"strcmp\(internalPlugins\[j\]\.descriptor->pluginName"),
            ("src/pluginmanager.cpp",
             r"strcmp\(entry->descriptor->pluginName, pluginName\)"),
            ("src/pluginmanager.cpp",
             r"strcmp\(descr\.pluginApiVersion, DMTCP_PLUGIN_API_VERSION\)"),
            ("src/dmtcp_coordinator.cpp",
             r"strcmp\(flags\.ckptDir\.c_str\(\), extraData\)"),
            ("src/plugin/socket/connectionmessage.h",
             r"strcmp\(sign, HANDSHAKE_SIGNATURE_MSG\)"),
        )
        for relative_path, pattern in checks:
            with self.subTest(path=relative_path, pattern=pattern):
                self.assert_file_does_not_match(relative_path, pattern)

    def test_hostname_checks_use_string_view_comparison(self):
        for relative_path in (
            "src/dmtcp_coordinator.cpp",
            "src/plugin/ssh/ssh.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assert_file_does_not_contain(relative_path, "strncmp(")

    def test_coordinator_host_assert_uses_string_comparison(self):
        self.assert_file_does_not_contain("src/coordinatorapi.cpp",
                                          "strcmp(host.c_str()")

    def test_vector_value_removal_uses_std_erase(self):
        checks = (
            (
                "src/dmtcp_coordinator.cpp",
                r"clients\.erase\(clients\.begin\(\) \+ i\)",
            ),
            (
                "src/plugin/connection.cpp",
                r"_fds\.erase\(_fds\.begin\(\) \+ i\)",
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

    def test_core_success_asserts_use_comparison_helpers(self):
        self.assert_file_does_not_contain("src/util_exec.cpp",
                                          "ASSERT(rc == 0")
        self.assert_file_does_not_contain("src/util_exec.cpp",
                                          "ASSERT_EQ_MSG(0, safeSystem")

    def test_non_pthread_zero_return_checks_use_named_helpers(self):
        self.assert_file_does_not_contain("src/plugin/file/ptyconnection.cpp",
                                          "ASSERT(ret == 0")

    def test_syscall_return_checks_use_errno_diagnostics(self):
        checks = (
            ("src/dmtcprestartinternal.cpp", "ASSERT_EQ(0, lseek"),
            ("src/plugin/file/fileconnection.cpp", "ASSERT_EQ(0, ftruncate"),
            ("src/plugin/file/fileconnection.cpp", "ASSERT_NE(-1, tempfd"),
        )
        for relative_path, token in checks:
            with self.subTest(path=relative_path, token=token):
                self.assert_file_does_not_contain(relative_path, token)
        self.assert_file_does_not_match(
            "src/processinfo.cpp",
            r"ASSERT_EQ\s*\(\s*0\s*,\s*madvise\s*\(",
        )

    def test_syscall_style_asserts_use_named_helpers(self):
        checks = (
            ("src/dmtcprestartinternal.cpp", r"ASSERT_NE\(-1, fd\)"),
            ("src/plugin/connectionlist.cpp",
             r"ASSERT_ERRNO\(ret != -1,"),
            ("src/plugin/socket/socketconnection.cpp",
             r"ASSERT_ERRNO\(ret == 0,"),
            ("src/plugin/socket/socketwrappers.cpp",
             r"ASSERT_NE\(-1, ret\)"),
        )
        for relative_path, pattern in checks:
            with self.subTest(path=relative_path):
                self.assert_file_does_not_match(relative_path, pattern)

    def test_syscall_style_warnings_use_named_helpers(self):
        for relative_path in (
            "src/plugin/event/eventconnection.cpp",
            "src/plugin/socket/socketconnection.cpp",
        ):
            with self.subTest(path=relative_path):
                self.assert_file_does_not_match(relative_path,
                                                r"WARNING_ERRNO\(ret == 0,")

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

    def test_signal_handler_uses_signal_safe_diagnostics(self):
        body = self.extract_function_body("src/threadlist.cpp",
                                          "stopthisthread")
        self.assertIsNone(
            re.search(r"\b(?:ASSERT|WARNING)(?:_[A-Z0-9]+)?\s*\(", body),
            "stopthisthread is a signal handler; use signal-safe diagnostics",
        )

    def test_old_jalib_diagnostic_usage_is_tracked(self):
        self.assertEqual(self.old_jalib_diagnostic_files(),
                         self.read_diagnostic_migration_allowlist())

    def test_worker_initialization_advances_explicit_phases(self):
        body = self.extract_function_body("src/dmtcpworker.cpp",
                                          "dmtcp_initialize_entry_point")
        self.assert_patterns_in_order(
            body,
            (
                r"advanceWorkerInitPhase\s*\(\s*WorkerInitPhase::Uninitialized\s*,\s*WorkerInitPhase::RuntimePrimitives\s*\)",
                r"initializeRuntimePrimitives\s*\(\s*\);",
                r"advanceWorkerInitPhase\s*\(\s*WorkerInitPhase::RuntimePrimitives\s*,\s*WorkerInitPhase::BootstrapThreadState\s*\)",
                r"initializeBootstrapThreadState\s*\(\s*\);",
                r"advanceWorkerInitPhase\s*\(\s*WorkerInitPhase::BootstrapThreadState\s*,\s*WorkerInitPhase::PluginManagerAndProcessState\s*\)",
                r"initializePluginManagerAndProcessState\s*\(\s*\);",
                r"advanceWorkerInitPhase\s*\(\s*WorkerInitPhase::PluginManagerAndProcessState\s*,\s*WorkerInitPhase::RuntimeOptions\s*\)",
                r"initializeRuntimeOptions\s*\(\s*\);",
                r"advanceWorkerInitPhase\s*\(\s*WorkerInitPhase::RuntimeOptions\s*,\s*WorkerInitPhase::PluginsAndCheckpointThread\s*\)",
                r"initializePluginsAndCheckpointThread\s*\(\s*\);",
                r"advanceWorkerInitPhase\s*\(\s*WorkerInitPhase::PluginsAndCheckpointThread\s*,\s*WorkerInitPhase::Complete\s*\)",
            ),
        )

    def test_worker_plugin_init_contract_is_checked(self):
        body = self.extract_function_body("src/dmtcpworker.cpp",
                                          "initializePluginsAndCheckpointThread")
        self.assert_patterns_in_order(
            body,
            (
                r"assertWorkerInitPhase\s*\(\s*WorkerInitPhase::PluginsAndCheckpointThread",
                r"PluginManager::eventHook\s*\(\s*DMTCP_EVENT_INIT\s*,\s*NULL\s*\)",
                r"tzset\s*\(\s*\);",
                r"ThreadList::createCkptThread\s*\(\s*\);",
            ),
        )
        body_without_comments = self.strip_comments(body).strip()
        self.assertRegex(body_without_comments,
                         r"ThreadList::createCkptThread\s*\(\s*\);\s*$")

    def test_restart_reestablishes_curthread_after_tls_restore(self):
        post_restart_body = self.extract_function_body("src/threadlist.cpp",
                                                       "ThreadList::postRestart")
        self.assert_patterns_in_order(
            post_restart_body,
            (
                r"TLSInfo_RestoreTLSState\s*\(\s*motherofall\s*\);",
                r"TLSInfo_RestoreTLSTidPid\s*\(\s*motherofall\s*\);",
                r"curThread\s*=\s*motherofall\s*;",
                r"motherpid\s*=\s*getpid\s*\(\s*\);",
            ),
        )

        restart_thread_body = self.extract_function_body("src/threadlist.cpp",
                                                         "restarthread")
        self.assert_patterns_in_order(
            restart_thread_body,
            (
                r"TLSInfo_RestoreTLSState\s*\(\s*thread\s*\);",
                r"TLSInfo_RestoreTLSTidPid\s*\(\s*thread\s*\);",
                r"curThread\s*=\s*thread\s*;",
                r"TLSInfo_HaveThreadSysinfoOffset\s*\(\s*\)",
            ),
        )


if __name__ == "__main__":
    unittest.main()
