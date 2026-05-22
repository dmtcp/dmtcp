#!/usr/bin/env python3
"""Static closeout-scope verifier for M003 internal plugin consolidation.

Modes:
  self-test  inline positive/negative fixtures for this verifier contract
  full       validate the repo-local M003 closeout document and explicit anchors

The verifier intentionally reads only the explicit repository-relative paths in
EXPLICIT_INPUTS.  It does not read hidden planning state such as .gsd, .git,
.planning, or .audits, and it does not walk the repository.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
CLOSEOUT_DOC = "doc/internal-plugin-consolidation-m003-closeout.md"
THIS_VERIFIER = "test/verify-consolidation-closeout-scope.py"

EXPLICIT_INPUTS = (
    CLOSEOUT_DOC,
    "doc/internal-plugin-consolidation-design-plan.md",
    "doc/internal-plugin-consolidation-migration-plan.md",
    "doc/internal-plugin-consolidation-registration-architecture.md",
    "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
    "src/Makefile.am",
    "src/plugin/Makefile.am",
    "src/dmtcp_launch.cpp",
    "src/execwrappers.cpp",
    "src/util_exec.cpp",
    "src/pluginmanager.cpp",
    "include/dmtcp.h",
    "test/autotest.py",
    "test/plugin-init.cpp",
    "test/dlopen1.c",
    "test/dlopen2.cpp",
    "test/verify-timer-built-in-gate.py",
    "test/verify-svipc-built-in-gate.py",
    "test/verify-ipc-built-in-gate.py",
    "test/verify-pid-built-in-gate.py",
    "test/verify-alloc-built-in-gate.py",
    "test/verify-dl-built-in-gate.py",
    "test/verify-consolidation-preflight.py",
    "test/verify-consolidation-registration-plan.py",
    "test/verify-consolidation-migration-plan.py",
    THIS_VERIFIER,
)

HIDDEN_COMPONENTS = (".git", ".gsd", ".planning", ".audits", ".hg", ".svn")
ALLOWED_ROOTS = ("doc", "src", "test", "include")

REMOVED_DSOS = (
    "libdmtcp_timer.so",
    "libdmtcp_svipc.so",
    "libdmtcp_ipc.so",
    "libdmtcp_pid.so",
    "libdmtcp_alloc.so",
    "libdmtcp_dl.so",
)

REQUIRED_HEADINGS = (
    "M003 scope statement",
    "Final evidence map",
    "Removed DSO absence evidence",
    "D019 broad-suite policy",
    "Requirement scope table",
    "Explicit non-proofs and handoffs",
    "Verifier contract",
)

REQUIRED_DOC_REFERENCES = (
    "src/Makefile.am",
    "src/plugin/Makefile.am",
    "src/dmtcp_launch.cpp",
    "src/execwrappers.cpp",
    "src/util_exec.cpp",
    "src/pluginmanager.cpp",
    "include/dmtcp.h",
    "doc/internal-plugin-consolidation-registration-architecture.md",
    "doc/internal-plugin-consolidation-migration-plan.md",
    "test/autotest.py",
    "test/plugin-init.cpp",
    "test/dlopen1.c",
    "test/dlopen2.cpp",
    "test/verify-timer-built-in-gate.py",
    "test/verify-svipc-built-in-gate.py",
    "test/verify-ipc-built-in-gate.py",
    "test/verify-pid-built-in-gate.py",
    "test/verify-alloc-built-in-gate.py",
    "test/verify-dl-built-in-gate.py",
    "test/verify-consolidation-preflight.py",
    "test/verify-consolidation-registration-plan.py",
    "test/verify-consolidation-migration-plan.py",
    THIS_VERIFIER,
)

EXPECTED_REQUIREMENT_FRAGMENTS = {
    "R001": ("already validated by m001", "not re-owned by s07"),
    "R002": ("already validated by m001", "not re-owned by s07"),
    "R003": ("already validated by m002", "not re-owned by s07"),
    "R004": ("m003 covered",),
    "R005": ("m003 directly supported",),
    "R006": ("m003 covered",),
    "R007": ("m003 directly supported",),
    "R008": ("m003 covered",),
    "R009": ("support-only smoke evidence", "m005"),
    "R010": ("m005-owned",),
    "R011": ("m004-owned",),
    "R012": ("deferred",),
    "R013": ("deferred",),
    "R014": ("support-only smoke evidence", "m005"),
    "R015": ("out of scope",),
}

FORBIDDEN_ROW_FRAGMENTS = {
    "R009": ("m003 covered", "m003 delivered", "validated by m003", "full compatibility proof"),
    "R010": ("m003 covered", "m003 delivered", "delivered by m003", "validated by m003"),
    "R011": ("m003 covered", "m003 delivered", "delivered by m003", "validated by m003"),
    "R012": ("m003 covered", "m003 delivered", "validated by m003", "tsan guarantee"),
    "R013": ("m003 covered", "m003 delivered", "validated by m003", "overhead threshold enforced"),
    "R014": ("m003 covered", "m003 delivered", "validated by m003", "full compatibility proof"),
    "R015": ("m003 covered", "m003 delivered", "validated by m003"),
}

SENSITIVE_TERMS = (
    "tsan",
    "threadsanitizer",
    "sanitizer",
    "rtld_next",
    "dlsym(rtld_next)",
    "dl_iterate_phdr",
    "default-version",
    "default version",
    "wrapper-stress",
    "wrapper stress",
    "full optional/external plugin compatibility",
    "optional/external plugin compatibility",
    "external plugin update recipe",
    "external plugin maintainer recipe",
)

PROOF_VERBS = (
    "prove",
    "proves",
    "proved",
    "proven",
    "validate",
    "validates",
    "validated",
    "guarantee",
    "guarantees",
    "guaranteed",
    "deliver",
    "delivers",
    "delivered",
    "passed",
    "green",
)

SAFE_CLAIM_TOKENS = (
    "does not",
    "do not",
    "not prove",
    "not proven",
    "not a proof",
    "no m003 proof",
    "defer",
    "deferred",
    "handoff",
    "m004-owned",
    "m005-owned",
    "m004 owns",
    "m005 owns",
    "support-only smoke",
    "future evidence",
    "outside m003",
    "remains m004",
    "remains m005",
)


class Result:
    def __init__(self, emit: bool = True) -> None:
        self.emit = emit
        self.passes = 0
        self.failures: list[tuple[str, str, list[str]]] = []

    def pass_(self, contract: str, msg: str) -> None:
        self.passes += 1
        if self.emit:
            print("PASS {0}: {1}".format(contract, msg))

    def fail(self, contract: str, msg: str, details: list[str] | None = None) -> None:
        self.failures.append((contract, msg, details or []))
        if self.emit:
            print("FAIL {0}: {1}".format(contract, msg))
            for detail in details or []:
                print("  - {0}".format(detail))

    def finish(self) -> int:
        if self.failures:
            print(
                "FAIL summary: {0} contract(s) failed; {1} contract(s) passed.".format(
                    len(self.failures), self.passes
                )
            )
            return 1
        print("PASS summary: {0} contract(s) passed.".format(self.passes))
        return 0


class Reader:
    def __init__(self, fixtures: dict[str, str] | None = None, allowed: tuple[str, ...] = EXPLICIT_INPUTS) -> None:
        self.fixtures = fixtures
        self.allowed = allowed

    def read_text(self, rel: str, result: Result, contract: str) -> str:
        if rel not in self.allowed or path_errors(rel, require_exists=False):
            result.fail("input-scope", "attempted to inspect a hidden or unapproved path", [rel])
            return ""
        if self.fixtures is not None:
            if rel not in self.fixtures:
                result.fail(contract, "explicit fixture is missing", [rel])
                return ""
            return self.fixtures[rel]
        try:
            return (REPO_ROOT / rel).read_text(encoding="utf-8")
        except Exception as exc:  # pragma: no cover - exercised by full mode only
            result.fail(contract, "required file is not readable", ["{0}: {1}".format(rel, exc)])
            return ""


def path_errors(rel: str, require_exists: bool) -> list[str]:
    errors: list[str] = []
    path = Path(rel)
    parts = path.parts
    if not rel:
        errors.append("empty path")
    if path.is_absolute():
        errors.append("absolute path: {0}".format(rel))
    if ".." in parts:
        errors.append("parent traversal: {0}".format(rel))
    if not parts or parts[0] not in ALLOWED_ROOTS:
        errors.append("path is outside explicit doc/source/test roots: {0}".format(rel))
    hidden = [part for part in parts if part in HIDDEN_COMPONENTS or part.startswith(".")]
    if hidden:
        errors.append("hidden component in path {0}: {1}".format(rel, "/".join(hidden)))
    if require_exists and not (REPO_ROOT / rel).is_file():
        errors.append("path does not exist: {0}".format(rel))
    return errors


def validate_input_scope(result: Result, inputs: tuple[str, ...] = EXPLICIT_INPUTS, require_exists: bool = True) -> None:
    details: list[str] = []
    seen: set[str] = set()
    for rel in inputs:
        if rel in seen:
            details.append("duplicate explicit input: {0}".format(rel))
        seen.add(rel)
        details.extend(path_errors(rel, require_exists=require_exists))
    if details:
        result.fail("input-scope", "explicit input list violates repository-relative source contract", details)
    else:
        result.pass_("input-scope", "all verifier inputs are explicit non-hidden repository-relative doc/source/test paths")


def extract_section(text: str, heading: str) -> str:
    pattern = re.compile(r"^##\s+" + re.escape(heading) + r"\s*$", re.M)
    match = pattern.search(text)
    if not match:
        return ""
    next_heading = re.search(r"^##\s+", text[match.end():], re.M)
    if not next_heading:
        return text[match.end():]
    return text[match.end():match.end() + next_heading.start()]


def validate_headings(result: Result, text: str) -> None:
    missing = [heading for heading in REQUIRED_HEADINGS if not re.search(r"^##\s+" + re.escape(heading) + r"\s*$", text, re.M)]
    if missing:
        result.fail("closeout-headings", "closeout document is missing required sections", missing)
    else:
        result.pass_("closeout-headings", "closeout document has all required scope/evidence sections")


def validate_no_hidden_doc_refs(result: Result, text: str) -> None:
    hits = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if re.search(r"(^|[\s`(])\.(git|gsd|planning|audits)(/|\b)", line):
            hits.append("line {0}: {1}".format(lineno, line.strip()))
    if hits:
        result.fail("hidden-path-reference", "closeout document cites hidden planning/audit paths", hits)
    else:
        result.pass_("hidden-path-reference", "closeout document avoids hidden planning/audit path citations")


def validate_doc_references(result: Result, text: str) -> None:
    missing = [rel for rel in REQUIRED_DOC_REFERENCES if rel not in text]
    if missing:
        result.fail("evidence-references", "closeout document is missing source/test evidence references", missing)
    else:
        result.pass_("evidence-references", "closeout evidence map names the expected explicit source/test anchors")


def validate_removed_dso_section(result: Result, text: str) -> None:
    section = extract_section(text, "Removed DSO absence evidence")
    if not section:
        result.fail("removed-dso-evidence", "removed DSO absence section is missing", [])
        return
    missing = [dso for dso in REMOVED_DSOS if dso not in section]
    if missing:
        result.fail("removed-dso-evidence", "removed DSO absence section omits former internal DSO names", missing)
    else:
        result.pass_("removed-dso-evidence", "removed DSO absence section names all six former internal plugin DSOs")


def markdown_requirement_rows(text: str) -> dict[str, str]:
    rows: dict[str, str] = {}
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            continue
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        if not cells:
            continue
        if re.fullmatch(r"R\d{3}", cells[0]):
            rows[cells[0]] = " | ".join(cells)
    return rows


def validate_requirement_scope(result: Result, text: str) -> None:
    rows = markdown_requirement_rows(text)
    expected_ids = set(EXPECTED_REQUIREMENT_FRAGMENTS)
    details: list[str] = []
    missing = sorted(expected_ids - set(rows))
    extra = sorted(set(rows) - expected_ids)
    if missing:
        details.extend("missing requirement row: {0}".format(rid) for rid in missing)
    if extra:
        details.extend("unexpected requirement row: {0}".format(rid) for rid in extra)
    for rid, fragments in EXPECTED_REQUIREMENT_FRAGMENTS.items():
        row = rows.get(rid, "")
        lower = row.lower()
        for fragment in fragments:
            if fragment not in lower:
                details.append("{0} missing classification fragment: {1}".format(rid, fragment))
        for fragment in FORBIDDEN_ROW_FRAGMENTS.get(rid, ()):  # reject mixed/contradictory rows too
            if fragment in lower:
                details.append("{0} contains forbidden M003 ownership fragment: {1}".format(rid, fragment))
    if details:
        result.fail("requirement-scope", "requirement table does not preserve M003/M004/M005/deferred scope", details)
    else:
        result.pass_("requirement-scope", "requirement table maps R001-R015 to covered, support-only, deferred, or out-of-scope owners")


def validate_d019_policy(result: Result, text: str) -> None:
    section = extract_section(text, "D019 broad-suite policy")
    required = (
        "make check is run standalone",
        "a non-green result is not called green",
        "d019 classification is allowed only after static closeout gates and focused runtime tests pass",
    )
    lower = section.lower().replace("`", "")
    missing = [phrase for phrase in required if phrase not in lower]
    if missing:
        result.fail("d019-broad-suite-policy", "D019 broad-suite policy is incomplete", missing)
    else:
        result.pass_("d019-broad-suite-policy", "D019 policy keeps make check standalone and non-green caveats explicit")


def sentence_like_units(text: str) -> list[tuple[int, str]]:
    units: list[tuple[int, str]] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for part in re.split(r"(?<=[.!?])\s+", line):
            if part.strip():
                units.append((lineno, part.strip()))
    return units


def validate_non_proof_statement(result: Result, text: str) -> None:
    section = extract_section(text, "Explicit non-proofs and handoffs")
    lower = section.lower()
    required = (
        "m003 does not prove",
        "sanitizer",
        "wrapper-stress",
        "deep `dlsym(rtld_next)`",
        "`dl_iterate_phdr`",
        "default-version lookup",
        "full optional/external plugin compatibility",
        "m004 owns",
        "m005 owns",
    )
    missing = [phrase for phrase in required if phrase not in lower]
    if missing:
        result.fail("non-proof-statement", "explicit non-proof/handoff statement is incomplete", missing)
    else:
        result.pass_("non-proof-statement", "M004/M005 non-proof handoffs are explicit")


def validate_unsupported_claims(result: Result, text: str) -> None:
    hits = []
    for lineno, unit in sentence_like_units(text):
        lower = unit.lower().replace("`", "")
        if not any(term in lower for term in SENSITIVE_TERMS):
            continue
        if not any(verb in lower for verb in PROOF_VERBS):
            continue
        if any(token in lower for token in SAFE_CLAIM_TOKENS):
            continue
        hits.append("line {0}: {1}".format(lineno, unit))
    if hits:
        result.fail("unsupported-proof-claim", "closeout text over-claims M004/M005-owned proof", hits)
    else:
        result.pass_("unsupported-proof-claim", "closeout text does not over-claim sanitizer, RTLD_NEXT, loader, or external-plugin proof")


def validate_self_commands(result: Result, text: str) -> None:
    required = (
        "python3 test/verify-consolidation-closeout-scope.py self-test",
        "python3 test/verify-consolidation-closeout-scope.py full",
    )
    missing = [cmd for cmd in required if cmd not in text]
    if missing:
        result.fail("verifier-commands", "closeout document omits closeout verifier commands", missing)
    else:
        result.pass_("verifier-commands", "closeout document records self-test and full verifier modes")


def validate_closeout_doc(result: Result, text: str) -> None:
    validate_headings(result, text)
    validate_no_hidden_doc_refs(result, text)
    validate_doc_references(result, text)
    validate_removed_dso_section(result, text)
    validate_requirement_scope(result, text)
    validate_d019_policy(result, text)
    validate_non_proof_statement(result, text)
    validate_unsupported_claims(result, text)
    validate_self_commands(result, text)


def validate_supporting_inputs(result: Result, reader: Reader) -> None:
    for rel in EXPLICIT_INPUTS:
        reader.read_text(rel, result, "explicit-input-readable")
    if any(contract == "explicit-input-readable" for contract, _msg, _details in result.failures):
        return
    result.pass_("explicit-input-readable", "all explicit closeout doc/source/test inputs are readable")


def positive_doc_fixture() -> str:
    dso_list = "\n".join("- `{0}`".format(dso) for dso in REMOVED_DSOS)
    rows = "\n".join(
        (
            "| R001 | Already validated by M001 | Already validated by M001; not re-owned by S07 | design plan only |",
            "| R002 | Already validated by M001 | Already validated by M001; not re-owned by S07 | wrapper-order analysis only |",
            "| R003 | Already validated by M002 | Already validated by M002; not re-owned by S07 | staged decomposition only |",
            "| R004 | M003 covered | M003-owned implementation closeout | six internal DSOs absent |",
            "| R005 | M003 directly supported | M003 plus M004 support boundary | focused behavior plus standalone make check caveat |",
            "| R006 | M003 covered | M003-owned implementation closeout | central wrapper architecture |",
            "| R007 | M003 directly supported | M003 implementation evidence with M004 sanitizer handoff | static initialization review |",
            "| R008 | M003 covered | M003-owned implementation closeout | disable flags preserved |",
            "| R009 | Support-only smoke evidence | M005 owns full optional/external plugin evaluation | smoke only |",
            "| R010 | M005-owned | M005-owned external plugin maintainer recipe | no M003 recipe deliverable |",
            "| R011 | M004-owned | M004-owned sanitizer and wrapper-order stress | no M003 proof |",
            "| R012 | Deferred | Deferred TSAN checkpoint/restart guarantee | future evidence required |",
            "| R013 | Deferred | Deferred runtime-overhead threshold | no threshold selected |",
            "| R014 | Support-only smoke evidence | M005 owns full optional/external plugin compatibility | smoke only |",
            "| R015 | Out of scope | Out of scope | no autotest harness refactor |",
        )
    )
    refs = "\n".join("- `{0}`".format(rel) for rel in REQUIRED_DOC_REFERENCES)
    return """# Fixture\n\n## M003 scope statement\nM003 owns the implementation closeout and S07 does not create a new implementation promise.\n\n## Final evidence map\n{refs}\n\n## Removed DSO absence evidence\n{dso_list}\n\n## D019 broad-suite policy\n`make check` is run standalone. A non-green result is not called green. D019 classification is allowed only after static closeout gates and focused runtime tests pass.\n\n## Requirement scope table\n| Requirement | M003 closeout classification | Owner / status | Evidence boundary |\n|---|---|---|---|\n{rows}\n\n## Explicit non-proofs and handoffs\nM003 does not prove sanitizer, wrapper-stress, deep `dlsym(RTLD_NEXT)`, `dl_iterate_phdr`, default-version lookup, or full optional/external plugin compatibility. M004 owns sanitizer and loader stress. M005 owns optional/external plugin compatibility.\n\n## Verifier contract\npython3 test/verify-consolidation-closeout-scope.py self-test\npython3 test/verify-consolidation-closeout-scope.py full\n""".format(refs=refs, dso_list=dso_list, rows=rows)


