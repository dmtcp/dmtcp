#!/usr/bin/env python3
"""Static checks for the internal-plugin consolidation wrapper/TSAN plan.

`preload-map` validates the current-order appendix sections produced first for
M001/S01.  `full` validates the complete S01 appendix, including R002 coverage,
TSAN/RTLD_NEXT/dl_iterate_phdr feasibility, follow-up experiments, and handoff
constraints.  Both modes inspect a small, explicit set of non-hidden repository
paths instead of walking the repo, so generated planning artifacts cannot
accidentally satisfy source-citation requirements.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DOC_PATH = Path("doc/internal-plugin-consolidation-wrapper-order-tsan.md")

PRELOAD_REQUIRED_HEADINGS = [
    "Scope and R002 evidence boundary",
    "Current dmtcp_launch LD_PRELOAD construction",
    "User DMTCP_PLUGIN and original LD_PRELOAD placement",
    "Built-in plugin shared-object order",
    "ENV_VAR_HIJACK_LIBS capture point",
    "libdmtcp constructor entry",
    "dmtcp_initialize and dmtcp_prepare_wrappers",
    "PluginManager initialization and registration chain",
    "DMTCP_DECL_PLUGIN and NEXT_FNC chaining",
    "Forward versus reverse event order",
    "Current dlsym RTLD_NEXT and dl_iterate_phdr risk",
    "Immediate consolidation implications for S02 and S03",
]

FULL_REQUIRED_HEADINGS = [
    *PRELOAD_REQUIRED_HEADINGS[:-1],
    "R002 coverage checklist",
    "TSAN feasibility assessment",
    "dlsym RTLD_NEXT behavior under DMTCP",
    "dl_iterate_phdr feasibility and bypass options",
    "What consolidation solves",
    "What consolidation cannot prove or solve in S01",
    "Likely gaps before design",
    "M004/R011 follow-up experiment matrix",
    "Handoff constraints for S02/S03/S04",
    PRELOAD_REQUIRED_HEADINGS[-1],
]

PRELOAD_REQUIRED_FILES = [
    "src/dmtcp_launch.cpp",
    "src/dmtcpworker.cpp",
    "src/pluginmanager.cpp",
    "include/dmtcp.h",
    "src/syscallsreal.c",
    "src/Makefile.am",
    "src/plugin/Makefile.am",
    "src/plugin/ipc/ipc.cpp",
    "src/plugin/timer/timerlist.cpp",
    "src/plugin/svipc/sysvipc.cpp",
    "src/plugin/pid/pid.cpp",
]

FULL_REQUIRED_FILES = sorted(
    set(
        PRELOAD_REQUIRED_FILES
        + [
            "src/dlwrappers.cpp",
            "src/dmtcp_dlsym_wrappers.cpp",
            "src/dmtcp_dlsym.cpp",
            "doc/dmtcp_dlsym.txt",
            "src/plugin/dl/dlwrappers.cpp",
        ]
    )
)

PRELOAD_SECTION_CITATIONS = {
    "Scope and R002 evidence boundary": ["src/dlwrappers.cpp", "src/dmtcpworker.cpp"],
    "Current dmtcp_launch LD_PRELOAD construction": ["src/dmtcp_launch.cpp"],
    "User DMTCP_PLUGIN and original LD_PRELOAD placement": [
        "src/dmtcp_launch.cpp",
        "src/dmtcpworker.cpp",
    ],
    "Built-in plugin shared-object order": [
        "src/dmtcp_launch.cpp",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
    ],
    "ENV_VAR_HIJACK_LIBS capture point": [
        "src/dmtcp_launch.cpp",
        "src/dmtcpworker.cpp",
    ],
    "libdmtcp constructor entry": ["src/dmtcpworker.cpp"],
    "dmtcp_initialize and dmtcp_prepare_wrappers": [
        "src/dmtcpworker.cpp",
        "src/syscallsreal.c",
        "include/dmtcp.h",
    ],
    "PluginManager initialization and registration chain": [
        "src/pluginmanager.cpp",
        "src/dmtcpworker.cpp",
    ],
    "DMTCP_DECL_PLUGIN and NEXT_FNC chaining": [
        "include/dmtcp.h",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/pid/pid.cpp",
    ],
    "Forward versus reverse event order": ["src/pluginmanager.cpp"],
    "Current dlsym RTLD_NEXT and dl_iterate_phdr risk": [
            "include/dmtcp.h",
        "src/syscallsreal.c",
        "src/dlwrappers.cpp",
        "src/dmtcp_dlsym_wrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "doc/dmtcp_dlsym.txt",
    ],
    "Immediate consolidation implications for S02 and S03": [
            "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "src/dlwrappers.cpp",
    ],
}

FULL_SECTION_CITATIONS = {
    **PRELOAD_SECTION_CITATIONS,
    "R002 coverage checklist": [
            "src/dmtcp_launch.cpp",
        "src/dmtcpworker.cpp",
        "src/dlwrappers.cpp",
        "src/dmtcp_dlsym.cpp",
    ],
    "TSAN feasibility assessment": [
            "src/dlwrappers.cpp",
        "src/dmtcpworker.cpp",
        "src/syscallsreal.c",
        "src/dmtcp_dlsym_wrappers.cpp",
    ],
    "dlsym RTLD_NEXT behavior under DMTCP": [
        "src/dlwrappers.cpp",
        "src/dmtcp_dlsym_wrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "doc/dmtcp_dlsym.txt",
        "include/dmtcp.h",
        "src/pluginmanager.cpp",
    ],
    "dl_iterate_phdr feasibility and bypass options": [
            "src/dlwrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "include/dmtcp.h",
    ],
    "What consolidation solves": [
        "src/dmtcp_launch.cpp",
        "src/dmtcpworker.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
    ],
    "What consolidation cannot prove or solve in S01": [
            "src/dlwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "src/syscallsreal.c",
        "doc/dmtcp_dlsym.txt",
    ],
    "Likely gaps before design": [
            "src/dlwrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "src/pluginmanager.cpp",
    ],
    "M004/R011 follow-up experiment matrix": [
            "src/dlwrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "src/dmtcp_dlsym_wrappers.cpp",
        "src/dmtcpworker.cpp",
        "doc/dmtcp_dlsym.txt",
    ],
    "Handoff constraints for S02/S03/S04": [
            "src/dlwrappers.cpp",
        "src/dmtcp_launch.cpp",
        "src/dmtcpworker.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
    ],
}

R002_LABELS = [
    "Constructor order",
    "Wrapper order",
    "TSAN interaction",
    "dlsym(RTLD_NEXT)",
    "dl_iterate_phdr",
]

EXPERIMENT_MARKERS = [
    "-fsanitize=thread",
    "bin/dmtcp_launch",
    "__tsan::Initialize",
    "dmtcp_initialize_entry_point",
    "dlsym/RTLD_NEXT link-map behavior",
    "dl_iterate_phdr interception or bypass prototype",
    "Expected pass signal",
    "Expected fail signal",
]

HANDOFF_MARKERS = ["S02", "S03", "S04"]

# Matches source-path style citations while ignoring shared-object names such as
# libdmtcp.so.  A line suffix like src/foo.cpp:123 is accepted but normalized.
SOURCE_PATH_RE = re.compile(
    r"(?<![\w.-])(?:[A-Za-z0-9_+.-]+/)*[A-Za-z0-9_+.-]+\.(?:cpp|c|h|am|txt|md|py)(?::\d+(?:-\d+)?)?"
)
PLACEHOLDER_RE = re.compile(r"\b(?:TODO|TBD|FIXME|PLACEHOLDER)\b", re.IGNORECASE)
HIDDEN_SENTINELS = {".gsd", ".planning"}


def normalize_source_path(raw: str) -> str:
    """Strip optional line suffixes without damaging paths such as src/a:b.c."""
    return re.sub(r":\d+(?:-\d+)?$", "", raw)


def is_hidden_or_outside_repo(path: str) -> bool:
    rel = Path(path)
    return (
        rel.is_absolute()
        or ".." in rel.parts
        or any(part.startswith(".") for part in rel.parts)
    )


def repo_file_exists(path: str) -> bool:
    return (REPO_ROOT / path).is_file()


def extract_source_paths(text: str) -> set[str]:
    return {normalize_source_path(match.group(0)) for match in SOURCE_PATH_RE.finditer(text)}


def markdown_section(text: str, heading: str) -> str | None:
    marker = re.compile(rf"^##\s+{re.escape(heading)}\s*$", re.MULTILINE)
    match = marker.search(text)
    if not match:
        return None
    next_heading = re.search(r"^##\s+", text[match.end() :], re.MULTILINE)
    end = match.end() + next_heading.start() if next_heading else len(text)
    return text[match.end() : end]


def validate_scan_set(errors: list[str], required_files: list[str], mode: str) -> None:
    explicit_scan_paths = [DOC_PATH, *map(Path, required_files)]
    for rel_path in explicit_scan_paths:
        rel = str(rel_path)
        if is_hidden_or_outside_repo(rel):
            errors.append(
                f"Verifier scan set includes hidden/out-of-repo path {rel!r}; "
                f"{mode} must inspect only non-hidden repo paths."
            )
        elif rel_path.parts and rel_path.parts[0] in HIDDEN_SENTINELS:
            errors.append(
                f"Verifier scan set includes forbidden artifact path {rel!r}; "
                "do not satisfy appendix checks from generated planning artifacts."
            )


def validate_common(
    *,
    mode: str,
    required_headings: list[str],
    required_files: list[str],
    section_citations: dict[str, list[str]],
) -> tuple[list[str], str, dict[str, str]]:
    errors: list[str] = []
    validate_scan_set(errors, required_files, mode)

    doc_abs = REPO_ROOT / DOC_PATH
    if not doc_abs.is_file():
        errors.append(f"Missing appendix document: {DOC_PATH}")
        return errors, "", {}

    text = doc_abs.read_text(encoding="utf-8")

    if any(f"{hidden}/" in text or f"`{hidden}" in text for hidden in HIDDEN_SENTINELS):
        errors.append(
            "Appendix references hidden planning artifacts; source evidence must cite "
            "non-hidden repo source paths only."
        )

    cited_paths = extract_source_paths(text)
    for path in sorted(cited_paths):
        if is_hidden_or_outside_repo(path):
            errors.append(f"Hidden or out-of-repo source citation found: {path}")
        elif not repo_file_exists(path):
            errors.append(f"Source citation does not exist in repo: {path}")

    for required in required_files:
        if required not in cited_paths:
            errors.append(f"Missing required source citation: {required}")

    sections: dict[str, str] = {}
    for heading in required_headings:
        section = markdown_section(text, heading)
        if section is None:
            errors.append(f"Missing required heading: ## {heading}")
            continue
        sections[heading] = section
        section_paths = extract_source_paths(section)
        if not section_paths:
            errors.append(f"Section ## {heading} has no source-path citation")
        for required in section_citations.get(heading, []):
            if required not in section_paths:
                errors.append(
                    f"Section ## {heading} must cite {required}; "
                    f"found {', '.join(sorted(section_paths)) or 'no citations'}"
                )

    placeholder_match = PLACEHOLDER_RE.search("\n".join(sections.values()))
    if placeholder_match:
        errors.append(
            f"Placeholder marker {placeholder_match.group(0)!r} remains in {mode} sections; "
            "replace it with source-backed text before passing verification."
        )

    return errors, text, sections


def validate_full_extra(errors: list[str], sections: dict[str, str]) -> None:
    coverage = sections.get("R002 coverage checklist", "").replace("`", "")
    for label in R002_LABELS:
        if label not in coverage:
            errors.append(f"R002 coverage checklist missing label: {label}")

    experiments = sections.get("M004/R011 follow-up experiment matrix", "").replace("`", "")
    for marker in EXPERIMENT_MARKERS:
        if marker not in experiments:
            errors.append(f"M004/R011 experiment matrix missing marker: {marker}")

    handoff = sections.get("Handoff constraints for S02/S03/S04", "").replace("`", "")
    for marker in HANDOFF_MARKERS:
        if marker not in handoff:
            errors.append(f"Handoff section missing downstream slice marker: {marker}")


def validate_preload_map() -> int:
    errors, _, _ = validate_common(
        mode="preload-map",
        required_headings=PRELOAD_REQUIRED_HEADINGS,
        required_files=PRELOAD_REQUIRED_FILES,
        section_citations=PRELOAD_SECTION_CITATIONS,
    )
    return report("preload-map", errors)


def validate_full() -> int:
    errors, _, sections = validate_common(
        mode="full",
        required_headings=FULL_REQUIRED_HEADINGS,
        required_files=FULL_REQUIRED_FILES,
        section_citations=FULL_SECTION_CITATIONS,
    )
    validate_full_extra(errors, sections)
    return report("full", errors)


def report(mode: str, errors: list[str]) -> int:
    if errors:
        print(f"{mode} verification failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(f"{mode} verification passed: required headings, citations, and placeholder checks are satisfied.")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["preload-map", "full"], help="verification mode to run")
    args = parser.parse_args(argv)

    if args.mode == "preload-map":
        return validate_preload_map()
    if args.mode == "full":
        return validate_full()
    raise AssertionError(f"unhandled mode: {args.mode}")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
