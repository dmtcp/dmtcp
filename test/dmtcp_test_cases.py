#!/usr/bin/env python3

import os
import platform
import shutil
from random import sample
from typing import Iterable, List

import autotest_config
from dmtcp_test_harness import ROOT, TestSpec


def _config_yes(name: str) -> bool:
    return getattr(autotest_config, name, "no") == "yes"


def _use_m32() -> bool:
    return bool(getattr(autotest_config, "USE_M32", 0))


def _processor_is_arm() -> bool:
    processor = platform.processor() or os.uname().machine
    return processor.startswith("arm")


def _machine_is_armv7() -> bool:
    return os.uname().machine in ["armv7", "armv7l"]


def _launcher_allows_gzip() -> bool:
    if os.uname().machine == "aarch64":
        return False
    return shutil.which("gzip") is not None


def _frisbee_commands() -> List[str]:
    p1, p2, p3 = [str(port) for port in sample(range(2000, 10000), 3)]
    return [
        f"./test/frisbee {p1} localhost {p2}",
        f"./test/frisbee {p2} localhost {p3}",
        f"./test/frisbee {p3} localhost {p1} starter",
    ]


TESTS = [
    TestSpec("dmtcp1", 1, ["./test/dmtcp1"]),
    TestSpec("command-json-kill", 1, ["./test/dmtcp1"], cycles=0,
             limits=["cycles=0"]),
    TestSpec("command-json-quit", 1, ["./test/dmtcp1"], cycles=0,
             completion_command="--quit",
             limits=["cycles=0"]),
    TestSpec("command-json-bcheckpoint", 1, ["./test/dmtcp1"], cycles=1,
             checkpoint_command="--bcheckpoint",
             tags=["command-json", "checkpoint"],
             requirements=["real-worker"],
             limits=["cycles=1"]),
    TestSpec("coordinator-exit-on-last", 1, ["./test/dmtcp1"], cycles=0,
             completion_command="--kill-exit-on-last",
             coordinator_args=["--exit-on-last"],
             tags=["coordinator", "exit-on-last"],
             requirements=["real-worker"],
             limits=["cycles=0"]),
    TestSpec("dmtcp2", 1, ["./test/dmtcp2"]),
    TestSpec("dmtcp3", 1, ["./test/dmtcp3"]),
    TestSpec("dmtcp4", 1, ["./test/dmtcp4"]),
    TestSpec("alarm", 1, ["./test/alarm"]),
    TestSpec("sched_test", 2, ["./test/sched_test"]),
    TestSpec("coordinator-barrier", 2, ["./test/sched_test"], cycles=1,
             tags=["coordinator", "barrier"],
             requirements=["real-worker"],
             limits=["cycles=1"]),
    TestSpec("gettid", 1, ["./test/gettid"]),
    TestSpec("file1", 1, ["./test/file1"]),
    TestSpec("file3", 1, ["./test/file3"]),
    TestSpec("stat", 1, ["./test/stat"]),
    TestSpec("mmap1", 1, ["./test/mmap1"]),
    TestSpec("mremap", 1, ["./test/mremap"]),
    TestSpec("gettimeofday", 1, ["./test/gettimeofday"]),
    TestSpec("sigchild", 1, ["./test/sigchild"]),
    TestSpec("rlimit-restore", 1, ["./test/rlimit-restore"]),
    TestSpec("poll", 1, ["./test/poll"]),
    TestSpec("environ", 1, ["./test/environ"]),
    TestSpec("realpath", 1, ["./test/realpath"]),
    TestSpec("pthread1", 1, ["./test/pthread1"]),
    TestSpec("pthread2", 1, ["./test/pthread2"]),
    TestSpec("pthread4", 1, ["./test/pthread4"]),
    TestSpec("pthread5", 1, ["./test/pthread5"]),
    TestSpec("pthread6", 1, ["./test/pthread6"]),
    TestSpec("mutex1", 1, ["./test/mutex1"]),
    TestSpec("mutex2", 1, ["./test/mutex2"]),
    TestSpec("mutex3", 1, ["./test/mutex3"]),
    TestSpec("mutex4", 1, ["./test/mutex4"]),
    TestSpec("timer1", 1, ["./test/timer1"]),
    TestSpec("clock", 1, ["./test/clock"]),
    TestSpec("dlopen1", 1, ["./test/dlopen1"]),
    TestSpec("dmtcp5", 2, ["./test/dmtcp5"]),
    TestSpec("syscall-tester", 1,
             ["--checkpoint-open-files ./test/syscall-tester"],
             checkpoint_command="--kcheckpoint"),
    TestSpec("shared-fd1", 2, ["./test/shared-fd1"]),
    TestSpec("shared-fd2", 2, ["./test/shared-fd2"]),
    TestSpec("stale-fd", 2, ["./test/stale-fd"]),
    TestSpec("rlimit-nofile", 2, ["./test/rlimit-nofile"]),
    TestSpec("procfd1", 2, ["./test/procfd1"]),
    TestSpec("epoll1", 2, ["./test/epoll1"]),
    TestSpec("forkexec", 2, ["./test/forkexec"]),
    TestSpec("client-server", 2, ["./test/client-server"]),
    TestSpec("seqpacket", 2, ["./test/seqpacket"]),
    TestSpec("shared-memory1", 2, ["./test/shared-memory1"]),
    TestSpec("shared-memory2", 2, ["./test/shared-memory2"]),
    TestSpec("sysv-shm1", 2, ["./test/sysv-shm1"]),
    TestSpec("sysv-shm2", 2, ["./test/sysv-shm2"]),
    TestSpec("sysv-sem", 2, ["./test/sysv-sem"]),
    TestSpec("sysv-msg", 2, ["./test/sysv-msg"]),
    TestSpec("file2", 1, ["./test/file2"], pre_checkpoint_delay=3.0),
    TestSpec("presuspend", [1, 2], ["./test/presuspend"]),
    TestSpec("plugin-sleep2", 1,
             [
                 "--with-plugin "
                 f"{ROOT}/test/plugin/sleep1/libdmtcp_sleep1.so:"
                 f"{ROOT}/test/plugin/sleep2/libdmtcp_sleep2.so "
                 "./test/dmtcp1"
             ]),
    TestSpec("plugin-init", 1,
             [
                 f"--with-plugin {ROOT}/test/libdmtcp_plugin-init.so "
                 "./test/dmtcp1"
             ]),
    TestSpec("popen1", [1, 2], ["./test/popen1"]),
    TestSpec("poll-disable-event-plugin", 1,
             ["--disable-event-plugin ./test/poll"]),
    TestSpec("pthread3", 1, ["./test/pthread2 80"]),
    TestSpec("restartdir", 1, ["./test/dmtcp1"],
             restart_uses_directory=True),
    TestSpec("pty1", 2, ["./test/pty1"]),
    TestSpec("pty2", 2, ["./test/pty2"]),
    TestSpec("vfork1", [1, 2, 3, 4], ["./test/vfork1 'ls | wc'"]),
    TestSpec("vfork2", [2, 3, 4],
             ["./test/vfork1 'while true; do date; sleep 1; done'"]),
    TestSpec("frisbee", 3, _frisbee_commands(),
             env={"DMTCP_GZIP": "1"}, post_launch_delay=2.0),
    TestSpec("nocheckpoint", [1, 2], ["./test/nocheckpoint"], cycles=1,
             limits=["cycles=1"]),
    TestSpec("checkpoint-header", 1, ["./test/dmtcp1"], cycles=1,
             env={"DMTCP_GZIP": "0"},
             validate_checkpoint_headers=True,
             expect_checkpoint_gzip=False,
             tags=["checkpoint-header"],
             requirements=["plain-checkpoint-image"],
             limits=["cycles=1"]),
    TestSpec("gzip-invalid-env", 1, ["./test/dmtcp1"], cycles=1,
             env={"DMTCP_GZIP": "12x"},
             validate_checkpoint_headers=True,
             expect_checkpoint_gzip=False,
             tags=["checkpoint-header", "gzip"],
             requirements=["invalid-gzip-env"],
             limits=["cycles=1"]),
]