def expect_failure(name: str, validator, contract: str) -> bool:
    result = Result(emit=False)
    validator(result)
    if not any(seen == contract for seen, _msg, _details in result.failures):
        print("FAIL self-test: {0} fixture was not rejected for {1}".format(name, contract))
        for seen, msg, details in result.failures:
            print("  - observed {0}: {1}: {2}".format(seen, msg, "; ".join(details)))
        return False
    return True


def self_test() -> int:
    doc = positive_doc_fixture()
    fixtures = {rel: "fixture for {0}\n".format(rel) for rel in EXPLICIT_INPUTS}
    fixtures[CLOSEOUT_DOC] = doc
    reader = Reader(fixtures)

    ok = Result(emit=False)
    validate_input_scope(ok, require_exists=False)
    validate_supporting_inputs(ok, reader)
    validate_closeout_doc(ok, doc)
    if ok.failures:
        print("FAIL self-test: positive fixture unexpectedly failed")
        for contract, msg, details in ok.failures:
            print("  - {0}: {1}: {2}".format(contract, msg, "; ".join(details)))
        return 1

    missing_req = doc.replace("| R008 | M003 covered | M003-owned implementation closeout | disable flags preserved |\n", "")
    if not expect_failure("missing requirement ID", lambda r: validate_requirement_scope(r, missing_req), "requirement-scope"):
        return 1

    bad_r011 = doc.replace("| R011 | M004-owned | M004-owned sanitizer and wrapper-order stress | no M003 proof |",
                           "| R011 | M003 covered | M003 delivered sanitizer and wrapper-order stress | claimed delivery |")
    if not expect_failure("R011 misclassified as delivered", lambda r: validate_requirement_scope(r, bad_r011), "requirement-scope"):
        return 1

    bad_r010 = doc.replace("| R010 | M005-owned | M005-owned external plugin maintainer recipe | no M003 recipe deliverable |",
                           "| R010 | M003 covered | M003 delivered external plugin maintainer recipe | claimed delivery |")
    if not expect_failure("R010 misclassified as delivered", lambda r: validate_requirement_scope(r, bad_r010), "requirement-scope"):
        return 1

    missing_dso = doc.replace("- `libdmtcp_pid.so`\n", "")
    if not expect_failure("omitted removed DSO", lambda r: validate_removed_dso_section(r, missing_dso), "removed-dso-evidence"):
        return 1

    if not expect_failure("hidden input list", lambda r: validate_input_scope(r, EXPLICIT_INPUTS + (".gsd/hidden.md",), require_exists=False), "input-scope"):
        return 1

    if not expect_failure("hidden path read", lambda r: Reader(fixtures).read_text(".gsd/hidden.md", r, "hidden-read"), "input-scope"):
        return 1

    sanitizer_claim = doc + "\nM003 proves sanitizer and deep dlsym(RTLD_NEXT) stress.\n"
    if not expect_failure("sanitizer proof claim", lambda r: validate_unsupported_claims(r, sanitizer_claim), "unsupported-proof-claim"):
        return 1

    rtld_claim = doc + "\nM003 validated RTLD_NEXT stress and dl_iterate_phdr behavior as green.\n"
    if not expect_failure("RTLD_NEXT stress proof claim", lambda r: validate_unsupported_claims(r, rtld_claim), "unsupported-proof-claim"):
        return 1

    print("PASS self-test: closeout verifier accepts the positive fixture and rejects missing requirement IDs, R010/R011 M003 delivery claims, removed-DSO omissions, hidden path reads, and sanitizer/RTLD_NEXT proof claims")
    return 0


def full() -> int:
    result = Result()
    reader = Reader()
    validate_input_scope(result)
    validate_supporting_inputs(result, reader)
    closeout = reader.read_text(CLOSEOUT_DOC, result, "closeout-readable")
    if closeout:
        validate_closeout_doc(result, closeout)
    return result.finish()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("self-test", "full"))
    args = parser.parse_args(argv)
    if args.mode == "self-test":
        return self_test()
    return full()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
