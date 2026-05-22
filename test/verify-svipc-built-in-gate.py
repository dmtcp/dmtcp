#!/usr/bin/env python3
"""Verifier for the SysV IPC built-in consolidation gate.

`static` reads only the explicit source/configuration files listed in
STATIC_INPUTS.  It does not walk the repository, inspect generated artifacts, or
read hidden planning/audit state such as .git, .gsd, .planning, or .audits.

`overlap` reports the currently expected transitional PID/core overlap for SysV
IPC wrappers.  It scans only src/plugin/pid source files and fails if the SysV
IPC overlap appears outside the documented PID files.

`artifacts` is intended for use after a build.  It checks generated outputs for a
reintroduced libdmtcp_svipc.so artifact and verifies that the built
DMTCP-launch binary no longer embeds that removed DSO name.

`full` runs static, overlap, artifacts, and the launch-to-exec runtime probe in
normal plus --disable-all-plugins modes when the built outputs are present.
"""

from __future__ import print_function

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]

EXCLUDED_SOURCE_COMPONENTS = (".git", ".gsd", ".planning", ".audits")
STATIC_ALLOWED_PREFIXES = ("plugin", "src")
STATIC_ALLOWED_SUFFIXES = ("", ".am", ".cpp", ".h", ".in")

SVIPC_SOURCE_PATHS = (
    "src/plugin/svipc/sysvipc.cpp",
    "src/plugin/svipc/sysvipc.h",
    "src/plugin/svipc/sysvipcwrappers.cpp",
    "src/plugin/svipc/sysvipcwrappers.h",
)

STATIC_INPUTS = (
    "src/Makefile.am",
    "src/Makefile.in",
    "src/Makefile",
    "src/plugin/Makefile.am",
    "src/plugin/Makefile.in",
    "src/plugin/Makefile",
    "src/dmtcp_launch.cpp",
    "src/execwrappers.cpp",
    "src/util_exec.cpp",
    "src/pluginmanager.cpp",
) + SVIPC_SOURCE_PATHS

SVIPC_MAKEFILE_SOURCES = tuple(p.replace("src/", "", 1) for p in SVIPC_SOURCE_PATHS)
SVIPC_CPP_SOURCES = tuple(p for p in SVIPC_MAKEFILE_SOURCES if p.endswith(".cpp"))
SVIPC_OBJECTS = tuple(p.replace(".cpp", ".$(OBJEXT)") for p in SVIPC_CPP_SOURCES)
SVIPC_DEPFILES = tuple(p.replace(".cpp", ".Po") for p in SVIPC_CPP_SOURCES)
SVIPC_DEPFILES = tuple(
    p.rsplit("/", 1)[0] + "/$(DEPDIR)/" + p.rsplit("/", 1)[1]
    for p in SVIPC_DEPFILES
)

FORBIDDEN_SVIPC_DSO_NEEDLES = ("libdmtcp_svipc.so", "libdmtcp_svipc")
NO_SVIPC_DSO_STATIC_PATHS = (
    "src/plugin/Makefile.am",
    "src/plugin/Makefile.in",
    "src/plugin/Makefile",
    "src/dmtcp_launch.cpp",
    "src/execwrappers.cpp",
    "src/util_exec.cpp",
)

FINAL_PRELOAD_LIB = "libdmtcp.so"
OPTIONAL_PRELOAD_ROWS = (
    "libdmtcp_modify-env.so",
    "libdmtcp_unique-ckpt.so",
    "libdmtcp_pathvirt.so",
)
REMOVED_INTERNAL_DSO_ROWS = (
    ("timer", "libdmtcp_timer"),
    ("SysV IPC", "libdmtcp_svipc"),
    ("IPC", "libdmtcp_ipc"),
    ("PID", "libdmtcp_pid"),
    ("alloc", "libdmtcp_alloc"),
    ("DL", "libdmtcp_dl"),
)

SVIPC_DESCRIPTOR_ACCESSOR = "dmtcp_SysVIPC_PluginDescr"
SVIPC_PLUGIN_ENABLED_HELPER = "sysvipcPluginEnabled"
SVIPC_WRAPPER_FAST_PASS = (
    ("shmget", "_real_shmget"),
    ("shmat", "_real_shmat"),
    ("shmdt", "_real_shmdt"),
    ("shmctl", "_real_shmctl"),
    ("semget", "_real_semget"),
    ("semop", "_real_semop"),
    ("semtimedop", "_real_semtimedop"),
    ("semctl", "_real_semctl"),
    ("msgget", "_real_msgget"),
    ("msgsnd", "_real_msgsnd"),
    ("msgrcv", "_real_msgrcv"),
    ("msgctl", "_real_msgctl"),
)

PID_OVERLAP_TOKENS = (
    "shmget",
    "shmat",
    "shmdt",
    "shmctl",
    "semctl",
    "msgctl",
    "dmtcp_svipc_inside_shmdt",
)
PID_OVERLAP_ALLOWED_FILES = (
    "src/plugin/pid/pid_miscwrappers.cpp",
    "src/plugin/pid/pidwrappers.h",
    "src/plugin/pid/pid_syscallsreal.c",
)
PID_SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hh", ".hpp")

RUNTIME_PROBE = "test/svipc-built-in-preload"
RUNTIME_TIMEOUT_SECONDS = 45


class Result(object):
    def __init__(self, emit=True):
        self.passes = 0
        self.failures = []
        self.emit = emit

    def pass_(self, contract, message):
        self.passes += 1
        if self.emit:
            print("PASS {0}: {1}".format(contract, message))

    def fail(self, contract, message, details=None):
        self.failures.append((contract, message, details or []))
        if self.emit:
            print("FAIL {0}: {1}".format(contract, message))
            for detail in details or []:
                print("  - {0}".format(detail))

    def finish(self):
        if self.failures:
            print(
                "FAIL summary: {0} contract(s) failed; {1} contract(s) passed.".format(
                    len(self.failures), self.passes
                )
            )
            return 1
        print("PASS summary: {0} contract(s) passed.".format(self.passes))
        return 0


