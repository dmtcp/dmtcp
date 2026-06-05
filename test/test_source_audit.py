#!/usr/bin/env python3

import pathlib
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

    def test_coordinator_clock_gettime_uses_errno_diagnostics(self):
        self.assert_file_does_not_contain(
            "src/dmtcp_coordinator.cpp", "ASSERT_EQ(0, clock_gettime")

    def test_threadsync_wrapper_locks_use_assert_diagnostics(self):
        for forbidden in ("fprintf(stderr", "_exit(DMTCP_FAIL_RC)"):
            with self.subTest(token=forbidden):
                self.assert_file_does_not_contain("src/threadsync.cpp",
                                                  forbidden)


if __name__ == "__main__":
    unittest.main()
