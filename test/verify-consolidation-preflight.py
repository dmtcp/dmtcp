#!/usr/bin/env python3
"""Static preflight verifier for internal plugin consolidation work.

The checks in this file are intentionally source-map based: they read only
tracked repository inputs that describe the current 2.5 plugin/wrapper layout.
They do not inspect GSD planning state, generated build trees, installed library
locations, or environment values.
"""

from __future__ import print_function

import io
import os
import sys
from collections import namedtuple
from pathlib import Path

Marker = namedtuple("Marker", ["label", "needles"])
FileCheck = namedtuple("FileCheck", ["path", "markers"])
Stats = namedtuple("Stats", ["paths", "markers"])

MODE_NAMES = ("self-test", "source-map", "preflight-doc", "integration", "full")
PREFLIGHT_DOC_PATH = "doc/internal-plugin-consolidation-preflight.md"
DESIGN_DOC_PATH = "doc/internal-plugin-consolidation-design-plan.md"
MIGRATION_DOC_PATH = "doc/internal-plugin-consolidation-migration-plan.md"
INTEGRATION_DOC_PATHS = (PREFLIGHT_DOC_PATH, DESIGN_DOC_PATH, MIGRATION_DOC_PATH)

# Configuration paths must never point at hidden planning/audit state, generated
# build products, or installed library directories.  This protects future edits
# from weakening the preflight by checking local artifacts instead of source.
FORBIDDEN_HIDDEN_COMPONENTS = (".git", ".gsd", ".planning", ".audits", ".hg", ".svn")
FORBIDDEN_GENERATED_COMPONENTS = (
    ".deps",
    ".libs",
    "autom4te.cache",
    "__pycache__",
    "build",
    "BUILD",
    "dist",
    "install",
    "installed",
)
FORBIDDEN_INSTALLED_PREFIXES = ("lib", "lib64", "usr", "usr/local", "opt")
FORBIDDEN_DOC_CITATIONS = (".gsd", ".planning", ".audits")


def marker(label, *needles):
    if not needles:
        raise ValueError("marker requires at least one needle")
    return Marker(label, tuple(needles))


def file_check(path, *markers):
    return FileCheck(path, tuple(markers))


