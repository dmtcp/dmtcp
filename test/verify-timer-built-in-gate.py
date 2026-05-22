#!/usr/bin/env python3
"""Verifier for the timer-built-in consolidation gate.

`static` reads only the explicit source/configuration files listed in
STATIC_INPUTS.  It does not walk the repository, inspect generated artifacts, or
read hidden planning/audit state such as .gsd, .planning, or .audits.

`pid-overlap` reports the currently expected transitional PID/timer overlap for
clock_getcpuclockid and timer_create.  It scans only src/plugin/pid source files
and fails if either wrapper appears outside the two documented PID files.

`artifacts` is intended for use after a build.  It checks generated outputs for a
reintroduced libdmtcp_timer.so artifact and verifies that the built dmtcp_launch
binary no longer embeds that removed DSO name.
"""

from __future__ import print_function

import argparse
import os
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]

EXCLUDED_STATIC_COMPONENTS = (".git", ".gsd", ".planning", ".audits")
STATIC_ALLOWED_PREFIXES = ("plugin", "src")
STATIC_ALLOWED_SUFFIXES = (".am", ".cpp", ".h", ".in")

TIMER_SOURCE_PATHS = (
    "src/plugin/timer/timer_create.cpp",
    "src/plugin/timer/timerlist.cpp",
    "src/plugin/timer/timerlist.h",
    "src/plugin/timer/timerwrappers.cpp",
    "src/plugin/timer/timerwrappers.h",
)

STATIC_INPUTS = (
    "src/Makefile.am",
    "src/Makefile.in",
    "plugin/Makefile.am",
    "plugin/Makefile.in",
    "src/dmtcp_launch.cpp",
    "src/execwrappers.cpp",
    "src/util_exec.cpp",
    "src/pluginmanager.cpp",
) + TIMER_SOURCE_PATHS

TIMER_MAKEFILE_SOURCES = tuple(p.replace("src/", "", 1) for p in TIMER_SOURCE_PATHS)
TIMER_CPP_SOURCES = tuple(p for p in TIMER_MAKEFILE_SOURCES if p.endswith(".cpp"))
TIMER_OBJECTS = tuple(
    p.replace(".cpp", ".$(OBJEXT)") for p in TIMER_CPP_SOURCES
)
TIMER_DEPFILES = tuple(p.replace(".cpp", ".Po") for p in TIMER_CPP_SOURCES)
TIMER_DEPFILES = tuple(
    p.rsplit("/", 1)[0] + "/$(DEPDIR)/" + p.rsplit("/", 1)[1]
    for p in TIMER_DEPFILES
)

