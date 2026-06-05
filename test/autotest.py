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
    parser.add_argument("--retry-once", action="store_true",
                        help="retry a failing test once before reporting it")
    parser.add_argument("--retain-success-artifacts", action="store_true",
                        help="keep per-test artifact directories for "
                             "successful tests")
    parser.add_argument("--tag", action="append", default=[],
                        help="run or list tests with this metadata tag")
    parser.add_argument("--requires", action="append", default=[],
                        help="run or list tests with this requirement marker")
    parser.add_argument("tests", nargs="*", metavar="TESTNAME",
                        help="test names to run")
    return parser.parse_args()


def report(message):
    print(message, flush=True)


def _format_csv(values):
    return ",".join(str(value) for value in values)


def format_list_entry(spec):
    fields = [
        spec.name,
        f"peers={_format_csv(spec.peer_counts())}",
        f"cycles={spec.cycles}",
    ]
    if spec.tags:
        fields.append(f"tags={_format_csv(spec.tags)}")
    if spec.requirements:
        fields.append(f"requires={_format_csv(spec.requirements)}")
    if spec.limits:
        fields.append(f"limits={_format_csv(spec.limits)}")
    return "\t".join(fields)


def _has_all(values, required):
    return all(value in values for value in required)


def filter_tests_by_metadata(tests, tags=None, requirements=None):
    tags = tags or []
    requirements = requirements or []
    return [
        test for test in tests
        if _has_all(test.tags, tags) and
           _has_all(test.requirements, requirements)
    ]


def select_tests(names=None, tags=None, requirements=None):
    if names:
        tests = [get_test(name) for name in names]
    else:
        tests = list(iter_tests())
    return filter_tests_by_metadata(tests, tags, requirements)


def run_with_optional_retry(harness, spec, retry_once):
    result = harness.run(spec)
    if result.passed or not retry_once:
        return result

    report(f"{spec.name}: retrying after phase={result.phase} "
           f"msg={result.message}")
    retry_result = harness.run(spec)
    if not retry_result.passed:
        return retry_result

    report(f"{spec.name}: PASSED on retry")
    return retry_result


def main():
    args = parse_args()
    if args.list:
        for test in select_tests(args.tests, args.tag, args.requires):
            print(format_list_entry(test))
        return 0

    try:
        selected = select_tests(args.tests, args.tag, args.requires)
    except KeyError as error:
        print(f"Unknown test: {error.args[0]}", file=sys.stderr)
        return 2

    if not selected:
        print("No tests selected", file=sys.stderr)
        return 2

    harness = DmtcpHarness(
        verbose=args.verbose,
        retain_success_artifacts=args.retain_success_artifacts,
    )
    passed = 0
    report("== Tests ==")
    for spec in selected:
        if args.verbose:
            report(f"{spec.name}: starting")
        result = run_with_optional_retry(harness, spec, args.retry_once)
        if result.passed:
            passed += 1
            report(f"{spec.name}: PASSED")
            if result.artifact_dir is not None:
                report(f"{spec.name}: artifacts={result.artifact_dir}")
        else:
            report(f"{spec.name}: FAILED phase={result.phase} "
                   f"msg={result.message}")
            report(f"{spec.name}: artifacts={result.artifact_dir}")

    report("== Summary ==")
    report(f"{passed} of {len(selected)} tests passed")
    return 0 if passed == len(selected) else 1


if __name__ == "__main__":
    sys.exit(main())