if _config_yes("HAS_EPOLL_CREATE1"):
    TESTS.append(TestSpec("epoll2", 2,
                          ["./test/epoll1 --use-epoll-create1"]))

if not _use_m32():
    TESTS.append(TestSpec("dlopen2", 1, ["./test/dlopen2"]))

if _config_yes("HAS_CMA"):
    TESTS.append(TestSpec("cma", 2, ["./test/cma"]))

if not _processor_is_arm() and _config_yes("TEST_POSIX_MQ"):
    TESTS.append(TestSpec("posix-mq1", 2, ["./test/posix-mq1"]))
    if _config_yes("HAS_SYS_MQ_OPEN"):
        TESTS.append(TestSpec("posix-mq-close-untracked", 1,
                              ["./test/posix-mq-close-untracked"]))

if not _machine_is_armv7():
    TESTS.append(TestSpec("pthread_atfork1", 2,
                          ["./test/pthread_atfork1"],
                          library_paths=[f"{ROOT}/test"]))
    TESTS.append(TestSpec("pthread_atfork2", 2,
                          ["./test/pthread_atfork2"],
                          library_paths=[f"{ROOT}/test"]))

if not _use_m32():
    TESTS.append(TestSpec("waitpid", 2, ["./test/waitpid"],
                          pre_checkpoint_delay=1.0))
    TESTS.append(TestSpec("waitid-syscall", 1, ["./test/waitid-syscall"],
                          pre_checkpoint_delay=1.0))