class SourceReader(object):
    def __init__(self, repo_root=REPO_ROOT, fixtures=None):
        self.repo_root = Path(repo_root)
        self.fixtures = fixtures

    def read_text(self, rel, result, contract):
        if self.fixtures is not None and rel in self.fixtures:
            return self.fixtures[rel]

        path = self.repo_root / rel
        try:
            return path.read_text(encoding="utf-8")
        except OSError as exc:
            result.fail(
                contract,
                "required file is not readable",
                ["{0}: {1}".format(rel, exc)],
            )
        except UnicodeDecodeError as exc:
            result.fail(
                contract,
                "required file is not UTF-8 text",
                ["{0}: {1}".format(rel, exc)],
            )
        return ""

    def read_static(self, rel, result, contract):
        if rel not in STATIC_INPUTS:
            result.fail(
                "static-input-scope",
                "static mode attempted to inspect an unapproved path",
                [rel],
            )
            return ""
        return self.read_text(rel, result, contract)


DEFAULT_READER = SourceReader()


def relpath(path):
    return str(Path(path).relative_to(REPO_ROOT))


def strip_cpp_comments_preserve_lines(text):
    def block_repl(match):
        return "".join("\n" if ch == "\n" else " " for ch in match.group(0))

    text = re.sub(r"/\*.*?\*/", block_repl, text, flags=re.S)
    return re.sub(r"//.*", "", text)


def strip_cpp_comments(text):
    return strip_cpp_comments_preserve_lines(text)


def line_hits(text, needle):
    hits = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if needle in line:
            hits.append("line {0}: {1}".format(lineno, line.strip()))
    return hits


def makefile_var_block(text, variable):
    lines = text.splitlines()
    prefix = variable + " ="
    for idx, line in enumerate(lines):
        if line.startswith(prefix):
            block = [line]
            cursor = idx
            while block[-1].rstrip().endswith("\\") and cursor + 1 < len(lines):
                cursor += 1
                block.append(lines[cursor])
            return "\n".join(block)
    return ""


def require_items_in_makefile_var(result, reader, rel, variable, items, contract):
    text = reader.read_static(rel, result, contract)
    block = makefile_var_block(text, variable)
    if not block:
        result.fail(contract, "Makefile variable is missing", ["{0}: {1}".format(rel, variable)])
        return
    missing = [item for item in items if item not in block]
    if missing:
        result.fail(
            contract,
            "Makefile variable is missing SysV IPC built-in entries",
            ["{0}: {1} missing {2}".format(rel, variable, item) for item in missing],
        )
        return
    result.pass_(
        contract,
        "{0}:{1} contains {2} required SysV IPC entry/entries".format(
            rel, variable, len(items)
        ),
    )


def extract_braced_body(text, open_brace_index):
    depth = 0
    for idx in range(open_brace_index, len(text)):
        ch = text[idx]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace_index + 1 : idx]
    return None


def function_body(text, signature_pattern):
    for match in re.finditer(signature_pattern, text, flags=re.S | re.M):
        open_brace = text.find("{", match.end())
        semicolon = text.find(";", match.end())
        if open_brace == -1:
            continue
        if semicolon != -1 and semicolon < open_brace:
            continue
        body = extract_braced_body(text, open_brace)
        if body is not None:
            return body
    return None


def final_preload_state_details(body):
    rows = re.findall(r"\{\s*&([A-Za-z_][A-Za-z0-9_]*)\s*,\s*\"([^\"]+)\"\s*\}", body)
    libs = [lib for _flag, lib in rows]
    details = []

    for label, needle in REMOVED_INTERNAL_DSO_ROWS:
        stale = [lib for lib in libs if needle in lib]
        if stale:
            details.append(
                "final pluginInfo must not contain removed {0} DSO row(s): {1}".format(
                    label, ", ".join(stale)
                )
            )

    if FINAL_PRELOAD_LIB not in libs:
        details.append("final pluginInfo table is missing {0}".format(FINAL_PRELOAD_LIB))
    elif libs.count(FINAL_PRELOAD_LIB) != 1:
        details.append("final pluginInfo table must contain exactly one {0} row".format(FINAL_PRELOAD_LIB))
    else:
        final_idx = libs.index(FINAL_PRELOAD_LIB)
        trailing = libs[final_idx + 1 :]
        if trailing:
            details.append(
                "{0} must remain the final DMTCP-owned preload row; trailing row(s): {1}".format(
                    FINAL_PRELOAD_LIB, ", ".join(trailing)
                )
            )
        misplaced_optional = [
            lib for lib in OPTIONAL_PRELOAD_ROWS if lib in libs and libs.index(lib) > final_idx
        ]
        if misplaced_optional:
            details.append(
                "optional launcher row(s) must appear before {0}: {1}".format(
                    FINAL_PRELOAD_LIB, ", ".join(misplaced_optional)
                )
            )

    return details, libs


