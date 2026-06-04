#!/usr/bin/env python3

import argparse
import sys

from dmtcp_test_cases import get_test, iter_tests
from dmtcp_test_harness import DmtcpHarness


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print per-test status")
    parser.add_argument("--list", action="store_true",
                        help="list tests known to the new harness")
    parser.add_argument("tests", nargs="*", metavar="TESTNAME",
                        help="test names to run")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.list:
        for test in iter_tests():
            print(test.name)
        return 0

    try:
        selected = [get_test(name) for name in args.tests] if args.tests else list(iter_tests())
    except KeyError as error:
        print(f"Unknown test: {error.args[0]}", file=sys.stderr)
        return 2

    harness = DmtcpHarness(verbose=args.verbose)
    passed = 0
    print("== Tests ==")
    for spec in selected:
        if args.verbose:
            print(f"{spec.name}: starting")
        result = harness.run(spec)
        if result.passed:
            passed += 1
            print(f"{spec.name}: PASSED")
        else:
            print(f"{spec.name}: FAILED phase={result.phase} msg={result.message}")
            print(f"{spec.name}: artifacts={result.artifact_dir}")

    print("== Summary ==")
    print(f"{passed} of {len(selected)} tests passed")
    return 0 if passed == len(selected) else 1


if __name__ == "__main__":
    sys.exit(main())
