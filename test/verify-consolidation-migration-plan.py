#!/usr/bin/env python3
"""Static checks for the internal-plugin consolidation migration plan.

`self-test` validates this verifier's own contract without requiring the S03
migration document to exist: it uses explicit non-hidden repository paths,
keeps all validation tables non-empty, rejects recursive scan APIs, verifies
current source-anchor files, and exercises the negative guards for hidden paths,
out-of-repo paths, placeholder patterns, empty tables, and missing source files.

`stage-map` validates the source-backed current migration surface and the
accepted migration order: timer, svipc, ipc, pid, alloc, dl.

`gate-plan` validates the per-stage implementation gates: build edits,
launcher/util_exec/exec-wrapper behavior, duplicate initializer and wrapper
avoidance, rollback points, stale artifact cleanup, focused tests, and make
check placement.

`full` validates the complete migration appendix, including requirement
coverage, negative checks, the S09 final validation gate, and S04 handoff text.

All modes inspect only the explicit non-hidden paths listed below.  The verifier
does not walk the repository and does not read generated planning artifacts such
as hidden project-state directories.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DOC_PATH = Path("doc/internal-plugin-consolidation-migration-plan.md")
EXPECTED_DOC_PATH = "doc/internal-plugin-consolidation-migration-plan.md"

PRIOR_PLAN_FILES = [
    "doc/internal-plugin-consolidation-registration-architecture.md",
    "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
]

S09_FINAL_GATE_EVIDENCE_FILES = [
    "test/verify-consolidation-preflight.py",
    "test/verify-consolidation-registration-plan.py",
    "test/verify-consolidation-design-plan.py",
    "test/verify-consolidation-migration-plan.py",
    "test/verify-consolidation-wrapper-tsan-plan.py",
    "doc/dmtcp_dlsym.txt",
    "src/dlwrappers.cpp",
    "src/dmtcp_dlsym_wrappers.cpp",
    "src/dmtcp_dlsym.cpp",
]

IPC_DSO_SOURCE_FILES = [
    "src/plugin/ipc/connection.cpp",
    "src/plugin/ipc/connection.h",
    "src/plugin/ipc/connectionidentifier.cpp",
    "src/plugin/ipc/connectionidentifier.h",
    "src/plugin/ipc/connectionlist.cpp",
    "src/plugin/ipc/connectionlist.h",
    "src/plugin/ipc/ipc.cpp",
    "src/plugin/ipc/ipc.h",
    "src/plugin/ipc/event/eventconnection.cpp",
    "src/plugin/ipc/event/eventconnection.h",
    "src/plugin/ipc/event/eventconnlist.cpp",
    "src/plugin/ipc/event/eventconnlist.h",
    "src/plugin/ipc/event/eventwrappers.cpp",
    "src/plugin/ipc/event/eventwrappers.h",
    "src/plugin/ipc/event/util_descriptor.cpp",
    "src/plugin/ipc/event/util_descriptor.h",
    "src/plugin/ipc/file/fileconnection.cpp",
    "src/plugin/ipc/file/fileconnection.h",
    "src/plugin/ipc/file/fileconnlist.cpp",
    "src/plugin/ipc/file/fileconnlist.h",
    "src/plugin/ipc/file/filewrappers.cpp",
    "src/plugin/ipc/file/filewrappers.h",
    "src/plugin/ipc/file/openwrappers.cpp",
    "src/plugin/ipc/file/posixipcwrappers.cpp",
    "src/plugin/ipc/file/ptyconnection.cpp",
    "src/plugin/ipc/file/ptyconnection.h",
    "src/plugin/ipc/file/ptyconnlist.cpp",
    "src/plugin/ipc/file/ptyconnlist.h",
    "src/plugin/ipc/file/ptywrappers.cpp",
    "src/plugin/ipc/file/ptywrappers.h",
    "src/plugin/ipc/socket/connectionmessage.h",
    "src/plugin/ipc/socket/connectionrewirer.cpp",
    "src/plugin/ipc/socket/connectionrewirer.h",
    "src/plugin/ipc/socket/kernelbufferdrainer.cpp",
    "src/plugin/ipc/socket/kernelbufferdrainer.h",
    "src/plugin/ipc/socket/socketconnection.cpp",
    "src/plugin/ipc/socket/socketconnection.h",
    "src/plugin/ipc/socket/socketconnlist.cpp",
    "src/plugin/ipc/socket/socketconnlist.h",
    "src/plugin/ipc/socket/socketwrappers.cpp",
    "src/plugin/ipc/socket/socketwrappers.h",
    "src/plugin/ipc/ssh/ssh.cpp",
    "src/plugin/ipc/ssh/sshdrainer.cpp",
    "src/plugin/ipc/ssh/sshdrainer.h",
    "src/plugin/ipc/ssh/ssh.h",
]

IPC_DSO_MAKEFILE_SOURCE_MARKERS = [
    path.replace("src/plugin/", "") for path in IPC_DSO_SOURCE_FILES
]

IPC_HELPER_BOUNDARY_FILES = [
    "src/plugin/ipc/ssh/util_ssh.cpp",
    "src/plugin/ipc/ssh/dmtcp_ssh.cpp",
    "src/plugin/ipc/ssh/dmtcp_sshd.cpp",
]

IPC_STAGE_MAP_REPRESENTATIVE_FILES = [
    "src/plugin/ipc/ipc.cpp",
    "src/plugin/ipc/ssh/ssh.cpp",
    "src/plugin/ipc/event/eventconnlist.cpp",
    "src/plugin/ipc/file/fileconnlist.cpp",
    "src/plugin/ipc/file/ptyconnlist.cpp",
    "src/plugin/ipc/socket/socketconnlist.cpp",
]

IPC_DSO_DETAIL_SOURCE_FILES = [
    path for path in IPC_DSO_SOURCE_FILES if path not in IPC_STAGE_MAP_REPRESENTATIVE_FILES
]

IPC_FOCUSED_TEST_FILES = [
    "test/file1.c",
    "test/file2.c",
    "test/file3.c",
    "test/shared-fd1.c",
    "test/shared-fd2.c",
    "test/stale-fd.c",
    "test/procfd1.c",
    "test/poll.c",
    "test/epoll1.c",
    "test/inotify1.c",
    "test/client-server.c",
    "test/seqpacket.c",
    "test/posix-mq1.c",
    "test/posix-mq2.c",
    "test/pty1.c",
    "test/pty2.c",
    "test/ssh1.c",
]

PID_DSO_SOURCE_FILES = [
    "src/plugin/pid/glibc_pthread.cpp",
    "src/plugin/pid/glibc_pthread.h",
    "src/plugin/pid/pid.cpp",
    "src/plugin/pid/pid_filewrappers.cpp",
    "src/plugin/pid/pid.h",
    "src/plugin/pid/pid_miscwrappers.cpp",
    "src/plugin/pid/pid_syscallsreal.c",
    "src/plugin/pid/pidwrappers.cpp",
    "src/plugin/pid/pidwrappers.h",
    "src/plugin/pid/sched_wrappers.cpp",
    "src/plugin/pid/virtualpidtable.cpp",
    "src/plugin/pid/virtualpidtable.h",
]

PID_DSO_MAKEFILE_SOURCE_MARKERS = [
    path.replace("src/plugin/", "") for path in PID_DSO_SOURCE_FILES
]

PID_DSO_DETAIL_SOURCE_FILES = [
    path for path in PID_DSO_SOURCE_FILES if path not in [
        "src/plugin/pid/pid.cpp",
        "src/plugin/pid/pidwrappers.cpp",
    ]
]

PID_FOCUSED_TEST_FILES = [
    "test/plugin-init.cpp",
    "test/gettid.c",
    "test/sched_test.c",
    "test/forkexec.c",
    "test/vfork1.c",
    "test/pthread_atfork1.c",
    "test/pthread_atfork2.c",
    "test/waitpid.c",
    "test/syscall-tester.c",
]

PID_DUPLICATE_REAL_HELPERS = []

ALLOC_DSO_SOURCE_FILES = [
    "src/plugin/alloc/alloc.h",
    "src/plugin/alloc/mallocwrappers.cpp",
    "src/plugin/alloc/mmapwrappers.cpp",
]

ALLOC_DSO_MAKEFILE_SOURCE_MARKERS = [
    path.replace("src/plugin/", "") for path in ALLOC_DSO_SOURCE_FILES
]

ALLOC_ACTIVE_WRAPPER_DOC_MARKER = (
    "dmtcp_alloc_enabled, calloc, malloc, memalign, posix_memalign, "
    "valloc, free, and realloc"
)
ALLOC_MMAP_WRAPPER_DOC_MARKER = "mmap, mmap64, munmap, and mremap"

ALLOC_DOC_STAGE_MARKERS = [
    "__d_libdir__libdmtcp_alloc_so_SOURCES",
    "src/plugin/alloc/alloc.h",
    "src/plugin/alloc/mallocwrappers.cpp",
    "src/plugin/alloc/mmapwrappers.cpp",
    ALLOC_ACTIVE_WRAPPER_DOC_MARKER,
    "dmtcp_alloc_enabled() export",
    "returns 1",
    "not yet that predicate",
    "--disable-alloc-plugin",
    "DMTCP_ALLOC_PLUGIN=0",
    "--disable-all-plugins",
    "fast-pass",
    "#ifdef ENABLE_MMAP_WRAPPERS",
    ALLOC_MMAP_WRAPPER_DOC_MARKER,
    "stale libdmtcp_alloc.so",
    "test/plugin-init.cpp",
    "no descriptor accessor",
    "no DmtcpPluginDescriptor_t",
    "no PluginManager event row",
    "no public include/dmtcp.h API",
    "Alloc rollback point",
    "Alloc stop/go gate",
]

DL_DSO_SOURCE_FILES = [
    "src/plugin/dl/dlwrappers.cpp",
]

DL_DSO_MAKEFILE_SOURCE_MARKERS = [
    path.replace("src/plugin/", "") for path in DL_DSO_SOURCE_FILES
]

DL_ACTIVE_WRAPPER_DOC_MARKER = (
    "dlopen, dlclose, _real_dlopen, _real_dlclose"
)

DL_DOC_STAGE_MARKERS = [
    "__d_libdir__libdmtcp_dl_so_SOURCES",
    "src/plugin/dl/dlwrappers.cpp",
    DL_ACTIVE_WRAPPER_DOC_MARKER,
    "_real_dlopen",
    "_real_dlclose",
    "getRpathRunPath",
    "dlopen_try_paths",
    "LibDlWrapperLock",
    "--disable-dl-plugin",
    "DMTCP_DL_PLUGIN=0",
    "--disable-all-plugins",
    "fast-pass",
    "stale libdmtcp_dl.so",
    "test/dlopen1.c",
    "test/dlopen2.cpp",
    "no descriptor accessor",
    "no DmtcpPluginDescriptor_t",
    "no DMTCP_DECL_PLUGIN",
    "no dmtcp_initialize_plugin",
    "no PluginManager event row",
    "no public include/dmtcp.h API",
    "DL rollback point",
    "DL stop/go gate",
    "TSAN",
    "dlsym/RTLD_NEXT",
    "dl_iterate_phdr",
    "default-version lookup",
]

ALLOC_DESCRIPTOR_CLAIM_TERMS = (
    "dmtcp_alloc_plugindescr",
    "dmtcp_decl_plugin(alloc",
    "alloc descriptor accessor",
    "alloc descriptor row",
    "pluginmanager event row for alloc",
    "alloc pluginmanager event row",
)

DL_DESCRIPTOR_CLAIM_TERMS = (
    "dmtcp_dl_plugindescr",
    "dmtcp_decl_plugin(dl",
    "dl descriptor accessor",
    "dl descriptor row",
    "dl plugin descriptor",
    "pluginmanager event row for dl",
    "dl pluginmanager event row",
    "dl dmtcp_initialize_plugin",
    "dmtcp_initialize_plugin ownership",
)

MIGRATION_RUNTIME_CLAIM_TERMS = (
    "runtime migration",
    "alloc runtime",
    "allocator runtime",
    "dl runtime",
    "dlopen runtime",
    "dl wrapper runtime",
)

CURRENT_SOURCE_FILES = [
    "src/dmtcp_launch.cpp",
    "src/util_exec.cpp",
    "src/execwrappers.cpp",
    "src/miscwrappers.cpp",
    "src/pluginmanager.cpp",
    "src/pluginmanager.h",
    "include/dmtcp.h",
    "src/Makefile.am",
    "src/plugin/Makefile.am",
    "src/plugin/timer/timer_create.cpp",
    "src/plugin/timer/timerlist.cpp",
    "src/plugin/timer/timerlist.h",
    "src/plugin/timer/timerwrappers.cpp",
    "src/plugin/timer/timerwrappers.h",
    "src/plugin/svipc/sysvipc.cpp",
    "src/plugin/svipc/sysvipc.h",
    "src/plugin/svipc/sysvipcwrappers.cpp",
    "src/plugin/svipc/sysvipcwrappers.h",
    *IPC_DSO_SOURCE_FILES,
    *IPC_HELPER_BOUNDARY_FILES,
    "src/syscallsreal.c",
    "src/constants.h",
    *PID_DSO_SOURCE_FILES,
    *ALLOC_DSO_SOURCE_FILES,
    *DL_DSO_SOURCE_FILES,
    "test/autotest.py",
    "test/sysv-shm1.c",
    "test/sysv-shm2.c",
    "test/sysv-sem.c",
    "test/sysv-msg.c",
    "test/timer1.c",
    "test/timer2.c",
    "test/clock.c",
    "test/dlopen1.c",
    "test/dlopen2.cpp",
    *PID_FOCUSED_TEST_FILES,
    *IPC_FOCUSED_TEST_FILES,
]

PID_TIMER_COLLISION_SOURCE_FILES = [
    "src/plugin/pid/pid_miscwrappers.cpp",
    "src/plugin/pid/pidwrappers.h",
    "src/plugin/pid/pid_syscallsreal.c",
]

REQUIRED_EVIDENCE_FILES = [*PRIOR_PLAN_FILES, *S09_FINAL_GATE_EVIDENCE_FILES, *CURRENT_SOURCE_FILES]
CURRENT_SOURCE_MAP_EVIDENCE_FILES = [
    path
    for path in REQUIRED_EVIDENCE_FILES
    if path
    not in [
        *S09_FINAL_GATE_EVIDENCE_FILES,
        *PID_TIMER_COLLISION_SOURCE_FILES,
        *PID_DSO_DETAIL_SOURCE_FILES,
        *PID_FOCUSED_TEST_FILES,
        "src/syscallsreal.c",
        "src/constants.h",
        "src/miscwrappers.cpp",
        "src/plugin/alloc/alloc.h",
        *IPC_DSO_DETAIL_SOURCE_FILES,
        *IPC_HELPER_BOUNDARY_FILES,
        *IPC_FOCUSED_TEST_FILES,
    ]
]

STAGE_MAP_REQUIRED_HEADINGS = [
    "Scope and evidence boundary",
    "Current migration source map",
    "Accepted staged migration order",
    "Current launcher and exec propagation surfaces",
    "Current PluginManager and public ABI boundary",
    "Current IPC subplugin and pid-last ordering",
    "Current alloc and dl wrapper-only boundaries",
    "Current build and focused test boundaries",
]

GATE_EXTRA_HEADINGS = [
    "Timer stage gate",
    "SysV IPC stage gate",
    "IPC stage gate",
    "PID stage gate",
    "Alloc stage gate",
    "DL stage gate",
    "Duplicate initializer and wrapper avoidance gates",
    "Rollback and stale artifact cleanup gates",
    "Focused tests and make check gates",
]

FULL_EXTRA_HEADINGS = [
    "Requirement coverage and negative checks",
    "S09 final validation and handoff gate",
    "S04 handoff coverage",
]

GATE_PLAN_REQUIRED_HEADINGS = [*STAGE_MAP_REQUIRED_HEADINGS, *GATE_EXTRA_HEADINGS]
FULL_REQUIRED_HEADINGS = [*GATE_PLAN_REQUIRED_HEADINGS, *FULL_EXTRA_HEADINGS]

STAGE_MAP_TABLE_HEADINGS = [
    "Current migration source map",
    "Accepted staged migration order",
]

GATE_PLAN_TABLE_HEADINGS = [
    *STAGE_MAP_TABLE_HEADINGS,
    "Timer stage gate",
    "SysV IPC stage gate",
    "IPC stage gate",
    "PID stage gate",
    "Alloc stage gate",
    "DL stage gate",
    "Duplicate initializer and wrapper avoidance gates",
    "Rollback and stale artifact cleanup gates",
    "Focused tests and make check gates",
]

FULL_TABLE_HEADINGS = [
    *GATE_PLAN_TABLE_HEADINGS,
    "Requirement coverage and negative checks",
    "S09 final validation and handoff gate",
    "S04 handoff coverage",
]

STAGE_MAP_SECTION_CITATIONS = {
    "Scope and evidence boundary": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
    ],
    "Current migration source map": CURRENT_SOURCE_MAP_EVIDENCE_FILES,
    "Accepted staged migration order": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "src/plugin/Makefile.am",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
        "test/clock.c",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/pid/pid.cpp",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
    "Current launcher and exec propagation surfaces": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
    ],
    "Current PluginManager and public ABI boundary": [
        "src/pluginmanager.cpp",
        "src/pluginmanager.h",
        "include/dmtcp.h",
    ],
    "Current IPC subplugin and pid-last ordering": [
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/event/eventconnlist.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/file/ptyconnlist.cpp",
        "src/plugin/ipc/socket/socketconnlist.cpp",
        "src/plugin/pid/pid.cpp",
        "src/dmtcp_launch.cpp",
    ],
    "Current alloc and dl wrapper-only boundaries": [
        "src/plugin/Makefile.am",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/plugin/alloc/alloc.h",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/alloc/mmapwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
    "Current build and focused test boundaries": [
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "test/timer1.c",
        "test/timer2.c",
        "test/clock.c",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
    ],
}

GATE_PLAN_SECTION_CITATIONS = {
    **STAGE_MAP_SECTION_CITATIONS,
    "Timer stage gate": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "src/plugin/pid/pidwrappers.h",
        "src/plugin/pid/pid_syscallsreal.c",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "test/timer1.c",
        "test/timer2.c",
        "test/clock.c",
    ],
    "SysV IPC stage gate": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/miscwrappers.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "src/plugin/pid/pidwrappers.h",
        "src/plugin/pid/pid_syscallsreal.c",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
    ],
    "IPC stage gate": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "test/autotest.py",
        *IPC_DSO_SOURCE_FILES,
        *IPC_HELPER_BOUNDARY_FILES,
        *IPC_FOCUSED_TEST_FILES,
    ],
    "PID stage gate": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/constants.h",
        "src/miscwrappers.cpp",
        "src/syscallsreal.c",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        "test/autotest.py",
        *PID_DSO_SOURCE_FILES,
        *PID_FOCUSED_TEST_FILES,
        "test/procfd1.c",
        "test/timer1.c",
        "test/timer2.c",
        "test/posix-mq1.c",
        "test/posix-mq2.c",
    ],
    "Alloc stage gate": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/constants.h",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        *ALLOC_DSO_SOURCE_FILES,
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "test/plugin-init.cpp",
    ],
    "DL stage gate": [
        "src/dmtcp_launch.cpp",
        "src/constants.h",
        "src/util_exec.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "src/execwrappers.cpp",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "test/dlopen1.c",
        "test/dlopen2.cpp",
    ],
    "Duplicate initializer and wrapper avoidance gates": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "include/dmtcp.h",
        "src/pluginmanager.cpp",
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
        "src/syscallsreal.c",
        "src/plugin/pid/pid.cpp",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "src/plugin/pid/pid_syscallsreal.c",
        "src/plugin/pid/pidwrappers.cpp",
        "src/plugin/pid/pidwrappers.h",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/miscwrappers.cpp",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/event/eventwrappers.cpp",
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        "src/plugin/ipc/socket/socketwrappers.cpp",
        "src/plugin/pid/pid.cpp",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/alloc/mmapwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "test/plugin-init.cpp",
    ],
    "Rollback and stale artifact cleanup gates": [
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
    ],
    "Focused tests and make check gates": [
        "test/autotest.py",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "test/timer1.c",
        "test/timer2.c",
        "test/clock.c",
        "test/dlopen1.c",
        "test/dlopen2.cpp",
        "test/plugin-init.cpp",
        *IPC_FOCUSED_TEST_FILES,
        "src/plugin/timer/timerlist.cpp",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/event/eventwrappers.cpp",
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        "src/plugin/ipc/socket/socketwrappers.cpp",
        "src/plugin/pid/glibc_pthread.cpp",
        "src/plugin/pid/pid.cpp",
        "src/plugin/pid/pid_filewrappers.cpp",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "src/plugin/pid/pid_syscallsreal.c",
        "src/plugin/pid/pidwrappers.cpp",
        "src/plugin/pid/sched_wrappers.cpp",
        "src/plugin/pid/virtualpidtable.cpp",
        *PID_FOCUSED_TEST_FILES,
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/alloc/mmapwrappers.cpp",
        "src/plugin/dl/dlwrappers.cpp",
    ],
}

FULL_SECTION_CITATIONS = {
    **GATE_PLAN_SECTION_CITATIONS,
    "Requirement coverage and negative checks": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "src/dmtcp_launch.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "src/miscwrappers.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
        "src/constants.h",
        "src/syscallsreal.c",
        *PID_DSO_SOURCE_FILES,
        *PID_FOCUSED_TEST_FILES,
        *ALLOC_DSO_SOURCE_FILES,
        *DL_DSO_SOURCE_FILES,
        "test/autotest.py",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "test/timer1.c",
        "test/timer2.c",
        "test/clock.c",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/event/eventconnlist.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/file/ptyconnlist.cpp",
        "src/plugin/ipc/socket/socketconnlist.cpp",
        "src/plugin/ipc/event/eventwrappers.cpp",
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        "src/plugin/ipc/socket/socketwrappers.cpp",
        "src/plugin/ipc/socket/socketconnection.cpp",
        *IPC_HELPER_BOUNDARY_FILES,
        *IPC_FOCUSED_TEST_FILES,
    ],
    "S09 final validation and handoff gate": [
        "test/verify-consolidation-preflight.py",
        "test/verify-consolidation-registration-plan.py",
        "test/verify-consolidation-design-plan.py",
        "test/verify-consolidation-migration-plan.py",
        "test/verify-consolidation-wrapper-tsan-plan.py",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "test/timer1.c",
        "test/clock.c",
        "test/timer2.c",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "test/plugin-init.cpp",
        "test/dlopen1.c",
        "test/dlopen2.cpp",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "doc/dmtcp_dlsym.txt",
        "src/dlwrappers.cpp",
        "src/dmtcp_dlsym_wrappers.cpp",
        "src/dmtcp_dlsym.cpp",
        "src/plugin/dl/dlwrappers.cpp",
        "include/dmtcp.h",
        "src/pluginmanager.cpp",
    ],
    "S04 handoff coverage": [
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "doc/internal-plugin-consolidation-wrapper-order-tsan.md",
        "src/dmtcp_launch.cpp",
        "src/util_exec.cpp",
        "src/execwrappers.cpp",
        "src/pluginmanager.cpp",
        "include/dmtcp.h",
        "src/miscwrappers.cpp",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.cpp",
        "src/plugin/timer/timerwrappers.h",
        "src/constants.h",
        "src/syscallsreal.c",
        *PID_DSO_SOURCE_FILES,
        *PID_FOCUSED_TEST_FILES,
        *ALLOC_DSO_SOURCE_FILES,
        *DL_DSO_SOURCE_FILES,
        "test/autotest.py",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "test/timer1.c",
        "test/timer2.c",
        "test/clock.c",
        "src/plugin/Makefile.am",
        "src/plugin/ipc/ipc.cpp",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/event/eventconnlist.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/file/ptyconnlist.cpp",
        "src/plugin/ipc/socket/socketconnlist.cpp",
        "src/plugin/ipc/event/eventwrappers.cpp",
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        "src/plugin/ipc/socket/socketwrappers.cpp",
        "src/plugin/ipc/socket/socketconnection.cpp",
        *IPC_HELPER_BOUNDARY_FILES,
        *IPC_FOCUSED_TEST_FILES,
    ],
}

STAGE_MAP_MARKER_GROUPS = {
    "accepted migration order": [
        "timer,svipc,ipc,pid,alloc,dl",
        "timer stage",
        "SysV IPC stage",
        "IPC stage",
        "PID stage",
        "alloc stage",
        "DL stage",
    ],
    "launcher and exec surfaces": [
        "ENV_VAR_HIJACK_LIBS",
        "ENV_VAR_ALLOC_PLUGIN",
        "ENV_VAR_DL_PLUGIN",
        "Util::getDmtcpArgs",
        "getUpdatedLdPreload",
        "--disable-all-plugins",
        "--disable-alloc-plugin",
        "--disable-dl-plugin",
    ],
    "PluginManager and ABI surfaces": [
        "PluginManager::initialize",
        "dmtcp_register_plugin",
        "dmtcp_initialize_plugin",
        "NEXT_FNC(dmtcp_initialize_plugin)",
        "DMTCP_DECL_PLUGIN",
    ],
    "current libdmtcp core order": [
        "path translator,syslog,rlimit-float,alarm,terminal,coordinator,processinfo,uniquepid",
    ],
    "IPC and pid ordering": [
        "ssh,event,file,pty,socket",
        "socket,pty,file,event,ssh",
        "pid last",
        "libdmtcp_pid.so",
    ],
    "alloc and dl wrapper-only state": [
        "wrapper-only",
        "src/plugin/alloc/alloc.h",
        ALLOC_ACTIVE_WRAPPER_DOC_MARKER,
        "dmtcp_alloc_enabled() currently returns 1",
        "ENABLE_MMAP_WRAPPERS",
        ALLOC_MMAP_WRAPPER_DOC_MARKER,
        "no descriptor accessor",
        "no DmtcpPluginDescriptor_t",
        "PluginManager event row",
        "ENV_VAR_ALLOC_PLUGIN",
        "ENV_VAR_DL_PLUGIN",
    ],
    "DL current wrapper-only source state": [
        "__d_libdir__libdmtcp_dl_so_SOURCES",
        "src/plugin/dl/dlwrappers.cpp",
        DL_ACTIVE_WRAPPER_DOC_MARKER,
        "getRpathRunPath",
        "dlopen_try_paths",
        "LibDlWrapperLock",
        "no descriptor accessor",
        "no DmtcpPluginDescriptor_t",
        "no DMTCP_DECL_PLUGIN",
        "no dmtcp_initialize_plugin",
        "no PluginManager event row",
        "no public include/dmtcp.h API",
        "--disable-dl-plugin",
        "DMTCP_DL_PLUGIN=0",
        "--disable-all-plugins",
        "fast-pass",
        "stale libdmtcp_dl.so",
        "dlopen1",
        "dlopen2",
    ],
    "build and focused tests": [
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "test/autotest.py",
        "sysv-shm1",
        "sysv-shm2",
        "sysv-sem",
        "sysv-msg",
        "IPC_PRIVATE",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "timer1",
        "clock",
        "timer2 optional",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.h",
        "timer_create_sigev_thread()",
        "dlopen1",
        "dlopen2",
    ],
}

GATE_PLAN_MARKER_GROUPS = {
    "per-stage build edits": [
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "libdmtcp.so",
        "libdmtcp_timer.so",
        "libdmtcp_svipc.so",
        "libdmtcp_ipc.so",
        "libdmtcp_pid.so",
        "libdmtcp_alloc.so",
        "libdmtcp_dl.so",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.h",
        "test/clock.c",
    ],
    "per-stage launcher and restart argument behavior": [
        "ENV_VAR_HIJACK_LIBS",
        "Util::getDmtcpArgs",
        "getUpdatedLdPreload",
        "--disable-all-plugins",
        "--disable-alloc-plugin",
        "--disable-dl-plugin",
        "DMTCP_ALLOC_PLUGIN=0",
        "DMTCP_DL_PLUGIN=0",
    ],
    "DL stage source disable stale and handoff contract": [
        *DL_DOC_STAGE_MARKERS,
    ],
    "duplicate initializer and wrapper avoidance": [
        "duplicate dmtcp_initialize_plugin",
        "one libdmtcp dmtcp_initialize_plugin",
        "one libdmtcp-owned internal dmtcp_initialize_plugin node",
        "no DMTCP_DECL_PLUGIN for consolidated built-ins",
        "no DMTCP_DECL_PLUGIN left in consolidated built-ins",
        "duplicate wrapper",
        "no descriptor accessor",
        "no old DSO plus new libdmtcp object exporting the same wrappers",
        "stale libdmtcp_timer.so",
        "hard blocker",
        "temporary libdmtcp_pid.so",
        "timer_create",
        "clock_getcpuclockid",
        "S06 handoff marker",
    ],
    "rollback and stale artifact cleanup": [
        "rollback point",
        "stale artifact cleanup",
        "old separate DSO",
        "old DSO boundary",
        "clean install",
        "build lib directory",
        "install lib directory",
    ],
    "focused tests and make check": [
        "python3 test/verify-consolidation-migration-plan.py",
        "stage-map",
        "gate-plan",
        "full",
        "test/autotest.py",
        "sysv-shm1",
        "sysv-shm2",
        "sysv-sem",
        "sysv-msg",
        "IPC_PRIVATE",
        "timer1",
        "clock",
        "timer1 and clock",
        "timer2",
        "optional",
        "dlopen1",
        "dlopen2",
        "plugin-init",
        "make check",
    ],
    "SysV IPC source/header boundary": [
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/miscwrappers.cpp",
        "src/plugin/pid/pid_miscwrappers.cpp",
    ],
    "SysV IPC behavior preservation": [
        "IPC_PRIVATE",
        "keyed shared memory",
        "semaphore",
        "message-queue",
        "repeated and detached shmat",
        "shmctl",
        "semget",
        "semop",
        "semtimedop",
        "semctl",
        "msgget",
        "msgsnd",
        "msgrcv",
        "msgctl",
        "virtual/real ID and key translation",
        "restart/refill behavior",
        "dmtcp_svipc_inside_shmdt()",
    ],
    "SysV IPC disable-state behavior": [
        "--disable-all-plugins",
        "skipped or inert",
        "disabled built-in path",
        "disabled built-in path",
        "Util::getDmtcpArgs",
        "getUpdatedLdPreload",
    ],
    "SysV IPC stale artifact and handoff": [
        "libdmtcp_svipc.so",
        "hard blocker",
        "hard blocker",
        "PID/core overlap",
        "S06 PID-convergence handoff",
        "PID/core overlap",
        "direct SysV shm syscalls",
    ],
    "IPC source/header boundary": [
        "__d_libdir__libdmtcp_ipc_so_SOURCES",
        "45-entry",
        "Common IPC core",
        "Event descriptors and wrappers",
        "File, pty, and POSIX mqueue descriptors and wrappers",
        "Socket descriptors and wrappers",
        "SSH runtime pieces inside the IPC DSO",
        *IPC_DSO_SOURCE_FILES,
    ],
    "IPC helper boundary exclusions": [
        "Helper exclusions are explicit",
        "libssh.a",
        "src/plugin/ipc/ssh/util_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_sshd.cpp",
        "helper binaries",
        "helper binary targets",
        "not entries in the old IPC DSO source boundary",
    ],
    "IPC descriptor order and ABI": [
        "five explicit descriptor rows",
        "dmtcp_IpcSsh_PluginDescr()",
        "dmtcp_IpcEvent_PluginDescr()",
        "dmtcp_IpcFile_PluginDescr()",
        "dmtcp_IpcPty_PluginDescr()",
        "dmtcp_IpcSocket_PluginDescr()",
        "ssh,event,file,pty,socket",
        "socket,pty,file,event,ssh",
        "PluginManager::eventHook",
        "public external plugin ABI in include/dmtcp.h must not grow a new IPC descriptor API",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
    ],
    "IPC launcher exec and disable-state behavior": [
        "Launcher, exec, and disable-all semantics",
        "src/dmtcp_launch.cpp",
        "ENV_VAR_HIJACK_LIBS",
        "getUpdatedLdPreload",
        "no new Util::getDmtcpArgs restart flag",
        "--disable-all-plugins",
        "DMTCP_DISABLE_ALL_PLUGINS",
        "descriptor rows must be skipped or inert",
        "fast-pass to real functions or remain inert",
        "protected built-in enable-state check",
    ],
    "IPC stale DSO and wrapper blocker": [
        "stale libdmtcp_ipc.so",
        "hard blocker",
        "duplicate ownership",
        "duplicate initializer",
        "event wrappers",
        "poll",
        "__poll_chk",
        "pselect",
        "select",
        "signalfd",
        "eventfd",
        "epoll_create",
        "epoll_create1",
        "epoll_ctl",
        "epoll_wait",
        "inotify wrappers",
        "POSIX mqueue wrappers",
        "mq_open",
        "mq_close",
        "mq_notify",
        "mq_send",
        "mq_receive",
        "mq_timedsend",
        "mq_timedreceive",
        "socket wrappers",
        "socket",
        "connect",
        "bind",
        "listen",
        "accept",
        "accept4",
        "setsockopt",
        "getsockopt",
        "socketpair",
        "getaddrinfo",
        "getnameinfo",
        "gethostbyname",
        "gethostbyaddr",
        "stale artifact cleanup",
        "rollback or clean stale artifacts before PID",
    ],
    "IPC focused validation classes": [
        "file1",
        "file2",
        "file3",
        "shared-fd1",
        "shared-fd2",
        "stale-fd",
        "procfd1",
        "poll",
        "epoll1",
        "optional epoll2",
        "inotify1",
        "client-server",
        "seqpacket",
        "posix-mq1",
        "posix-mq2",
        "mq-notify status called out",
        "pty1",
        "pty2",
        "ssh1",
        "launch",
        "exec",
        "checkpoint/restart",
        "stale-artifact audit",
        "make check",
    ],
    "IPC static-initialization and S06 handoff": [
        "Static-initialization review",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/socket/socketconnection.cpp",
        "mq_notify",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "sigev_notify_thread_id",
        "S06 PID-convergence handoff",
    ],
}

GATE_PLAN_SECTION_MARKERS = {
    "Timer stage gate": [
        "timer descriptor accessor",
        "dmtcp_Timer_PluginDescr()",
        "libdmtcp_timer.so",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.h",
        "timer_create_sigev_thread()",
        "include/dmtcp.h",
        "--disable-all-plugins",
        "skipped or inert",
        "fast-pass or remain inert",
        "timer1",
        "clock",
        "timer1 and clock",
        "timer2 optional",
        "stale libdmtcp_timer.so",
        "hard blocker",
        "libdmtcp_pid.so",
        "intentional temporary collision",
        "S06 handoff marker",
        "clock_getcpuclockid",
        "Timer rollback point",
        "Timer stop/go gate",
        "Util::getDmtcpArgs",
        "getUpdatedLdPreload",
    ],
    "SysV IPC stage gate": [
        "dmtcp_Sysvipc_PluginDescr()",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/miscwrappers.cpp",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "src/plugin/pid/pidwrappers.h",
        "src/plugin/pid/pid_syscallsreal.c",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "libdmtcp_svipc.so",
        "sysv-shm1",
        "sysv-shm2",
        "sysv-sem",
        "sysv-msg",
        "IPC_PRIVATE",
        "keyed shared memory",
        "semaphore",
        "message-queue",
        "shmctl",
        "semctl",
        "msgctl",
        "dmtcp_svipc_inside_shmdt()",
        "skipped or inert",
        "disabled built-in path",
        "disabled built-in path",
        "libdmtcp_svipc.so",
        "hard blocker",
        "PID/core overlap",
        "S06 PID-convergence handoff",
        "direct SysV shm syscalls",
        "SysV IPC rollback point",
        "SysV IPC stop/go gate",
        "Util::getDmtcpArgs",
        "getUpdatedLdPreload",
    ],
    "IPC stage gate": [
        "__d_libdir__libdmtcp_ipc_so_SOURCES",
        "45-entry",
        *IPC_DSO_SOURCE_FILES,
        "Helper exclusions are explicit",
        "libssh.a",
        "src/plugin/ipc/ssh/util_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_sshd.cpp",
        "five explicit descriptor rows",
        "dmtcp_IpcSsh_PluginDescr()",
        "dmtcp_IpcEvent_PluginDescr()",
        "dmtcp_IpcFile_PluginDescr()",
        "dmtcp_IpcPty_PluginDescr()",
        "dmtcp_IpcSocket_PluginDescr()",
        "PluginManager::eventHook",
        "public external plugin ABI in include/dmtcp.h must not grow a new IPC descriptor API",
        "ssh,event,file,pty,socket",
        "socket,pty,file,event,ssh",
        "src/Makefile.am",
        "src/plugin/Makefile.am",
        "src/dmtcp_launch.cpp",
        "ENV_VAR_HIJACK_LIBS",
        "getUpdatedLdPreload",
        "no new Util::getDmtcpArgs restart flag",
        "--disable-all-plugins",
        "DMTCP_DISABLE_ALL_PLUGINS",
        "descriptor rows must be skipped or inert",
        "fast-pass to real functions or remain inert",
        "protected built-in enable-state check",
        "libdmtcp_ipc.so",
        "stale libdmtcp_ipc.so",
        "hard blocker",
        "event wrappers",
        "poll",
        "__poll_chk",
        "pselect",
        "select",
        "signalfd",
        "eventfd",
        "epoll_create",
        "epoll_create1",
        "epoll_ctl",
        "epoll_wait",
        "inotify wrappers",
        "POSIX mqueue wrappers",
        "mq_open",
        "mq_close",
        "mq_notify",
        "mq_send",
        "mq_receive",
        "mq_timedsend",
        "mq_timedreceive",
        "socket wrappers",
        "socket",
        "connect",
        "bind",
        "listen",
        "accept",
        "accept4",
        "setsockopt",
        "getsockopt",
        "socketpair",
        "getaddrinfo",
        "getnameinfo",
        "gethostbyname",
        "gethostbyaddr",
        "rollback or clean stale artifacts before PID",
        "file1",
        "file2",
        "file3",
        "shared-fd1",
        "shared-fd2",
        "stale-fd",
        "procfd1",
        "epoll1",
        "optional epoll2",
        "inotify1",
        "client-server",
        "seqpacket",
        "posix-mq1",
        "posix-mq2",
        "mq-notify status called out",
        "pty1",
        "pty2",
        "ssh1",
        "launch",
        "exec",
        "checkpoint/restart",
        "stale-artifact audit",
        "make check",
        "Static-initialization review",
        "src/plugin/ipc/ssh/ssh.cpp",
        "src/plugin/ipc/file/fileconnlist.cpp",
        "src/plugin/ipc/socket/socketconnection.cpp",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "sigev_notify_thread_id",
        "S06 PID-convergence handoff",
        "IPC rollback point",
        "IPC stop/go gate",
    ],
    "PID stage gate": [
        "dmtcp_Pid_PluginDescr()",
        "post-core",
        "pid last",
        "libdmtcp_pid.so",
        "PID rollback point",
        "PID stop/go gate",
    ],
    "Alloc stage gate": [
        *ALLOC_DOC_STAGE_MARKERS,
    ],
    "DL stage gate": [
        *DL_DOC_STAGE_MARKERS,
    ],
    "Duplicate initializer and wrapper avoidance gates": [
        "one libdmtcp-owned internal dmtcp_initialize_plugin node",
        "no DMTCP_DECL_PLUGIN left in consolidated built-ins",
        "no alloc descriptor row",
        "no dl descriptor row",
        "no old DSO plus new libdmtcp object exporting the same wrappers",
        "stale libdmtcp_timer.so",
        "libdmtcp_svipc.so",
        "hard blocker",
        "hard blocker",
        "libdmtcp_pid.so",
        "temporary PID collision",
        "PID/core overlap",
        "S06 PID-convergence handoff",
        "S06",
        "timer_create",
        "clock_getcpuclockid",
        "pid-last check",
        "duplicate-symbol audit",
        "libdmtcp_ipc.so",
        "stale libdmtcp_ipc.so",
        "event wrappers",
        "POSIX mqueue wrappers",
        "socket wrappers",
        "poll",
        "__poll_chk",
        "mq_notify",
        "mq_timedsend",
        "accept4",
        "getaddrinfo",
        "duplicate initializer",
        "sigev_notify_thread_id",
    ],
    "Rollback and stale artifact cleanup gates": [
        "stale artifact cleanup",
        "build lib directory",
        "install lib directory",
        "old DSO boundary",
        "accessor gate",
        "table row gate",
        "build gate",
        "symbol gate",
        "disable gate",
        "focused-test gate",
        "rollback point",
        "libdmtcp_ipc.so",
        "rollback or clean before running the next stage",
        "clean before running the next stage",
    ],
    "Focused tests and make check gates": [
        "make check",
        "self-test",
        "stage-map",
        "gate-plan",
        "full",
        "sysv-shm1",
        "sysv-shm2",
        "sysv-sem",
        "sysv-msg",
        "IPC_PRIVATE",
        "keyed and repeated/detached shmat",
        "semget",
        "semop",
        "semctl",
        "msgget",
        "msgsnd",
        "msgrcv",
        "msgctl",
        "dmtcp_svipc_inside_shmdt()",
        "libdmtcp_svipc.so",
        "dlopen1",
        "dlopen2",
        "timer1",
        "clock",
        "timer1 and clock",
        "timer2",
        "local test configuration enables it",
        "stale libdmtcp_timer.so",
        "hard blocker",
        "temporary libdmtcp_pid.so",
        "S06 handoff marker",
        "plugin-init",
        "ssh,event,file,pty,socket",
        "socket,pty,file,event,ssh",
        "file1",
        "file2",
        "file3",
        "shared-fd1",
        "shared-fd2",
        "stale-fd",
        "procfd1",
        "poll",
        "epoll1",
        "optional epoll2",
        "inotify1",
        "client-server",
        "seqpacket",
        "posix-mq1",
        "posix-mq2",
        "mq-notify status called out",
        "pty1",
        "pty2",
        "ssh1",
        "launch",
        "exec",
        "checkpoint/restart",
        "stale-artifact audit",
        "static-initialization",
    ],
}

S09_FINAL_VALIDATION_MARKERS = [
    "source-backed final M002 gate",
    "S02-S08 stage contracts",
    "test/verify-consolidation-preflight.py",
    "test/verify-consolidation-registration-plan.py",
    "test/verify-consolidation-design-plan.py",
    "test/verify-consolidation-migration-plan.py",
    "test/verify-consolidation-wrapper-tsan-plan.py",
    "localize roadmap drift before implementation starts",
    "missing final-gate rows",
    "stale-DSO omissions",
    "unsupported runtime-proof claims",
    "missing M004/M005 handoffs",
    "generated build files",
    "generated Autotools outputs",
    "src/Makefile.am",
    "src/plugin/Makefile.am",
    "clean rebuild",
    "focused test/autotest.py selections",
    "timer1",
    "clock",
    "timer2",
    "sysv-shm1",
    "sysv-shm2",
    "sysv-sem",
    "sysv-msg",
    "IPC file/event/socket/POSIX-mqueue/pty/SSH classes",
    "plugin-init",
    "gettid",
    "sched_test",
    "forkexec",
    "vfork1",
    "pthread_atfork1",
    "pthread_atfork2",
    "waitpid",
    "procfd1",
    "syscall-tester",
    "alloc startup/disable smoke",
    "dlopen1",
    "dlopen2",
    "make check",
    "duplicate wrapper and symbol audit",
    "duplicate dmtcp_initialize_plugin ownership",
    "consolidated descriptor rows",
    "wrapper-only alloc/DL invariants",
    "duplicate _real_* helper ownership",
    "Launch, exec, and checkpoint-restart propagation",
    "ENV_VAR_HIJACK_LIBS",
    "Util::getDmtcpArgs",
    "getUpdatedLdPreload",
    "checkpoint/restart command class",
    "Stale DSO audit for all six former internal libraries",
    "libdmtcp_timer.so",
    "libdmtcp_svipc.so",
    "libdmtcp_ipc.so",
    "libdmtcp_pid.so",
    "libdmtcp_alloc.so",
    "libdmtcp_dl.so",
    "build and install lib directories",
    "M004 loader and sanitizer stress handoff",
    "TSAN constructor ordering",
    "external dlsym(RTLD_NEXT)",
    "dl_iterate_phdr",
    "default-version lookup stress",
    "not proven by S09",
    "M004 owns loader and sanitizer stress results",
    "M005 optional and external plugin handoff",
    "Optional and external plugin compatibility",
    "separate shared objects",
    "DMTCP_DECL_PLUGIN",
    "dmtcp_initialize_plugin",
    "NEXT_FNC(dmtcp_initialize_plugin)",
    "dmtcp_register_plugin",
    "M005 owns external plugin compatibility",
]

FULL_MARKER_GROUPS = {
    "requirements coverage": [
        "R001",
        "R003",
        "R004",
        "R005",
        "R006",
        "R007",
        "R008",
        "R009",
        "R014",
        "R006 central built-in ownership model",
        "static design proof",
        "runtime acceptance gates",
        "Timer focused runtime gate",
        "timer1 and clock",
        "environment-enabled",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.h",
        "stale libdmtcp_timer.so",
        "SysV IPC focused runtime gate",
        "sysv-shm1",
        "sysv-shm2",
        "sysv-sem",
        "sysv-msg",
        "IPC_PRIVATE",
        "dmtcp_svipc_inside_shmdt()",
        "libdmtcp_svipc.so",
        "PID/core overlap",
        "libdmtcp_pid.so",
        "clock_getcpuclockid",
        "S06 handoff marker",
        "IPC and PID focused runtime gate",
        "__d_libdir__libdmtcp_ipc_so_SOURCES",
        "libdmtcp_ipc.so",
        "DMTCP_DISABLE_ALL_PLUGINS",
        "stale IPC DSO ownership",
        "helper-boundary drift",
        "static-initialization review",
        "src/plugin/ipc/ssh/util_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_sshd.cpp",
        "src/plugin/ipc/socket/socketconnection.cpp",
        "mq_notify",
        "sigev_notify_thread_id",
        "S06 PID-convergence handoff",
    ],
    "validation matrix runtime command classes": [
        "M003",
        "M004",
        "build regeneration where needed",
        "clean rebuild",
        "duplicate-symbol audit",
        "stale installed-DSO audit",
        "focused test/autotest.py selections",
        "full make check",
        "external plugin smoke",
        "deferred TSAN/dlsym/dl_iterate_phdr stress",
    ],
    "DL focused runtime and M004 handoff gate": [
        *DL_DOC_STAGE_MARKERS,
    ],
    "quality gate outcomes": [
        "Q3",
        "Q4",
        "Q5",
        "Q6",
        "Q7",
        "Pass",
        "Flag for runtime follow-up",
    ],
    "negative checks": [
        "wrong stage order",
        "missing pid-last",
        "alloc/dl-no-descriptor",
        "missing disable-all",
        "missing stale-artifact cleanup",
        "stale timer/SysV DSO ownership",
        "stale IPC DSO ownership",
        "helper-boundary drift",
        "missing IPC static-initialization review",
        "unsupported mq-notify proof",
        "source-anchor drift",
        "unsupported runtime proof",
        "missing rollback point",
        "template fill-in",
        "hidden path citation",
    ],
    "S04 handoff": [
        "S04",
        "M003",
        "M004",
        "ABI preservation",
        "event-order preservation",
        "wrapper-only disable behavior",
        "static-initialization safety",
        "incorporate this appendix",
        "rather than re-open the roadmap",
        "implementation evidence contradicts",
        "timer source/build boundary",
        "required timer timer1/clock focused tests",
        "stale timer DSO blocker",
        "temporary PID-collision handoff",
        "src/plugin/timer/timer_create.cpp",
        "src/plugin/timer/timerlist.h",
        "src/plugin/timer/timerwrappers.h",
        "test/clock.c",
        "timer2 optional/environment-enabled",
        "--disable-all-plugins",
        "skip/inert timer behavior",
        "SysV IPC carry-forward",
        "src/plugin/svipc/sysvipc.cpp",
        "src/plugin/svipc/sysvipc.h",
        "src/plugin/svipc/sysvipcwrappers.cpp",
        "src/plugin/svipc/sysvipcwrappers.h",
        "src/miscwrappers.cpp",
        "test/sysv-shm1.c",
        "test/sysv-shm2.c",
        "test/sysv-sem.c",
        "test/sysv-msg.c",
        "IPC_PRIVATE",
        "keyed shared memory",
        "control calls",
        "dmtcp_svipc_inside_shmdt()",
        "libdmtcp_svipc.so",
        "hard blocker",
        "PID/core overlap",
        "S06",
        "libdmtcp_pid.so",
        "timer_create/clock_getcpuclockid",
        "IPC carry-forward",
        "__d_libdir__libdmtcp_ipc_so_SOURCES",
        "45 current IPC DSO entries",
        "dmtcp_IpcSsh_PluginDescr()",
        "dmtcp_IpcEvent_PluginDescr()",
        "dmtcp_IpcFile_PluginDescr()",
        "dmtcp_IpcPty_PluginDescr()",
        "dmtcp_IpcSocket_PluginDescr()",
        "ssh,event,file,pty,socket",
        "socket,pty,file,event,ssh",
        "disable-all descriptor skip/inert",
        "wrapper fast-pass or inert behavior",
        "stale libdmtcp_ipc.so",
        "event/POSIX-mqueue/socket wrapper ownership",
        "IPC helper-boundary exclusions",
        "src/plugin/ipc/ssh/util_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_ssh.cpp",
        "src/plugin/ipc/ssh/dmtcp_sshd.cpp",
        "static/global state",
        "src/plugin/ipc/socket/socketconnection.cpp",
        "focused file, event, socket, POSIX mqueue, pty, SSH",
        "POSIX mq_notify PID translation convergence",
    ],
}

FULL_SECTION_MARKERS = {
    **GATE_PLAN_SECTION_MARKERS,
    "S09 final validation and handoff gate": S09_FINAL_VALIDATION_MARKERS,
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
    "PLACEHOLDER": re.compile(r"\bPLACEHOLDER\b"),
    "template variable": re.compile(r"{{[^}\n]+}}"),
    "markdown fill-in marker": re.compile(
        r"\[(?:fill in|todo|tbd|placeholder|replace)[^\]\n]*\]",
        re.IGNORECASE,
    ),
}

HIDDEN_SENTINELS = {".gsd", ".planning", ".audits"}
STAGE_ORDER = ("timer", "svipc", "ipc", "pid", "alloc", "dl")

RUNTIME_CLAIM_TERMS = (
    "TSAN",
    "dlsym",
    "RTLD_NEXT",
    "dl_iterate_phdr",
    "default version",
    "default-version",
    "version lookup",
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
    "without",
    "unproven",
    "unsupported",
    "risk",
    "boundary",
    "M003",
    "M004",
    "M005",
)

S09_RUNTIME_PROOF_CLAIM_TERMS = (
    "build",
    "rebuild",
    "focused test",
    "focused tests",
    "make check",
    "TSAN",
    "dlsym",
    "RTLD_NEXT",
    "dl_iterate_phdr",
    "default-version",
    "external plugin",
    "optional plugin",
    "compatibility",
)
S09_RUNTIME_PROOF_WORDS = (
    *RUNTIME_PROOF_WORDS,
    "ran",
    "run",
    "completed",
)
S09_RUNTIME_SAFE_QUALIFIERS = (
    *SAFE_RUNTIME_QUALIFIERS,
    "does not claim",
    "not claim",
    "names the gate only",
    "proof class",
    "proof boundary",
    "proof belongs",
    "owns",
    "owned by",
    "expected to",
    "required command classes",
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
        "src/dmtcp_launch.cpp",
        (
            "pluginInfo[]",
            "ENV_VAR_HIJACK_LIBS",
            "ENV_VAR_ALLOC_PLUGIN",
            "ENV_VAR_DL_PLUGIN",
            "--disable-all-plugins",
            "disableAllPlugins",
            "libdmtcp_pathvirt.so",
            "ENV_VAR_DISABLE_ALL_PLUGINS",
            "libdmtcp.so",
            "setenv(ENV_VAR_HIJACK_LIBS, preloadLibs.c_str(), 1);",
        ),
    ),
    SourceAnchor(
        "src/util_exec.cpp",
        (
            "Util::getDmtcpArgs",
            "ENV_VAR_ALLOC_PLUGIN",
            "ENV_VAR_DL_PLUGIN",
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
            "ENV_VAR_HIJACK_LIBS_M32",
            "ENV_VAR_ORIG_LD_PRELOAD",
        ),
    ),
    SourceAnchor(
        "src/miscwrappers.cpp",
        (
            "case SYS_shmget:",
            "case SYS_shmat:",
            "case SYS_shmdt:",
            "dmtcp_svipc_inside_shmdt != NULL",
            "dmtcp_svipc_inside_shmdt())",
            "ret = _real_syscall(SYS_shmdt, shmaddr);",
            "case SYS_shmctl:",
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
            "DMTCP_EVENT_RESUME",
            "DMTCP_EVENT_RESTART",
        ),
    ),
    SourceAnchor(
        "src/pluginmanager.h",
        (
            "class PluginManager",
            "registerPlugin",
            "eventHook",
            "vector<PluginInfo *>pluginInfos",
        ),
    ),
    SourceAnchor(
        "include/dmtcp.h",
        (
            "void dmtcp_initialize_plugin(void) __attribute((weak))",
            "#define DMTCP_DECL_PLUGIN",
            "NEXT_FNC(dmtcp_initialize_plugin)",
            "DmtcpPluginDescriptor_t",
        ),
    ),
    SourceAnchor(
        "include/util.h",
        (
            "EXTERNC bool dmtcp_svipc_inside_shmdt(void) __attribute__((weak));",
            "bool isSysVShmArea(const ProcMapsArea &area);",
            "char **getDmtcpArgs();",
        ),
    ),
    SourceAnchor(
        "src/Makefile.am",
        (
            "__d_libdir__libdmtcp_so_SOURCES",
            "libdmtcpinternal_a_SOURCES",
            "pluginmanager.cpp",
            "execwrappers.cpp",
            "util_exec.cpp",
            "plugin/svipc/sysvipc.cpp",
            "plugin/svipc/sysvipcwrappers.cpp",
            "plugin/ipc/ipc.cpp",
            "plugin/ipc/file/posixipcwrappers.cpp",
            "plugin/pid/pid.cpp",
            "plugin/pid/pidwrappers.cpp",
            "plugin/pid/pid_miscwrappers.cpp",
            "plugin/alloc/alloc.h",
            "plugin/alloc/mallocwrappers.cpp",
            "plugin/alloc/mmapwrappers.cpp",
            "plugin/dl/dlwrappers.cpp",
        ),
    ),
    SourceAnchor(
        "src/plugin/Makefile.am",
        (
            "noinst_LIBRARIES += libssh.a",
            "libssh_a_SOURCES",
            "ipc/ssh/util_ssh.cpp",
            "bin_PROGRAMS += $(d_bindir)/dmtcp_ssh",
            "__d_bindir__dmtcp_ssh_SOURCES",
            "ipc/ssh/dmtcp_ssh.cpp",
            "bin_PROGRAMS += $(d_bindir)/dmtcp_sshd",
            "__d_bindir__dmtcp_sshd_SOURCES",
            "ipc/ssh/dmtcp_sshd.cpp",
            "The PID plugin is built into the core DMTCP library.",
            "Timer wrappers and the timer descriptor are now built into libdmtcp.so",
        ),
    ),
    SourceAnchor(
        "src/plugin/timer/timer_create.cpp",
        (
            "#include \"timerwrappers.h\"",
            "active_timer_sigev_thread",
            "timer_create_reset_on_fork",
            "timer_create_sigev_thread(clockid_t clock_id",
            "_real_timer_create(clock_id, sevOut, timerid)",
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
        "src/plugin/timer/timerlist.h",
        (
            "class TimerList",
            "on_timer_create",
            "on_clock_getcpuclockid",
            "VirtualIdTable<timer_t>",
            "VirtualIdTable<clockid_t>",
        ),
    ),
    SourceAnchor(
        "src/plugin/timer/timerwrappers.cpp",
        (
            "timer_create(clockid_t clockid",
            "timer_create_sigev_thread(realClockId, sevpForCreate, &realId,",
            "timer_delete(timer_t timerid)",
            "clock_getcpuclockid(pid_t pid",
        ),
    ),
    SourceAnchor(
        "src/plugin/timer/timerwrappers.h",
        (
            "# define _real_timer_create",
            "# define _real_clock_getcpuclockid",
            "timer_create_sigev_thread(clockid_t clock_id",
        ),
    ),
    SourceAnchor(
        "src/plugin/svipc/sysvipc.cpp",
        (
            "sysvipc_event_hook",
            "DmtcpPluginDescriptor_t sysvipcPlugin",
            "dmtcp_SysVIPC_PluginDescr()",
            "static SysVShm *sysvShmInst",
            "static SysVSem *sysvSemInst",
            "static SysVMsq *sysvMsqInst",
            "SysVShm::instance().preCheckpoint();",
            "SysVSem::instance().preCheckpoint();",
            "SysVMsq::instance().preCheckpoint();",
            "SysVShm::instance().refill();",
            "SysVSem::instance().refill();",
            "SysVMsq::instance().refill();",
            "SysVSem::on_semctl(int semid, int semnum, int cmd, union semun arg)",
            "SysVMsq::on_msgctl(int msqid, int cmd, struct msqid_ds *buf)",
            "_real_shmctl(_realId, IPC_STAT",
            "_real_semctl(_realId, 0, GETPID)",
            "_real_msgctl(_realId, IPC_STAT",
        ),
    ),
    SourceAnchor(
        "src/plugin/svipc/sysvipc.h",
        (
            "#include \"sysvipcwrappers.h\"",
            "REAL_TO_VIRTUAL_SHM_ID",
            "REAL_TO_VIRTUAL_SHM_KEY",
            "REAL_TO_VIRTUAL_SEM_ID",
            "REAL_TO_VIRTUAL_MSQ_ID",
            "class SysVShm",
            "class SysVSem",
            "class SysVMsq",
            "virtual void on_semctl(int semid, int semnum, int cmd, union semun arg)",
            "virtual void on_msgctl(int msqid, int cmd, struct msqid_ds *buf)",
            "class Semaphore",
            "class MsgQueue",
            "void refill();",
        ),
    ),
    SourceAnchor(
        "src/plugin/svipc/sysvipcwrappers.cpp",
        (
            "shmget(key_t key",
            "shmat(int shmid",
            "dmtcp_svipc_inside_shmdt()",
            "shmdt(const void *shmaddr)",
            "shmctl(int shmid",
            "semget(key_t key",
            "semop(int semid",
            "semtimedop(int semid",
            "semctl(int semid",
            "msgget(key_t key",
            "msgsnd(int msqid",
            "msgrcv(int msqid",
            "msgctl(int msqid",
        ),
    ),
    SourceAnchor(
        "src/plugin/svipc/sysvipcwrappers.h",
        (
            "# define _real_shmget",
            "# define _real_shmat",
            "# define _real_shmdt",
            "# define _real_shmctl",
            "# define _real_semget",
            "# define _real_semctl",
            "# define _real_semop",
            "# define _real_semtimedop",
            "# define _real_msgget",
            "# define _real_msgctl",
            "# define _real_msgsnd",
            "# define _real_msgrcv",
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
            "Util::getDmtcpArgs",
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
        "src/plugin/ipc/event/eventwrappers.cpp",
        (
            "poll(struct pollfd *fds",
            "__poll_chk(struct pollfd *fds",
            "pselect(int nfds",
            "select(int nfds",
            "signalfd(int fd",
            "eventfd(EVENTFD_VAL_TYPE initval",
            "epoll_create(int size)",
            "epoll_create1(int flags)",
            "epoll_ctl(int epfd",
            "epoll_wait(int epfd",
            "inotify_init()",
            "inotify_init1(int flags)",
            "inotify_add_watch(int fd",
            "inotify_rm_watch(int fd",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        (
            "mq_open(const char *name",
            "mq_close(mqd_t mqdes)",
            "mq_notify(mqd_t mqdes",
            "mq_send(mqd_t mqdes",
            "mq_receive(mqd_t mqdes",
            "mq_timedsend(mqd_t mqdes",
            "mq_timedreceive(mqd_t mqdes",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/socket/socketwrappers.cpp",
        (
            "socket(int domain",
            "connect(int sockfd",
            "bind(int sockfd",
            "listen(int sockfd",
            "accept(int sockfd",
            "accept4(int sockfd",
            "setsockopt(int sockfd",
            "getsockopt(int sockfd",
            "socketpair(int d",
            "getaddrinfo(const char *node",
            "getnameinfo(const struct sockaddr *sa",
            "gethostbyname(const char *name)",
            "gethostbyaddr(const void *addr",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/ssh/util_ssh.cpp",
        (
            "client_loop(int ssh_stdin, int ssh_stdout, int ssh_stderr, int sock)",
            "poll((struct pollfd *)&fds[0], fds.size(), 10 * 1000)",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/ssh/dmtcp_ssh.cpp",
        (
            "openListenSocket()",
            "socket(AF_INET, SOCK_STREAM, 0)",
            "bind(sock, (struct sockaddr *)&saddr, sizeof saddr)",
            "listen(sock, 1)",
            "accept(listenSock, NULL, NULL)",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/ssh/dmtcp_sshd.cpp",
        (
            "connectToRemotePeer(char *host, int port)",
            "socket(AF_INET, SOCK_STREAM, 0)",
            "connect(sock, (sockaddr *)&saddr, sizeof saddr)",
            "ENV_VAR_REMOTE_SHELL_CMD",
        ),
    ),
    SourceAnchor(
        "src/plugin/ipc/socket/socketconnection.cpp",
        (
            "#include \"socketconnection.h\"",
            "_real_socket(_sockDomain, _sockType, _sockProtocol)",
            "_real_bind(_fds[0]",
            "_real_listen(_fds[0]",
            "_real_setsockopt(_fds[0]",
        ),
    ),
    SourceAnchor(
        "src/syscallsreal.c",
        (
            *PID_DUPLICATE_REAL_HELPERS,
            "IPC64_FLAG",
            "cmd | IPC64_FLAG",
        ),
    ),
    SourceAnchor(
        "src/constants.h",
        (
            "ENV_VAR_HIJACK_LIBS",
            "ENV_VAR_HIJACK_LIBS_M32",
            "ENV_VAR_VIRTUAL_PID",
            "ENV_VAR_DISABLE_ALL_PLUGINS",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/glibc_pthread.cpp",
        (
            "#include \"glibc_pthread.h\"",
            "dmtcp_pthread_get_addrs(pthread_t th)",
            "dmtcp_pthread_get_tid(pthread_t th)",
            "dmtcp_pthread_set_schedparam(pthread_t th",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/glibc_pthread.h",
        (
            "DMTCP_GLIBC_PTHREAD_H",
            "USE_VIRTUAL_TID_LIBC_STRUCT_PTHREAD",
            "struct libc_pthread_addr",
            "dmtcp_pthread_get_tid(pthread_t th)",
            "dmtcp_pthread_set_schedparam(pthread_t thread",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pid.cpp",
        (
            "pid_event_hook",
            "DmtcpPluginDescriptor_t pidPlugin",
            "dmtcp_Pid_PluginDescr()",
            "DMTCP_EVENT_RESTART",
            "DMTCP_EVENT_RESUME",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pid_filewrappers.cpp",
        (
            "ioctl(int d, unsigned long int request, ...)",
            "send_sigwinch",
            "_real_ioctl(d, request, win)",
            "kill(getpid(), SIGWINCH)",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pid.h",
        ("#include \"dmtcp.h\"",),
    ),
    SourceAnchor(
        "src/plugin/pid/pid_miscwrappers.cpp",
        (
            "static pid_t childVirtualPid",
            "pidVirt_atfork_prepare()",
            "pidVirt_atfork_child()",
            "SharedData::setPidMap(childVirtualPid, realPid)",
            "pidVirt_vfork_prepare()",
            "vfork_saved_virtPidTableInst",
            "Wait/fcntl/syscall wrappers are composed in src/miscwrappers.cpp",
            "Timer, SysV IPC, and POSIX mqueue PID translations",
            "process_vm_readv(pid_t pid",
            "process_vm_writev(pid_t pid",
            "pid_real_process_vm_readv(realPid",
            "pid_real_process_vm_writev(realPid",
            "DMTCP_PLUGIN_DISABLE_CKPT();",
            "DMTCP_PLUGIN_ENABLE_CKPT();",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pidwrappers.cpp",
        (
            "getpid()",
            "getppid()",
            "kill(pid_t pid, int sig)",
            "pid_real_kill(currPid, sig)",
            "tcsetpgrp(int fd, pid_t pgrp)",
            "pthread_cancel (pthread_t th)",
            "wait-family and fcntl wrappers are composed in src/miscwrappers.cpp",
            "src/wrappers.cpp",
            "VIRTUAL_TO_REAL_PID",
            "set_tid_address",
            "rt_sigqueueinfo",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pidwrappers.h",
        (
            "ENV_VAR_VIRTUAL_PID",
            "wait/fcntl/syscall entries remain in this lookup list",
            "MACRO(waitid)",
            "MACRO(wait4)",
            "MACRO(fcntl)",
            "int pid_real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);",
            "int pid_real_fcntl(int fd, int cmd, void *arg);",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/pid_syscallsreal.c",
        (
            "pid_real_func_addr",
            "pid_real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)",
            "REAL_FUNC_PASSTHROUGH(waitid) (idtype, id, infop, options);",
            "pid_real_wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage)",
            "REAL_FUNC_PASSTHROUGH(wait4) (pid, status, options, rusage);",
            "pid_real_fcntl(int fd, int cmd, void *arg)",
            "REAL_FUNC_PASSTHROUGH(fcntl) (fd, cmd, arg);",
            "pid_real_process_vm_readv(pid_t pid,",
            "pid_real_process_vm_writev(pid_t pid,",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/sched_wrappers.cpp",
        (
            "pthread_getaffinity_np (pthread_t th",
            "sched_getaffinity(dmtcp_pthread_get_tid(th)",
            "pthread_setaffinity_np(pthread_t th",
            "sched_setaffinity(dmtcp_pthread_get_tid(th)",
            "pthread_getschedparam (pthread_t th",
            "pthread_setschedparam (pthread_t th",
            "dmtcp_pthread_get_addrs(th)",
            "rt_sigqueueinfo",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/virtualpidtable.cpp",
        (
            "VirtualPidTable *virtPidTableInst = NULL",
            "VirtualPidTable::instance()",
            "VirtualPidTable::resetPidPpid()",
            "VirtualPidTable::resetTid(pid_t tid)",
            "VirtualPidTable::gettid()",
            "VirtualPidTable::updateMapping(pid_t virtualId, pid_t realId)",
            "VirtualPidTable::resetOnFork()",
        ),
    ),
    SourceAnchor(
        "src/plugin/pid/virtualpidtable.h",
        (
            "REAL_TO_VIRTUAL_PID(pid)",
            "VIRTUAL_TO_REAL_PID(pid)",
            "class VirtualPidTable : public VirtualIdTable<pid_t>",
            "static VirtualPidTable &instance();",
            "static pid_t gettid();",
            "void updateMapping(pid_t virtualId, pid_t realId);",
        ),
    ),
    SourceAnchor(
        "src/plugin/alloc/alloc.h",
        (
            "#ifndef ALLOC_H",
            "#include \"dmtcp.h\"",
            "extern \"C\" void *__libc_memalign(size_t boundary, size_t size);",
            "#define _real_malloc         NEXT_FNC(malloc)",
            "#define _real_calloc         NEXT_FNC(calloc)",
            "#define _real_valloc         NEXT_FNC(valloc)",
            "#define _real_realloc        NEXT_FNC(realloc)",
            "#define _real_free           NEXT_FNC(free)",
            "#define _real_memalign       NEXT_FNC(memalign)",
            "#define _real_posix_memalign NEXT_FNC(posix_memalign)",
            "#define _real_libc_memalign  NEXT_FNC(__libc_memalign)",
            "#define _real_mmap           NEXT_FNC(mmap)",
            "#define _real_mmap64         NEXT_FNC(mmap64)",
            "#define _real_munmap         NEXT_FNC(munmap)",
            "#define _real_mremap         NEXT_FNC(mremap)",
            "#endif // ALLOC_H",
        ),
    ),
    SourceAnchor(
        "src/plugin/alloc/mallocwrappers.cpp",
        (
            "static int dmtcpAllocEnabledCache = -1;",
            "EXTERNC LIB_PRIVATE int\ndmtcp_alloc_enabled()",
            "getenv(ENV_VAR_DISABLE_ALL_PLUGINS)",
            "getenv(ENV_VAR_ALLOC_PLUGIN)",
            "__atomic_store_n(&dmtcpAllocEnabledCache, enabled, __ATOMIC_RELAXED);",
            "extern \"C\" void *calloc(size_t nmemb, size_t size)",
            "void *retval = _real_calloc(nmemb, size);",
            "extern \"C\" void *malloc(size_t size)",
            "void *retval = _real_malloc(size);",
            "extern \"C\" void *memalign(size_t boundary, size_t size)",
            "void *retval = _real_memalign(boundary, size);",
            "posix_memalign(void **memptr, size_t alignment, size_t size)",
            "int retval = _real_posix_memalign(memptr, alignment, size);",
            "extern \"C\" void *valloc(size_t size)",
            "void *retval = _real_valloc(size);",
            "free(void *ptr)",
            "_real_free(ptr);",
            "extern \"C\" void *realloc(void *ptr, size_t size)",
            "void *retval = _real_realloc(ptr, size);",
            "DMTCP_PLUGIN_DISABLE_CKPT();",
            "DMTCP_PLUGIN_ENABLE_CKPT();",
        ),
    ),
    SourceAnchor(
        "src/plugin/alloc/mmapwrappers.cpp",
        (
            "// #define ENABLE_MMAP_WRAPPERS",
            "#ifdef ENABLE_MMAP_WRAPPERS",
            "extern \"C\" void *mmap(void *addr, size_t length, int prot, int flags,",
            "void *retval = _real_mmap(addr, length, prot, flags, fd, offset);",
            "extern \"C\" void *mmap64(void *addr, size_t length, int prot, int flags,",
            "void *retval = _real_mmap64(addr, length, prot, flags, fd, offset);",
            "munmap(void *addr, size_t length)",
            "int retval = _real_munmap(addr, length);",
            "# if __GLIBC_PREREQ(2, 4)",
            "extern \"C\" void *mremap(void *old_address, size_t old_size,",
            "if (flags & MREMAP_FIXED) {",
            "retval = _real_mremap(old_address, old_size, new_size, flags, new_address);",
            "retval = _real_mremap(old_address, old_size, new_size, flags);",
            "#endif // ENABLE_MMAP_WRAPPERS",
        ),
    ),
    SourceAnchor(
        "src/plugin/dl/dlwrappers.cpp",
        (
            "#define _real_dlopen  NEXT_FNC(dlopen)",
            "#define _real_dlclose NEXT_FNC(dlclose)",
            "void\ngetRpathRunPath(void *caller, char *rpathStr, char *runpathStr)",
            "static void *\ndlopen_try_paths(const char *filename, int flag, const char *paths)",
            "extern \"C\"\nvoid *dlopen(const char *filename, int flag)",
            "LibDlWrapperLock wrapperLock;",
            "return _real_dlopen(filename, flag);",
            "extern \"C\"\nint\ndlclose(void *handle)",
            "return _real_dlclose(handle);",
        ),
    ),
    SourceAnchor(
        "test/autotest.py",
        (
            "runTest(\"sysv-shm1\"",
            "runTest(\"sysv-shm2\"",
            "runTest(\"sysv-sem\"",
            "runTest(\"sysv-msg\"",
            "runTest(\"timer1\"",
            "runTest(\"clock\"",
            "runTest(\"dlopen1\"",
            "runTest(\"dlopen2\"",
            "runTest(\"plugin-init\"",
            "runTest(\"gettid\"",
            "runTest(\"sched_test\"",
            "runTest(\"forkexec\"",
            "runTest(\"pthread_atfork1\"",
            "runTest(\"pthread_atfork2\"",
            "runTest(\"waitpid\"",
            "runTest(\"syscall-tester\"",
            "runTest(\"posix-mq1\"",
            "runTest(\"posix-mq2\"",
        ),
    ),
    SourceAnchor(
        "test/sysv-shm1.c",
        (
            "IPC_PRIVATE",
            "assert(shmid[0] != shmid[1]);",
        ),
    ),
    SourceAnchor(
        "test/sysv-shm2.c",
        (
            "create_named_shm(size_t size)",
            "shmget(key, size, IPC_CREAT | 0666)",
            "munmap(addr2, SIZE)",
        ),
    ),
    SourceAnchor(
        "test/sysv-sem.c",
        (
            "semget((key_t)9977, 1, IPC_CREAT | 0666)",
            "semop(semid, &sops, 1)",
        ),
    ),
    SourceAnchor(
        "test/sysv-msg.c",
        (
            "msgget((key_t)9977, IPC_CREAT | 0666)",
            "msgsnd(msqid",
            "msgrcv(msqid",
        ),
    ),
    SourceAnchor(
        "test/timer1.c",
        (
            "timer_create(CLOCKID",
            "timer_settime(timerid1",
            "timer_delete",
        ),
    ),
    SourceAnchor(
        "test/timer2.c",
        (
            "SIGEV_THREAD",
            "timer_create(CLOCK_REALTIME",
            "timer_settime(timer_id",
        ),
    ),
    SourceAnchor(
        "test/clock.c",
        (
            "clock_getcpuclockid(getpid(), &id)",
            "clock_gettime(id, &ts)",
            "CPU-time clock",
        ),
    ),
    SourceAnchor(
        "test/dlopen1.c",
        (
            "dlopen(\"test/libdlopen-lib1.so\"",
            "dlclose(handle)",
            "dlsym(handle, \"fnc\")",
        ),
    ),
    SourceAnchor(
        "test/dlopen2.cpp",
        (
            "dlopen(\"test/libdlopen-lib3.so\"",
            "dlclose(handle)",
            "dlsym(handle, \"fnc\")",
        ),
    ),
    SourceAnchor(
        "test/plugin-init.cpp",
        (
            "DMTCP_EVENT_INIT",
            "std::cout",
            "wrapper then tries to initialize DMTCP",
        ),
    ),
    SourceAnchor(
        "test/gettid.c",
        (
            "syscall(SYS_gettid)",
            "gettid() != getpid()",
        ),
    ),
    SourceAnchor(
        "test/sched_test.c",
        (
            "pid_t ret = fork();",
            "sched_setaffinity(ret",
            "sched_getaffinity(ret",
        ),
    ),
    SourceAnchor(
        "test/forkexec.c",
        ("fork() == 0",),
    ),
    SourceAnchor(
        "test/vfork1.c",
        (
            "vfork()",
            "waitpid(sn_popen_pid",
        ),
    ),
    SourceAnchor(
        "test/pthread_atfork1.c",
        (
            "pthread_atfork(prepare, parent, child)",
            "int childpid = fork();",
            "waitpid(childpid, NULL, 0);",
        ),
    ),
    SourceAnchor(
        "test/pthread_atfork2.c",
        (
            "pthread_create(&thread, NULL, busy_loop, NULL);",
            "pthread_atfork(prepare, parent, child)",
            "int childpid = fork();",
        ),
    ),
    SourceAnchor(
        "test/waitpid.c",
        (
            "int childpid = fork();",
            "rc = waitpid(childpid, NULL, 0);",
        ),
    ),
    SourceAnchor(
        "test/syscall-tester.c",
        (
            "syscall(SYS_getpid)",
            "syscall(SYS_gettid)",
            "BasicGettid",
        ),
    ),
]

FORBIDDEN_SOURCE_MARKERS = [
    ForbiddenSourceMarker(
        "src/dmtcp_launch.cpp",
        "alloc and DL wrappers are built into libdmtcp.so and must not be emitted as separate launcher DSOs",
        ("libdmtcp_alloc.so", "libdmtcp_dl.so"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/Makefile.am",
        "alloc and DL wrappers are no longer owned by src/plugin/Makefile.am DSO rows",
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
        "src/plugin/alloc/alloc.h",
        "alloc header must remain wrapper-only and must not declare plugin descriptor plumbing",
        ("DmtcpPluginDescriptor_t", "dmtcp_initialize_plugin", "DMTCP_DECL_PLUGIN", "dmtcp_Alloc_PluginDescr"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/alloc/mallocwrappers.cpp",
        "alloc must remain wrapper-only and must not define a plugin descriptor",
        ("DmtcpPluginDescriptor_t", "dmtcp_initialize_plugin", "DMTCP_DECL_PLUGIN", "dmtcp_Alloc_PluginDescr"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/alloc/mmapwrappers.cpp",
        "alloc mmap wrappers must remain wrapper-only and must not define a plugin descriptor",
        ("DmtcpPluginDescriptor_t", "dmtcp_initialize_plugin", "DMTCP_DECL_PLUGIN", "dmtcp_Alloc_PluginDescr"),
    ),
    ForbiddenSourceMarker(
        "src/pluginmanager.cpp",
        "alloc must not gain a PluginManager descriptor accessor or event-table row",
        ("dmtcp_Alloc_PluginDescr", "allocPlugin", "Alloc_PluginDescr"),
    ),
    ForbiddenSourceMarker(
        "src/pluginmanager.cpp",
        "dl must not gain a PluginManager descriptor accessor or event-table row",
        ("dmtcp_Dl_PluginDescr", "dmtcp_DL_PluginDescr", "Dl_PluginDescr", "DL_PluginDescr"),
    ),
    ForbiddenSourceMarker(
        "src/plugin/dl/dlwrappers.cpp",
        "dl must remain wrapper-only and must not define a plugin descriptor",
        (
            "DmtcpPluginDescriptor_t",
            "dmtcp_initialize_plugin",
            "DMTCP_DECL_PLUGIN",
            "dmtcp_Dl_PluginDescr",
            "dmtcp_DL_PluginDescr",
        ),
    ),
    ForbiddenSourceMarker(
        "include/dmtcp.h",
        "descriptor accessors for consolidated built-ins must remain internal, not public ABI",
        (
            "dmtcp_Timer_PluginDescr",
            "dmtcp_Sysvipc_PluginDescr",
            "dmtcp_IpcSsh_PluginDescr",
            "dmtcp_Pid_PluginDescr",
            "dmtcp_Alloc_PluginDescr",
            "dmtcp_Dl_PluginDescr",
            "dmtcp_DL_PluginDescr",
        ),
    ),
    ForbiddenSourceMarker(
        "src/util_exec.cpp",
        "PID consolidation must not add a PID-specific restart argument without later design evidence",
        ("--disable-pid-plugin", "ENV_VAR_PID_PLUGIN", "DMTCP_PID_PLUGIN"),
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
        "src/Makefile.am",
        "timer sources are linked into libdmtcp.so",
        (
            "plugin/timer/timer_create.cpp",
            "plugin/timer/timerlist.cpp",
            "plugin/timer/timerlist.h",
            "plugin/timer/timerwrappers.cpp",
            "plugin/timer/timerwrappers.h",
        ),
    ),
    OrderedSourceAnchor(
        "src/pluginmanager.cpp",
        "current libdmtcp built-in order with PID last",
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
                "do not satisfy migration checks from generated planning artifacts."
            )


def real_helper_mentions(path: str) -> set[str]:
    text = (REPO_ROOT / path).read_text(encoding="utf-8", errors="replace")
    return set(re.findall(r"\b(_real_[A-Za-z0-9_]+)\s*\(", text))


def validate_pid_duplicate_real_helpers(errors: list[str]) -> None:
    core_path = "src/syscallsreal.c"
    pid_path = "src/plugin/pid/pid_syscallsreal.c"
    for path in (core_path, pid_path):
        if not repo_file_exists(path):
            errors.append(f"Missing source anchor file for PID duplicate-helper scan: {path}")
            return
    actual = sorted(real_helper_mentions(core_path) & real_helper_mentions(pid_path))
    if actual != PID_DUPLICATE_REAL_HELPERS:
        errors.append(
            "PID duplicate _real_* helper collision set drifted: "
            f"expected {', '.join(PID_DUPLICATE_REAL_HELPERS)}, "
            f"found {', '.join(actual) or 'none'}"
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

    validate_pid_duplicate_real_helpers(errors)


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


def find_unsupported_migration_runtime_claims(text: str) -> list[str]:
    claims: list[str] = []
    for line in text.splitlines():
        for segment in re.split(r"(?<=[.!?])\s+", line):
            stripped = segment.strip()
            if not stripped:
                continue
            lowered = stripped.lower()
            if not any(term in lowered for term in MIGRATION_RUNTIME_CLAIM_TERMS):
                continue
            if not any(word in lowered for word in RUNTIME_PROOF_WORDS):
                continue
            if any(qualifier.lower() in lowered for qualifier in SAFE_RUNTIME_QUALIFIERS):
                continue
            claims.append(stripped)
    return claims


def find_unsupported_s09_runtime_claims(text: str) -> list[str]:
    claims: list[str] = []
    for line in text.splitlines():
        for segment in re.split(r"(?<=[.!?])\s+", line):
            stripped = segment.strip()
            if not stripped:
                continue
            lowered = stripped.lower().replace("`", "")
            if "s09" not in lowered:
                continue
            if not any(term.lower() in lowered for term in S09_RUNTIME_PROOF_CLAIM_TERMS):
                continue
            if not any(word in lowered for word in S09_RUNTIME_PROOF_WORDS):
                continue
            if any(qualifier.lower() in lowered for qualifier in S09_RUNTIME_SAFE_QUALIFIERS):
                continue
            claims.append(stripped)
    return claims


def find_forbidden_descriptor_claims(text: str, terms: tuple[str, ...]) -> list[str]:
    claims: list[str] = []
    safe_descriptor_qualifiers = (
        "no ",
        "not ",
        "must not",
        "should not",
        "without",
        "fail on",
        "reject",
        "rejected",
        "excluded",
        "keep no",
        "stays out",
        "stay out",
        "remain wrapper-only",
        "remains wrapper-only",
    )
    safe_descriptor_contexts = (
        ("must audit", "wrapper-only alloc/dl invariants"),
    )
    for line in text.splitlines():
        for segment in re.split(r"(?<=[.!?])\s+", line):
            stripped = segment.strip()
            if not stripped:
                continue
            lowered = stripped.lower().replace("`", "")
            if not any(term in lowered for term in terms):
                continue
            if any(all(part in lowered for part in context) for context in safe_descriptor_contexts):
                continue
            if any(qualifier in lowered for qualifier in safe_descriptor_qualifiers):
                continue
            claims.append(stripped)
    return claims


def find_forbidden_alloc_descriptor_claims(text: str) -> list[str]:
    return find_forbidden_descriptor_claims(text, ALLOC_DESCRIPTOR_CLAIM_TERMS)


def find_forbidden_dl_descriptor_claims(text: str) -> list[str]:
    return find_forbidden_descriptor_claims(text, DL_DESCRIPTOR_CLAIM_TERMS)


def missing_required_markers(haystack: str, markers: list[str]) -> list[str]:
    return [marker for marker in markers if not marker_present(haystack, marker)]


def validate_common(
    *,
    mode: str,
    required_headings: list[str],
    required_files: list[str],
    section_citations: dict[str, list[str]],
    marker_groups: dict[str, list[str]],
    table_headings: list[str],
    section_marker_groups: dict[str, list[str]] | None = None,
) -> tuple[list[str], str, dict[str, str]]:
    errors: list[str] = []
    validate_scan_set(errors, [DOC_PATH, *required_files], mode)
    validate_source_anchors(errors)

    doc_abs = REPO_ROOT / DOC_PATH
    if not doc_abs.is_file():
        errors.append(f"Missing migration plan document: {DOC_PATH}")
        return errors, "", {}

    text = doc_abs.read_text(encoding="utf-8")

    for hidden in HIDDEN_SENTINELS:
        if f"{hidden}/" in text or f"`{hidden}" in text or f"{hidden}" in text:
            errors.append(
                f"Migration plan references hidden planning artifact {hidden}; "
                "source evidence must cite explicit non-hidden repository paths only."
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
        for marker in missing_required_markers(checked_text, markers):
            errors.append(f"Missing required marker for {group}: {marker}")

    for heading, markers in (section_marker_groups or {}).items():
        section = sections.get(heading, "")
        for marker in missing_required_markers(section, markers):
            errors.append(f"Missing required marker in ## {heading}: {marker}")

    if mode in {"stage-map", "gate-plan", "full"}:
        stage_section = sections.get("Accepted staged migration order", "")
        if not marker_present(stage_section, ",".join(STAGE_ORDER)):
            errors.append(
                "Missing order constraint in ## Accepted staged migration order: "
                f"expected compact marker {','.join(STAGE_ORDER)}"
            )

    for claim in find_unsupported_runtime_claims(checked_text):
        errors.append(
            "Unsupported runtime-proof claim found: "
            f"{claim!r}. TSAN, dlsym/RTLD_NEXT, and dl_iterate_phdr runtime validation "
            "must remain deferred until a later runtime-validation milestone supplies evidence."
        )

    for claim in find_unsupported_migration_runtime_claims(checked_text):
        errors.append(
            "Unsupported migration runtime-proof claim found: "
            f"{claim!r}. The migration plan is a source-backed static contract; "
            "runtime migration evidence belongs to M003/M004 after implementation."
        )

    for claim in find_unsupported_s09_runtime_claims(
        sections.get("S09 final validation and handoff gate", "")
    ):
        errors.append(
            "Unsupported S09 proof claim found: "
            f"{claim!r}. S09 may name M003/M004/M005 proof obligations, but it must not "
            "claim that S09 passed build, focused runtime tests, make check, TSAN, "
            "RTLD_NEXT, dl_iterate_phdr, or external-plugin compatibility."
        )

    for claim in find_forbidden_alloc_descriptor_claims(checked_text):
        errors.append(
            "Forbidden alloc descriptor claim found: "
            f"{claim!r}. Alloc is wrapper-only: no descriptor accessor, "
            "no DmtcpPluginDescriptor_t, and no PluginManager event row."
        )

    for claim in find_forbidden_dl_descriptor_claims(checked_text):
        errors.append(
            "Forbidden DL descriptor claim found: "
            f"{claim!r}. DL is wrapper-only: no descriptor accessor, "
            "no DmtcpPluginDescriptor_t, no DMTCP_DECL_PLUGIN, "
            "no dmtcp_initialize_plugin, and no PluginManager event row."
        )

    return errors, text, sections


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
                "S03 checks must stay on the explicit source list."
            )


def validate_table_shape(errors: list[str]) -> None:
    tables = {
        "PRIOR_PLAN_FILES": PRIOR_PLAN_FILES,
        "S09_FINAL_GATE_EVIDENCE_FILES": S09_FINAL_GATE_EVIDENCE_FILES,
        "IPC_DSO_SOURCE_FILES": IPC_DSO_SOURCE_FILES,
        "IPC_DSO_MAKEFILE_SOURCE_MARKERS": IPC_DSO_MAKEFILE_SOURCE_MARKERS,
        "IPC_HELPER_BOUNDARY_FILES": IPC_HELPER_BOUNDARY_FILES,
        "IPC_STAGE_MAP_REPRESENTATIVE_FILES": IPC_STAGE_MAP_REPRESENTATIVE_FILES,
        "IPC_DSO_DETAIL_SOURCE_FILES": IPC_DSO_DETAIL_SOURCE_FILES,
        "IPC_FOCUSED_TEST_FILES": IPC_FOCUSED_TEST_FILES,
        "PID_DSO_SOURCE_FILES": PID_DSO_SOURCE_FILES,
        "PID_DSO_MAKEFILE_SOURCE_MARKERS": PID_DSO_MAKEFILE_SOURCE_MARKERS,
        "PID_DSO_DETAIL_SOURCE_FILES": PID_DSO_DETAIL_SOURCE_FILES,
        "PID_FOCUSED_TEST_FILES": PID_FOCUSED_TEST_FILES,
        "ALLOC_DSO_SOURCE_FILES": ALLOC_DSO_SOURCE_FILES,
        "ALLOC_DSO_MAKEFILE_SOURCE_MARKERS": ALLOC_DSO_MAKEFILE_SOURCE_MARKERS,
        "ALLOC_DOC_STAGE_MARKERS": ALLOC_DOC_STAGE_MARKERS,
        "ALLOC_DESCRIPTOR_CLAIM_TERMS": ALLOC_DESCRIPTOR_CLAIM_TERMS,
        "DL_DSO_SOURCE_FILES": DL_DSO_SOURCE_FILES,
        "DL_DSO_MAKEFILE_SOURCE_MARKERS": DL_DSO_MAKEFILE_SOURCE_MARKERS,
        "DL_ACTIVE_WRAPPER_DOC_MARKER": [DL_ACTIVE_WRAPPER_DOC_MARKER],
        "DL_DOC_STAGE_MARKERS": DL_DOC_STAGE_MARKERS,
        "DL_DESCRIPTOR_CLAIM_TERMS": DL_DESCRIPTOR_CLAIM_TERMS,
        "MIGRATION_RUNTIME_CLAIM_TERMS": MIGRATION_RUNTIME_CLAIM_TERMS,
        "CURRENT_SOURCE_FILES": CURRENT_SOURCE_FILES,
        "CURRENT_SOURCE_MAP_EVIDENCE_FILES": CURRENT_SOURCE_MAP_EVIDENCE_FILES,
        "REQUIRED_EVIDENCE_FILES": REQUIRED_EVIDENCE_FILES,
        "STAGE_MAP_REQUIRED_HEADINGS": STAGE_MAP_REQUIRED_HEADINGS,
        "GATE_PLAN_REQUIRED_HEADINGS": GATE_PLAN_REQUIRED_HEADINGS,
        "FULL_REQUIRED_HEADINGS": FULL_REQUIRED_HEADINGS,
        "STAGE_MAP_SECTION_CITATIONS": STAGE_MAP_SECTION_CITATIONS,
        "GATE_PLAN_SECTION_CITATIONS": GATE_PLAN_SECTION_CITATIONS,
        "FULL_SECTION_CITATIONS": FULL_SECTION_CITATIONS,
        "STAGE_MAP_MARKER_GROUPS": STAGE_MAP_MARKER_GROUPS,
        "GATE_PLAN_MARKER_GROUPS": GATE_PLAN_MARKER_GROUPS,
        "GATE_PLAN_SECTION_MARKERS": GATE_PLAN_SECTION_MARKERS,
        "S09_FINAL_VALIDATION_MARKERS": S09_FINAL_VALIDATION_MARKERS,
        "FULL_MARKER_GROUPS": FULL_MARKER_GROUPS,
        "FULL_SECTION_MARKERS": FULL_SECTION_MARKERS,
        "STAGE_MAP_TABLE_HEADINGS": STAGE_MAP_TABLE_HEADINGS,
        "GATE_PLAN_TABLE_HEADINGS": GATE_PLAN_TABLE_HEADINGS,
        "FULL_TABLE_HEADINGS": FULL_TABLE_HEADINGS,
        "SOURCE_ANCHORS": SOURCE_ANCHORS,
        "FORBIDDEN_SOURCE_MARKERS": FORBIDDEN_SOURCE_MARKERS,
        "ORDERED_SOURCE_ANCHORS": ORDERED_SOURCE_ANCHORS,
        "PLACEHOLDER_PATTERNS": PLACEHOLDER_PATTERNS,
    }
    for name, table in tables.items():
        if not table:
            errors.append(f"Verifier table is empty: {name}")

    stage_headings = set(STAGE_MAP_REQUIRED_HEADINGS)
    gate_headings = set(GATE_PLAN_REQUIRED_HEADINGS)
    full_headings = set(FULL_REQUIRED_HEADINGS)
    for heading in STAGE_MAP_SECTION_CITATIONS:
        if heading not in stage_headings:
            errors.append(f"STAGE_MAP_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in GATE_PLAN_SECTION_CITATIONS:
        if heading not in gate_headings:
            errors.append(f"GATE_PLAN_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in FULL_SECTION_CITATIONS:
        if heading not in full_headings:
            errors.append(f"FULL_SECTION_CITATIONS references unknown heading: {heading}")
    for heading in GATE_PLAN_SECTION_MARKERS:
        if heading not in gate_headings:
            errors.append(f"GATE_PLAN_SECTION_MARKERS references unknown heading: {heading}")
    for heading in FULL_SECTION_MARKERS:
        if heading not in full_headings:
            errors.append(f"FULL_SECTION_MARKERS references unknown heading: {heading}")

    for heading in STAGE_MAP_TABLE_HEADINGS:
        if heading not in stage_headings:
            errors.append(f"STAGE_MAP_TABLE_HEADINGS references unknown heading: {heading}")
    for heading in GATE_PLAN_TABLE_HEADINGS:
        if heading not in gate_headings:
            errors.append(f"GATE_PLAN_TABLE_HEADINGS references unknown heading: {heading}")
    for heading in FULL_TABLE_HEADINGS:
        if heading not in full_headings:
            errors.append(f"FULL_TABLE_HEADINGS references unknown heading: {heading}")

    required_file_set = set(REQUIRED_EVIDENCE_FILES)
    for table_name, citation_table in {
        "STAGE_MAP_SECTION_CITATIONS": STAGE_MAP_SECTION_CITATIONS,
        "GATE_PLAN_SECTION_CITATIONS": GATE_PLAN_SECTION_CITATIONS,
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
        **STAGE_MAP_MARKER_GROUPS,
        **GATE_PLAN_MARKER_GROUPS,
        **GATE_PLAN_SECTION_MARKERS,
        **FULL_MARKER_GROUPS,
        **FULL_SECTION_MARKERS,
    }.items():
        if not markers:
            errors.append(f"Marker group is empty: {group_name}")
        for marker in markers:
            if not marker.strip():
                errors.append(f"Marker group {group_name} contains an empty marker")

    if len(IPC_DSO_SOURCE_FILES) != 45:
        errors.append(
            "IPC_DSO_SOURCE_FILES must enumerate the 45 explicit "
            "__d_libdir__libdmtcp_ipc_so_SOURCES entries"
        )
    if len(IPC_DSO_MAKEFILE_SOURCE_MARKERS) != len(IPC_DSO_SOURCE_FILES):
        errors.append("IPC Makefile source-marker table drifted from IPC_DSO_SOURCE_FILES")
    for helper in IPC_HELPER_BOUNDARY_FILES:
        if helper in IPC_DSO_SOURCE_FILES:
            errors.append(f"IPC helper boundary leaked into IPC DSO source table: {helper}")
    if len(PID_DSO_SOURCE_FILES) != 12:
        errors.append(
            "PID_DSO_SOURCE_FILES must enumerate the 12 explicit "
            "__d_libdir__libdmtcp_pid_so_SOURCES entries"
        )
    if len(PID_DSO_MAKEFILE_SOURCE_MARKERS) != len(PID_DSO_SOURCE_FILES):
        errors.append("PID Makefile source-marker table drifted from PID_DSO_SOURCE_FILES")
    if PID_DUPLICATE_REAL_HELPERS:
        errors.append("PID_DUPLICATE_REAL_HELPERS must stay empty after PID helper namespace consolidation")
    if ALLOC_DSO_SOURCE_FILES != [
        "src/plugin/alloc/alloc.h",
        "src/plugin/alloc/mallocwrappers.cpp",
        "src/plugin/alloc/mmapwrappers.cpp",
    ]:
        errors.append(
            "ALLOC_DSO_SOURCE_FILES must enumerate the exact 3-entry "
            "__d_libdir__libdmtcp_alloc_so_SOURCES boundary"
        )
    if len(ALLOC_DSO_MAKEFILE_SOURCE_MARKERS) != len(ALLOC_DSO_SOURCE_FILES):
        errors.append("Alloc Makefile source-marker table drifted from ALLOC_DSO_SOURCE_FILES")
    for alloc_path in ALLOC_DSO_SOURCE_FILES:
        if alloc_path not in CURRENT_SOURCE_FILES:
            errors.append(f"CURRENT_SOURCE_FILES is missing alloc boundary path: {alloc_path}")
    for required_alloc_marker in [
        ALLOC_ACTIVE_WRAPPER_DOC_MARKER,
        "#ifdef ENABLE_MMAP_WRAPPERS",
        ALLOC_MMAP_WRAPPER_DOC_MARKER,
        "stale libdmtcp_alloc.so",
        "test/plugin-init.cpp",
        "no PluginManager event row",
    ]:
        if required_alloc_marker not in ALLOC_DOC_STAGE_MARKERS:
            errors.append(f"ALLOC_DOC_STAGE_MARKERS is missing required alloc marker: {required_alloc_marker}")
    if DL_DSO_SOURCE_FILES != ["src/plugin/dl/dlwrappers.cpp"]:
        errors.append(
            "DL_DSO_SOURCE_FILES must enumerate the exact 1-entry "
            "__d_libdir__libdmtcp_dl_so_SOURCES boundary"
        )
    if DL_DSO_MAKEFILE_SOURCE_MARKERS != ["dl/dlwrappers.cpp"]:
        errors.append("DL Makefile source-marker table drifted from DL_DSO_SOURCE_FILES")
    for dl_path in DL_DSO_SOURCE_FILES:
        if dl_path not in CURRENT_SOURCE_FILES:
            errors.append(f"CURRENT_SOURCE_FILES is missing DL boundary path: {dl_path}")
    for required_dl_marker in [
        DL_ACTIVE_WRAPPER_DOC_MARKER,
        "DMTCP_DL_PLUGIN=0",
        "--disable-all-plugins",
        "fast-pass",
        "stale libdmtcp_dl.so",
        "LibDlWrapperLock",
        "getRpathRunPath",
        "dlopen_try_paths",
        "test/dlopen1.c",
        "test/dlopen2.cpp",
        "no DMTCP_DECL_PLUGIN",
        "no dmtcp_initialize_plugin",
        "no PluginManager event row",
        "TSAN",
        "dlsym/RTLD_NEXT",
        "dl_iterate_phdr",
        "default-version lookup",
    ]:
        if required_dl_marker not in DL_DOC_STAGE_MARKERS:
            errors.append(f"DL_DOC_STAGE_MARKERS is missing required DL marker: {required_dl_marker}")
    for s09_path in S09_FINAL_GATE_EVIDENCE_FILES:
        if s09_path not in REQUIRED_EVIDENCE_FILES:
            errors.append(f"REQUIRED_EVIDENCE_FILES is missing S09 evidence file: {s09_path}")
    for required_s09_marker in [
        "test/verify-consolidation-preflight.py",
        "generated build files",
        "clean rebuild",
        "make check",
        "duplicate wrapper and symbol audit",
        "Launch, exec, and checkpoint-restart propagation",
        "M004 loader and sanitizer stress handoff",
        "external dlsym(RTLD_NEXT)",
        "dl_iterate_phdr",
        "default-version lookup stress",
        "M005 optional and external plugin handoff",
        "Optional and external plugin compatibility",
    ]:
        if required_s09_marker not in S09_FINAL_VALIDATION_MARKERS:
            errors.append(
                f"S09_FINAL_VALIDATION_MARKERS is missing required marker: {required_s09_marker}"
            )
    for stale_dso in [
        "libdmtcp_timer.so",
        "libdmtcp_svipc.so",
        "libdmtcp_ipc.so",
        "libdmtcp_pid.so",
        "libdmtcp_alloc.so",
        "libdmtcp_dl.so",
    ]:
        if stale_dso not in S09_FINAL_VALIDATION_MARKERS:
            errors.append(f"S09 final gate marker set is missing stale DSO marker: {stale_dso}")
    for required_pid_path in [
        "src/plugin/pid/pid.cpp",
        "src/plugin/pid/pid_miscwrappers.cpp",
        "src/plugin/pid/pid_syscallsreal.c",
        "src/plugin/pid/pidwrappers.cpp",
        "src/plugin/pid/sched_wrappers.cpp",
        "src/plugin/pid/virtualpidtable.cpp",
    ]:
        if required_pid_path not in PID_DSO_SOURCE_FILES:
            errors.append(f"PID DSO source table is missing required PID boundary path: {required_pid_path}")
    for required_pid_test in [
        "test/gettid.c",
        "test/sched_test.c",
        "test/forkexec.c",
        "test/vfork1.c",
        "test/pthread_atfork1.c",
        "test/pthread_atfork2.c",
        "test/waitpid.c",
        "test/syscall-tester.c",
    ]:
        if required_pid_test not in PID_FOCUSED_TEST_FILES:
            errors.append(f"PID focused-test table is missing required target: {required_pid_test}")
    if "src/plugin/ipc/ssh/util_ssh.h" in REQUIRED_EVIDENCE_FILES:
        errors.append("Do not require src/plugin/ipc/ssh/util_ssh.h as a verifier citation")
    if "test/epoll2.c" in REQUIRED_EVIDENCE_FILES:
        errors.append("Do not require nonexistent test/epoll2.c as a verifier citation")


def validate_negative_guards(errors: list[str]) -> None:
    for bad_path in [
        ".gsd/example.md",
        ".planning/STATE.md",
        ".audits/report.md",
        "../outside.cpp",
        "/tmp/outside.cpp",
    ]:
        if not is_hidden_or_outside_repo(bad_path):
            errors.append(f"Hidden/out-of-repo guard failed to reject: {bad_path}")

    for good_path in [
        "src/dmtcp_launch.cpp",
        "doc/internal-plugin-consolidation-registration-architecture.md",
        "test/autotest.py",
    ]:
        if is_hidden_or_outside_repo(good_path):
            errors.append(f"Hidden/out-of-repo guard rejected valid repo path: {good_path}")

    if repo_file_exists("test/does-not-exist-for-migration-verifier.cpp"):
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

    unsafe_claim = "TSAN runtime validation passed in tests."
    safe_claim = "TSAN runtime validation remains deferred to M004."
    if not find_unsupported_runtime_claims(unsafe_claim):
        errors.append("Runtime-proof guard failed to reject an unsupported runtime proof claim")
    if find_unsupported_runtime_claims(safe_claim):
        errors.append("Runtime-proof guard rejected a deferred runtime-validation statement")

    forbidden_source_fixture = (
        "libdmtcp_alloc.so libdmtcp_dl.so "
        "__d_libdir__libdmtcp_alloc_so_SOURCES __d_libdir__libdmtcp_dl_so_SOURCES "
        "dmtcp_Alloc_PluginDescr dmtcp_Dl_PluginDescr DmtcpPluginDescriptor_t"
    )
    for label, marker in {
        "stale alloc DSO row": "libdmtcp_alloc.so",
        "stale DL DSO row": "libdmtcp_dl.so",
        "synthetic alloc accessor": "dmtcp_Alloc_PluginDescr",
        "synthetic DL accessor": "dmtcp_Dl_PluginDescr",
    }.items():
        if marker not in forbidden_source_fixture:
            errors.append(f"Forbidden-source negative fixture failed to include {label}")

    s09_contract = "\n".join(S09_FINAL_VALIDATION_MARKERS)
    if missing_required_markers(s09_contract, S09_FINAL_VALIDATION_MARKERS):
        errors.append("S09 marker fixture should satisfy all final-gate markers")
    for label, marker in {
        "final batch make check": "make check",
        "timer stale DSO audit": "libdmtcp_timer.so",
        "M004 stress handoff": "M004 loader and sanitizer stress handoff",
        "M005 external-plugin handoff": "M005 optional and external plugin handoff",
    }.items():
        weakened_contract = s09_contract.replace(marker, "")
        if marker not in missing_required_markers(weakened_contract, S09_FINAL_VALIDATION_MARKERS):
            errors.append(f"S09 negative fixture failed to reject missing {label}")

    unsafe_s09_claim = "S09 passed make check and verified external plugin compatibility."
    safe_s09_claim = "S09 does not claim make check passed; M003 owns the final batch transcript."
    if not find_unsupported_s09_runtime_claims(unsafe_s09_claim):
        errors.append("S09 proof guard failed to reject make-check/external-plugin success")
    if find_unsupported_s09_runtime_claims(safe_s09_claim):
        errors.append("S09 proof guard rejected a deferred make-check statement")

    alloc_contract = "\n".join(ALLOC_DOC_STAGE_MARKERS)
    if missing_required_markers(alloc_contract, ALLOC_DOC_STAGE_MARKERS):
        errors.append("Alloc marker fixture should satisfy all alloc-stage markers")
    for label, marker in {
        "active wrapper list": ALLOC_ACTIVE_WRAPPER_DOC_MARKER,
        "mmap compile guard": "#ifdef ENABLE_MMAP_WRAPPERS",
        "disabled fast-pass": "fast-pass",
        "stale alloc artifact": "stale libdmtcp_alloc.so",
        "plugin-init startup coverage": "test/plugin-init.cpp",
    }.items():
        weakened_contract = alloc_contract.replace(marker, "")
        if marker not in missing_required_markers(weakened_contract, ALLOC_DOC_STAGE_MARKERS):
            errors.append(f"Alloc negative fixture failed to reject missing {label}")

    unsafe_migration_claim = "Alloc runtime migration passed in focused tests."
    safe_migration_claim = "Alloc runtime migration remains deferred to M003."
    if not find_unsupported_migration_runtime_claims(unsafe_migration_claim):
        errors.append("Migration runtime-proof guard failed to reject alloc runtime migration success")
    if find_unsupported_migration_runtime_claims(safe_migration_claim):
        errors.append("Migration runtime-proof guard rejected a deferred alloc migration statement")

    unsafe_descriptor_claim = "The alloc stage adds dmtcp_Alloc_PluginDescr() as an alloc descriptor row."
    safe_descriptor_claim = "The alloc stage has no alloc descriptor row."
    if not find_forbidden_alloc_descriptor_claims(unsafe_descriptor_claim):
        errors.append("Alloc descriptor guard failed to reject a fake alloc descriptor claim")
    if find_forbidden_alloc_descriptor_claims(safe_descriptor_claim):
        errors.append("Alloc descriptor guard rejected a no-descriptor statement")

    dl_contract = "\n".join(DL_DOC_STAGE_MARKERS)
    if missing_required_markers(dl_contract, DL_DOC_STAGE_MARKERS):
        errors.append("DL marker fixture should satisfy all DL-stage markers")
    for label, marker in {
        "DL environment disable": "DMTCP_DL_PLUGIN=0",
        "disable-all semantics": "--disable-all-plugins",
        "disabled fast-pass": "fast-pass",
        "stale DL artifact": "stale libdmtcp_dl.so",
        "active lock helper": "LibDlWrapperLock",
        "RPATH/RUNPATH helper": "getRpathRunPath",
        "path-probing helper": "dlopen_try_paths",
        "no descriptor macro": "no DMTCP_DECL_PLUGIN",
        "no initializer ownership": "no dmtcp_initialize_plugin",
        "focused C dlopen coverage": "test/dlopen1.c",
        "focused C++ dlopen coverage": "test/dlopen2.cpp",
        "M004 TSAN handoff": "TSAN",
        "M004 RTLD_NEXT handoff": "dlsym/RTLD_NEXT",
        "M004 dl_iterate_phdr handoff": "dl_iterate_phdr",
        "M004 default-version handoff": "default-version lookup",
    }.items():
        weakened_contract = dl_contract.replace(marker, "")
        if marker not in missing_required_markers(weakened_contract, DL_DOC_STAGE_MARKERS):
            errors.append(f"DL negative fixture failed to reject missing {label}")

    unsafe_dl_runtime_claim = "DL runtime migration passed in focused tests."
    safe_dl_runtime_claim = "DL runtime migration remains deferred to M003."
    if not find_unsupported_migration_runtime_claims(unsafe_dl_runtime_claim):
        errors.append("Migration runtime-proof guard failed to reject DL runtime migration success")
    if find_unsupported_migration_runtime_claims(safe_dl_runtime_claim):
        errors.append("Migration runtime-proof guard rejected a deferred DL migration statement")

    unsafe_default_version_claim = "Default-version lookup runtime validation passed in tests."
    safe_default_version_claim = "Default-version lookup stress remains deferred to M004."
    if not find_unsupported_runtime_claims(unsafe_default_version_claim):
        errors.append("Runtime-proof guard failed to reject default-version lookup success")
    if find_unsupported_runtime_claims(safe_default_version_claim):
        errors.append("Runtime-proof guard rejected a deferred default-version lookup statement")

    unsafe_dl_descriptor_claim = "The DL stage adds dmtcp_Dl_PluginDescr() as a DL descriptor row."
    safe_dl_descriptor_claim = "The DL stage has no DL descriptor row."
    if not find_forbidden_dl_descriptor_claims(unsafe_dl_descriptor_claim):
        errors.append("DL descriptor guard failed to reject a fake DL descriptor claim")
    if find_forbidden_dl_descriptor_claims(safe_dl_descriptor_claim):
        errors.append("DL descriptor guard rejected a no-descriptor statement")


def validate_self_test() -> int:
    errors: list[str] = []
    if str(DOC_PATH) != EXPECTED_DOC_PATH:
        errors.append(
            f"Verifier DOC_PATH drifted: expected {EXPECTED_DOC_PATH}, found {DOC_PATH}"
        )

    validate_table_shape(errors)
    validate_scan_set(errors, REQUIRED_EVIDENCE_FILES, "self-test")
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

    return report("self-test", errors)


def validate_stage_map() -> int:
    errors, _, _ = validate_common(
        mode="stage-map",
        required_headings=STAGE_MAP_REQUIRED_HEADINGS,
        required_files=REQUIRED_EVIDENCE_FILES,
        section_citations=STAGE_MAP_SECTION_CITATIONS,
        marker_groups=STAGE_MAP_MARKER_GROUPS,
        table_headings=STAGE_MAP_TABLE_HEADINGS,
    )
    return report("stage-map", errors)


def validate_gate_plan() -> int:
    errors, _, _ = validate_common(
        mode="gate-plan",
        required_headings=GATE_PLAN_REQUIRED_HEADINGS,
        required_files=REQUIRED_EVIDENCE_FILES,
        section_citations=GATE_PLAN_SECTION_CITATIONS,
        marker_groups={**STAGE_MAP_MARKER_GROUPS, **GATE_PLAN_MARKER_GROUPS},
        table_headings=GATE_PLAN_TABLE_HEADINGS,
        section_marker_groups=GATE_PLAN_SECTION_MARKERS,
    )
    return report("gate-plan", errors)


def validate_full() -> int:
    errors, _, _ = validate_common(
        mode="full",
        required_headings=FULL_REQUIRED_HEADINGS,
        required_files=REQUIRED_EVIDENCE_FILES,
        section_citations=FULL_SECTION_CITATIONS,
        marker_groups={
            **STAGE_MAP_MARKER_GROUPS,
            **GATE_PLAN_MARKER_GROUPS,
            **FULL_MARKER_GROUPS,
        },
        table_headings=FULL_TABLE_HEADINGS,
        section_marker_groups=FULL_SECTION_MARKERS,
    )
    return report("full", errors)


def report(mode: str, errors: list[str]) -> int:
    if errors:
        print(f"{mode} verification failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(
        f"{mode} verification passed: required headings, tables, citations, markers, "
        "source anchors, and forbidden-scan checks are satisfied."
    )
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "mode",
        choices=["self-test", "stage-map", "gate-plan", "full"],
        help="verification mode to run",
    )
    args = parser.parse_args(argv)

    if args.mode == "self-test":
        return validate_self_test()
    if args.mode == "stage-map":
        return validate_stage_map()
    if args.mode == "gate-plan":
        return validate_gate_plan()
    if args.mode == "full":
        return validate_full()
    raise AssertionError(f"unhandled mode: {args.mode}")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