def validate_final_launch_preload_state(result, text, contract):
    match = re.search(
        r"static\s+struct\s+PluginInfo\s+pluginInfo\[\]\s*=\s*\{(?P<body>.*?)^\};",
        text,
        flags=re.S | re.M,
    )
    if not match:
        result.fail(contract, "pluginInfo preload table is missing")
        return

    details, _libs = final_preload_state_details(match.group("body"))

    setld_body = function_body(text, r"static\s+void\s+setLDPreloadLibs\s*\(")
    if setld_body is None:
        details.append("setLDPreloadLibs body is missing")
    else:
        user_idx = setld_body.find("preloadLibs += getenv(ENV_VAR_PLUGIN)")
        loop_idx = setld_body.find("for (size_t i = 0; i < numLibs; i++)")
        if user_idx == -1 or loop_idx == -1 or user_idx > loop_idx:
            details.append("user ENV_VAR_PLUGIN preload entries are not prepended before DMTCP-owned rows")
        if 'preloadLibs = Util::getPath("libdmtcp.so")' not in setld_body:
            details.append("disable-all preload branch no longer reduces to libdmtcp.so")
        for label, needle in REMOVED_INTERNAL_DSO_ROWS:
            if needle in setld_body:
                details.append(
                    "setLDPreloadLibs body contains removed {0} DSO name {1}".format(
                        label, needle
                    )
                )

    if details:
        result.fail(contract, "final launch preload state contract is broken", details)
        return
    result.pass_(
        contract,
        "user plugins precede optional DMTCP rows, libdmtcp.so is final, and removed timer/SysV IPC/IPC/PID/alloc/DL DSO rows are absent",
    )


def validate_static_scope(result, reader=DEFAULT_READER):
    details = []
    seen = set()
    for rel in STATIC_INPUTS:
        path = Path(rel)
        parts = path.parts
        if rel in seen:
            details.append("duplicate static input: {0}".format(rel))
        seen.add(rel)
        if path.is_absolute() or ".." in parts:
            details.append("non-repository-relative static input: {0}".format(rel))
        if not parts or parts[0] not in STATIC_ALLOWED_PREFIXES:
            details.append("static input is not source/configuration scoped: {0}".format(rel))
        if any(part in EXCLUDED_SOURCE_COMPONENTS for part in parts):
            details.append("static input uses forbidden hidden state: {0}".format(rel))
        if path.suffix not in STATIC_ALLOWED_SUFFIXES:
            details.append("static input suffix is not source/configuration-like: {0}".format(rel))
        if reader.fixtures is None and not (REPO_ROOT / rel).is_file():
            details.append("static input does not exist: {0}".format(rel))
        if reader.fixtures is not None and rel not in reader.fixtures:
            details.append("static fixture is missing: {0}".format(rel))
    if details:
        result.fail("static-input-scope", "static input list violates the source-only contract", details)
        return
    result.pass_(
        "static-input-scope",
        "static mode is limited to {0} explicit source/config paths and excludes {1}".format(
            len(STATIC_INPUTS), ", ".join(EXCLUDED_SOURCE_COMPONENTS)
        ),
    )


def validate_makefile_svipc_builtin_path(result, reader=DEFAULT_READER):
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile.am",
        "__d_libdir__libdmtcp_so_SOURCES",
        SVIPC_MAKEFILE_SOURCES,
        "makefile-libdmtcp-svipc-sources-am",
    )
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile.in",
        "__d_libdir__libdmtcp_so_SOURCES",
        SVIPC_MAKEFILE_SOURCES,
        "makefile-libdmtcp-svipc-sources-in",
    )
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile.in",
        "am___d_libdir__libdmtcp_so_OBJECTS",
        SVIPC_OBJECTS,
        "makefile-libdmtcp-svipc-objects-in",
    )
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile.in",
        "am__depfiles_remade",
        SVIPC_DEPFILES,
        "makefile-libdmtcp-svipc-dependencies-in",
    )
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile",
        "__d_libdir__libdmtcp_so_SOURCES",
        SVIPC_MAKEFILE_SOURCES,
        "makefile-libdmtcp-svipc-sources-configured",
    )
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile",
        "am___d_libdir__libdmtcp_so_OBJECTS",
        SVIPC_OBJECTS,
        "makefile-libdmtcp-svipc-objects-configured",
    )
    require_items_in_makefile_var(
        result,
        reader,
        "src/Makefile",
        "am__depfiles_remade",
        SVIPC_DEPFILES,
        "makefile-libdmtcp-svipc-dependencies-configured",
    )


def validate_removed_svipc_dso_references(result, reader=DEFAULT_READER):
    offenders = []
    for rel in NO_SVIPC_DSO_STATIC_PATHS:
        text = reader.read_static(rel, result, "no-libdmtcp-svipc-dso")
        for needle in FORBIDDEN_SVIPC_DSO_NEEDLES:
            for hit in line_hits(text, needle):
                offenders.append("{0}: {1}".format(rel, hit))
    if offenders:
        result.fail(
            "no-libdmtcp-svipc-dso",
            "removed SysV IPC DSO is still constructed or referenced",
            offenders,
        )
        return
    result.pass_(
        "no-libdmtcp-svipc-dso",
        "plugin Makefiles plus launch/exec preload code contain no libdmtcp_svipc.so reference",
    )


def validate_launch_preload_order(result, reader=DEFAULT_READER):
    text = reader.read_static("src/dmtcp_launch.cpp", result, "launch-preload-order")
    validate_final_launch_preload_state(result, text, "launch-preload-order")


