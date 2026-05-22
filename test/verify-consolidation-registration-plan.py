#!/usr/bin/env python3
"""Static checks for the built-in plugin registration architecture plan.

`self-test` validates this verifier's own contract: it uses one explicit,
non-hidden source list, targets the S02 registration-architecture appendix, and
has non-empty heading/citation/marker tables.  `current-map` validates the
source-backed current registration map.  `integration` validates that the
accepted design and migration appendices keep explicit S02 registration-gate
links to the architecture appendix and verifier without hidden planning artifacts
or unsupported runtime proof claims.  `full` validates the complete S02
architecture appendix and the integration references, including ABI preservation,
PluginManager-owned built-in registration, order constraints, wrapper-only
alloc/dl enable semantics, timer tracer-bullet coverage, R001/R006/R007 handoff,
and the no-duplicate-internal initializer rule.

All modes inspect only the explicit non-hidden paths listed below.  The verifier
does not walk the repository and does not read generated planning artifacts such
as .gsd, .planning, .audits, or other hidden paths.  Integration mode reads only
the explicit design and migration appendices.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DOC_PATH = Path("doc/internal-plugin-consolidation-registration-architecture.md")
DESIGN_DOC_PATH = Path("doc/internal-plugin-consolidation-design-plan.md")
MIGRATION_DOC_PATH = Path("doc/internal-plugin-consolidation-migration-plan.md")
REGISTRATION_VERIFIER_PATH = Path("test/verify-consolidation-registration-plan.py")
EXPECTED_DOC_PATH = "doc/internal-plugin-consolidation-registration-architecture.md"

INTEGRATION_DOC_PATHS = [DESIGN_DOC_PATH, MIGRATION_DOC_PATH]

CURRENT_MAP_REQUIRED_FILES = [
    "src/dmtcp_launch.cpp",
    "src/pluginmanager.cpp",
    "src/pluginmanager.h",
    "include/dmtcp.h",
    "src/plugin/timer/timerlist.cpp",
    "src/plugin/svipc/sysvipc.cpp",
    "src/plugin/ipc/ipc.cpp",
    "src/plugin/ipc/file/fileconnlist.cpp",
    "src/plugin/ipc/file/ptyconnlist.cpp",
    "src/plugin/ipc/socket/socketconnlist.cpp",
    "src/plugin/ipc/event/eventconnlist.cpp",
    "src/plugin/ipc/ssh/ssh.cpp",
    "src/plugin/pid/pid.cpp",
    "src/plugin/alloc/mallocwrappers.cpp",
    "src/plugin/dl/dlwrappers.cpp",
    "src/Makefile.am",
    "src/plugin/Makefile.am",
]

CURRENT_MAP_REQUIRED_HEADINGS = [
    "Scope and evidence boundary",
    "Current source map",
    "Current dmtcp_launch preload and build boundary",
    "Current PluginManager registration chain",
    "Current external plugin ABI and DMTCP_DECL_PLUGIN chain",
    "Current built-in and external ordering",
    "Current IPC subplugin ordering",
    "Current wrapper-only alloc and dl enable semantics",
]

FULL_EXTRA_HEADINGS = [
    "Proposed PluginManager-owned built-in registration table",
    "Descriptor accessor naming and ABI boundary",
    "Consolidated IPC registration design",
    "Timer tracer bullet implementation plan",
    "Requirements and handoff coverage",
    "Duplicate dmtcp_initialize_plugin prevention",
]

FULL_REQUIRED_HEADINGS = [*CURRENT_MAP_REQUIRED_HEADINGS, *FULL_EXTRA_HEADINGS]

CURRENT_MAP_SECTION_CITATIONS = {
    "Scope and evidence boundary": [
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
    ],
    "Current source map": CURRENT_MAP_REQUIRED_FILES,
    "Current dmtcp_launch preload and build boundary": [
        "src/dmtcp_launch.cpp",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
    ],
    "Current PluginManager registration chain": [
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
    ],
    "Current external plugin ABI and DMTCP_DECL_PLUGIN chain": [
        "include/dmtcp.h",
        "src/pluginmanager.cpp",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/pid/pid.cpp",
    ],
    "Current built-in and external ordering": [
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
    ],
    "Current IPC subplugin ordering": [
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/event/eventconnlist.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/file/ptyconnlist.cpp",
        "src/plugin/ipc/socket/socketconnlist.cpp",
    ],
    "Current wrapper-only alloc and dl enable semantics": [
        "src/dmtcp_launch.cpp",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
}

FULL_SECTION_CITATIONS = {
    **CURRENT_MAP_SECTION_CITATIONS,
    "Proposed PluginManager-owned built-in registration table": [
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
        "src/dmtcp_launch.cpp",
    ],
    "Descriptor accessor naming and ABI boundary": [
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/pid/pid.cpp",
    ],
    "Consolidated IPC registration design": [
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/event/eventconnlist.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/file/ptyconnlist.cpp",
        "src/plugin/ipc/socket/socketconnlist.cpp",
    ],
    "Timer tracer bullet implementation plan": [
        "src/plugin/timer/timerlist.cpp",
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
        "src/plugin/Makefile.am",
    ],
    "Requirements and handoff coverage": [
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
    "Duplicate dmtcp_initialize_plugin prevention": [
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/pid/pid.cpp",
    ],
}

CURRENT_MAP_MARKER_GROUPS = {
    "PluginManager registration surface": [
        "PluginManager::initialize",
        "dmtcp_register_plugin",
        "dmtcp_initialize_plugin",
        "NEXT_FNC(dmtcp_initialize_plugin)",
    ],
    "External plugin ABI macro": [
        "DMTCP_DECL_PLUGIN",
        "include/dmtcp.h",
        "dmtcp_initialize_plugin",
    ],
    "Launch preload and built-in shared-object order": [
        "ENV_VAR_HIJACK_LIBS",
        "libdmtcp_alloc.so",
        "libdmtcp_dl.so",
        "libdmtcp_ipc.so",
        "libdmtcp_svipc.so",
        "libdmtcp_timer.so",
        "libdmtcp_pid.so",
    ],
    "Current IPC registration order": [
        "ipc_initialize_plugin_ssh",
        "ipc_initialize_plugin_event",
        "ipc_initialize_plugin_file",
        "ipc_initialize_plugin_pty",
        "ipc_initialize_plugin_socket",
        "ssh,event,file,pty,socket",
    ],
    "Reverse resume and restart behavior": [
        "DMTCP_EVENT_RESUME",
        "DMTCP_EVENT_RESTART",
        "reverse order",
    ],
    "Alloc/dl wrapper enable flags": [
        "ENV_VAR_ALLOC_PLUGIN",
        "ENV_VAR_DL_PLUGIN",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
}

FULL_MARKER_GROUPS = {
    "External dmtcp_initialize_plugin chain": [
        "external dmtcp_initialize_plugin chain",
        "NEXT_FNC(dmtcp_initialize_plugin)",
        "dmtcp_register_plugin",
    ],
    "Unchanged DMTCP_DECL_PLUGIN ABI": [
        "unchanged DMTCP_DECL_PLUGIN",
        "DMTCP_DECL_PLUGIN",
        "include/dmtcp.h",
    ],
    "PluginManager-owned built-in table": [
        "PluginManager-owned built-in table",
        "built-in descriptor table",
        "PluginManager",
    ],
    "Descriptor accessor naming": [
        "descriptor accessor",
        "PluginDescr()",
        "dmtcp_",
    ],
    "IPC forward order": [
        "ssh,event,file,pty,socket",
    ],
    "IPC reverse restart order": [
        "socket,pty,file,event,ssh",
    ],
    "Alloc/dl wrapper-only enable path": [
        "engine-controlled wrapper-only enable path",
        "ENV_VAR_ALLOC_PLUGIN",
        "ENV_VAR_DL_PLUGIN",
        "wrapper-only",
    ],
    "Timer tracer bullet": [
        "timer tracer bullet",
        "timerPlugin",
        "src/plugin/timer/timerlist.cpp",
    ],
    "R001/R006/R007 handoff": [
        "R001",
        "R006",
        "R007",
    ],
    "No duplicate internal initializers": [
        "no duplicate internal dmtcp_initialize_plugin definitions",
        "one libdmtcp dmtcp_initialize_plugin",
        "no DMTCP_DECL_PLUGIN for consolidated built-ins",
    ],
}

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
    "markdown fill-in marker": re.compile(r"\[(?:fill in|todo|tbd|placeholder|replace)[^\]\n]*\]", re.IGNORECASE),
}

HIDDEN_SENTINELS = {".gsd", ".planning", ".audits"}

INTEGRATION_MARKER_GROUPS = {
    "S02 registration gate references": [
        "S02 registration gate",
        str(DOC_PATH),
        str(REGISTRATION_VERIFIER_PATH),
    ],
    "Public external ABI unchanged": [
        "public external ABI unchanged",
        "DMTCP_DECL_PLUGIN",
    ],
    "PluginManager-owned descriptor table": [
        "PluginManager-owned built-in descriptor table",
    ],
    "Non-public descriptor accessors": [
        "non-public accessors",
    ],
    "No duplicate internal initializers": [
        "no duplicate internal initializer definitions",
    ],
    "Alloc/dl wrapper-only boundary": [
        "alloc/dl wrapper-only",
    ],
    "Pid-last ordering": [
        "pid-last",
    ],
    "No runtime proof claims": [
        "no runtime proof claims",
    ],
}

UNSUPPORTED_RUNTIME_CLAIM_PATTERNS = {
    "S02 runtime proof claim": re.compile(r"\bS02 proves runtime\b", re.IGNORECASE),
    "S02 build success claim": re.compile(r"\bS02 build passed\b", re.IGNORECASE),
    "S02 runtime test success claim": re.compile(r"\bS02 runtime tests passed\b", re.IGNORECASE),
    "S02 checkpoint/restart success claim": re.compile(r"\bS02 checkpoint/restart passed\b", re.IGNORECASE),
    "S02 sanitizer success claim": re.compile(r"\bS02 sanitizer passed\b", re.IGNORECASE),
    "S02 external plugin success claim": re.compile(
        r"\bS02 external plugin compatibility passed\b", re.IGNORECASE
    ),
    "generic runtime proof completion claim": re.compile(
        r"\bruntime proof is complete\b", re.IGNORECASE
    ),
    "generic make-check success claim": re.compile(r"\bmake check passed\b", re.IGNORECASE),
    "generic checkpoint/restart success claim": re.compile(
        r"\bcheckpoint/restart passed\b", re.IGNORECASE
    ),
    "generic TSAN success claim": re.compile(r"\bTSAN passed\b", re.IGNORECASE),
}


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
        "src/dmtcp_launch.cpp",
        (
            "pluginInfo[]",
            "ENV_VAR_HIJACK_LIBS",
            "ENV_VAR_ALLOC_PLUGIN",
            "ENV_VAR_DL_PLUGIN",
            "libdmtcp_pathvirt.so",
            "ENV_VAR_DISABLE_ALL_PLUGINS",
            "libdmtcp.so",
            "setLDPreloadLibs(bool is32bitElf)",
        ),
    ),
    SourceAnchor(
        "src/pluginmanager.cpp",
        (
            "dmtcp_register_plugin",
            "PluginManager::initialize",
            "dmtcp_initialize_plugin",
            "NEXT_FNC(dmtcp_initialize_plugin)",
            "PluginManager::eventHook",
            "pluginInfos.push_back",
        ),
    ),
    SourceAnchor(
        "src/pluginmanager.h",
        (
            "class PluginManager",
            "registerPlugin",
            "eventHook",
            "vector<PluginInfo *>",
        ),
    ),
    SourceAnchor(
        "include/dmtcp.h",
        (
            "void dmtcp_initialize_plugin(void) __attribute((weak))",
            "#define DMTCP_DECL_PLUGIN",
            "NEXT_FNC(dmtcp_initialize_plugin)",
        ),
    ),
    SourceAnchor(
        "src/plugin/timer/timerlist.cpp",
        (
            "timer_event_hook",
            "DmtcpPluginDescriptor_t timerPlugin",
            "dmtcp_Timer_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/svipc/sysvipc.cpp",
        (
            "sysvipc_event_hook",
            "DmtcpPluginDescriptor_t sysvipcPlugin",
            "dmtcp_SysVIPC_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/ipc.cpp",
        (
            "IPC subplugin descriptors are registered by PluginManager",
            "do not define an IPC-local dmtcp_initialize_plugin() chain here",
            "#include \"ipc.h\"",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/ssh/ssh.cpp",
        (
            "DmtcpPluginDescriptor_t sshPlugin",
            "dmtcp_IpcSsh_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/event/eventconnlist.cpp",
        (
            "DmtcpPluginDescriptor_t eventPlugin",
            "dmtcp_IpcEvent_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/file/fileconnlist.cpp",
        (
            "DmtcpPluginDescriptor_t filePlugin",
            "dmtcp_IpcFile_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/file/ptyconnlist.cpp",
        (
            "DmtcpPluginDescriptor_t ptyPlugin",
            "dmtcp_IpcPty_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/socket/socketconnlist.cpp",
        (
            "DmtcpPluginDescriptor_t socketPlugin",
            "dmtcp_IpcSocket_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pid.cpp",
        (
            "pid_event_hook",
            "DmtcpPluginDescriptor_t pidPlugin",
            "dmtcp_Pid_PluginDescr()",
        ),
    ),
    SourceAnchor(
        "src/plugin/alloc/mallocwrappers.cpp",
        (
            "extern \"C\" void *malloc",
            "free(void *ptr)",
        ),
    ),
    SourceAnchor(
        "src/plugin/dl/dlwrappers.cpp",
        (
            "#define _real_dlopen",
            "thread_performing_dlopen_dlsym",
            "WRAPPER_EXECUTION_DISABLE_CKPT",
        ),
    ),
    SourceAnchor(
        "src/Makefile.am",
        (
            "__d_libdir__libdmtcp_so_SOURCES",
            "pluginmanager.cpp",
            "pluginmanager.h",
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
]

FORBIDDEN_SOURCE_MARKERS = [
    ForbiddenSourceMarker(
        "src/dmtcp_launch.cpp",
        "alloc and DL are wrapper-only inside libdmtcp.so and must not be emitted as launcher preload DSOs",
        ("libdmtcp_alloc.so", "libdmtcp_dl.so"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/Makefile.am",
        "alloc and DL wrappers are owned by src/Makefile.am, not separate plugin DSOs",
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
        "src/pluginmanager.cpp",
        "alloc and DL must not gain descriptor accessors or PluginManager event rows",
        (
            "dmtcp_Alloc_PluginDescr",
            "dmtcp_Dl_PluginDescr",
            "dmtcp_DL_PluginDescr",
            "Alloc_PluginDescr",
            "Dl_PluginDescr",
            "DL_PluginDescr",
        ),
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
        "PluginManager built-in registration order with PID last",
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
        "PluginManager IPC subplugin registration order",
        (
            "dmtcp_register_plugin(dmtcp_IpcSsh_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcEvent_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcFile_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcPty_PluginDescr());",
            "dmtcp_register_plugin(dmtcp_IpcSocket_PluginDescr());",
        ),
    ),
    OrderedSourceAnchor(
        "src/pluginmanager.cpp",
        "PluginManager restart/resume reverse traversal",
        (
            "DMTCP_EVENT_RESUME",
            "DMTCP_EVENT_RESTART",
            "for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--)",
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


def markdown_section(text: str, heading: str) -> str | None:
    marker = re.compile(rf"^##\s+{re.escape(heading)}\s*$", re.MULTILINE)
    match = marker.search(text)
    if not match:
        return None
    next_heading = re.search(r"^##\s+", text[match.end() :], re.MULTILINE)
    end = match.end() + next_heading.start() if next_heading else len(text)
    return text[match.end() : end]


def compact_marker_text(text: str) -> str:
    return re.sub(r"\s+", "", text.replace("`", "")).lower()


def marker_present(haystack: str, marker: str) -> bool:
    normalized = haystack.replace("`", "")
    if "," in marker:
        return marker.lower().replace(" ", "") in compact_marker_text(normalized)
    return marker.lower() in normalized.lower()


def validate_scan_set(errors: list[str], required_files: list[str], mode: str) -> None:
    explicit_scan_paths = [DOC_PATH, *map(Path, required_files)]
    for rel_path in explicit_scan_paths:
        rel = str(rel_path)
        if is_hidden_or_outside_repo(rel):
            errors.append(
                f"Verifier scan set includes hidden/out-of-repo path {rel!r}; "
                f"{mode} must inspect only explicit non-hidden repo paths."
            )
        elif rel_path.parts and rel_path.parts[0] in HIDDEN_SENTINELS:
            errors.append(
                f"Verifier scan set includes forbidden artifact path {rel!r}; "
                "do not satisfy registration checks from generated planning artifacts."
            )


def validate_explicit_non_hidden_paths(
    errors: list[str], paths: list[Path], mode: str
) -> None:
    for rel_path in paths:
        rel = str(rel_path)
        if is_hidden_or_outside_repo(rel):
            errors.append(
                f"Verifier scan set includes hidden/out-of-repo path {rel!r}; "
                f"{mode} must inspect only explicit non-hidden repo paths."
            )
            continue
        if rel_path.parts and rel_path.parts[0] in HIDDEN_SENTINELS:
            errors.append(
                f"Verifier scan set includes forbidden artifact path {rel!r}; "
                "do not satisfy registration checks from generated planning artifacts."
            )
            continue
        if not (REPO_ROOT / rel_path).is_file():
            errors.append(f"Missing integration document: {rel}")


def validate_no_hidden_artifact_references(
    errors: list[str], text: str, subject: str
) -> None:
    for hidden in HIDDEN_SENTINELS:
        if f"{hidden}/" in text or f"`{hidden}" in text or hidden in text:
            errors.append(
                f"{subject} references hidden planning artifact {hidden}; source evidence must cite "
                "explicit non-hidden repository paths only."
            )


def validate_no_unsupported_runtime_claims(
    errors: list[str], text: str, mode: str
) -> None:
    for label, pattern in UNSUPPORTED_RUNTIME_CLAIM_PATTERNS.items():
        match = pattern.search(text)
        if match:
            errors.append(
                f"Unsupported runtime proof claim {label!r} matched {match.group(0)!r} "
                f"in {mode}; keep the S02 gate static."
            )


def validate_marker_groups_in_text(
    errors: list[str], text: str, marker_groups: dict[str, list[str]], mode: str
) -> None:
    for group, markers in marker_groups.items():
        for marker in markers:
            if not marker_present(text, marker):
                errors.append(f"Missing required marker for {group} in {mode}: {marker}")


def validate_non_empty_table(errors: list[str], name: str, table: object) -> None:
    if not table:
        errors.append(f"Verifier table is empty: {name}")


def validate_cited_paths(
    errors: list[str], cited_paths: set[str], required_files: list[str]
) -> None:
    for path in sorted(cited_paths):
        if is_hidden_or_outside_repo(path):
            errors.append(f"Hidden or out-of-repo source citation found: {path}")
        elif not repo_file_exists(path):
            errors.append(f"Source citation does not exist in repo: {path}")

    for required in required_files:
        if required not in cited_paths:
            errors.append(f"Missing required source citation: {required}")


def validate_placeholder_free_text(errors: list[str], text: str, mode: str) -> None:
    for label, pattern in PLACEHOLDER_PATTERNS.items():
        match = pattern.search(text)
        if match:
            errors.append(
                f"Placeholder pattern {label!r} matched {match.group(0)!r} in {mode} sections; "
                "replace it with source-backed text before passing verification."
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


def validate_common(
    *,
    mode: str,
    required_headings: list[str],
    required_files: list[str],
    section_citations: dict[str, list[str]],
    marker_groups: dict[str, list[str]],
) -> tuple[list[str], str, dict[str, str]]:
    errors: list[str] = []
    validate_scan_set(errors, required_files, mode)

    doc_abs = REPO_ROOT / DOC_PATH
    if not doc_abs.is_file():
        errors.append(f"Missing appendix document: {DOC_PATH}")
        return errors, "", {}

    text = doc_abs.read_text(encoding="utf-8")

    for hidden in HIDDEN_SENTINELS:
        if f"{hidden}/" in text or f"`{hidden}" in text or f"{hidden}" in text:
            errors.append(
                f"Appendix references hidden planning artifact {hidden}; source evidence must cite "
                "explicit non-hidden repository paths only."
            )

    cited_paths = extract_source_paths(text)
    validate_cited_paths(errors, cited_paths, required_files)

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

    checked_text = "\n".join(sections.values())
    validate_placeholder_free_text(errors, checked_text, mode)

    for group, markers in marker_groups.items():
        for marker in markers:
            if not marker_present(checked_text, marker):
                errors.append(f"Missing required marker for {group}: {marker}")

    return errors, text, sections


def collect_integration_errors() -> list[str]:
    errors: list[str] = []
    validate_explicit_non_hidden_paths(errors, INTEGRATION_DOC_PATHS, "integration")
    validate_non_empty_table(errors, "INTEGRATION_DOC_PATHS", INTEGRATION_DOC_PATHS)
    validate_non_empty_table(errors, "INTEGRATION_MARKER_GROUPS", INTEGRATION_MARKER_GROUPS)

    for doc_path in INTEGRATION_DOC_PATHS:
        full_path = REPO_ROOT / doc_path
        if not full_path.is_file():
            continue
        text = full_path.read_text(encoding="utf-8", errors="replace")
        mode = f"integration:{doc_path}"
        validate_no_hidden_artifact_references(errors, text, str(doc_path))
        validate_placeholder_free_text(errors, text, mode)
        validate_no_unsupported_runtime_claims(errors, text, mode)
        validate_marker_groups_in_text(errors, text, INTEGRATION_MARKER_GROUPS, mode)

    return errors


def validate_integration() -> int:
    return report("integration", collect_integration_errors())


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
                "S02 checks must stay on the explicit source list."
            )


def validate_table_shape(errors: list[str]) -> None:
    tables = {
        "CURRENT_MAP_REQUIRED_FILES": CURRENT_MAP_REQUIRED_FILES,
        "CURRENT_MAP_REQUIRED_HEADINGS": CURRENT_MAP_REQUIRED_HEADINGS,
        "FULL_REQUIRED_HEADINGS": FULL_REQUIRED_HEADINGS,
        "CURRENT_MAP_SECTION_CITATIONS": CURRENT_MAP_SECTION_CITATIONS,
        "FULL_SECTION_CITATIONS": FULL_SECTION_CITATIONS,
        "CURRENT_MAP_MARKER_GROUPS": CURRENT_MAP_MARKER_GROUPS,
        "FULL_MARKER_GROUPS": FULL_MARKER_GROUPS,
        "INTEGRATION_DOC_PATHS": INTEGRATION_DOC_PATHS,
        "INTEGRATION_MARKER_GROUPS": INTEGRATION_MARKER_GROUPS,
        "UNSUPPORTED_RUNTIME_CLAIM_PATTERNS": UNSUPPORTED_RUNTIME_CLAIM_PATTERNS,
        "SOURCE_ANCHORS": SOURCE_ANCHORS,
        "FORBIDDEN_SOURCE_MARKERS": FORBIDDEN_SOURCE_MARKERS,
        "ORDERED_SOURCE_ANCHORS": ORDERED_SOURCE_ANCHORS,
    }
    for name, table in tables.items():
        validate_non_empty_table(errors, name, table)

    current_headings = set(CURRENT_MAP_REQUIRED_HEADINGS)
    full_headings = set(FULL_REQUIRED_HEADINGS)
    for heading in CURRENT_MAP_SECTION_CITATIONS:
        if heading not in current_headings:
            errors.append(f"CURRENT_MAP_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in FULL_SECTION_CITATIONS:
        if heading not in full_headings:
            errors.append(f"FULL_SECTION_CITATIONS references unknown heading: {heading}")

    required_file_set = set(CURRENT_MAP_REQUIRED_FILES)
    for heading, paths in FULL_SECTION_CITATIONS.items():
        if not paths:
            errors.append(f"Citation list is empty for heading: {heading}")
        for path in paths:
            if path not in required_file_set:
                errors.append(f"Citation table for ## {heading} references non-required source: {path}")

    for group_name, markers in {
        **CURRENT_MAP_MARKER_GROUPS,
        **FULL_MARKER_GROUPS,
        **INTEGRATION_MARKER_GROUPS,
    }.items():
        if not markers:
            errors.append(f"Marker group is empty: {group_name}")
        for marker in markers:
            if not marker.strip():
                errors.append(f"Marker group {group_name} contains an empty marker")

    for rel_path in INTEGRATION_DOC_PATHS:
        reason = is_hidden_or_outside_repo(str(rel_path))
        if reason:
            errors.append(f"Integration doc path is hidden/outside repo: {rel_path}")


def validate_self_test_negative_guards(errors: list[str]) -> None:
    """Prove the self-test would notice common verifier-contract regressions."""
    scan_errors: list[str] = []
    validate_scan_set(
        scan_errors,
        [".gsd/example.md", "../outside.cpp", "/tmp/outside.cpp"],
        "self-test negative fixture",
    )
    if len(scan_errors) < 3:
        errors.append(
            "Hidden/out-of-repo scan-set guard failed for self-test negative fixture"
        )

    empty_table_errors: list[str] = []
    validate_non_empty_table(empty_table_errors, "SELF_TEST_EMPTY_TABLE", [])
    if not any("Verifier table is empty: SELF_TEST_EMPTY_TABLE" in error for error in empty_table_errors):
        errors.append("Empty-table guard failed for self-test negative fixture")

    placeholder_errors: list[str] = []
    validate_placeholder_free_text(
        placeholder_errors,
        "## Self-test fixture\nThis TODO should be rejected.\n",
        "self-test negative fixture",
    )
    if not any("Placeholder pattern" in error for error in placeholder_errors):
        errors.append("Placeholder guard failed for self-test negative fixture")

    citation_errors: list[str] = []
    validate_cited_paths(citation_errors, set(), ["src/dmtcp_launch.cpp"])
    if not any(
        "Missing required source citation: src/dmtcp_launch.cpp" in error
        for error in citation_errors
    ):
        errors.append("Missing-source-citation guard failed for self-test negative fixture")

    hidden_doc_errors: list[str] = []
    validate_no_hidden_artifact_references(
        hidden_doc_errors,
        "Synthetic integration text references .gsd/example.md.",
        "integration negative fixture",
    )
    if not any("hidden planning artifact .gsd" in error for error in hidden_doc_errors):
        errors.append("Hidden-artifact guard failed for integration negative fixture")

    missing_marker_errors: list[str] = []
    validate_marker_groups_in_text(
        missing_marker_errors,
        "S02 registration gate text without the required appendix links.",
        INTEGRATION_MARKER_GROUPS,
        "integration negative fixture",
    )
    if not any(str(DOC_PATH) in error for error in missing_marker_errors):
        errors.append("Missing architecture-link guard failed for integration negative fixture")
    if not any(str(REGISTRATION_VERIFIER_PATH) in error for error in missing_marker_errors):
        errors.append("Missing verifier-link guard failed for integration negative fixture")

    runtime_claim_errors: list[str] = []
    validate_no_unsupported_runtime_claims(
        runtime_claim_errors,
        "S02 runtime tests passed and make check passed.",
        "integration negative fixture",
    )
    if len(runtime_claim_errors) < 2:
        errors.append("Unsupported-runtime-claim guard failed for integration negative fixture")

    forbidden_errors: list[str] = []
    for forbidden in [
        ForbiddenSourceMarker("synthetic", "stale alloc launcher row", ("libdmtcp_alloc.so",)),
        ForbiddenSourceMarker("synthetic", "stale dl launcher row", ("libdmtcp_dl.so",)),
        ForbiddenSourceMarker("synthetic", "synthetic alloc descriptor", ("dmtcp_Alloc_PluginDescr",)),
        ForbiddenSourceMarker("synthetic", "synthetic DL descriptor", ("dmtcp_Dl_PluginDescr",)),
    ]:
        text = "libdmtcp_alloc.so libdmtcp_dl.so dmtcp_Alloc_PluginDescr dmtcp_Dl_PluginDescr"
        for marker in forbidden.markers:
            if marker in text:
                forbidden_errors.append(f"Source anchor {forbidden.path} unexpectedly contains marker {marker!r}; {forbidden.reason}.")
    if not any("libdmtcp_alloc.so" in error for error in forbidden_errors):
        errors.append("Forbidden stale alloc launcher-row guard failed for self-test negative fixture")
    if not any("libdmtcp_dl.so" in error for error in forbidden_errors):
        errors.append("Forbidden stale DL launcher-row guard failed for self-test negative fixture")
    if not any("dmtcp_Alloc_PluginDescr" in error for error in forbidden_errors):
        errors.append("Forbidden synthetic alloc descriptor guard failed for self-test negative fixture")
    if not any("dmtcp_Dl_PluginDescr" in error for error in forbidden_errors):
        errors.append("Forbidden synthetic DL descriptor guard failed for self-test negative fixture")

    full_mode_errors: list[str] = []
    validate_full_mode_includes_integration(full_mode_errors)
    errors.extend(full_mode_errors)


def validate_full_mode_includes_integration(errors: list[str]) -> None:
    source = Path(__file__).read_text(encoding="utf-8")
    tree = ast.parse(source)
    validate_full_node = None
    for node in iter_ast_nodes(tree):
        if isinstance(node, ast.FunctionDef) and node.name == "validate_full":
            validate_full_node = node
            break
    if validate_full_node is None:
        errors.append("Full-mode regression guard could not find validate_full")
        return

    for node in iter_ast_nodes(validate_full_node):
        if isinstance(node, ast.Call) and call_name(node.func) == "collect_integration_errors":
            return
    errors.append("Full-mode regression guard failed: full does not include integration checks")


def validate_self_test() -> int:
    errors: list[str] = []
    if str(DOC_PATH) != EXPECTED_DOC_PATH:
        errors.append(
            f"Verifier DOC_PATH drifted: expected {EXPECTED_DOC_PATH}, found {DOC_PATH}"
        )

    validate_table_shape(errors)
    validate_scan_set(errors, CURRENT_MAP_REQUIRED_FILES, "self-test")
    validate_no_recursive_scan_apis(errors)
    validate_self_test_negative_guards(errors)

    seen_files: set[str] = set()
    for rel in CURRENT_MAP_REQUIRED_FILES:
        if rel in seen_files:
            errors.append(f"Duplicate source path in explicit source list: {rel}")
        seen_files.add(rel)
        if is_hidden_or_outside_repo(rel):
            errors.append(f"Explicit source list includes hidden/out-of-repo path: {rel}")
        elif not repo_file_exists(rel):
            errors.append(f"Explicit source list path does not exist: {rel}")

    for hidden in HIDDEN_SENTINELS:
        hidden_path = f"{hidden}/example"
        if not is_hidden_or_outside_repo(hidden_path):
            errors.append(f"Hidden path guard failed for sentinel: {hidden}")

    return report("self-test", errors)


def validate_current_map() -> int:
    source_errors: list[str] = []
    validate_source_anchors(source_errors)
    errors, _, _ = validate_common(
        mode="current-map",
        required_headings=CURRENT_MAP_REQUIRED_HEADINGS,
        required_files=CURRENT_MAP_REQUIRED_FILES,
        section_citations=CURRENT_MAP_SECTION_CITATIONS,
        marker_groups=CURRENT_MAP_MARKER_GROUPS,
    )
    return report("current-map", [*source_errors, *errors])


def validate_full() -> int:
    source_errors: list[str] = []
    validate_source_anchors(source_errors)
    errors, _, _ = validate_common(
        mode="full",
        required_headings=FULL_REQUIRED_HEADINGS,
        required_files=CURRENT_MAP_REQUIRED_FILES,
        section_citations=FULL_SECTION_CITATIONS,
        marker_groups={**CURRENT_MAP_MARKER_GROUPS, **FULL_MARKER_GROUPS},
    )
    integration_errors = collect_integration_errors()
    return report("full", [*source_errors, *errors, *integration_errors])


def report(mode: str, errors: list[str]) -> int:
    if errors:
        print(f"{mode} verification failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(
        f"{mode} verification passed: required headings, citations, markers, "
        "source anchors, and placeholder checks are satisfied."
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=["self-test", "current-map", "integration", "full"],
        help="verification mode to run",
    )
    args = parser.parse_args(argv)

    if args.mode == "self-test":
        return validate_self_test()
    if args.mode == "current-map":
        return validate_current_map()
    if args.mode == "integration":
        return validate_integration()
    if args.mode == "full":
        return validate_full()
    raise AssertionError(f"unhandled mode: {args.mode}")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