FORBIDDEN_TIMER_DSO_NEEDLES = ("libdmtcp_timer.so", "libdmtcp_timer")
NO_TIMER_DSO_STATIC_PATHS = (
    "plugin/Makefile.am",
    "plugin/Makefile.in",
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

TIMER_WRAPPER_FAST_PASS = (
    ("timer_create", "_real_timer_create"),
    ("timer_delete", "_real_timer_delete"),
    ("timer_settime", "_real_timer_settime"),
    ("timer_gettime", "_real_timer_gettime"),
    ("timer_getoverrun", "_real_timer_getoverrun"),
    ("clock_getcpuclockid", "_real_clock_getcpuclockid"),
    ("pthread_getcpuclockid", "_real_pthread_getcpuclockid"),
    ("clock_getres", "_real_clock_getres"),
    ("clock_gettime", "_real_clock_gettime"),
    ("clock_settime", "_real_clock_settime"),
    ("clock_nanosleep", "_real_clock_nanosleep"),
)

PID_OVERLAP_TOKENS = ("clock_getcpuclockid", "timer_create")
PID_OVERLAP_ALLOWED_FILES = (
    "src/plugin/pid/pid_miscwrappers.cpp",
    "src/plugin/pid/pidwrappers.h",
)
PID_SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hh", ".hpp")
# pid_syscallsreal.c contains low-level REAL_FUNC_PASSTHROUGH plumbing for the
# real libc entry points.  That is not a PID/timer wrapper ownership overlap;
# the transitional overlap this gate tracks is limited to wrapper definitions
# and FOREACH/prototype declarations.
PID_OVERLAP_IGNORED_LINE_MARKERS = ("REAL_FUNC_PASSTHROUGH(",)


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


def relpath(path):
    return str(path.relative_to(REPO_ROOT))


def read_text(rel, result, contract):
    path = REPO_ROOT / rel
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        result.fail(contract, "required file is not readable", ["{0}: {1}".format(rel, exc)])
    except UnicodeDecodeError as exc:
        result.fail(contract, "required file is not UTF-8 text", ["{0}: {1}".format(rel, exc)])
    return ""


def read_static(rel, result, contract):
    if rel not in STATIC_INPUTS:
        result.fail(
            "static-input-scope",
            "static mode attempted to inspect an unapproved path",
            [rel],
        )
        return ""
    return read_text(rel, result, contract)


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


def require_items_in_makefile_var(result, rel, variable, items, contract):
    text = read_static(rel, result, contract)
    block = makefile_var_block(text, variable)
    if not block:
        result.fail(contract, "Makefile variable is missing", ["{0}: {1}".format(rel, variable)])
        return
    missing = [item for item in items if item not in block]
    if missing:
        result.fail(
            contract,
            "Makefile variable is missing timer built-in entries",
            ["{0}: {1} missing {2}".format(rel, variable, item) for item in missing],
        )
        return
    result.pass_(
        contract,
        "{0}:{1} contains {2} required timer entry/entries".format(
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


def validate_launch_preload_state(result, text, contract):
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


def validate_static_scope(result):
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
        if any(part in EXCLUDED_STATIC_COMPONENTS for part in parts):
            details.append("static input uses forbidden hidden state: {0}".format(rel))
        if path.suffix not in STATIC_ALLOWED_SUFFIXES:
            details.append("static input suffix is not source/configuration-like: {0}".format(rel))
        if not (REPO_ROOT / rel).is_file():
            details.append("static input does not exist: {0}".format(rel))
    if details:
        result.fail("static-input-scope", "static input list violates the source-only contract", details)
        return
    result.pass_(
        "static-input-scope",
        "static mode is limited to {0} explicit source/config paths and excludes {1}".format(
            len(STATIC_INPUTS), ", ".join(EXCLUDED_STATIC_COMPONENTS)
        ),
    )


def validate_makefile_timer_builtin_path(result):
    require_items_in_makefile_var(
        result,
        "src/Makefile.am",
        "__d_libdir__libdmtcp_so_SOURCES",
        TIMER_MAKEFILE_SOURCES,
        "makefile-libdmtcp-sources-am",
    )
    require_items_in_makefile_var(
        result,
        "src/Makefile.in",
        "__d_libdir__libdmtcp_so_SOURCES",
        TIMER_MAKEFILE_SOURCES,
        "makefile-libdmtcp-sources-in",
    )
    require_items_in_makefile_var(
        result,
        "src/Makefile.in",
        "am___d_libdir__libdmtcp_so_OBJECTS",
        TIMER_OBJECTS,
        "makefile-libdmtcp-objects-in",
    )
    require_items_in_makefile_var(
        result,
        "src/Makefile.in",
        "am__depfiles_remade",
        TIMER_DEPFILES,
        "makefile-libdmtcp-dependencies-in",
    )


def validate_removed_timer_dso_references(result):
    offenders = []
    for rel in NO_TIMER_DSO_STATIC_PATHS:
        text = read_static(rel, result, "no-libdmtcp-timer-dso")
        for needle in FORBIDDEN_TIMER_DSO_NEEDLES:
            for hit in line_hits(text, needle):
                offenders.append("{0}: {1}".format(rel, hit))
    if offenders:
        result.fail(
            "no-libdmtcp-timer-dso",
            "removed timer DSO is still constructed or referenced",
            offenders,
        )
        return
    result.pass_(
        "no-libdmtcp-timer-dso",
        "plugin Makefiles plus launch/exec preload code contain no libdmtcp_timer.so reference",
    )


def validate_launch_preload_order(result):
    text = read_static("src/dmtcp_launch.cpp", result, "launch-preload-order")
    validate_launch_preload_state(result, text, "launch-preload-order")


def validate_pluginmanager_timer_registration(result):
    text = read_static("src/pluginmanager.cpp", result, "pluginmanager-timer-registration")
    text_no_comments = strip_cpp_comments(text)
    details = []
    if "DmtcpPluginDescriptor_t dmtcp_Timer_PluginDescr();" not in text_no_comments:
        details.append("missing dmtcp_Timer_PluginDescr() declaration")
    if "getenv(ENV_VAR_DISABLE_ALL_PLUGINS)" not in text_no_comments:
        details.append("timer registration gate does not read ENV_VAR_DISABLE_ALL_PLUGINS")
    if 'strcmp(disableAllPlugins, "1") != 0' not in text_no_comments:
        details.append("timer registration gate does not preserve disable-all value 1 semantics")

    init_body = function_body(
        text_no_comments,
        r"extern\s+\"C\"\s+void\s+dmtcp_initialize_plugin\s*\(",
    )
    if init_body is None:
        details.append("libdmtcp dmtcp_initialize_plugin() definition is missing")
    else:
        gated_timer_register = re.search(
            r"if\s*\(\s*timerPluginEnabled\s*\(\s*\)\s*\)\s*\{\s*"
            r"dmtcp_register_plugin\s*\(\s*dmtcp_Timer_PluginDescr\s*\(\s*\)\s*\)\s*;\s*\}",
            init_body,
            flags=re.S,
        )
        if not gated_timer_register:
            details.append("dmtcp_Timer_PluginDescr() is not registered under timerPluginEnabled()")
        if "NEXT_FNC(dmtcp_initialize_plugin)" not in init_body:
            details.append("dmtcp_initialize_plugin chain does not call NEXT_FNC")
        if not re.search(r"\(\s*\*\s*fn\s*\)\s*\(\s*\)\s*;", init_body):
            details.append("dmtcp_initialize_plugin NEXT_FNC result is not invoked")

    if details:
        result.fail(
            "pluginmanager-timer-registration",
            "built-in timer registration contract is broken",
            details,
        )
        return
    result.pass_(
        "pluginmanager-timer-registration",
        "pluginmanager declares the timer descriptor, gates its built-in row on disable-all, and preserves the NEXT_FNC initializer chain",
    )


def validate_timer_sources_do_not_self_register(result):
    offenders = []
    init_pattern = re.compile(
        r"(?:extern\s+\"C\"\s+)?void\s+dmtcp_initialize_plugin\s*\(",
        flags=re.S,
    )
    for rel in TIMER_SOURCE_PATHS:
        text = read_static(rel, result, "timer-no-self-registration")
        text_no_comments = strip_cpp_comments(text)
        for hit in line_hits(text_no_comments, "DMTCP_DECL_PLUGIN"):
            offenders.append("{0}: {1}".format(rel, hit))
        if init_pattern.search(text_no_comments):
            offenders.append("{0}: defines dmtcp_initialize_plugin()".format(rel))
    if offenders:
        result.fail(
            "timer-no-self-registration",
            "timer source files must not self-register as a standalone plugin",
            offenders,
        )
        return
    result.pass_(
        "timer-no-self-registration",
        "timer sources contain no DMTCP_DECL_PLUGIN and no timer-local dmtcp_initialize_plugin()",
    )


def validate_timer_disable_all_fast_pass(result):
    text = read_static("src/plugin/timer/timerwrappers.cpp", result, "timer-disable-all-fast-pass")
    text_no_comments = strip_cpp_comments(text)
    details = []
    if "getenv(ENV_VAR_DISABLE_ALL_PLUGINS)" not in text_no_comments:
        details.append("timer wrappers do not read ENV_VAR_DISABLE_ALL_PLUGINS")
    if 'strcmp(disableAllPlugins, "1") != 0' not in text_no_comments:
        details.append("timerPluginEnabled() does not fast-disable on value 1")

    for wrapper, real_func in TIMER_WRAPPER_FAST_PASS:
        body = function_body(
            text_no_comments,
            r"extern\s+\"C\"\s+int\s+" + re.escape(wrapper) + r"\s*\(",
        )
        if body is None:
            details.append("missing wrapper body for {0}".format(wrapper))
            continue
        body_start = body.lstrip()[:500]
        pattern = re.compile(
            r"^if\s*\(\s*!\s*timerPluginEnabled\s*\(\s*\)\s*\)\s*\{\s*"
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
            "timer-disable-all-fast-pass",
            "timer wrappers do not all fast-pass to real functions under disable-all",
            details,
        )
        return
    result.pass_(
        "timer-disable-all-fast-pass",
        "all timer/clock wrappers immediately delegate to _real_* when ENV_VAR_DISABLE_ALL_PLUGINS=1",
    )


def run_static(result):
    validate_static_scope(result)
    validate_makefile_timer_builtin_path(result)
    validate_removed_timer_dso_references(result)
    validate_launch_preload_order(result)
    validate_pluginmanager_timer_registration(result)
    validate_timer_sources_do_not_self_register(result)
    validate_timer_disable_all_fast_pass(result)


def pid_source_files():
    root = REPO_ROOT / "src/plugin/pid"
    if not root.is_dir():
        return []
    files = []
    for dirpath, dirnames, filenames in os.walk(str(root)):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDED_STATIC_COMPONENTS]
        for filename in filenames:
            path = Path(dirpath) / filename
            if path.suffix in PID_SOURCE_SUFFIXES:
                files.append(path)
    return sorted(files)


def validate_expected_pid_overlap_shapes(result):
    details = []
    timer_wrappers = read_text(
        "src/plugin/timer/timerwrappers.cpp", result, "pid-overlap-composed-owner"
    )
    timer_header = read_text(
        "src/plugin/timer/timerwrappers.h", result, "pid-overlap-composed-owner"
    )
    timer_no_comments = strip_cpp_comments(timer_wrappers)
    header_no_comments = strip_cpp_comments(timer_header)

    for token in ("clock_getcpuclockid", "timer_create"):
        if not re.search(r"extern\s+\"C\"\s+int\s+" + re.escape(token) + r"\s*\(", timer_no_comments, flags=re.S):
            details.append("src/plugin/timer/timerwrappers.cpp missing timer-owned wrapper {0}".format(token))
    for token in ("dmtcp_timer_virtual_to_real_pid", "dmtcp_virtual_to_real_pid", "SIGEV_THREAD_ID"):
        if token not in timer_no_comments and token not in header_no_comments:
            details.append("timer owner missing PID composition token {0}".format(token))
    if details:
        result.fail(
            "pid-overlap-composed-owner",
            "timer wrappers no longer have PID overlap but are missing composed PID translation",
            details,
        )
        return False
    result.pass_(
        "pid-overlap-composed-owner",
        "timer wrappers own clock_getcpuclockid/timer_create and compose PID translation locally",
    )
    return True


def run_pid_overlap(result):
    validate_expected_pid_overlap_shapes(result)
    token_re = re.compile(r"\b(?:" + "|".join(PID_OVERLAP_TOKENS) + r")\b")
    unexpected = []

    for path in pid_source_files():
        rel = relpath(path)
        original = read_text(rel, result, "pid-overlap-locations")
        searchable = strip_cpp_comments_preserve_lines(original)
        original_lines = original.splitlines()
        for lineno, line in enumerate(searchable.splitlines(), 1):
            if token_re.search(line):
                if any(marker in line for marker in PID_OVERLAP_IGNORED_LINE_MARKERS):
                    continue
                unexpected.append("{0}:{1}: {2}".format(
                    rel, lineno, original_lines[lineno - 1].strip()
                ))

    if unexpected:
        result.fail(
            "pid-overlap-locations",
            "timer/PID overlap remains in PID source after composition",
            unexpected,
        )
        return
    result.pass_(
        "pid-overlap-locations",
        "no timer/PID overlap remains in PID source files",
    )


def run_artifacts(result):
    lib_dir = REPO_ROOT / "lib"
    if not lib_dir.exists():
        result.fail(
            "artifacts-lib-output",
            "artifacts mode requires generated lib/ outputs; run a build before this mode",
        )
    else:
        offenders = []
        for path in sorted(lib_dir.rglob("*")):
            rel = relpath(path)
            if "libdmtcp_timer.so" in rel or "libdmtcp_timer" in rel:
                offenders.append(rel)
            if path.is_symlink():
                try:
                    target = os.readlink(str(path))
                except OSError:
                    target = ""
                if "libdmtcp_timer.so" in target or "libdmtcp_timer" in target:
                    offenders.append("{0} -> {1}".format(rel, target))
        if offenders:
            result.fail(
                "artifacts-lib-output",
                "generated lib outputs contain a removed timer DSO artifact",
                offenders,
            )
        else:
            result.pass_(
                "artifacts-lib-output",
                "generated lib outputs contain no libdmtcp_timer.so artifact",
            )

    launch = REPO_ROOT / "bin/dmtcp_launch"
    if not launch.exists():
        result.fail(
            "artifacts-launch-binary",
            "artifacts mode requires built bin/dmtcp_launch; run a build before this mode",
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
    if b"libdmtcp_timer.so" in data or b"libdmtcp_timer" in data:
        result.fail(
            "artifacts-launch-binary",
            "built dmtcp_launch still embeds the removed timer DSO name",
            ["bin/dmtcp_launch contains libdmtcp_timer"],
        )
        return
    result.pass_(
        "artifacts-launch-binary",
        "built dmtcp_launch embeds no libdmtcp_timer.so string",
    )


def launch_fixture(libs):
    rows = []
    for idx, lib in enumerate(libs):
        rows.append('  {{ &enablePlugin{0}, "{1}" }}'.format(idx, lib))
    return (
        "static struct PluginInfo pluginInfo[] = {\n" +
        ",\n".join(rows) +
        "\n};\n"
        "static void setLDPreloadLibs(bool is32bitElf) {\n"
        "  string preloadLibs = \"\";\n"
        "  if (getenv(ENV_VAR_PLUGIN) != NULL) { preloadLibs += getenv(ENV_VAR_PLUGIN); }\n"
        "  if (disableAllPlugins) { preloadLibs = Util::getPath(\"libdmtcp.so\"); }\n"
        "  for (size_t i = 0; i < numLibs; i++) {}\n"
        "}\n"
    )


def failure_contracts(result):
    return set(contract for contract, _message, _details in result.failures)


def run_self_test(result):
    positive = Result()
    positive.emit = False
    validate_launch_preload_state(
        positive,
        launch_fixture(
            (
                "libdmtcp_modify-env.so",
                "libdmtcp_unique-ckpt.so",
                "libdmtcp_pathvirt.so",
                "libdmtcp.so",
            )
        ),
        "launch-preload-order",
    )
    if positive.failures:
        result.fail(
            "self-test-optional-launch-rows",
            "optional final-state launcher rows were rejected",
            ["{0}: {1}".format(contract, message) for contract, message, _details in positive.failures],
        )
    else:
        result.pass_(
            "self-test-optional-launch-rows",
            "optional modify-env/unique-ckpt/pathvirt rows are accepted before libdmtcp.so",
        )

    stale_alloc_dl = Result()
    stale_alloc_dl.emit = False
    validate_launch_preload_state(
        stale_alloc_dl,
        launch_fixture(("libdmtcp_alloc.so", "libdmtcp_dl.so", "libdmtcp.so")),
        "launch-preload-order",
    )
    if "launch-preload-order" in failure_contracts(stale_alloc_dl):
        result.pass_(
            "self-test-stale-alloc-dl-launch-rows",
            "stale alloc/DL launcher rows are rejected with final-state diagnostics",
        )
    else:
        result.fail(
            "self-test-stale-alloc-dl-launch-rows",
            "stale alloc/DL launcher rows were not rejected",
        )

    missing_libdmtcp = Result()
    missing_libdmtcp.emit = False
    validate_launch_preload_state(
        missing_libdmtcp,
        launch_fixture(("libdmtcp_modify-env.so", "libdmtcp_pathvirt.so")),
        "launch-preload-order",
    )
    if "launch-preload-order" in failure_contracts(missing_libdmtcp):
        result.pass_(
            "self-test-missing-libdmtcp-launch-row",
            "missing libdmtcp.so launcher row is rejected",
        )
    else:
        result.fail(
            "self-test-missing-libdmtcp-launch-row",
            "missing libdmtcp.so launcher row was not rejected",
        )

    stale_timer = Result()
    stale_timer.emit = False
    validate_launch_preload_state(
        stale_timer,
        launch_fixture(("libdmtcp_timer.so", "libdmtcp.so")),
        "launch-preload-order",
    )
    if "launch-preload-order" in failure_contracts(stale_timer):
        result.pass_(
            "self-test-stale-timer-launch-row",
            "stale timer launcher row is rejected",
        )
    else:
        result.fail(
            "self-test-stale-timer-launch-row",
            "stale timer launcher row was not rejected",
        )


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=("self-test", "static", "pid-overlap", "artifacts", "full"),
        nargs="?",
        default="static",
        help="verification mode to run (default: static)",
    )
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    result = Result()
    if args.mode == "self-test":
        run_self_test(result)
    elif args.mode == "static":
        run_static(result)
    elif args.mode == "pid-overlap":
        run_pid_overlap(result)
    elif args.mode == "artifacts":
        run_artifacts(result)
    elif args.mode == "full":
        run_static(result)
        run_self_test(result)
        run_pid_overlap(result)
        run_artifacts(result)
    return result.finish()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