def validate_pluginmanager_svipc_registration(result, reader=DEFAULT_READER):
    text = reader.read_static("src/pluginmanager.cpp", result, "pluginmanager-svipc-registration")
    text_no_comments = strip_cpp_comments(text)
    details = []

    decl = r"DmtcpPluginDescriptor_t\s+" + re.escape(SVIPC_DESCRIPTOR_ACCESSOR) + r"\s*\(\s*\)\s*;"
    if not re.search(decl, text_no_comments):
        details.append("missing {0}() declaration".format(SVIPC_DESCRIPTOR_ACCESSOR))

    helper_body = function_body(
        text_no_comments,
        r"static\s+bool\s+" + re.escape(SVIPC_PLUGIN_ENABLED_HELPER) + r"\s*\(\s*\)",
    )
    if helper_body is None:
        details.append("missing {0}() disable-all helper".format(SVIPC_PLUGIN_ENABLED_HELPER))
    else:
        if "getenv(ENV_VAR_DISABLE_ALL_PLUGINS)" not in helper_body:
            details.append("SysV IPC registration gate does not read ENV_VAR_DISABLE_ALL_PLUGINS")
        if 'strcmp(disableAllPlugins, "1") != 0' not in helper_body:
            details.append("SysV IPC registration gate does not preserve disable-all value 1 semantics")

    init_body = function_body(
        text_no_comments,
        r"extern\s+\"C\"\s+void\s+dmtcp_initialize_plugin\s*\(",
    )
    if init_body is None:
        details.append("libdmtcp dmtcp_initialize_plugin() definition is missing")
    else:
        gated_register = re.search(
            r"if\s*\(\s*" + re.escape(SVIPC_PLUGIN_ENABLED_HELPER) + r"\s*\(\s*\)\s*\)\s*\{\s*"
            r"dmtcp_register_plugin\s*\(\s*" + re.escape(SVIPC_DESCRIPTOR_ACCESSOR) + r"\s*\(\s*\)\s*\)\s*;\s*\}",
            init_body,
            flags=re.S,
        )
        if not gated_register:
            details.append("{0}() is not registered under {1}()".format(
                SVIPC_DESCRIPTOR_ACCESSOR, SVIPC_PLUGIN_ENABLED_HELPER
            ))
        sysvipc_idx = init_body.find(SVIPC_DESCRIPTOR_ACCESSOR)
        path_idx = init_body.find("dmtcp_PathTranslator_PluginDescr")
        if sysvipc_idx != -1 and path_idx != -1 and sysvipc_idx > path_idx:
            details.append("SysV IPC built-in registration should remain before PathTranslator registration")
        if "NEXT_FNC(dmtcp_initialize_plugin)" not in init_body:
            details.append("dmtcp_initialize_plugin chain does not call NEXT_FNC")
        if not re.search(r"\(\s*\*\s*fn\s*\)\s*\(\s*\)\s*;", init_body):
            details.append("dmtcp_initialize_plugin NEXT_FNC result is not invoked")

    if details:
        result.fail(
            "pluginmanager-svipc-registration",
            "built-in SysV IPC registration contract is broken",
            details,
        )
        return
    result.pass_(
        "pluginmanager-svipc-registration",
        "pluginmanager declares the SysV IPC descriptor, gates its built-in row on disable-all, and preserves the NEXT_FNC initializer chain",
    )


def validate_svipc_descriptor_accessor(result, reader=DEFAULT_READER):
    text = reader.read_static("src/plugin/svipc/sysvipc.cpp", result, "svipc-descriptor-accessor")
    text_no_comments = strip_cpp_comments(text)
    details = []

    if "DMTCP_DECL_PLUGIN" in text_no_comments:
        details.append("sysvipc.cpp still self-registers with DMTCP_DECL_PLUGIN")
    if re.search(r"(?:extern\s+\"C\"\s+)?void\s+dmtcp_initialize_plugin\s*\(", text_no_comments):
        details.append("sysvipc.cpp still defines dmtcp_initialize_plugin()")
    if not re.search(r"DmtcpPluginDescriptor_t\s+sysvipcPlugin\s*=\s*\{", text_no_comments):
        details.append("sysvipc.cpp no longer exposes a sysvipcPlugin descriptor object")
    accessor_pattern = (
        r"DmtcpPluginDescriptor_t\s+" + re.escape(SVIPC_DESCRIPTOR_ACCESSOR) +
        r"\s*\(\s*\)\s*\{\s*return\s+sysvipcPlugin\s*;\s*\}"
    )
    if not re.search(accessor_pattern, text_no_comments, flags=re.S):
        details.append("missing {0}() accessor returning sysvipcPlugin".format(SVIPC_DESCRIPTOR_ACCESSOR))

    if details:
        result.fail(
            "svipc-descriptor-accessor",
            "SysV IPC source descriptor/accessor contract is broken",
            details,
        )
        return
    result.pass_(
        "svipc-descriptor-accessor",
        "sysvipc.cpp exposes a libdmtcp-owned descriptor accessor and no longer self-registers",
    )


def validate_svipc_wrapper_real_accessors(result, reader=DEFAULT_READER):
    text = reader.read_static("src/plugin/svipc/sysvipcwrappers.h", result, "svipc-real-accessors")
    details = []
    for wrapper, real_func in SVIPC_WRAPPER_FAST_PASS:
        needle = "# define {0}".format(real_func)
        if needle not in text or "NEXT_FNC({0})".format(wrapper) not in text:
            details.append("missing {0} -> NEXT_FNC({1}) accessor".format(real_func, wrapper))
    if details:
        result.fail(
            "svipc-real-accessors",
            "SysV IPC real-function accessor contract is broken",
            details,
        )
        return
    result.pass_(
        "svipc-real-accessors",
        "sysvipcwrappers.h maps every SysV IPC wrapper to its _real_* NEXT_FNC accessor",
    )


