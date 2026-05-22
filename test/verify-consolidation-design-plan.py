#!/usr/bin/env python3
"""Static checks for the final internal-plugin consolidation design plan.

`self-test` validates this verifier's own contract without requiring the final
S04 design document to exist: it uses explicit non-hidden evidence paths, keeps
all validation tables non-empty, rejects recursive scan APIs, verifies bounded
source anchors, and exercises negative fixtures for missing sections, hidden
planning-path citations, placeholder leakage, missing source anchors, missing
D001-D008 decisions, missing R001/R002/R003/R006/R009/R010/R011/R012/R013/R014
coverage, wrong staged migration order, missing external plugin ABI boundary,
missing no-transitional-shim recommendation, and unsupported claims that TSAN,
dlsym/RTLD_NEXT, or dl_iterate_phdr runtime validation has already passed.

`synthesis-map` validates the source-backed synthesis sections of the final
artifact: doc/internal-plugin-consolidation-design-plan.md.

`coverage-map` validates the decision, requirement, explicit R012/R013 deferred
scoping, risk, runtime-boundary, and M002 through M005 handoff sections of that
same final artifact.

`full` runs both maps plus the shared source-anchor checks.

All modes inspect only the explicit non-hidden paths listed below.  The verifier
does not walk the repository and does not read generated planning artifacts such
as .gsd, .planning, .audits, .git, or other hidden paths.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DOC_PATH = Path("doc/internal-plugin-consolidation-design-plan.md")
EXPECTED_DOC_PATH = "doc/internal-plugin-consolidation-design-plan.md"

PRIOR_APPENDIX_FILES = [
    "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
    "doc/internal-plugin-consolidation-registration-architecture.md",
    "doc/internal-plugin-consolidation-migration-plan.md",
]

CURRENT_SOURCE_FILES = [
    "doc/dmtcp_dlsym.txt",
    "src/dmtcp_launch.cpp",
    "src/util_exec.cpp",
    "src/execwrappers.cpp",
    "src/pluginmanager.cpp",
    "src/pluginmanager.h",
    "include/dmtcp.h",
    "src/Makefile.am",
    "src/plugin/Makefile.am",
    "src/dlwrappers.cpp",
    "src/dmtcp_dlsym_wrappers.cpp",
    "src/dmtcp_dlsym.cpp",
    "src/plugin/timer/timerlist.cpp",
    "src/plugin/svipc/sysvipc.cpp",
    "src/plugin/ipc/ipc.cpp",
    "src/plugin/pid/pid.cpp",
    "src/plugin/alloc/mallocwrappers.cpp",
    "src/plugin/alloc/mmapwrappers.cpp",
    "src/plugin/dl/dlwrappers.cpp",
    "test/autotest.py",
    "test/sysv-shm1.c",
    "test/sysv-shm2.c",
    "test/timer1.c",
    "test/timer2.c",
    "test/dlopen1.c",
    "test/dlopen2.cpp",
    "test/plugin-init.cpp",
]

REQUIRED_EVIDENCE_FILES = [*PRIOR_APPENDIX_FILES, *CURRENT_SOURCE_FILES]

SYNTHESIS_REQUIRED_HEADINGS = [
    "Scope and evidence boundary",
    "Source appendix synthesis map",
    "Accepted consolidation design",
    "Built-in registration and external plugin ABI boundary",
    "Staged migration order and gate handoff",
    "Wrapper-only alloc/dl and runtime-risk boundary",
    "Alternatives considered",
]

COVERAGE_REQUIRED_HEADINGS = [
    "Decision coverage D001-D008",
    "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014",
    "No-transitional-shim recommendation",
    "Runtime proof boundaries and deferred validation",
    "M002 through M005 handoff",
    "Known risks and diagnostics",
]

FULL_REQUIRED_HEADINGS = [*SYNTHESIS_REQUIRED_HEADINGS, *COVERAGE_REQUIRED_HEADINGS]

SYNTHESIS_TABLE_HEADINGS = [
    "Source appendix synthesis map",
    "Staged migration order and gate handoff",
    "Alternatives considered",
]

COVERAGE_TABLE_HEADINGS = [
    "Decision coverage D001-D008",
    "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014",
    "M002 through M005 handoff",
    "Known risks and diagnostics",
]

FULL_TABLE_HEADINGS = [*SYNTHESIS_TABLE_HEADINGS, *COVERAGE_TABLE_HEADINGS]

SYNTHESIS_SECTION_CITATIONS = {
    "Scope and evidence boundary": [
            "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
    ],
    "Source appendix synthesis map": REQUIRED_EVIDENCE_FILES,
    "Accepted consolidation design": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
    ],
    "Built-in registration and external plugin ABI boundary": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/pid/pid.cpp",
    ],
    "Staged migration order and gate handoff": [
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/pluginmanager.cpp",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/pid/pid.cpp",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
    "Wrapper-only alloc/dl and runtime-risk boundary": [
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "doc/dmtcp_dlsym.txt",
        "src/dmtcp_launch.cpp",
        "src/dlwrappers.cpp",
        "src/dmtcp_dlsym_wrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/alloc/mmapwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
    "Alternatives considered": [
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
    ],
}

COVERAGE_SECTION_CITATIONS = {
    "Decision coverage D001-D008": [
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
    ],
    "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014": [
            "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "test/autotest.py",
    ],
    "No-transitional-shim recommendation": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
    ],
    "Runtime proof boundaries and deferred validation": [
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "doc/dmtcp_dlsym.txt",
        "src/dlwrappers.cpp",
        "src/dmtcp_dlsym_wrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "test/dlopen1.c",
        "test/dlopen2.cpp",
    ],
    "M002 through M005 handoff": [
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "test/autotest.py",
    ],
    "Known risks and diagnostics": [
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "test/autotest.py",
    ],
}

FULL_SECTION_CITATIONS = {**SYNTHESIS_SECTION_CITATIONS, **COVERAGE_SECTION_CITATIONS}

SYNTHESIS_MARKER_GROUPS = {
    "source appendix coverage": [
        "S01",
        "S02",
        "S03",
        "wrapper-order TSAN appendix",
        "registration architecture appendix",
        "migration plan appendix",
    ],
    "accepted consolidation design": [
        "libdmtcp.so",
        "PluginManager-owned built-in table",
        "built-in descriptor table",
        "dmtcp_register_plugin",
    ],
    "external plugin ABI boundary": [
        "external plugin ABI",
        "external dmtcp_initialize_plugin chain",
        "DMTCP_DECL_PLUGIN",
        "NEXT_FNC(dmtcp_initialize_plugin)",
    ],
    "staged migration order": [
        "timer,svipc,ipc,pid,alloc,dl",
        "timer stage",
        "SysV IPC stage",
        "IPC stage",
        "PID stage",
        "alloc stage",
        "DL stage",
    ],
    "wrapper and runtime-risk boundary": [
        "wrapper-only",
        "ENV_VAR_ALLOC_PLUGIN",
        "ENV_VAR_DL_PLUGIN",
        "TSAN",
        "dlsym/RTLD_NEXT",
        "dl_iterate_phdr",
        "deferred runtime validation",
    ],
    "alternatives coverage": [
        "status quo",
        "separate DSOs",
        "broad mainline merge",
        "transitional shim",
        "all-at-once migration",
        "merge optional and external plugins",
        "full TSAN checkpoint/restart",
        "first acceptance gate",
        "rejected",
    ],
}

COVERAGE_MARKER_GROUPS = {
    "decision coverage": [
        "D001",
        "D002",
        "D003",
        "D004",
        "D005",
        "D006",
        "D007",
        "D008",
    ],
    "requirement coverage": [
        "R001",
        "R002",
        "R003",
        "R006",
        "R009",
        "R010",
        "R011",
        "R014",
        "satisfied at M001 design-proof level",
        "deferred implementation/sanitizer/external-plugin requirements remain active handoffs",
    ],
    "no transitional shim recommendation": [
        "no transitional shim",
        "direct staged migration",
        "old DSO boundary",
        "stale artifact cleanup",
    ],
    "runtime proof boundary": [
        "not runtime proof",
        "TSAN runtime validation deferred",
        "dlsym/RTLD_NEXT runtime validation deferred",
        "dl_iterate_phdr runtime validation deferred",
        "M004",
    ],
    "M002-M005 handoff": [
        "M002",
        "M003",
        "M004",
        "M005",
        "ABI preservation",
        "event-order preservation",
        "wrapper-only disable behavior",
        "static-initialization safety",
    ],
    "quality gate diagnostics": [
        "Q3",
        "Q4",
        "Q5",
        "Q6",
        "Q7",
        "documentation artifact outcome",
        "out-of-scope testing-harness note",
        "python3 test/verify-consolidation-design-plan.py full",
    ],
}

REQUIREMENT_COVERAGE_HEADING = "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014"

DEFERRED_REQUIREMENT_SCOPING_MARKERS = {
    "R012": [
        "deferred TSAN checkpoint/restart",
        "no hard M001 or M003 acceptance gate",
        "future M004 or future TSAN runtime evidence before any checkpoint restart guarantee",
    ],
    "R013": [
        "deferred runtime-overhead threshold",
        "no concrete runtime overhead threshold defined or enforced in M001",
        "future measurement gate or benchmark only if maintainers define a threshold",
    ],
}

FULL_MARKER_GROUPS = {**SYNTHESIS_MARKER_GROUPS, **COVERAGE_MARKER_GROUPS}

SYNTHESIS_SECTION_MARKERS = {
    "Built-in registration and external plugin ABI boundary": [
        "external plugin ABI",
        "external dmtcp_initialize_plugin chain",
        "DMTCP_DECL_PLUGIN",
        "NEXT_FNC(dmtcp_initialize_plugin)",
    ],
    "Staged migration order and gate handoff": [
        "timer,svipc,ipc,pid,alloc,dl",
        "timer stage",
        "SysV IPC stage",
        "IPC stage",
        "PID stage",
        "alloc stage",
        "DL stage",
    ],
    "Wrapper-only alloc/dl and runtime-risk boundary": [
        "wrapper-only",
        "ENV_VAR_ALLOC_PLUGIN",
        "ENV_VAR_DL_PLUGIN",
        "TSAN",
        "dlsym/RTLD_NEXT",
        "dl_iterate_phdr",
        "deferred runtime validation",
    ],
}

COVERAGE_SECTION_MARKERS = {
    "Decision coverage D001-D008": COVERAGE_MARKER_GROUPS["decision coverage"],
    "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014": COVERAGE_MARKER_GROUPS[
        "requirement coverage"
    ],
    "No-transitional-shim recommendation": COVERAGE_MARKER_GROUPS[
        "no transitional shim recommendation"
    ],
    "Runtime proof boundaries and deferred validation": COVERAGE_MARKER_GROUPS[
        "runtime proof boundary"
    ],
    "M002 through M005 handoff": COVERAGE_MARKER_GROUPS["M002-M005 handoff"],
    "Known risks and diagnostics": COVERAGE_MARKER_GROUPS["quality gate diagnostics"],
}

FULL_SECTION_MARKERS = {**SYNTHESIS_SECTION_MARKERS, **COVERAGE_SECTION_MARKERS}

STAGE_ORDER = ("timer", "svipc", "ipc", "pid", "alloc", "dl")
EXPECTED_STAGE_ORDER = ",".join(STAGE_ORDER)

# Matches source-path style citations while ignoring shared-object names such as
# libdmtcp.so.  A line suffix like src/foo.cpp:123 is accepted but normalized.
SOURCE_PATH_RE = re.compile(
    r"(?<![\w.-])(?:[A-Za-z0-9_+.-]+/)*[A-Za-z0-9_+.-]+\.(?:cpp|c|h|am|txt|md|py)(?::\d+(?:-\d+)?)?"
)

PLACEHOLDER_PATTERNS = {
    "TODO": re.compile(r"\bTODO\b", re.IGNORECASE),
    "TBD": re.compile(r"\bTBD\b", re.IGNORECASE),
    "FIXME": re.compile(r"\bFIXME\b", re.IGNORECASE),
    "PLACEHOLDER": re.compile(r"\bPLACEHOLDER\b", re.IGNORECASE),
    "template variable": re.compile(r"{{[^}\n]+}}"),
    "markdown fill-in marker": re.compile(
        r"\[(?:fill in|todo|tbd|placeholder|replace)[^\]\n]*\]",
        re.IGNORECASE,
    ),
}

HIDDEN_SENTINELS = {".gsd", ".planning", ".audits", ".git"}

STAGE_ORDER_RE = re.compile(
    r"\b(?:timer|svipc|ipc|pid|alloc|dl)\s*,\s*"
    r"(?:timer|svipc|ipc|pid|alloc|dl)\s*,\s*"
    r"(?:timer|svipc|ipc|pid|alloc|dl)\s*,\s*"
    r"(?:timer|svipc|ipc|pid|alloc|dl)\s*,\s*"
    r"(?:timer|svipc|ipc|pid|alloc|dl)\s*,\s*"
    r"(?:timer|svipc|ipc|pid|alloc|dl)\b",
    re.IGNORECASE,
)

RUNTIME_CLAIM_TERMS = (
    "TSAN",
    "dlsym",
    "RTLD_NEXT",
    "dl_iterate_phdr",
)
RUNTIME_PROOF_WORDS = (
    "passed",
    "validated",
    "verified",
    "proven",
    "proved",
    "succeeded",
    "complete",
    "accepted",
)
RUNTIME_CONTEXT_WORDS = (
    "runtime",
    "stress",
    "experiment",
    "validation",
    "test",
    "tests",
)
SAFE_RUNTIME_QUALIFIERS = (
    "not ",
    "no ",
    "cannot",
    "defer",
    "future",
    "follow-up",
    "planned",
    "pending",
    "must ",
    "needs",
    "should",
    "unproven",
    "unsupported",
    "risk",
    "boundary",
    "M004",
    "M005",
)


@dataclass(frozen=True)
class SourceAnchor:
    path: str
    markers: tuple[str, ...]


@dataclass(frozen=True)
class ForbiddenSourceMarker:
    path: str
    reason: str
    markers: tuple[str, ...]


@dataclass(frozen=True)
class OrderedSourceAnchor:
    path: str
    label: str
    markers: tuple[str, ...]


SOURCE_ANCHORS = [
    SourceAnchor(
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        (
            "R002 coverage checklist",
            "TSAN feasibility assessment",
            "dlsym RTLD_NEXT behavior under DMTCP",
            "dl_iterate_phdr feasibility and bypass options",
            "M004/R011 follow-up experiment matrix",
        ),
    ),
    SourceAnchor(
        "doc/internal-plugin-consolidation-registration-architecture.md",
        (
            "Proposed PluginManager-owned built-in registration table",
            "Descriptor accessor naming and ABI boundary",
            "Timer tracer bullet implementation plan",
            "Requirements and handoff coverage",
        ),
    ),
    SourceAnchor(
        "doc/internal-plugin-consolidation-migration-plan.md",
        (
            "Accepted staged migration order",
            "timer,svipc,ipc,pid,alloc,dl",
            "Duplicate initializer and wrapper avoidance gates",
            "Requirement coverage and negative checks",
            "S04 handoff coverage",
        ),
    ),
    SourceAnchor(
        "src/dmtcp_launch.cpp",
        (
            "pluginInfo[]",
            "ENV_VAR_HIJACK_LIBS",
            "ENV_VAR_ALLOC_PLUGIN",
            "ENV_VAR_DL_PLUGIN",
            "--disable-all-plugins",
            "libdmtcp_pathvirt.so",
            "ENV_VAR_DISABLE_ALL_PLUGINS",
            "libdmtcp.so",
        ),
    ),
    SourceAnchor(
        "src/util_exec.cpp",
        (
            "Util::getDmtcpArgs",
            "--disable-alloc-plugin",
            "--disable-dl-plugin",
            "--with-plugin",
        ),
    ),
    SourceAnchor(
        "src/execwrappers.cpp",
        (
            "getUpdatedLdPreload",
            "ENV_VAR_HIJACK_LIBS",
            "ENV_VAR_ORIG_LD_PRELOAD",
        ),
    ),
    SourceAnchor(
        "src/pluginmanager.cpp",
        (
            "PluginManager::initialize",
            "dmtcp_register_plugin",
            "dmtcp_initialize_plugin",
            "NEXT_FNC(dmtcp_initialize_plugin)",
            "PluginManager::eventHook",
        ),
    ),
    SourceAnchor(
        "src/pluginmanager.h",
        (
            "class PluginManager",
            "registerPlugin",
            "eventHook",
        ),
    ),
    SourceAnchor(
        "include/dmtcp.h",
        (
            "DmtcpPluginDescriptor_t",
            "#define DMTCP_DECL_PLUGIN",
            "NEXT_FNC(dmtcp_initialize_plugin)",
        ),
    ),
    SourceAnchor(
        "src/Makefile.am",
        (
            "__d_libdir__libdmtcp_so_SOURCES",
            "pluginmanager.cpp",
            "execwrappers.cpp",
            "util_exec.cpp",
            "plugin/alloc/mallocwrappers.cpp",
            "plugin/alloc/mmapwrappers.cpp",
            "plugin/dl/dlwrappers.cpp",
        ),
    ),
    SourceAnchor(
        "src/plugin/Makefile.am",
        (
            "noinst_LIBRARIES += libssh.a",
            "ipc/ssh/util_ssh.cpp",
            "The PID plugin is built into the core DMTCP library.",
            "Timer wrappers and the timer descriptor are now built into libdmtcp.so",
        ),
    ),
    SourceAnchor(
        "src/plugin/timer/timerlist.cpp",
        (
            "DmtcpPluginDescriptor_t timerPlugin",
            "dmtcp_Timer_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/svipc/sysvipc.cpp",
        (
            "DmtcpPluginDescriptor_t sysvipcPlugin",
            "dmtcp_SysVIPC_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/ipc.cpp",
        (
            "IPC subplugin descriptors are registered by PluginManager",
            "do not define an IPC-local dmtcp_initialize_plugin() chain here",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pid.cpp",
        (
            "DmtcpPluginDescriptor_t pidPlugin",
            "dmtcp_Pid_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/alloc/mallocwrappers.cpp",
        (
            "dmtcp_alloc_enabled",
            "extern \"C\" void *malloc",
            "free(void *ptr)",
        ),
    ),
    SourceAnchor(
        "src/plugin/alloc/mmapwrappers.cpp",
        (
            "extern \"C\" void *mmap",
            "munmap(void *addr",
        ),
    ),
    SourceAnchor(
        "src/plugin/dl/dlwrappers.cpp",
        (
            "#define _real_dlopen",
            "#define _real_dlclose",
            "thread_performing_dlopen_dlsym",
            "WRAPPER_EXECUTION_DISABLE_CKPT",
        ),
    ),
    SourceAnchor(
        "test/autotest.py",
        (
            "runTest(\"sysv-shm1\"",
            "runTest(\"sysv-shm2\"",
            "runTest(\"timer1\"",
            "runTest(\"dlopen1\"",
            "runTest(\"dlopen2\"",
        ),
    ),
]

FORBIDDEN_SOURCE_MARKERS = [
    ForbiddenSourceMarker(
        "src/dmtcp_launch.cpp",
        "alloc and DL are no longer separate launcher preload DSOs in the final design state",
        ("libdmtcp_alloc.so", "libdmtcp_dl.so"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/Makefile.am",
        "alloc and DL wrappers are owned by the core libdmtcp.so source list, not plugin DSO rows",
        (
            "libdmtcp_alloc.so",
            "__d_libdir__libdmtcp_alloc_so_SOURCES",
            "libdmtcp_dl.so",
            "__d_libdir__libdmtcp_dl_so_SOURCES",
            "alloc/mallocwrappers.cpp",
            "dl/dlwrappers.cpp",
        ),
    ),
    ForbiddenSourceMarker(
        "src/plugin/alloc/mallocwrappers.cpp",
        "alloc must remain wrapper-only and must not define a plugin descriptor",
        ("DmtcpPluginDescriptor_t", "dmtcp_initialize_plugin", "DMTCP_DECL_PLUGIN"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/alloc/mmapwrappers.cpp",
        "alloc mmap wrappers must remain wrapper-only and must not define a plugin descriptor",
        ("DmtcpPluginDescriptor_t", "dmtcp_initialize_plugin", "DMTCP_DECL_PLUGIN"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/dl/dlwrappers.cpp",
        "dl must remain wrapper-only and must not define a plugin descriptor",
        ("DmtcpPluginDescriptor_t", "dmtcp_initialize_plugin", "DMTCP_DECL_PLUGIN"),
    ),
]

ORDERED_SOURCE_ANCHORS = [
    OrderedSourceAnchor(
        "src/dmtcp_launch.cpp",
        "dmtcp_launch wrapper-only preload order",
        (
            "libdmtcp_pathvirt.so",
            "libdmtcp.so",
        ),
    ),
    OrderedSourceAnchor(
        "src/pluginmanager.cpp",
        "current libdmtcp built-in order with PID last before external initializer chain",
        (
            "dmtcp_register_plugin(dmtcp_Timer_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_SysVIPC_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcSsh_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcPty_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcSocket_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_PathTranslator_PluginDescr());",
            "dmtcp_register_plugin(UniquePid::pluginDescr());",
            "dmtcp_register_plugin(dmtcp_Pid_PluginDescr());",
            "NEXT_FNC(dmtcp_initialize_plugin)",
        ),
    ),
    OrderedSourceAnchor(
        "src/pluginmanager.cpp",
        "IPC subplugin registration order",
        (
            "dmtcp_register_plugin(dmtcp_IpcSsh_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcPty_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcSocket_PluginDescr());",
        ),
    ),
]


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


def markdown_section_span(text: str, heading: str) -> tuple[int, int] | None:
    marker = re.compile(rf"^##\s+{re.escape(heading)}\s*$", re.MULTILINE)
    match = marker.search(text)
    if not match:
        return None
    next_heading = re.search(r"^##\s+", text[match.end() :], re.MULTILINE)
    end = match.end() + next_heading.start() if next_heading else len(text)
    return match.start(), end


def markdown_section(text: str, heading: str) -> str | None:
    span = markdown_section_span(text, heading)
    if span is None:
        return None
    start, end = span
    heading_end = text.find("\n", start, end)
    if heading_end == -1:
        return ""
    return text[heading_end + 1 : end]


def compact_marker_text(text: str) -> str:
    return re.sub(r"\s+", "", text.replace("`", "")).lower()


def marker_present(haystack: str, marker: str) -> bool:
    normalized = haystack.replace("`", "")
    if "," in marker:
        return marker.lower().replace(" ", "") in compact_marker_text(normalized)
    return marker.lower() in normalized.lower()


def requirement_scoping_text(section: str, requirement_id: str) -> str:
    """Return the requirement-local text that must carry deferred-scope markers."""
    return "\n".join(
        line.strip() for line in section.splitlines() if marker_present(line, requirement_id)
    )


def validate_deferred_requirement_scoping(errors: list[str], section: str) -> None:
    """Require R012/R013 deferral semantics to be tied to their own coverage rows."""
    if not section:
        for requirement_id in DEFERRED_REQUIREMENT_SCOPING_MARKERS:
            errors.append(f"Missing required marker for {requirement_id} scoping: {requirement_id}")
        return

    for requirement_id, markers in DEFERRED_REQUIREMENT_SCOPING_MARKERS.items():
        scope_text = requirement_scoping_text(section, requirement_id)
        if not scope_text:
            errors.append(f"Missing required marker for {requirement_id} scoping: {requirement_id}")
            continue
        for marker in markers:
            if not marker_present(scope_text, marker):
                errors.append(f"Missing required marker for {requirement_id} scoping: {marker}")


def section_has_nonempty_table(section: str) -> bool:
    lines = section.splitlines()
    for index, line in enumerate(lines[:-1]):
        if not line.strip().startswith("|"):
            continue
        separator = lines[index + 1].strip()
        if not separator.startswith("|") or "---" not in separator:
            continue
        for data_line in lines[index + 2 :]:
            stripped = data_line.strip()
            if not stripped:
                break
            if not stripped.startswith("|"):
                break
            cells = [cell.strip() for cell in stripped.strip("|").split("|")]
            if any(cell and set(cell) != {"-"} for cell in cells):
                return True
    return False


def validate_scan_set(errors: list[str], scan_paths: list[str | Path], mode: str) -> None:
    for rel_path in map(Path, scan_paths):
        rel = str(rel_path)
        if is_hidden_or_outside_repo(rel):
            errors.append(
                f"Verifier scan set includes hidden/out-of-repo path {rel!r}; "
                f"{mode} must inspect only explicit non-hidden repo paths."
            )
        elif rel_path.parts and rel_path.parts[0] in HIDDEN_SENTINELS:
            errors.append(
                f"Verifier scan set includes forbidden artifact path {rel!r}; "
                "do not satisfy final design checks from generated planning artifacts."
            )


def validate_source_anchors(errors: list[str]) -> None:
    for anchor in SOURCE_ANCHORS:
        path = REPO_ROOT / anchor.path
        if not path.is_file():
            errors.append(f"Missing source anchor file: {anchor.path}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in anchor.markers:
            if marker not in text:
                errors.append(f"Source anchor {anchor.path} missing marker: {marker}")

    for forbidden in FORBIDDEN_SOURCE_MARKERS:
        path = REPO_ROOT / forbidden.path
        if not path.is_file():
            errors.append(f"Missing source anchor file for forbidden-marker check: {forbidden.path}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for marker in forbidden.markers:
            if marker in text:
                errors.append(
                    f"Source anchor {forbidden.path} unexpectedly contains marker {marker!r}; "
                    f"{forbidden.reason}."
                )

    for anchor in ORDERED_SOURCE_ANCHORS:
        path = REPO_ROOT / anchor.path
        if not path.is_file():
            errors.append(f"Missing source anchor file for {anchor.label}: {anchor.path}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        cursor = -1
        for marker in anchor.markers:
            next_pos = text.find(marker, cursor + 1)
            if next_pos == -1:
                errors.append(
                    f"Source order marker missing or out of order for {anchor.label} "
                    f"in {anchor.path}: {marker}"
                )
                break
            cursor = next_pos


def find_wrong_stage_orders(text: str) -> list[str]:
    wrong: list[str] = []
    for match in STAGE_ORDER_RE.finditer(text):
        parts = tuple(part.strip().lower() for part in match.group(0).split(","))
        if set(parts) == set(STAGE_ORDER) and parts != STAGE_ORDER:
            wrong.append(",".join(parts))
    return wrong


def find_unsupported_runtime_claims(text: str) -> list[str]:
    claims: list[str] = []
    for line in text.splitlines():
        for segment in re.split(r"(?<=[.!?])\s+", line):
            stripped = segment.strip()
            if not stripped:
                continue
            lowered = stripped.lower()
            if not any(term.lower() in lowered for term in RUNTIME_CLAIM_TERMS):
                continue
            if not any(word in lowered for word in RUNTIME_PROOF_WORDS):
                continue
            if not any(context in lowered for context in RUNTIME_CONTEXT_WORDS):
                continue
            if any(qualifier.lower() in lowered for qualifier in SAFE_RUNTIME_QUALIFIERS):
                continue
            claims.append(stripped)
    return claims


def validate_document_text(
    *,
    text: str,
    mode: str,
    required_headings: list[str],
    required_files: list[str],
    section_citations: dict[str, list[str]],
    marker_groups: dict[str, list[str]],
    table_headings: list[str],
    section_marker_groups: dict[str, list[str]],
) -> tuple[list[str], dict[str, str]]:
    errors: list[str] = []

    for hidden in HIDDEN_SENTINELS:
        if f"{hidden}/" in text or f"`{hidden}" in text or f"{hidden}" in text:
            errors.append(
                f"Final design plan references hidden planning artifact {hidden}; "
                "cite explicit non-hidden repository paths only."
            )

    cited_paths = extract_source_paths(text)
    for path in sorted(cited_paths):
        if is_hidden_or_outside_repo(path):
            errors.append(f"Hidden or out-of-repo source citation found: {path}")
        elif path != str(DOC_PATH) and not repo_file_exists(path):
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
        if heading in table_headings and not section_has_nonempty_table(section):
            errors.append(f"Section ## {heading} is missing required non-empty markdown table")

    checked_text = "\n".join(sections.values())
    for label, pattern in PLACEHOLDER_PATTERNS.items():
        match = pattern.search(checked_text)
        if match:
            errors.append(
                f"Placeholder pattern {label!r} matched {match.group(0)!r} in {mode} sections; "
                "replace it with source-backed text before passing verification."
            )

    for group, markers in marker_groups.items():
        for marker in markers:
            if not marker_present(checked_text, marker):
                errors.append(f"Missing required marker for {group}: {marker}")

    for heading, markers in section_marker_groups.items():
        section = sections.get(heading, "")
        for marker in markers:
            if not marker_present(section, marker):
                errors.append(f"Missing required marker in ## {heading}: {marker}")

    if REQUIREMENT_COVERAGE_HEADING in required_headings:
        validate_deferred_requirement_scoping(
            errors,
            sections.get(REQUIREMENT_COVERAGE_HEADING, ""),
        )

    for wrong_order in find_wrong_stage_orders(checked_text):
        errors.append(
            f"Wrong staged migration order found: {wrong_order}; "
            f"expected {EXPECTED_STAGE_ORDER}."
        )

    stage_section = sections.get("Staged migration order and gate handoff", "")
    if "Staged migration order and gate handoff" in required_headings and not marker_present(
        stage_section,
        EXPECTED_STAGE_ORDER,
    ):
        errors.append(
            "Missing order constraint in ## Staged migration order and gate handoff: "
            f"expected compact marker {EXPECTED_STAGE_ORDER}."
        )

    for claim in find_unsupported_runtime_claims(checked_text):
        errors.append(
            "Unsupported runtime-proof claim found: "
            f"{claim!r}. TSAN, dlsym/RTLD_NEXT, and dl_iterate_phdr runtime validation "
            "must remain deferred until a later runtime-validation milestone supplies evidence."
        )

    return errors, sections


def validate_common(
    *,
    mode: str,
    required_headings: list[str],
    section_citations: dict[str, list[str]],
    marker_groups: dict[str, list[str]],
    table_headings: list[str],
    section_marker_groups: dict[str, list[str]],
) -> int:
    errors: list[str] = []
    validate_scan_set(errors, [DOC_PATH, *REQUIRED_EVIDENCE_FILES], mode)
    validate_source_anchors(errors)

    doc_abs = REPO_ROOT / DOC_PATH
    if not doc_abs.is_file():
        errors.append(f"Missing final design plan document: {DOC_PATH}")
        return report(mode, errors)

    text = doc_abs.read_text(encoding="utf-8", errors="replace")
    document_errors, _ = validate_document_text(
        text=text,
        mode=mode,
        required_headings=required_headings,
        required_files=REQUIRED_EVIDENCE_FILES,
        section_citations=section_citations,
        marker_groups=marker_groups,
        table_headings=table_headings,
        section_marker_groups=section_marker_groups,
    )
    return report(mode, [*errors, *document_errors])


def iter_ast_nodes(node: ast.AST):
    yield node
    for child in ast.iter_child_nodes(node):
        yield from iter_ast_nodes(child)


def call_name(node: ast.AST) -> str | None:
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        parent = call_name(node.value)
        return f"{parent}.{node.attr}" if parent else node.attr
    return None


def validate_no_recursive_scan_apis(errors: list[str]) -> None:
    source = Path(__file__).read_text(encoding="utf-8")
    tree = ast.parse(source)
    forbidden_suffixes = ("os.walk", "glob.glob", "Path.glob", "Path.rglob")
    forbidden_names = {"walk", "glob", "rglob", "scandir"}
    for node in iter_ast_nodes(tree):
        if not isinstance(node, ast.Call):
            continue
        name = call_name(node.func)
        if not name:
            continue
        if name in {"ast.iter_child_nodes"}:
            continue
        if name.endswith(forbidden_suffixes) or name.split(".")[-1] in forbidden_names:
            errors.append(
                f"Verifier implementation uses recursive or directory scan API {name}; "
                "S04 checks must stay on the explicit evidence list."
            )


def validate_table_shape(errors: list[str]) -> None:
    tables = {
        "PRIOR_APPENDIX_FILES": PRIOR_APPENDIX_FILES,
        "CURRENT_SOURCE_FILES": CURRENT_SOURCE_FILES,
        "REQUIRED_EVIDENCE_FILES": REQUIRED_EVIDENCE_FILES,
        "SYNTHESIS_REQUIRED_HEADINGS": SYNTHESIS_REQUIRED_HEADINGS,
        "COVERAGE_REQUIRED_HEADINGS": COVERAGE_REQUIRED_HEADINGS,
        "FULL_REQUIRED_HEADINGS": FULL_REQUIRED_HEADINGS,
        "SYNTHESIS_SECTION_CITATIONS": SYNTHESIS_SECTION_CITATIONS,
        "COVERAGE_SECTION_CITATIONS": COVERAGE_SECTION_CITATIONS,
        "FULL_SECTION_CITATIONS": FULL_SECTION_CITATIONS,
        "SYNTHESIS_MARKER_GROUPS": SYNTHESIS_MARKER_GROUPS,
        "COVERAGE_MARKER_GROUPS": COVERAGE_MARKER_GROUPS,
        "DEFERRED_REQUIREMENT_SCOPING_MARKERS": DEFERRED_REQUIREMENT_SCOPING_MARKERS,
        "FULL_MARKER_GROUPS": FULL_MARKER_GROUPS,
        "SYNTHESIS_SECTION_MARKERS": SYNTHESIS_SECTION_MARKERS,
        "COVERAGE_SECTION_MARKERS": COVERAGE_SECTION_MARKERS,
        "FULL_SECTION_MARKERS": FULL_SECTION_MARKERS,
        "SYNTHESIS_TABLE_HEADINGS": SYNTHESIS_TABLE_HEADINGS,
        "COVERAGE_TABLE_HEADINGS": COVERAGE_TABLE_HEADINGS,
        "FULL_TABLE_HEADINGS": FULL_TABLE_HEADINGS,
        "SOURCE_ANCHORS": SOURCE_ANCHORS,
        "FORBIDDEN_SOURCE_MARKERS": FORBIDDEN_SOURCE_MARKERS,
        "ORDERED_SOURCE_ANCHORS": ORDERED_SOURCE_ANCHORS,
        "PLACEHOLDER_PATTERNS": PLACEHOLDER_PATTERNS,
    }
    for name, table in tables.items():
        if not table:
            errors.append(f"Verifier table is empty: {name}")

    synthesis_headings = set(SYNTHESIS_REQUIRED_HEADINGS)
    coverage_headings = set(COVERAGE_REQUIRED_HEADINGS)
    full_headings = set(FULL_REQUIRED_HEADINGS)
    for heading in SYNTHESIS_SECTION_CITATIONS:
        if heading not in synthesis_headings:
            errors.append(f"SYNTHESIS_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in COVERAGE_SECTION_CITATIONS:
        if heading not in coverage_headings:
            errors.append(f"COVERAGE_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in FULL_SECTION_CITATIONS:
        if heading not in full_headings:
            errors.append(f"FULL_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in SYNTHESIS_SECTION_MARKERS:
        if heading not in synthesis_headings:
            errors.append(f"SYNTHESIS_SECTION_MARKERS references unknown heading: {heading}")
    for heading in COVERAGE_SECTION_MARKERS:
        if heading not in coverage_headings:
            errors.append(f"COVERAGE_SECTION_MARKERS references unknown heading: {heading}")
    for heading in FULL_SECTION_MARKERS:
        if heading not in full_headings:
            errors.append(f"FULL_SECTION_MARKERS references unknown heading: {heading}")

    for heading in SYNTHESIS_TABLE_HEADINGS:
        if heading not in synthesis_headings:
            errors.append(f"SYNTHESIS_TABLE_HEADINGS references unknown heading: {heading}")
    for heading in COVERAGE_TABLE_HEADINGS:
        if heading not in coverage_headings:
            errors.append(f"COVERAGE_TABLE_HEADINGS references unknown heading: {heading}")
    for heading in FULL_TABLE_HEADINGS:
        if heading not in full_headings:
            errors.append(f"FULL_TABLE_HEADINGS references unknown heading: {heading}")

    required_file_set = set(REQUIRED_EVIDENCE_FILES)
    for table_name, citation_table in {
        "SYNTHESIS_SECTION_CITATIONS": SYNTHESIS_SECTION_CITATIONS,
        "COVERAGE_SECTION_CITATIONS": COVERAGE_SECTION_CITATIONS,
        "FULL_SECTION_CITATIONS": FULL_SECTION_CITATIONS,
    }.items():
        for heading, paths in citation_table.items():
            if not paths:
                errors.append(f"{table_name} citation list is empty for heading: {heading}")
            for path in paths:
                if path not in required_file_set:
                    errors.append(
                        f"{table_name} for ## {heading} references non-required evidence file: {path}"
                    )

    for group_name, markers in {
        **SYNTHESIS_MARKER_GROUPS,
        **COVERAGE_MARKER_GROUPS,
        **SYNTHESIS_SECTION_MARKERS,
        **COVERAGE_SECTION_MARKERS,
    }.items():
        if not markers:
            errors.append(f"Marker group is empty: {group_name}")
        for marker in markers:
            if not marker.strip():
                errors.append(f"Marker group {group_name} contains an empty marker")


def validate_negative_guards(errors: list[str]) -> None:
    for bad_path in [
        ".gsd/example.md",
        ".planning/STATE.md",
        ".audits/report.md",
        ".git/config",
        "../outside.cpp",
        "/tmp/outside.cpp",
    ]:
        if not is_hidden_or_outside_repo(bad_path):
            errors.append(f"Hidden/out-of-repo guard failed to reject: {bad_path}")

    for good_path in [
        "doc/internal-plugin-consolidation-migration-plan.md",
        "src/dmtcp_launch.cpp",
        "test/autotest.py",
    ]:
        if is_hidden_or_outside_repo(good_path):
            errors.append(f"Hidden/out-of-repo guard rejected valid repo path: {good_path}")

    if repo_file_exists("test/does-not-exist-for-design-verifier.cpp"):
        errors.append("Missing-file guard fixture unexpectedly exists in repository")

    for label, pattern in PLACEHOLDER_PATTERNS.items():
        samples = {
            "TODO": "TODO",
            "TBD": "TBD",
            "FIXME": "FIXME",
            "PLACEHOLDER": "PLACEHOLDER",
            "template variable": "{{missing_value}}",
            "markdown fill-in marker": "[fill in later]",
        }
        if not pattern.search(samples[label]):
            errors.append(f"Placeholder guard failed to match sample for: {label}")

    empty_table = "| Header |\n|---|\n"
    nonempty_table = "| Header |\n|---|\n| value |\n"
    if section_has_nonempty_table(empty_table):
        errors.append("Non-empty table guard accepted an empty markdown table")
    if not section_has_nonempty_table(nonempty_table):
        errors.append("Non-empty table guard rejected a populated markdown table")

    if find_wrong_stage_orders(f"The order is {EXPECTED_STAGE_ORDER}."):
        errors.append("Stage-order guard rejected the accepted timer,svipc,ipc,pid,alloc,dl order")
    wrong_orders = find_wrong_stage_orders("The order is timer,ipc,svipc,pid,alloc,dl.")
    if "timer,ipc,svipc,pid,alloc,dl" not in wrong_orders:
        errors.append("Stage-order guard failed to reject timer,ipc,svipc,pid,alloc,dl")

    unsafe_claims = find_unsupported_runtime_claims("TSAN runtime validation already passed.")
    if not unsafe_claims:
        errors.append("Runtime-proof guard failed to reject TSAN runtime validation already passed")
    safe_claims = find_unsupported_runtime_claims("TSAN runtime validation deferred to M004.")
    if safe_claims:
        errors.append("Runtime-proof guard rejected a deferred TSAN runtime validation statement")

    forbidden_source_fixture = (
        "libdmtcp_alloc.so libdmtcp_dl.so "
        "__d_libdir__libdmtcp_alloc_so_SOURCES __d_libdir__libdmtcp_dl_so_SOURCES "
        "dmtcp_Alloc_PluginDescr dmtcp_Dl_PluginDescr DmtcpPluginDescriptor_t"
    )
    for label, marker in {
        "stale alloc DSO row": "libdmtcp_alloc.so",
        "stale DL DSO row": "libdmtcp_dl.so",
        "synthetic alloc descriptor": "dmtcp_Alloc_PluginDescr",
        "synthetic DL descriptor": "dmtcp_Dl_PluginDescr",
    }.items():
        if marker not in forbidden_source_fixture:
            errors.append(f"Forbidden-source negative fixture failed to include {label}")


def citation_line(paths: list[str]) -> str:
    return "Source citations: " + ", ".join(f"`{path}`" for path in paths)


def table_block(*rows: tuple[str, str]) -> str:
    lines = ["| Item | Coverage |", "|---|---|"]
    lines.extend(f"| {left} | {right} |" for left, right in rows)
    return "\n".join(lines)


def build_valid_fixture() -> str:
    section_notes = {
        "Scope and evidence boundary": [
            citation_line(SYNTHESIS_SECTION_CITATIONS["Scope and evidence boundary"]),
            "Final S04 design synthesis is bounded to non-hidden source evidence and public appendices.",
        ],
        "Source appendix synthesis map": [
            citation_line(REQUIRED_EVIDENCE_FILES),
            table_block(
                ("S01", "wrapper-order TSAN appendix"),
                ("S02", "registration architecture appendix"),
                ("S03", "migration plan appendix"),
            ),
        ],
        "Accepted consolidation design": [
            citation_line(SYNTHESIS_SECTION_CITATIONS["Accepted consolidation design"]),
            "The accepted design moves built-ins into libdmtcp.so through a PluginManager-owned "
            "built-in table and a built-in descriptor table that calls dmtcp_register_plugin.",
        ],
        "Built-in registration and external plugin ABI boundary": [
            citation_line(
                SYNTHESIS_SECTION_CITATIONS[
                    "Built-in registration and external plugin ABI boundary"
                ]
            ),
            "The external plugin ABI remains unchanged: external dmtcp_initialize_plugin chain, "
            "DMTCP_DECL_PLUGIN, and NEXT_FNC(dmtcp_initialize_plugin) stay available to external plugins.",
        ],
        "Staged migration order and gate handoff": [
            citation_line(SYNTHESIS_SECTION_CITATIONS["Staged migration order and gate handoff"]),
            table_block(
                ("order", "timer,svipc,ipc,pid,alloc,dl"),
                ("stages", "timer stage; SysV IPC stage; IPC stage; PID stage; alloc stage; DL stage"),
            ),
        ],
        "Wrapper-only alloc/dl and runtime-risk boundary": [
            citation_line(
                SYNTHESIS_SECTION_CITATIONS["Wrapper-only alloc/dl and runtime-risk boundary"]
            ),
            "alloc and dl remain wrapper-only around ENV_VAR_ALLOC_PLUGIN and ENV_VAR_DL_PLUGIN; "
            "TSAN, dlsym/RTLD_NEXT, and dl_iterate_phdr stay under deferred runtime validation.",
        ],
        "Alternatives considered": [
            citation_line(SYNTHESIS_SECTION_CITATIONS["Alternatives considered"]),
            table_block(
                ("status quo separate DSOs", "rejected because it preserves current fragmentation"),
                ("broad mainline merge", "rejected because it imports unrelated churn"),
                ("transitional shim", "rejected in favor of direct staged migration"),
                ("all-at-once migration", "rejected because it hides stage-local rollback"),
                ("merge optional and external plugins", "rejected because external plugins stay separate"),
                ("full TSAN checkpoint/restart first acceptance gate", "rejected because M004 owns runtime stress evidence"),
            ),
        ],
        "Decision coverage D001-D008": [
            citation_line(COVERAGE_SECTION_CITATIONS["Decision coverage D001-D008"]),
            table_block(
                ("D001", "covered"),
                ("D002", "covered"),
                ("D003", "covered"),
                ("D004", "covered"),
                ("D005", "covered"),
                ("D006", "covered"),
                ("D007", "covered"),
                ("D008", "covered"),
            ),
        ],
        "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014": [
            citation_line(
                COVERAGE_SECTION_CITATIONS[
                    "Requirement coverage R001 R002 R003 R006 R009 R010 R011 R014"
                ]
            ),
            table_block(
                ("R001", "satisfied at M001 design-proof level"),
                ("R002", "covered"),
                ("R003", "covered"),
                ("R006", "covered"),
                ("R009", "covered"),
                ("R010", "covered"),
                ("R011", "covered"),
                (
                    "R012",
                    "deferred TSAN checkpoint/restart scope with no hard M001 or M003 "
                    "acceptance gate; future M004 or future TSAN runtime evidence before any "
                    "checkpoint restart guarantee",
                ),
                (
                    "R013",
                    "deferred runtime-overhead threshold scope with no concrete runtime overhead "
                    "threshold defined or enforced in M001; future measurement gate or benchmark "
                    "only if maintainers define a threshold",
                ),
                ("R014", "covered"),
            ),
            "deferred implementation/sanitizer/external-plugin requirements remain active handoffs.",
        ],
        "No-transitional-shim recommendation": [
            citation_line(COVERAGE_SECTION_CITATIONS["No-transitional-shim recommendation"]),
            "Recommendation: no transitional shim; use direct staged migration, preserve the old DSO "
            "boundary only as a rollback point, and include stale artifact cleanup.",
        ],
        "Runtime proof boundaries and deferred validation": [
            citation_line(COVERAGE_SECTION_CITATIONS["Runtime proof boundaries and deferred validation"]),
            "This final design is not runtime proof. TSAN runtime validation deferred; "
            "dlsym/RTLD_NEXT runtime validation deferred; dl_iterate_phdr runtime validation deferred to M004.",
        ],
        "M002 through M005 handoff": [
            citation_line(COVERAGE_SECTION_CITATIONS["M002 through M005 handoff"]),
            table_block(
                ("M002", "ABI preservation and event-order preservation"),
                ("M003", "wrapper-only disable behavior and migration gates"),
                ("M004", "TSAN/dlsym/dl_iterate_phdr runtime validation"),
                ("M005", "static-initialization safety and release hardening"),
            ),
        ],
        "Known risks and diagnostics": [
            citation_line(COVERAGE_SECTION_CITATIONS["Known risks and diagnostics"]),
            table_block(
                ("Q3", "false acceptance risk blocked by verifier"),
                ("Q4", "requirement handoff coverage checked"),
                ("Q5", "malformed documents fail with precise diagnostics"),
                ("Q6", "static bounded scans only"),
                ("Q7", "negative fixtures exercise contract"),
            ),
            "Documentation artifact outcome: the plan remains a static design proof.",
            "Out-of-scope testing-harness note: harness redesign is not part of this closeout.",
            "Diagnostic command: python3 test/verify-consolidation-design-plan.py full.",
        ],
    }

    parts = ["# Internal plugin consolidation final design plan", ""]
    for heading in FULL_REQUIRED_HEADINGS:
        parts.append(f"## {heading}")
        parts.extend(section_notes[heading])
        parts.append("")
    return "\n".join(parts)


def remove_section(text: str, heading: str) -> str:
    span = markdown_section_span(text, heading)
    if span is None:
        return text
    start, end = span
    return text[:start] + text[end:]


def expect_fixture_error(
    errors: list[str],
    *,
    label: str,
    text: str,
    expected_fragment: str,
) -> None:
    fixture_errors, _ = validate_document_text(
        text=text,
        mode="self-test fixture",
        required_headings=FULL_REQUIRED_HEADINGS,
        required_files=REQUIRED_EVIDENCE_FILES,
        section_citations=FULL_SECTION_CITATIONS,
        marker_groups=FULL_MARKER_GROUPS,
        table_headings=FULL_TABLE_HEADINGS,
        section_marker_groups=FULL_SECTION_MARKERS,
    )
    if not any(expected_fragment in error for error in fixture_errors):
        errors.append(
            f"Negative fixture {label!r} did not produce expected diagnostic "
            f"{expected_fragment!r}; got: {fixture_errors[:5]}"
        )


def validate_negative_fixtures(errors: list[str]) -> None:
    fixture = build_valid_fixture()
    fixture_errors, _ = validate_document_text(
        text=fixture,
        mode="self-test positive fixture",
        required_headings=FULL_REQUIRED_HEADINGS,
        required_files=REQUIRED_EVIDENCE_FILES,
        section_citations=FULL_SECTION_CITATIONS,
        marker_groups=FULL_MARKER_GROUPS,
        table_headings=FULL_TABLE_HEADINGS,
        section_marker_groups=FULL_SECTION_MARKERS,
    )
    if fixture_errors:
        errors.append(
            "Positive self-test fixture unexpectedly failed final design contract: "
            + "; ".join(fixture_errors[:8])
        )
        return

    expect_fixture_error(
        errors,
        label="missing required heading",
        text=remove_section(fixture, "Accepted consolidation design"),
        expected_fragment="Missing required heading: ## Accepted consolidation design",
    )
    expect_fixture_error(
        errors,
        label="hidden path citation",
        text=fixture.replace(
            "Final S04 design synthesis",
            "Final S04 design synthesis using `.gsd/milestones/M001/hidden.md`",
            1,
        ),
        expected_fragment="Final design plan references hidden planning artifact .gsd",
    )
    expect_fixture_error(
        errors,
        label="placeholder leakage",
        text=fixture.replace("covered", "TODO", 1),
        expected_fragment="Placeholder pattern 'TODO' matched",
    )
    expect_fixture_error(
        errors,
        label="missing source anchor",
        text=fixture.replace("src/pluginmanager.cpp", "src/pluginmanager-missing.cpp"),
        expected_fragment="Missing required source citation: src/pluginmanager.cpp",
    )
    expect_fixture_error(
        errors,
        label="missing decision IDs",
        text=fixture.replace("D008", "D00X"),
        expected_fragment="Missing required marker for decision coverage: D008",
    )
    expect_fixture_error(
        errors,
        label="missing requirement IDs",
        text=fixture.replace("R014", "R01X"),
        expected_fragment="Missing required marker for requirement coverage: R014",
    )
    expect_fixture_error(
        errors,
        label="missing R012 deferred scoping",
        text="\n".join(line for line in fixture.splitlines() if "R012" not in line),
        expected_fragment="Missing required marker for R012 scoping: R012",
    )
    expect_fixture_error(
        errors,
        label="missing R013 deferred scoping",
        text="\n".join(line for line in fixture.splitlines() if "R013" not in line),
        expected_fragment="Missing required marker for R013 scoping: R013",
    )
    expect_fixture_error(
        errors,
        label="wrong migration order",
        text=fixture.replace(EXPECTED_STAGE_ORDER, "timer,ipc,svipc,pid,alloc,dl"),
        expected_fragment=(
            "Wrong staged migration order found: timer,ipc,svipc,pid,alloc,dl; "
            f"expected {EXPECTED_STAGE_ORDER}."
        ),
    )
    expect_fixture_error(
        errors,
        label="missing external plugin ABI boundary",
        text=fixture.replace("external plugin ABI", "external boundary"),
        expected_fragment="Missing required marker for external plugin ABI boundary: external plugin ABI",
    )
    expect_fixture_error(
        errors,
        label="missing no-transitional-shim recommendation",
        text=fixture.replace("no transitional shim", "temporary compatibility shim"),
        expected_fragment="Missing required marker for no transitional shim recommendation: no transitional shim",
    )
    expect_fixture_error(
        errors,
        label="missing all-at-once rejected alternative",
        text=fixture.replace("all-at-once migration", "single broad migration"),
        expected_fragment="Missing required marker for alternatives coverage: all-at-once migration",
    )
    expect_fixture_error(
        errors,
        label="missing external-plugin merge rejected alternative",
        text=fixture.replace("merge optional and external plugins", "fold optional plugin behavior"),
        expected_fragment="Missing required marker for alternatives coverage: merge optional and external plugins",
    )
    expect_fixture_error(
        errors,
        label="missing R001 design-proof status",
        text=fixture.replace("satisfied at M001 design-proof level", "covered at design level"),
        expected_fragment=(
            "Missing required marker for requirement coverage: "
            "satisfied at M001 design-proof level"
        ),
    )
    expect_fixture_error(
        errors,
        label="missing active downstream handoff statement",
        text=fixture.replace(
            "deferred implementation/sanitizer/external-plugin requirements remain active handoffs",
            "downstream requirements are mentioned",
        ),
        expected_fragment=(
            "Missing required marker for requirement coverage: "
            "deferred implementation/sanitizer/external-plugin requirements remain active handoffs"
        ),
    )
    expect_fixture_error(
        errors,
        label="missing out-of-scope testing-harness note",
        text=fixture.replace("Out-of-scope testing-harness note", "Testing note"),
        expected_fragment=(
            "Missing required marker for quality gate diagnostics: "
            "out-of-scope testing-harness note"
        ),
    )
    expect_fixture_error(
        errors,
        label="unsupported runtime proof claim",
        text=fixture.replace(
            "This final design is not runtime proof.",
            "TSAN runtime validation already passed.",
        ),
        expected_fragment="Unsupported runtime-proof claim found",
    )


def validate_self_test() -> int:
    errors: list[str] = []
    if str(DOC_PATH) != EXPECTED_DOC_PATH:
        errors.append(f"Verifier DOC_PATH drifted: expected {EXPECTED_DOC_PATH}, found {DOC_PATH}")

    validate_table_shape(errors)
    validate_scan_set(errors, [DOC_PATH, *REQUIRED_EVIDENCE_FILES], "self-test")
    validate_no_recursive_scan_apis(errors)
    validate_negative_guards(errors)
    validate_source_anchors(errors)

    seen_files: set[str] = set()
    for rel in REQUIRED_EVIDENCE_FILES:
        if rel in seen_files:
            errors.append(f"Duplicate path in explicit evidence list: {rel}")
        seen_files.add(rel)
        if is_hidden_or_outside_repo(rel):
            errors.append(f"Explicit evidence list includes hidden/out-of-repo path: {rel}")
        elif not repo_file_exists(rel):
            errors.append(f"Explicit evidence list path does not exist: {rel}")

    validate_negative_fixtures(errors)
    return report("self-test", errors)


def validate_synthesis_map() -> int:
    return validate_common(
        mode="synthesis-map",
        required_headings=SYNTHESIS_REQUIRED_HEADINGS,
        section_citations=SYNTHESIS_SECTION_CITATIONS,
        marker_groups=SYNTHESIS_MARKER_GROUPS,
        table_headings=SYNTHESIS_TABLE_HEADINGS,
        section_marker_groups=SYNTHESIS_SECTION_MARKERS,
    )


def validate_coverage_map() -> int:
    return validate_common(
        mode="coverage-map",
        required_headings=COVERAGE_REQUIRED_HEADINGS,
        section_citations=COVERAGE_SECTION_CITATIONS,
        marker_groups=COVERAGE_MARKER_GROUPS,
        table_headings=COVERAGE_TABLE_HEADINGS,
        section_marker_groups=COVERAGE_SECTION_MARKERS,
    )


def validate_full() -> int:
    return validate_common(
        mode="full",
        required_headings=FULL_REQUIRED_HEADINGS,
        section_citations=FULL_SECTION_CITATIONS,
        marker_groups=FULL_MARKER_GROUPS,
        table_headings=FULL_TABLE_HEADINGS,
        section_marker_groups=FULL_SECTION_MARKERS,
    )


def report(mode: str, errors: list[str]) -> int:
    if errors:
        print(f"{mode} verification failed for {DOC_PATH}:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(
        f"{mode} verification passed for {DOC_PATH}: required headings, tables, citations, "
        "markers, source anchors, negative fixtures, runtime-claim guards, and forbidden-scan "
        "checks are satisfied."
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=["self-test", "synthesis-map", "coverage-map", "full"],
        help="verification mode to run",
    )
    args = parser.parse_args(argv)

    if args.mode == "self-test":
        return validate_self_test()
    if args.mode == "synthesis-map":
        return validate_synthesis_map()
    if args.mode == "coverage-map":
        return validate_coverage_map()
    if args.mode == "full":
        return validate_full()
    raise AssertionError(f"unhandled mode: {args.mode}")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