if not _use_m32():
    TESTS.append(TestSpec("gzip", 1, ["./test/dmtcp1"],
                          env={"DMTCP_GZIP": "1"},
                          validate_checkpoint_headers=True,
                          expect_checkpoint_gzip=_launcher_allows_gzip(),
                          tags=["checkpoint-header", "gzip"],
                          requirements=["gzip-launcher"]))
    TESTS.append(TestSpec("perl", 1, ["/usr/bin/perl"],
                          post_launch_delay=2.0))
    if _config_yes("HAS_PYTHON") or _config_yes("HAS_PYTHON3"):
        python_cmd = "/usr/bin/env python"
        if not _config_yes("HAS_PYTHON"):
            python_cmd = "/usr/bin/env python3"
        TESTS.append(TestSpec("python", 1, [python_cmd],
                              post_launch_delay=2.0))
    TESTS.append(TestSpec("bash", 2,
                          ["/bin/bash --norc -c 'ls; sleep 30; ls'"],
                          env={"DMTCP_GZIP": "1"},
                          post_launch_delay=2.0))
    if _config_yes("HAS_DASH"):
        TESTS.append(TestSpec("dash", 2,
                              ["/bin/dash -c 'ls; sleep 30; ls'"],
                              env={"DMTCP_GZIP": "0", "ENV": None},
                              post_launch_delay=2.0))
    if _config_yes("HAS_ZSH"):
        TESTS.append(TestSpec("zsh", 2,
                              ["/bin/zsh -f -c 'ls; sleep 30; ls'"],
                              env={"DMTCP_GZIP": "0"},
                              post_launch_delay=2.0,
                              pre_checkpoint_delay=3.6))

if _config_yes("HAS_JAVA") and _config_yes("HAS_JAVAC"):
    TESTS.append(TestSpec("java1", 1, ["java -Xmx5M java1"],
                          env={"CLASSPATH": "./test"}))

if _config_yes("HAS_OPENMP"):
    TESTS.append(TestSpec("openmp-1", 1, ["./test/openmp-1"], cycles=1,
                          limits=["cycles=1"]))
    TESTS.append(TestSpec("openmp-2", 1, ["./test/openmp-2"], cycles=1,
                          limits=["cycles=1"]))


def iter_tests(names: List[str] = None) -> Iterable[TestSpec]:
    if not names:
        return iter(TESTS)
    selected = set(names)
    return (test for test in TESTS if test.name in selected)


def get_test(name: str) -> TestSpec:
    for test in TESTS:
        if test.name == name:
            return test
    raise KeyError(name)