def validate_svipc_disable_all_fast_pass(result, reader=DEFAULT_READER):
    text = reader.read_static("src/plugin/svipc/sysvipcwrappers.cpp", result, "svipc-disable-all-fast-pass")
    text_no_comments = strip_cpp_comments(text)
    details = []

    helper_body = function_body(
        text_no_comments,
        r"static\s+bool\s+" + re.escape(SVIPC_PLUGIN_ENABLED_HELPER) + r"\s*\(\s*\)",
    )
    if helper_body is None:
        details.append("SysV IPC wrappers do not define {0}()".format(SVIPC_PLUGIN_ENABLED_HELPER))
    else:
        if "getenv(ENV_VAR_DISABLE_ALL_PLUGINS)" not in helper_body:
            details.append("SysV IPC wrappers do not read ENV_VAR_DISABLE_ALL_PLUGINS")
        if 'strcmp(disableAllPlugins, "1") != 0' not in helper_body:
            details.append("{0}() does not fast-disable on value 1".format(SVIPC_PLUGIN_ENABLED_HELPER))

    for wrapper, real_func in SVIPC_WRAPPER_FAST_PASS:
        body = function_body(
            text_no_comments,
            r"extern\s+\"C\"\s+(?:(?:int|ssize_t)\s+|void\s*\*\s*)" + re.escape(wrapper) + r"\s*\(",
        )
        if body is None:
            details.append("missing wrapper body for {0}".format(wrapper))
            continue
        body_start = body.lstrip()[:1600]

        if wrapper == "shmdt":
            guard_match = re.search(
                r"if\s*\(\s*!\s*" + re.escape(SVIPC_PLUGIN_ENABLED_HELPER) +
                r"\s*\(\s*\)\s*\)\s*\{(?P<branch>.*?)return\s+ret\s*;",
                body_start,
                flags=re.S,
            )
            branch = guard_match.group("branch") if guard_match else ""
            if (
                guard_match is None or
                "inside_shmdt" not in branch or
                "_real_shmdt(shmaddr)" not in branch or
                "DMTCP_PLUGIN_DISABLE_CKPT" in branch or
                "SysVShm::instance" in branch or
                "VIRTUAL_TO_REAL" in branch
            ):
                details.append(
                    "shmdt disable-all branch must preserve inside_shmdt around _real_shmdt without checkpoint or id bookkeeping"
                )
            continue

        if wrapper == "semctl":
            if "semctlCmdUsesArg(cmd)" not in body_start:
                details.append("semctl does not use a command-aware vararg helper")
            va_start_idx = body_start.find("va_start")
            noarg_idx = body_start.find("_real_semctl(semid, semnum, cmd)")
            if noarg_idx == -1 or (va_start_idx != -1 and noarg_idx > va_start_idx):
                details.append("semctl disable-all no-argument commands do not return _real_semctl(..., cmd) before va_start")
            if not re.search(r"if\s*\(\s*hasArg\s*\)\s*\{.*?va_arg\s*\(", body_start, flags=re.S):
                details.append("semctl still appears to read a vararg without a hasArg guard")
            if not re.search(
                r"if\s*\(\s*!\s*enabled\s*\)\s*\{\s*return\s+_real_semctl\s*\(\s*semid\s*,\s*semnum\s*,\s*cmd\s*,\s*uarg\s*\)",
                body_start,
                flags=re.S,
            ):
                details.append("semctl disable-all argument-taking commands do not return _real_semctl(..., uarg)")
            continue

        pattern = re.compile(
            r"if\s*\(\s*!\s*" + re.escape(SVIPC_PLUGIN_ENABLED_HELPER) + r"\s*\(\s*\)\s*\)\s*\{\s*"
            r"return\s+" + re.escape(real_func) + r"\s*\(",
            flags=re.S,
        )
        if not pattern.search(body_start):
            details.append(
                "{0} does not immediately return {1}(...) when disable-all is active".format(
                    wrapper, real_func
                )
            )
    if details:
        result.fail(
            "svipc-disable-all-fast-pass",
            "SysV IPC wrappers do not all fast-pass to real functions under disable-all",
            details,
        )
        return
    result.pass_(
        "svipc-disable-all-fast-pass",
        "all SysV IPC wrappers delegate to _real_* under disable-all, with shmdt recursion guard and semctl no-arg safety",
    )


def run_static(result, reader=DEFAULT_READER):
    validate_static_scope(result, reader)
    validate_makefile_svipc_builtin_path(result, reader)
    validate_removed_svipc_dso_references(result, reader)
    validate_launch_preload_order(result, reader)
    validate_pluginmanager_svipc_registration(result, reader)
    validate_svipc_descriptor_accessor(result, reader)
    validate_svipc_wrapper_real_accessors(result, reader)
    validate_svipc_disable_all_fast_pass(result, reader)


def pid_source_files():
    root = REPO_ROOT / "src/plugin/pid"
    if not root.is_dir():
        return []
    files = []
    for dirpath, dirnames, filenames in os.walk(str(root)):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDED_SOURCE_COMPONENTS]
        for filename in filenames:
            path = Path(dirpath) / filename
            if path.suffix in PID_SOURCE_SUFFIXES:
                files.append(path)
    return sorted(files)


def validate_expected_pid_overlap_shapes(result, reader=DEFAULT_READER):
    details = []
    svipc = reader.read_text("src/plugin/svipc/sysvipcwrappers.cpp", result, "overlap-composed-owner")
    misc = reader.read_text("src/miscwrappers.cpp", result, "overlap-composed-owner")
    svipc_no_comments = strip_cpp_comments(svipc)
    misc_no_comments = strip_cpp_comments(misc)

    for token in ("shmctl", "semctl", "msgctl"):
        if not re.search(r"extern\s+\"C\"\s+int\s+" + re.escape(token) + r"\s*\(", svipc_no_comments, flags=re.S):
            details.append("src/plugin/svipc/sysvipcwrappers.cpp missing SysV-owned wrapper {0}".format(token))
    for token in ("dmtcp_real_to_virtual_pid", "realToVirtualPidIfEnabled", "shm_cpid", "shm_lpid", "msg_lspid", "msg_lrpid", "GETPID"):
        if token not in svipc_no_comments:
            details.append("SysV owner missing PID field translation token {0}".format(token))
    for token in ("shmget", "shmat", "shmdt", "shmctl"):
        if not re.search(r"case\s+SYS_" + re.escape(token) + r"\s*:", misc_no_comments):
            details.append("src/miscwrappers.cpp missing SYS_{0} syscall handoff".format(token))
    if "dmtcp_svipc_inside_shmdt" not in misc_no_comments:
        details.append("src/miscwrappers.cpp missing dmtcp_svipc_inside_shmdt recursion guard")

    if details:
        result.fail(
            "overlap-composed-owner",
            "SysV IPC wrappers no longer have PID overlap but are missing composed PID translation",
            details,
        )
        return False
    result.pass_(
        "overlap-composed-owner",
        "SysV IPC owners translate PID fields locally and core syscall handoff keeps shmdt recursion guard",
    )
    return True