SOURCE_MAP_CHECKS = (
    file_check(
        "src/dmtcp_launch.cpp",
        marker("launch global disable switch", "static bool disableAllPlugins = false;"),
        marker("alloc plugin enable state", "static bool enableAllocPlugin = true;"),
        marker("dl plugin enable state", "static bool enableDlPlugin = true;"),
        marker("pathvirt plugin enable state", "static bool enablePathVirtPlugin = false;"),
        marker("internal disable-all env export", "setenv(ENV_VAR_DISABLE_ALL_PLUGINS"),
        marker("pathvirt preload entry", "{ &enablePathVirtPlugin,  \"libdmtcp_pathvirt.so\"}"),
        marker("core preload entry", "{ &enableLibDMTCP, \"libdmtcp.so\" }"),
        marker("disable alloc launch flag", "setenv(ENV_VAR_ALLOC_PLUGIN, \"0\", 1);"),
        marker("disable dl launch flag", "setenv(ENV_VAR_DL_PLUGIN, \"0\", 1);"),
        marker("setLDPreloadLibs entry point", "setLDPreloadLibs(bool is32bitElf)"),
        marker("hijack libs export", "setenv(ENV_VAR_HIJACK_LIBS, preloadLibs.c_str(), 1);"),
        marker("LD_PRELOAD export", "setenv(\"LD_PRELOAD\", preloadLibs.c_str(), 1);"),
    ),
    file_check(
        "src/constants.h",
        marker("original LD_PRELOAD env name", "#define ENV_VAR_ORIG_LD_PRELOAD"),
        marker("hijack libs env name", "#define ENV_VAR_HIJACK_LIBS"),
        marker("32 bit hijack libs env name", "#define ENV_VAR_HIJACK_LIBS_M32"),
        marker("plugin env name", "#define ENV_VAR_PLUGIN"),
        marker("alloc plugin env name", "#define ENV_VAR_ALLOC_PLUGIN"),
        marker("dl plugin env name", "#define ENV_VAR_DL_PLUGIN"),
        marker("disable-all plugin env name", "#define ENV_VAR_DISABLE_ALL_PLUGINS"),
        marker("protected env list", "#define ENV_VARS_ALL"),
    ),
    file_check(
        "src/util_exec.cpp",
        marker("restart arg builder", "Util::getDmtcpArgs(void)"),
        marker("restart reads alloc plugin env", "const char *allocPlugin = getenv(ENV_VAR_ALLOC_PLUGIN);"),
        marker("restart reads dl plugin env", "const char *dlPlugin = getenv(ENV_VAR_DL_PLUGIN);"),
        marker("restart propagates alloc disable flag", "argVector.push_back(\"--disable-alloc-plugin\");"),
        marker("restart propagates dl disable flag", "argVector.push_back(\"--disable-dl-plugin\");"),
        marker("restart argv null terminator", "args[argVector.size()] = NULL;"),
        marker("installed path resolver", "Util::getPath(const char *cmd, bool is32bit)"),
    ),
    file_check(
        "src/execwrappers.cpp",
        marker("exec preload reconstruction", "getUpdatedLdPreload(const char *filename, const char *currLdPreload)"),
        marker("exec reads hijack libs", "getenv(ENV_VAR_HIJACK_LIBS)"),
        marker("exec reads 32 bit hijack libs", "getenv(ENV_VAR_HIJACK_LIBS_M32)"),
        marker("exec saves original preload", "setenv(ENV_VAR_ORIG_LD_PRELOAD"),
        marker("exec patches user env", "patchUserEnv(const char *env[], const char *filename)"),
        marker("exec rebuilds LD_PRELOAD", "string ldPreloadStr = \"LD_PRELOAD=\";"),
        marker("exec wrapper trampoline", "dmtcp_execvpe(const char *filename, char *const argv[], char *const envp[])")
    ),
    file_check(
        "include/dmtcp.h",
        marker("plugin API version", "#define DMTCP_PLUGIN_API_VERSION"),
        marker("event enum", "} DmtcpEvent_t;"),
        marker("event data union", "} DmtcpEventData_t;"),
        marker("plugin descriptor ABI", "} DmtcpPluginDescriptor_t;"),
        marker("plugin registration ABI", "void dmtcp_register_plugin(DmtcpPluginDescriptor_t)"),
        marker("plugin declaration ABI", "#define DMTCP_DECL_PLUGIN(descr)"),
        marker("plugin disable checkpoint ABI", "#define DMTCP_PLUGIN_DISABLE_CKPT()"),
        marker("plugin enable checkpoint ABI", "#define DMTCP_PLUGIN_ENABLE_CKPT()"),
        marker("next function chain ABI", "#define NEXT_FNC(func)"),
    ),
    file_check(
        "src/pluginmanager.cpp",
        marker("plugin registration function", "dmtcp_register_plugin(DmtcpPluginDescriptor_t descr)"),
        marker("plugin info chain append", "pluginInfos.push_back(info);"),
        marker("builtin plugin initializer", "dmtcp_initialize_plugin()"),
        marker("builtin path translator descriptor", "dmtcp_PathTranslator_PluginDescr()"),
        marker("builtin syslog descriptor", "dmtcp_Syslog_PluginDescr()"),
        marker("builtin rlimit descriptor", "dmtcp_Rlimit_Float_PluginDescr()"),
        marker("builtin alarm descriptor", "dmtcp_Alarm_PluginDescr()"),
        marker("builtin terminal descriptor", "dmtcp_Terminal_PluginDescr()"),
        marker("builtin coordinator descriptor", "CoordinatorAPI::pluginDescr()"),
        marker("builtin process info descriptor", "dmtcp_ProcessInfo_PluginDescr()"),
        marker("builtin unique pid descriptor", "UniquePid::pluginDescr()"),
        marker("plugin initializer chain", "NEXT_FNC(dmtcp_initialize_plugin)"),
        marker("event hook dispatcher", "PluginManager::eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)"),
        marker("forward plugin event order", "for (size_t i = 0; i < pluginManager->pluginInfos.size(); i++)"),
        marker("reverse plugin event order", "for (int i = pluginManager->pluginInfos.size() - 1; i >= 0; i--)"),
    ),
    file_check(
        "src/pluginmanager.h",
        marker("plugin manager register method", "void registerPlugin(DmtcpPluginDescriptor_t descr);"),
        marker("plugin manager event hook method", "static void eventHook(DmtcpEvent_t event, DmtcpEventData_t *data = NULL);"),
        marker("plugin info vector", "vector<PluginInfo *>pluginInfos;"),
    ),
    file_check(
        "src/Makefile.am",
        marker("core libdmtcp DSO boundary", "dmtcplib_PROGRAMS = $(d_libdir)/libdmtcp.so"),
        marker("core libdmtcp source list", "__d_libdir__libdmtcp_so_SOURCES"),
        marker("core dl wrappers source", "dlwrappers.cpp"),
        marker("core alloc header source", "plugin/alloc/alloc.h"),
        marker("core alloc malloc wrapper source", "plugin/alloc/mallocwrappers.cpp"),
        marker("core alloc mmap wrapper source", "plugin/alloc/mmapwrappers.cpp"),
        marker("core dl plugin wrapper source", "plugin/dl/dlwrappers.cpp"),
        marker("core exec wrappers source", "execwrappers.cpp"),
        marker("core plugin manager source", "pluginmanager.cpp"),
        marker("core timer descriptor source", "plugin/timer/timerlist.cpp"),
        marker("core timer wrappers source", "plugin/timer/timerwrappers.cpp"),
        marker("core sysvipc descriptor source", "plugin/svipc/sysvipc.cpp"),
        marker("core sysvipc wrappers source", "plugin/svipc/sysvipcwrappers.cpp"),
        marker("core ipc descriptor source", "plugin/ipc/ipc.cpp"),
        marker("core ipc posix wrapper source", "plugin/ipc/file/posixipcwrappers.cpp"),
        marker("core pid descriptor source", "plugin/pid/pid.cpp"),
        marker("core pid wrappers source", "plugin/pid/pidwrappers.cpp"),
        marker("core generic wrappers source", "wrappers.cpp"),
        marker("launch binary source", "__d_bindir__dmtcp_launch_SOURCES = dmtcp_launch.cpp"),
    ),
    file_check(
        "src/plugin/Makefile.am",
        marker("ipc helper archive boundary", "noinst_LIBRARIES += libssh.a"),
        marker("ipc ssh helper source", "ipc/ssh/util_ssh.cpp"),
        marker("pid built-in boundary comment", "The PID plugin is built into the core DMTCP library."),
        marker("timer built-in boundary comment", "Timer wrappers and the timer descriptor are now built into libdmtcp.so"),
    ),
    file_check(
        "src/plugin/timer/timerlist.cpp",
        marker("timer descriptor", "DmtcpPluginDescriptor_t timerPlugin = {"),
        marker("timer event hook", "timer_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)"),
        marker("timer built-in accessor", "dmtcp_Timer_PluginDescr()"),
    ),
    file_check(
        "src/plugin/timer/timerwrappers.cpp",
        marker("timer_create wrapper", "timer_create(clockid_t clockid, struct sigevent *sevp, timer_t *timerid)"),
        marker("timer virtual id mapping", "TimerList::instance().on_timer_create"),
        marker("timer wrapper disables checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("timer wrapper enables checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/svipc/sysvipc.cpp",
        marker("svipc descriptor", "DmtcpPluginDescriptor_t sysvipcPlugin = {"),
        marker("svipc event hook", "sysvipc_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)"),
        marker("svipc built-in accessor", "dmtcp_SysVIPC_PluginDescr()"),
    ),
    file_check(
        "src/plugin/svipc/sysvipcwrappers.cpp",
        marker("sysv shmget wrapper", "shmget(key_t key, size_t size, int shmflg)"),
        marker("sysv shmat wrapper", "void *shmat(int shmid, const void *shmaddr, int shmflg)"),
        marker("sysv shmdt wrapper", "shmdt(const void *shmaddr)"),
        marker("sysv shmctl wrapper", "shmctl(int shmid, int cmd, struct shmid_ds *buf)"),
        marker("sysv semget wrapper", "semget(key_t key, int nsems, int semflg)"),
        marker("sysv semop wrapper", "semop(int semid, struct sembuf *sops, size_t nsops)"),
        marker("sysv semctl wrapper", "semctl(int semid, int semnum, int cmd, ...)"),
        marker("sysv msgget wrapper", "msgget(key_t key, int msgflg)"),
        marker("sysv msgsnd wrapper", "msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg)"),
        marker("sysv msgrcv wrapper", "msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg)"),
        marker("sysv msgctl wrapper", "msgctl(int msqid, int cmd, struct msqid_ds *buf)"),
        marker("sysv wrappers disable checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("sysv wrappers enable checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/ipc/ipc.cpp",
        marker("ipc built-in boundary comment", "IPC subplugin descriptors are registered by PluginManager"),
        marker("ipc no nested initializer", "do not define an IPC-local dmtcp_initialize_plugin() chain here"),
        marker("ipc source include", "#include \"ipc.h\""),
    ),
    file_check(
        "src/plugin/ipc/file/posixipcwrappers.cpp",
        marker("posix mq_open wrapper", "mq_open(const char *name, int oflag, ...)"),
        marker("posix mq_send wrapper", "mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio)"),
        marker("posix mq_receive wrapper", "mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio)"),
        marker("posix ipc disables checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("posix ipc enables checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/pid/pid.cpp",
        marker("pid descriptor", "DmtcpPluginDescriptor_t pidPlugin = {"),
        marker("pid event hook", "pid_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)"),
        marker("pid built-in accessor", "dmtcp_Pid_PluginDescr()"),
    ),
    file_check(
        "src/plugin/pid/pidwrappers.cpp",
        marker("getpid virtual wrapper", "getpid()"),
        marker("pid virtual to real mapping", "VIRTUAL_TO_REAL_PID"),
        marker("pid real to virtual mapping", "REAL_TO_VIRTUAL_PID"),
        marker("pid kill wrapper", "kill(pid_t pid, int sig)"),
        marker("pid wrappers disable checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("pid wrappers enable checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/pid/pid_miscwrappers.cpp",
        marker("pid misc atfork prepare helper", "pidVirt_atfork_prepare()"),
        marker("pid misc vfork wrapper", "pidVirt_vfork()"),
        marker("pid misc wrapper composition comment", "Timer, SysV IPC, and POSIX mqueue PID translations"),
        marker("pid misc process vm read wrapper", "process_vm_readv(pid_t pid"),
        marker("pid misc process vm write wrapper", "process_vm_writev(pid_t pid"),
        marker("pid misc disables checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("pid misc enables checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/alloc/mallocwrappers.cpp",
        marker("alloc enabled cache", "static int dmtcpAllocEnabledCache = -1;"),
        marker("alloc disable-all env probe", "getenv(ENV_VAR_DISABLE_ALL_PLUGINS)"),
        marker("alloc plugin env probe", "getenv(ENV_VAR_ALLOC_PLUGIN)"),
        marker("alloc enabled cache store", "__atomic_store_n(&dmtcpAllocEnabledCache, enabled, __ATOMIC_RELAXED);"),
        marker("calloc wrapper", "extern \"C\" void *calloc(size_t nmemb, size_t size)"),
        marker("malloc wrapper", "extern \"C\" void *malloc(size_t size)"),
        marker("free wrapper", "free(void *ptr)"),
        marker("realloc wrapper", "extern \"C\" void *realloc(void *ptr, size_t size)"),
        marker("alloc wrappers disable checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("alloc wrappers enable checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/alloc/mmapwrappers.cpp",
        marker("mmap wrapper gate", "#ifdef ENABLE_MMAP_WRAPPERS"),
        marker("mmap wrapper", "extern \"C\" void *mmap(void *addr, size_t length, int prot, int flags,"),
        marker("mmap64 wrapper", "extern \"C\" void *mmap64(void *addr, size_t length, int prot, int flags,"),
        marker("munmap wrapper", "munmap(void *addr, size_t length)"),
        marker("mremap wrapper", "extern \"C\" void *mremap(void *old_address, size_t old_size,"),
        marker("mmap wrappers disable checkpoint", "DMTCP_PLUGIN_DISABLE_CKPT();"),
        marker("mmap wrappers enable checkpoint", "DMTCP_PLUGIN_ENABLE_CKPT();"),
    ),
    file_check(
        "src/plugin/dl/dlwrappers.cpp",
        marker("real dlopen chain", "#define _real_dlopen  NEXT_FNC(dlopen)"),
        marker("real dlclose chain", "#define _real_dlclose NEXT_FNC(dlclose)"),
        marker("dlopen wrapper", "void *dlopen(const char *filename, int flag)"),
        marker("dlclose wrapper", "dlclose(void *handle)"),
        marker("dl wrapper recursion guard", "thread_performing_dlopen_dlsym"),
    ),
    file_check(
        "test/sysv-shm1.c",
        marker("sysv shm IPC_PRIVATE coverage", "shmget(IPC_PRIVATE"),
        marker("sysv shm attach coverage", "shmat(shmid"),
        marker("sysv shmctl coverage", "shmctl(shmid"),
    ),
    file_check(
        "test/sysv-shm2.c",
        marker("sysv shm named helper", "create_named_shm(size_t size)"),
        marker("sysv shm keyed coverage", "shmget(key, size, IPC_CREAT | 0666)"),
        marker("sysv shm double attach coverage", "Child: second shmat failed"),
    ),
    file_check(
        "test/sysv-sem.c",
        marker("sysv semget coverage", "semget((key_t)9977"),
        marker("sysv semop coverage", "semop(semid"),
        marker("sysv semctl coverage", "semctl(parent_semid"),
        marker("sysv sem shm coverage", "shmget((key_t)9979"),
    ),
    file_check(
        "test/sysv-msg.c",
        marker("sysv msgget coverage", "msgget((key_t)9977"),
        marker("sysv msgsnd coverage", "msgsnd(msqid"),
        marker("sysv msgrcv coverage", "msgrcv(msqid"),
    ),
    file_check(
        "test/timer1.c",
        marker("timer_create test coverage", "timer_create(CLOCKID"),
        marker("timer_settime test coverage", "timer_settime(timerid"),
    ),
    file_check(
        "test/dlopen1.c",
        marker("dlopen c lib1 coverage", "dlopen(\"test/libdlopen-lib1.so\", RTLD_NOW)"),
        marker("dlopen c lib2 coverage", "dlopen(\"test/libdlopen-lib2.so\", RTLD_LAZY)"),
        marker("dlsym c coverage", "dlsym(handle, \"fnc\")"),
    ),
    file_check(
        "test/dlopen2.cpp",
        marker("dlopen cpp lib3 coverage", "dlopen(\"test/libdlopen-lib3.so\", RTLD_NOW)"),
        marker("dlopen cpp lib4 coverage", "dlopen(\"test/libdlopen-lib4.so\", RTLD_LAZY)"),
        marker("dlsym cpp coverage", "dlsym(handle, \"fnc\")"),
    ),
    file_check(
        "test/plugin-init.cpp",
        marker("plugin init includes public ABI", "#include \"dmtcp.h\""),
        marker("plugin init event hook", "void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t* data)"),
        marker("plugin init event coverage", "DMTCP_EVENT_INIT"),
    ),
)

SOURCE_MAP_FORBIDDEN_CHECKS = (
    file_check(
        "src/dmtcp_launch.cpp",
        marker("stale alloc preload entry", "libdmtcp_alloc.so"),
        marker("stale dl preload entry", "libdmtcp_dl.so"),
    ),
    file_check(
        "src/plugin/Makefile.am",
        marker("stale alloc DSO ownership", "libdmtcp_alloc.so"),
        marker("stale alloc DSO source variable", "__d_libdir__libdmtcp_alloc_so_SOURCES"),
        marker("stale dl DSO ownership", "libdmtcp_dl.so"),
        marker("stale dl DSO source variable", "__d_libdir__libdmtcp_dl_so_SOURCES"),
        marker("stale alloc wrapper source under plugin makefile", "alloc/mallocwrappers.cpp"),
        marker("stale dl wrapper source under plugin makefile", "dl/dlwrappers.cpp"),
    ),
    file_check(
        "src/plugin/Makefile.in",
        marker("stale alloc DSO ownership", "libdmtcp_alloc.so"),
        marker("stale alloc DSO source variable", "__d_libdir__libdmtcp_alloc_so_SOURCES"),
        marker("stale dl DSO ownership", "libdmtcp_dl.so"),
        marker("stale dl DSO source variable", "__d_libdir__libdmtcp_dl_so_SOURCES"),
        marker("stale alloc wrapper source under plugin makefile", "alloc/mallocwrappers.cpp"),
        marker("stale dl wrapper source under plugin makefile", "dl/dlwrappers.cpp"),
    ),
    file_check(
        "src/plugin/Makefile",
        marker("stale alloc DSO ownership", "libdmtcp_alloc.so"),
        marker("stale alloc DSO source variable", "__d_libdir__libdmtcp_alloc_so_SOURCES"),
        marker("stale dl DSO ownership", "libdmtcp_dl.so"),
        marker("stale dl DSO source variable", "__d_libdir__libdmtcp_dl_so_SOURCES"),
        marker("stale alloc wrapper source under plugin makefile", "alloc/mallocwrappers.cpp"),
        marker("stale dl wrapper source under plugin makefile", "dl/dlwrappers.cpp"),
    ),
)

PREFLIGHT_DOC_CHECKS = (
    file_check(
        PREFLIGHT_DOC_PATH,
        marker("preflight title", "Plugin consolidation preflight"),
        marker("source evidence boundary section", "Source evidence boundary"),
        marker("contract proof boundary", "contract proof, not runtime proof"),
        marker("built-in enable-state section", "Built-in enable state"),
        marker("DMTCP_PLUGIN enable state", "DMTCP_PLUGIN"),
        marker("disable all plugins enable state", "--disable-all-plugins"),
        marker("alloc plugin enable env", "DMTCP_ALLOC_PLUGIN"),
        marker("dl plugin enable env", "DMTCP_DL_PLUGIN"),
        marker("hijack libs enable env", "DMTCP_HIJACK_LIBS"),
        marker("hijack libs m32 enable env", "DMTCP_HIJACK_LIBS_M32"),
        marker("original preload enable env", "DMTCP_ORIG_LD_PRELOAD"),
        marker("restart argument propagation", "restart argument propagation"),
        marker("exec-time preload reconstruction", "exec-time preload reconstruction"),
        marker("descriptor-bearing built-ins", "descriptor-bearing built-ins"),
        marker("wrapper-only alloc and DL", "wrapper-only alloc and DL"),
        marker("unchanged external ABI", "unchanged external DMTCP_DECL_PLUGIN ABI"),
        marker("wrapper ownership section", "Wrapper ownership"),
        marker("wrapper ownership current owner header", "Current owner"),
        marker("wrapper ownership future owner header", "Future owner"),
        marker("wrapper ownership collision risk header", "Collision risk"),
        marker("wrapper ownership audit target header", "Audit target"),
        marker("wrapper ownership hard stop header", "Hard stop rule"),
        marker("timer matrix row", "| timer |"),
        marker("SysV IPC matrix row", "| SysV IPC |"),
        marker("IPC matrix row", "| IPC |"),
        marker("PID matrix row", "| PID |"),
        marker("alloc matrix row", "| alloc |"),
        marker("DL matrix row", "| DL |"),
        marker("PID interaction ownership", "fork, vfork, fcntl, syscall, wait, timer wrapper interactions, SysV IPC wrapper interactions, and POSIX mq_notify ownership"),
        marker("duplicate symbol audit section", "Duplicate symbol audit"),
        marker("nm audit command", "nm -D --defined-only"),
        marker("readelf audit command", "readelf -Ws"),
        marker("after build command class", "after a build"),
        marker("stale artifact cleanup section", "Stale artifact cleanup"),
        marker("removed timer artifact", "libdmtcp_timer.so"),
        marker("removed svipc artifact", "libdmtcp_svipc.so"),
        marker("removed ipc artifact", "libdmtcp_ipc.so"),
        marker("removed pid artifact", "libdmtcp_pid.so"),
        marker("removed alloc artifact", "libdmtcp_alloc.so"),
        marker("removed dl artifact", "libdmtcp_dl.so"),
        marker("rollback gate section", "Rollback gate"),
        marker("requirement coverage section", "Requirement coverage"),
        marker("R003 ownership", "R003 is owned here"),
        marker("R006 downstream", "R006 is a downstream proof gate"),
        marker("R007 downstream", "R007 is a downstream proof gate"),
        marker("R008 downstream", "R008 is a downstream proof gate"),
        marker("source-map verifier command", "python3 test/verify-consolidation-preflight.py source-map"),
    ),
)

INTEGRATION_CHECKS = (
    file_check(
        PREFLIGHT_DOC_PATH,
        marker("launch source cross-reference", "src/dmtcp_launch.cpp"),
        marker("exec wrappers source cross-reference", "src/execwrappers.cpp"),
        marker("plugin ABI source cross-reference", "include/dmtcp.h"),
        marker("plugin manager source cross-reference", "src/pluginmanager.cpp"),
        marker("core DSO source-list cross-reference", "src/Makefile.am"),
        marker("plugin DSO source-list cross-reference", "src/plugin/Makefile.am"),
        marker("sysv shm focused test cross-reference", "test/sysv-shm1.c"),
        marker("integration verifier command", "python3 test/verify-consolidation-preflight.py integration"),
    ),
    file_check(
        DESIGN_DOC_PATH,
        marker("design points to S01 preflight", PREFLIGHT_DOC_PATH),
        marker("design names M002 first gate", "M002 first gate"),
        marker("design requires full preflight verifier", "python3 test/verify-consolidation-preflight.py full"),
        marker("design preserves deferred runtime boundary", "without claiming runtime proof"),
    ),
    file_check(
        MIGRATION_DOC_PATH,
        marker("migration points to S01 preflight", PREFLIGHT_DOC_PATH),
        marker("migration has preflight first gate section", "S01 preflight first gate"),
        marker("migration requires integration preflight verifier", "python3 test/verify-consolidation-preflight.py integration"),
        marker("migration requires full preflight verifier", "python3 test/verify-consolidation-preflight.py full"),
        marker("migration gates every implementation stage", "before timer, SysV IPC, IPC, PID, alloc, or DL source movement"),
        marker("migration preserves deferred proof boundary", "remain M003, M004, or M005 handoffs"),
    ),
)


def repo_root():
    return Path(__file__).resolve().parents[1]


def is_forbidden_config_path(path):
    rel = Path(path)
    if rel.is_absolute():
        return "absolute paths are not allowed"

    parts = rel.parts
    if not parts:
        return "empty path is not allowed"

    for part in parts:
        if part in ("", ".", ".."):  # no implicit cwd or parent traversal
            return "path traversal is not allowed"
        if part.startswith("."):
            return "hidden path component is not allowed"
        if part in FORBIDDEN_HIDDEN_COMPONENTS:
            return "hidden path component is not allowed"
        if part in FORBIDDEN_GENERATED_COMPONENTS:
            return "generated build path component is not allowed"

    # These names are ambiguous in a source tree and commonly refer to installed
    # library roots.  Source paths in this verifier should be under src/, include/,
    # test/, doc/, etc., not local installation prefixes.
    first = parts[0]
    if first in FORBIDDEN_INSTALLED_PREFIXES:
        return "installed library path prefix is not allowed"

    return None


def read_text(path):
    with io.open(str(path), "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def verify_file_checks(checks, root):
    errors = []
    checked_paths = 0
    checked_markers = 0

    for check in checks:
        reason = is_forbidden_config_path(check.path)
        if reason is not None:
            errors.append("hidden/generated path forbidden: {0} ({1})".format(check.path, reason))
            continue

        full_path = root / check.path
        if not full_path.is_file():
            errors.append("missing path: {0}".format(check.path))
            continue

        try:
            resolved_root = root.resolve()
            resolved_path = full_path.resolve()
            resolved_path.relative_to(resolved_root)
        except ValueError:
            errors.append("hidden/generated path forbidden: {0} (resolves outside repository)".format(check.path))
            continue

        checked_paths += 1
        text = read_text(full_path)
        for mark in check.markers:
            checked_markers += 1
            if not any(needle in text for needle in mark.needles):
                errors.append("missing marker: {0} :: {1}".format(check.path, mark.label))

    return errors, Stats(checked_paths, checked_markers)


def verify_forbidden_file_checks(checks, root):
    errors = []
    checked_paths = 0
    checked_markers = 0

    for check in checks:
        reason = is_forbidden_config_path(check.path)
        if reason is not None:
            errors.append("hidden/generated path forbidden: {0} ({1})".format(check.path, reason))
            continue

        full_path = root / check.path
        if not full_path.is_file():
            errors.append("missing path: {0}".format(check.path))
            continue

        checked_paths += 1
        text = read_text(full_path)
        for mark in check.markers:
            checked_markers += 1
            hits = [
                "{0}:{1}: {2}".format(check.path, line_no, line.strip())
                for line_no, line in enumerate(text.splitlines(), 1)
                if any(needle in line for needle in mark.needles)
            ]
            if hits:
                errors.append("forbidden marker present: {0} :: {1} :: {2}".format(
                    check.path, mark.label, "; ".join(hits[:3])))

    return errors, Stats(checked_paths, checked_markers)


def find_forbidden_doc_citations(path, text):
    errors = []
    for line_no, line in enumerate(text.splitlines(), 1):
        for token in FORBIDDEN_DOC_CITATIONS:
            if token in line:
                errors.append("hidden path citation: {0}:{1}: {2}".format(path, line_no, token))
    return errors


def find_forbidden_doc_content(path, text):
    errors = []
    forbidden_tokens = (
        ("TBD", "template placeholder"),
        ("{{", "template placeholder"),
        ("}}", "template placeholder"),
    )
    forbidden_runtime_claims = (
        "S01 proves runtime",
        "S01 runtime build passed",
        "S01 build passed",
        "S01 checkpoint/restart passed",
        "S01 sanitizer passed",
        "S01 external plugin compatibility passed",
        "S01 proves external plugin compatibility",
        "runtime tests passed",
        "make check passed",
        "checkpoint/restart passed",
        "TSAN passed",
    )

    for line_no, line in enumerate(text.splitlines(), 1):
        for token, label in forbidden_tokens:
            if token in line:
                errors.append("forbidden {0}: {1}:{2}: {3}".format(label, path, line_no, token))
        for phrase in forbidden_runtime_claims:
            if phrase in line:
                errors.append("unsupported runtime claim: {0}:{1}: {2}".format(path, line_no, phrase))
    return errors


def scan_forbidden_doc_content(path, root):
    reason = is_forbidden_config_path(path)
    if reason is not None:
        return ["hidden/generated path forbidden: {0} ({1})".format(path, reason)]

    full_path = root / path
    if not full_path.is_file():
        return []

    return find_forbidden_doc_content(path, read_text(full_path))


def scan_forbidden_doc_citations(path, root):
    reason = is_forbidden_config_path(path)
    if reason is not None:
        return ["hidden/generated path forbidden: {0} ({1})".format(path, reason)]

    full_path = root / path
    if not full_path.is_file():
        return []

    return find_forbidden_doc_citations(path, read_text(full_path))


def scan_forbidden_docs(paths, root):
    errors = []
    for path in paths:
        errors.extend(scan_forbidden_doc_citations(path, root))
        errors.extend(scan_forbidden_doc_content(path, root))
    return errors


def emit_result(mode, errors, stats, out, err):
    if errors:
        err.write("{0}: FAIL ({1} issue(s))\n".format(mode, len(errors)))
        for issue in errors:
            err.write("- {0}\n".format(issue))
        return 1

    out.write("{0}: ok ({1} paths, {2} markers)\n".format(mode, stats.paths, stats.markers))
    return 0


def run_source_map(root, out, err):
    errors, stats = verify_file_checks(SOURCE_MAP_CHECKS, root)
    forbidden_errors, forbidden_stats = verify_forbidden_file_checks(SOURCE_MAP_FORBIDDEN_CHECKS, root)
    errors.extend(forbidden_errors)
    stats = Stats(stats.paths + forbidden_stats.paths, stats.markers + forbidden_stats.markers)
    return emit_result("source-map", errors, stats, out, err)


def run_preflight_doc(root, out, err):
    errors, stats = verify_file_checks(PREFLIGHT_DOC_CHECKS, root)
    errors.extend(scan_forbidden_doc_citations(PREFLIGHT_DOC_PATH, root))
    errors.extend(scan_forbidden_doc_content(PREFLIGHT_DOC_PATH, root))
    return emit_result("preflight-doc", errors, stats, out, err)


def run_integration(root, out, err):
    errors, stats = verify_file_checks(INTEGRATION_CHECKS, root)
    errors.extend(scan_forbidden_docs(INTEGRATION_DOC_PATHS, root))
    return emit_result("integration", errors, stats, out, err)


def run_full(root, out, err):
    total_stats = Stats(0, 0)
    all_errors = []
    for label, checks in (
        ("source-map", SOURCE_MAP_CHECKS),
        ("preflight-doc", PREFLIGHT_DOC_CHECKS),
        ("integration", INTEGRATION_CHECKS),
    ):
        errors, stats = verify_file_checks(checks, root)
        if label == "source-map":
            forbidden_errors, forbidden_stats = verify_forbidden_file_checks(SOURCE_MAP_FORBIDDEN_CHECKS, root)
            errors.extend(forbidden_errors)
            stats = Stats(stats.paths + forbidden_stats.paths, stats.markers + forbidden_stats.markers)
        if label == "preflight-doc":
            errors.extend(scan_forbidden_doc_citations(PREFLIGHT_DOC_PATH, root))
            errors.extend(scan_forbidden_doc_content(PREFLIGHT_DOC_PATH, root))
        elif label == "integration":
            errors.extend(scan_forbidden_docs(INTEGRATION_DOC_PATHS, root))
        all_errors.extend("{0}: {1}".format(label, issue) for issue in errors)
        total_stats = Stats(total_stats.paths + stats.paths, total_stats.markers + stats.markers)
    return emit_result("full", all_errors, total_stats, out, err)


def run_self_test(root, out, err):
    failures = []

    expected_modes = ("self-test", "source-map", "preflight-doc", "integration", "full")
    if MODE_NAMES != expected_modes:
        failures.append("mode table drifted: {0}".format(", ".join(MODE_NAMES)))

    integration_paths = tuple(check.path for check in INTEGRATION_CHECKS)
    if DESIGN_DOC_PATH not in integration_paths:
        failures.append("integration mode does not check the design appendix")
    if MIGRATION_DOC_PATH not in integration_paths:
        failures.append("integration mode does not check the migration appendix")
    for required_path in (DESIGN_DOC_PATH, MIGRATION_DOC_PATH):
        matching = [check for check in INTEGRATION_CHECKS if check.path == required_path]
        if not any(any(PREFLIGHT_DOC_PATH in mark.needles for mark in check.markers) for check in matching):
            failures.append("integration mode does not require the preflight link in {0}".format(required_path))

    unsupported_out = io.StringIO()
    unsupported_err = io.StringIO()
    unsupported_rc = main(["definitely-unsupported-mode"], unsupported_out, unsupported_err, root)
    if unsupported_rc == 0:
        failures.append("unsupported mode returned exit code 0")
    if "unsupported mode" not in unsupported_err.getvalue():
        failures.append("unsupported mode did not explain the mode failure")

    hidden_errors, _ = verify_file_checks((file_check(".planning/ROADMAP.md"),), root)
    if not any("hidden/generated path forbidden" in issue for issue in hidden_errors):
        failures.append("hidden planning path was not rejected")

    stale_row_errors, _ = verify_forbidden_file_checks(
        (file_check("test/verify-consolidation-preflight.py",
                    marker("intentional stale alloc row", "libdmtcp_alloc.so"),
                    marker("intentional stale dl row", "libdmtcp_dl.so")),),
        root,
    )
    if not any("intentional stale alloc row" in issue for issue in stale_row_errors):
        failures.append("stale alloc DSO row fixture was not rejected")
    if not any("intentional stale dl row" in issue for issue in stale_row_errors):
        failures.append("stale dl DSO row fixture was not rejected")

    missing_path_errors, _ = verify_file_checks((file_check("test/__missing_consolidation_source__.cpp"),), root)
    if not any(issue == "missing path: test/__missing_consolidation_source__.cpp" for issue in missing_path_errors):
        failures.append("missing source path was not reported")

    dynamic_missing = "__missing_marker_" + str(os.getpid()) + "__"
    missing_marker_errors, _ = verify_file_checks(
        (file_check("test/verify-consolidation-preflight.py", marker("intentional missing marker", dynamic_missing)),),
        root,
    )
    if not any("missing marker: test/verify-consolidation-preflight.py :: intentional missing marker" in issue
               for issue in missing_marker_errors):
        failures.append("missing source marker was not reported")

    doc_citation_errors = find_forbidden_doc_citations("synthetic", "clean line\nbad .gsd citation\n")
    if not any("hidden path citation: synthetic:2: .gsd" == issue for issue in doc_citation_errors):
        failures.append("hidden doc citation detection was not exercised")

    doc_content_errors = find_forbidden_doc_content(
        "synthetic", "clean line\nTBD\n{{placeholder}}\nS01 proves runtime\n")
    if not any("forbidden template placeholder: synthetic:2: TBD" == issue for issue in doc_content_errors):
        failures.append("TBD placeholder detection was not exercised")
    if not any("forbidden template placeholder: synthetic:3: {{" == issue for issue in doc_content_errors):
        failures.append("template brace detection was not exercised")
    if not any("unsupported runtime claim: synthetic:4: S01 proves runtime" == issue for issue in doc_content_errors):
        failures.append("runtime claim detection was not exercised")

    if failures:
        for failure in failures:
            err.write("self-test: {0}\n".format(failure))
        return 1

    out.write("self-test: ok ({0} modes, negative checks covered)\n".format(len(MODE_NAMES)))
    return 0


RUNNERS = {
    "self-test": run_self_test,
    "source-map": run_source_map,
    "preflight-doc": run_preflight_doc,
    "integration": run_integration,
    "full": run_full,
}


def usage():
    return "usage: {0} {{{1}}}\n".format(Path(sys.argv[0]).name, ",".join(MODE_NAMES))


def main(argv=None, out=None, err=None, root=None):
    if argv is None:
        argv = sys.argv[1:]
    if out is None:
        out = sys.stdout
    if err is None:
        err = sys.stderr
    if root is None:
        root = repo_root()

    if len(argv) != 1 or argv[0] in ("-h", "--help"):
        err.write(usage())
        return 2

    mode = argv[0]
    if mode not in RUNNERS:
        err.write("unsupported mode: {0}\n".format(mode))
        err.write(usage())
        return 2

    return RUNNERS[mode](root, out, err)


if __name__ == "__main__":
    sys.exit(main())
