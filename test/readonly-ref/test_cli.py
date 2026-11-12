#!/usr/bin/env python3

"""Validate native selector arguments before any user program is launched."""

import argparse
import os
import pathlib
import subprocess
import unittest


class CommandLineTests(unittest.TestCase):
    prefix = None

    def launch(self, arguments):
        environment = {
            k: os.environ[k]
            for k in ("PATH", "HOME", "USER") if k in os.environ
        }
        return subprocess.run(
            [str(self.prefix / "bin/dmtcp_launch")] + arguments,
            env=environment,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_help_exposes_native_options(self):
        result = self.launch(["--help"])
        self.assertEqual(result.returncode, 0)
        for flag in (
            "--externalize-readonly",
            "--readonly-include",
            "--readonly-exclude",
        ):
            self.assertIn(flag, result.stdout)

    def test_selector_requires_explicit_enable(self):
        for flag in ("--readonly-include", "--readonly-exclude"):
            with self.subTest(flag=flag):
                result = self.launch([flag, "/opt/assets", "/bin/true"])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("require --externalize-readonly", result.stderr)

    def test_invalid_paths_rejected(self):
        for value in ("*.bin", "/a\n/b", "/" + "a" * 4096):
            for flag in ("--readonly-include", "--readonly-exclude"):
                with self.subTest(value=value[:30], flag=flag):
                    result = self.launch(
                        ["--externalize-readonly", flag, value, "/bin/true"]
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(
                        "requires an absolute path/glob", result.stderr)

    def test_missing_selector_argument(self):
        result = self.launch(["--externalize-readonly", "--readonly-include"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires an absolute path/glob", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    CommandLineTests.prefix = args.prefix.resolve()
    unittest.main(argv=[__file__], verbosity=2)