def run_overlap(result, reader=DEFAULT_READER):
    validate_expected_pid_overlap_shapes(result, reader)
    token_re = re.compile(r"\b(?:" + "|".join(re.escape(t) for t in PID_OVERLAP_TOKENS) + r")\b")
    unexpected = []

    for path in pid_source_files():
        rel = relpath(path)
        original = reader.read_text(rel, result, "overlap-locations")
        searchable = strip_cpp_comments_preserve_lines(original)
        original_lines = original.splitlines()
        for lineno, line in enumerate(searchable.splitlines(), 1):
            if token_re.search(line):
                unexpected.append("{0}:{1}: {2}".format(
                    rel, lineno, original_lines[lineno - 1].strip()
                ))

    if unexpected:
        result.fail(
            "overlap-locations",
            "SysV IPC PID/core overlap remains in PID source after composition",
            unexpected,
        )
        return
    result.pass_(
        "overlap-locations",
        "no SysV IPC/PID overlap remains in PID source files",
    )


def run_artifacts(result):
    lib_dir = REPO_ROOT / "lib"
    if not lib_dir.exists():
        result.fail(
            "artifacts-lib-output",
            "artifacts mode requires generated lib/ outputs; run a build before this mode",
            ["lib/"],
        )
    else:
        offenders = []
        for path in sorted(lib_dir.rglob("*")):
            rel = relpath(path)
            if "libdmtcp_svipc.so" in rel or "libdmtcp_svipc" in rel:
                offenders.append(rel)
            if path.is_symlink():
                try:
                    target = os.readlink(str(path))
                except OSError:
                    target = ""
                if "libdmtcp_svipc.so" in target or "libdmtcp_svipc" in target:
                    offenders.append("{0} -> {1}".format(rel, target))
        if offenders:
            result.fail(
                "artifacts-lib-output",
                "generated lib outputs contain a removed SysV IPC DSO artifact",
                offenders,
            )
        else:
            result.pass_(
                "artifacts-lib-output",
                "generated lib outputs contain no libdmtcp_svipc.so artifact",
            )

    launch = REPO_ROOT / "bin/dmtcp_launch"
    if not launch.exists():
        result.fail(
            "artifacts-launch-binary",
            "artifacts mode requires built bin/dmtcp_launch; run a build before this mode",
            ["bin/dmtcp_launch"],
        )
        return
    try:
        data = launch.read_bytes()
    except OSError as exc:
        result.fail(
            "artifacts-launch-binary",
            "could not read built dmtcp_launch binary",
            [str(exc)],
        )
        return
    if b"libdmtcp_svipc.so" in data or b"libdmtcp_svipc" in data:
        result.fail(
            "artifacts-launch-binary",
            "built dmtcp_launch still embeds the removed SysV IPC DSO name",
            ["bin/dmtcp_launch contains libdmtcp_svipc"],
        )
        return
    result.pass_(
        "artifacts-launch-binary",
        "built dmtcp_launch embeds no libdmtcp_svipc.so string",
    )


def run_runtime_probe(result, disable_all):
    launch = REPO_ROOT / "bin/dmtcp_launch"
    probe = REPO_ROOT / RUNTIME_PROBE
    contract = "runtime-probe-disable-all" if disable_all else "runtime-probe-normal"
    details = []
    if not launch.exists():
        details.append("missing bin/dmtcp_launch")
    if not probe.exists():
        details.append("missing {0}".format(RUNTIME_PROBE))
    if details:
        result.fail(contract, "runtime probe requires built launch/probe artifacts", details)
        return

    cmd = [str(launch), "--coord-port", "0", "--quiet"]
    if disable_all:
        cmd.append("--disable-all-plugins")
    cmd.append(str(probe))
    env = os.environ.copy()
    env.pop("DMTCP_SVIPC_BUILT_IN_PRELOAD_AFTER_EXEC", None)
    try:
        completed = subprocess.run(
            cmd,
            cwd=str(REPO_ROOT),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            timeout=RUNTIME_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.output if exc.output is not None else ""
        result.fail(
            contract,
            "runtime probe timed out",
            ["command: {0}".format(" ".join(cmd)), "output: {0}".format(output[-2000:])],
        )
        return

    output = completed.stdout or ""
    expected_suffix = " (disable-all)" if disable_all else ""
    expected_line = "PASS svipc-built-in-preload: launch and exec preload state valid{0}".format(expected_suffix)
    if completed.returncode != 0 or expected_line not in output:
        result.fail(
            contract,
            "runtime probe failed",
            [
                "command: {0}".format(" ".join(cmd)),
                "exit code: {0}".format(completed.returncode),
                "expected line: {0}".format(expected_line),
                "output: {0}".format(output[-2000:]),
            ],
        )
        return
    result.pass_(contract, expected_line)


def run_full(result):
    run_static(result)
    run_self_test(result)
    run_overlap(result)
    run_artifacts(result)
    run_runtime_probe(result, disable_all=False)
    run_runtime_probe(result, disable_all=True)


def self_test_fixtures(extra=None):
    fixtures = {rel: "" for rel in STATIC_INPUTS}
    fixtures.update(
        {
            "src/Makefile.am": "__d_libdir__libdmtcp_so_SOURCES = " + " ".join(SVIPC_MAKEFILE_SOURCES) + "\n",
            "src/Makefile.in": (
                "__d_libdir__libdmtcp_so_SOURCES = " + " ".join(SVIPC_MAKEFILE_SOURCES) + "\n" +
                "am___d_libdir__libdmtcp_so_OBJECTS = " + " ".join(SVIPC_OBJECTS) + "\n" +
                "am__depfiles_remade = " + " ".join(SVIPC_DEPFILES) + "\n"
            ),
            "src/Makefile": (
                "__d_libdir__libdmtcp_so_SOURCES = " + " ".join(SVIPC_MAKEFILE_SOURCES) + "\n" +
                "am___d_libdir__libdmtcp_so_OBJECTS = " + " ".join(SVIPC_OBJECTS) + "\n" +
                "am__depfiles_remade = " + " ".join(SVIPC_DEPFILES) + "\n"
            ),
            "src/dmtcp_launch.cpp": (
                "static struct PluginInfo pluginInfo[] = {\n"
                "  { &enableModifyEnvPlugin, \"libdmtcp_modify-env.so\" },\n"
                "  { &enableUniqueCkptPlugin, \"libdmtcp_unique-ckpt.so\" },\n"
                "  { &enablePathVirtPlugin, \"libdmtcp_pathvirt.so\" },\n"
                "  { &enableLibDMTCP, \"libdmtcp.so\" }\n"
                "};\n"
                "static void setLDPreloadLibs(bool is32bitElf) {\n"
                "  string preloadLibs = \"\";\n"
                "  if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); }\n"
                "  if (disableAllPlugins) { preloadLibs = Util::getPath(\"libdmtcp.so\"); }\n"
                "  for (size_t i = 0; i < numLibs; i++) {}\n"
                "}\n"
            ),
            "src/pluginmanager.cpp": (
                "namespace dmtcp {\n"
                "DmtcpPluginDescriptor_t dmtcp_PathTranslator_PluginDescr();\n"
                "DmtcpPluginDescriptor_t dmtcp_SysVIPC_PluginDescr();\n"
                "static bool sysvipcPluginEnabled() {\n"
                "  const char *disableAllPlugins = getenv(ENV_VAR_DISABLE_ALL_PLUGINS);\n"
                "  return disableAllPlugins == NULL || strcmp(disableAllPlugins, \"1\") != 0;\n"
                "}\n"
                "extern \"C\" void dmtcp_initialize_plugin() {\n"
                "  if (sysvipcPluginEnabled()) { dmtcp_register_plugin(dmtcp_SysVIPC_PluginDescr()); }\n"
                "  dmtcp_register_plugin(dmtcp_PathTranslator_PluginDescr());\n"
                "  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);\n"
                "  if (fn != NULL) { (*fn)(); }\n"
                "}\n"
                "}\n"
            ),
            "src/plugin/svipc/sysvipc.cpp": (
                "DmtcpPluginDescriptor_t sysvipcPlugin = {};\n"
                "namespace dmtcp {\n"
                "DmtcpPluginDescriptor_t dmtcp_SysVIPC_PluginDescr() { return sysvipcPlugin; }\n"
                "}\n"
            ),
            "src/plugin/svipc/sysvipcwrappers.h": "\n".join(
                "# define {0} NEXT_FNC({1})".format(real_func, wrapper)
                for wrapper, real_func in SVIPC_WRAPPER_FAST_PASS
            ),
            "src/plugin/svipc/sysvipcwrappers.cpp": (
                "#include <string.h>\n"
                "static bool sysvipcPluginEnabled() {\n"
                "  const char *disableAllPlugins = getenv(ENV_VAR_DISABLE_ALL_PLUGINS);\n"
                "  return disableAllPlugins == NULL || strcmp(disableAllPlugins, \"1\") != 0;\n"
                "}\n"
                "static bool semctlCmdUsesArg(int cmd) { return cmd == SETVAL; }\n" +
                "\n".join(
                    "extern \"C\" int {0}() {{ if (!sysvipcPluginEnabled()) {{ return {1}(); }} return {1}(); }}".format(wrapper, real_func)
                    for wrapper, real_func in SVIPC_WRAPPER_FAST_PASS
                    if wrapper not in ("shmat", "shmdt", "semctl", "msgrcv")
                ) +
                "\nextern \"C\" void *shmat() { if (!sysvipcPluginEnabled()) { return _real_shmat(); } return _real_shmat(); }\n"
                "\nextern \"C\" int shmdt(const void *shmaddr) { if (!sysvipcPluginEnabled()) { bool wasInside = inside_shmdt; inside_shmdt = true; int ret = _real_shmdt(shmaddr); inside_shmdt = wasInside; return ret; } return _real_shmdt(shmaddr); }\n"
                "\nextern \"C\" int semctl(int semid, int semnum, int cmd, ...) { bool enabled = sysvipcPluginEnabled(); bool hasArg = semctlCmdUsesArg(cmd); if (!enabled && !hasArg) { return _real_semctl(semid, semnum, cmd); } union semun uarg; if (hasArg) { va_start(arg, cmd); uarg = va_arg(arg, union semun); va_end(arg); } if (!enabled) { return _real_semctl(semid, semnum, cmd, uarg); } return _real_semctl(semid, semnum, cmd, uarg); }\n"
                "\nextern \"C\" ssize_t msgrcv() { if (!sysvipcPluginEnabled()) { return _real_msgrcv(); } return _real_msgrcv(); }\n"
            ),
        }
    )
    if extra:
        fixtures.update(extra)
    return fixtures


def failure_contracts(result):
    return set(contract for contract, _message, _details in result.failures)


def run_self_test(result):
    forbidden = self_test_fixtures(
        {
            "src/dmtcp_launch.cpp": (
                "static struct PluginInfo pluginInfo[] = {\n"
                "  { &enableSvipcPlugin, \"libdmtcp_svipc.so\" }\n"
                "};\n"
                "static void setLDPreloadLibs(bool is32bitElf) {\n"
                "  string preloadLibs = Util::getPath(\"libdmtcp_svipc.so\");\n"
                "}\n"
            )
        }
    )
    inner = Result(emit=False)
    validate_removed_svipc_dso_references(inner, SourceReader(fixtures=forbidden))
    if "no-libdmtcp-svipc-dso" not in failure_contracts(inner):
        result.fail(
            "self-test-forbidden-dso",
            "forbidden libdmtcp_svipc.so fixture was not detected",
        )
    else:
        result.pass_(
            "self-test-forbidden-dso",
            "inline fixture with libdmtcp_svipc.so is rejected without reading hidden state",
        )

    optional_rows = Result(emit=False)
    validate_launch_preload_order(optional_rows, SourceReader(fixtures=self_test_fixtures()))
    if optional_rows.failures:
        result.fail(
            "self-test-optional-launch-rows",
            "optional final-state launcher rows were rejected",
            ["{0}: {1}".format(contract, message) for contract, message, _details in optional_rows.failures],
        )
    else:
        result.pass_(
            "self-test-optional-launch-rows",
            "optional modify-env/unique-ckpt/pathvirt rows are accepted before libdmtcp.so",
        )

    stale_alloc_dl = self_test_fixtures(
        {
            "src/dmtcp_launch.cpp": (
                "static struct PluginInfo pluginInfo[] = {\n"
                "  { &enableAllocPlugin, \"libdmtcp_alloc.so\" },\n"
                "  { &enableDlPlugin, \"libdmtcp_dl.so\" },\n"
                "  { &enableLibDMTCP, \"libdmtcp.so\" }\n"
                "};\n"
                "static void setLDPreloadLibs(bool is32bitElf) {\n"
                "  string preloadLibs = \"\";\n"
                "  if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); }\n"
                "  if (disableAllPlugins) { preloadLibs = Util::getPath(\"libdmtcp.so\"); }\n"
                "  for (size_t i = 0; i < numLibs; i++) {}\n"
                "}\n"
            )
        }
    )
    inner = Result(emit=False)
    validate_launch_preload_order(inner, SourceReader(fixtures=stale_alloc_dl))
    if "launch-preload-order" in failure_contracts(inner):
        result.pass_(
            "self-test-stale-alloc-dl-launch-rows",
            "stale alloc/DL launcher rows are rejected with final-state diagnostics",
        )
    else:
        result.fail(
            "self-test-stale-alloc-dl-launch-rows",
            "stale alloc/DL launcher rows were not rejected",
        )

    missing_libdmtcp = self_test_fixtures(
        {
            "src/dmtcp_launch.cpp": (
                "static struct PluginInfo pluginInfo[] = {\n"
                "  { &enableModifyEnvPlugin, \"libdmtcp_modify-env.so\" },\n"
                "  { &enablePathVirtPlugin, \"libdmtcp_pathvirt.so\" }\n"
                "};\n"
                "static void setLDPreloadLibs(bool is32bitElf) {\n"
                "  string preloadLibs = \"\";\n"
                "  if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); }\n"
                "  if (disableAllPlugins) { preloadLibs = Util::getPath(\"libdmtcp.so\"); }\n"
                "  for (size_t i = 0; i < numLibs; i++) {}\n"
                "}\n"
            )
        }
    )
    inner = Result(emit=False)
    validate_launch_preload_order(inner, SourceReader(fixtures=missing_libdmtcp))
    if "launch-preload-order" in failure_contracts(inner):
        result.pass_(
            "self-test-missing-libdmtcp-launch-row",
            "missing libdmtcp.so launcher row is rejected",
        )
    else:
        result.fail(
            "self-test-missing-libdmtcp-launch-row",
            "missing libdmtcp.so launcher row was not rejected",
        )

    missing_descriptor = self_test_fixtures(
        {
            "src/pluginmanager.cpp": (
                "namespace dmtcp {\n"
                "DmtcpPluginDescriptor_t dmtcp_PathTranslator_PluginDescr();\n"
                "extern \"C\" void dmtcp_initialize_plugin() {\n"
                "  dmtcp_register_plugin(dmtcp_PathTranslator_PluginDescr());\n"
                "  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);\n"
                "  if (fn != NULL) { (*fn)(); }\n"
                "}\n"
                "}\n"
            ),
            "src/plugin/svipc/sysvipc.cpp": "DmtcpPluginDescriptor_t sysvipcPlugin = {};\n",
        }
    )
    inner = Result(emit=False)
    reader = SourceReader(fixtures=missing_descriptor)
    validate_pluginmanager_svipc_registration(inner, reader)
    validate_svipc_descriptor_accessor(inner, reader)
    contracts = failure_contracts(inner)
    required = {"pluginmanager-svipc-registration", "svipc-descriptor-accessor"}
    if not required.issubset(contracts):
        result.fail(
            "self-test-missing-descriptor",
            "missing SysV descriptor/accessor fixture did not trip both contracts",
            ["observed contracts: {0}".format(", ".join(sorted(contracts)))],
        )
    else:
        result.pass_(
            "self-test-missing-descriptor",
            "inline fixture missing pluginmanager and source descriptor/accessors is rejected",
        )

    clean = Result(emit=False)
    run_static(clean, SourceReader(fixtures=self_test_fixtures()))
    if clean.failures:
        result.fail(
            "self-test-positive-fixture",
            "positive inline fixture unexpectedly failed static contracts",
            ["{0}: {1}".format(contract, message) for contract, message, _details in clean.failures],
        )
    else:
        result.pass_(
            "self-test-positive-fixture",
            "positive inline fixture satisfies the source-only static contracts",
        )


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=("self-test", "build", "static", "overlap", "artifacts", "full"),
        nargs="?",
        default="static",
        help="verification mode to run (default: static; build is an alias for static)",
    )
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    result = Result()
    if args.mode == "self-test":
        run_self_test(result)
    elif args.mode in ("build", "static"):
        run_static(result)
    elif args.mode == "overlap":
        run_overlap(result)
    elif args.mode == "artifacts":
        run_artifacts(result)
    elif args.mode == "full":
        run_full(result)
    return result.finish()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
