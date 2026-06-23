# Autotest Parity Ledger

This ledger tracks old `autotest_old.py` coverage while the new Python
`autotest.py` harness becomes the authoritative runner. It is not a skip list
for normal test execution; it records migration status and why some old tests
are intentionally absent.

Use this file when porting old tests so omissions are deliberate. Known
failures on `main`, such as current terminal/editor cases around `vim`, should
stay deferred until they are triaged separately.

## Classification Rules

- **Port now**: direct `dmtcp_launch` checkpoint/restart tests that fit the
  default `TestSpec` model or a small hook.
- **Shim**: tests that should remain covered, but need special harness support
  such as PTY driving, MPI launch supervision, external-service setup, or
  legacy runtime probing.
- **Deferred**: tests that are known to fail on `main`, require unavailable
  host support, or were already disabled/broken in the old runner. Deferred
  tests are not migration gates until separately triaged.

## Authoritative Port-Now Set

These direct old-harness tests are ported to the new harness and are migration
gates for `make check`. Keep this list in sync with `./test/autotest.py --list`
when adding or removing new-harness coverage. Configure-dependent tests in
this table are authoritative when their corresponding probes, required files,
or executable-path checks enable them.

| Group | Ported tests |
| --- | --- |
| Harness and command protocol | `command-json-bcheckpoint`, `command-json-kill`, `command-json-quit`, `coordinator-exit-on-last`, `coordinator-replacement-worker`, `coordinator-reject-restart-while-running` |
| Core smoke and process state | `dmtcp1`, `dmtcp1-m32`, `dmtcp1-quiet`, `dmtcp1-trace`, `dmtcp2`, `dmtcp3`, `dmtcp4`, `dmtcp5`, `alarm`, `sched_test`, `coordinator-barrier`, `gettid`, `sigchild`, `rlimit-restore`, `rlimit-nofile`, `environ`, `realpath`, `forkexec`, `vfork1`, `vfork2`, `frisbee`, `checkpoint-header`, `restart-debug-pause`, `ckptdir-flag`, `ckpt-signal-flag`, `gzip-flag`, `no-gzip-flag`, `tmpdir-env`, `unique-ckpt-env`, `unique-ckpt-flag`, `selinux1`, `cma`, `waitpid`, `waitid-syscall` |
| Logging | `logging-runtime`, `logging-quiet`, `logging-overrides` |
| File, fd, and path behavior | `file1`, `file2`, `file3`, `stat`, `mmap1`, `mremap`, `poll`, `shared-fd1`, `shared-fd2`, `stale-fd`, `procfd1`, `epoll1`, `epoll2`, `gzip` |
| Threads and synchronization | `pthread1`, `pthread2`, `pthread3`, `pthread4`, `pthread5`, `pthread6`, `pthread_atfork1`, `pthread_atfork2`, `mutex1`, `mutex2`, `mutex3`, `mutex4`, `timer1`, `clock`, `gettimeofday` |
| IPC, sockets, and PTY smoke | `client-server`, `seqpacket`, `ssh1`, `shared-memory1`, `shared-memory2`, `shared-memory3`, `sysv-shm1`, `sysv-shm2`, `sysv-sem`, `sysv-msg`, `posix-mq1`, `posix-mq-close-untracked`, `pty1`, `pty2` |
| Plugins and events | `dlopen1`, `dlopen2`, `syscall-tester`, `presuspend`, `plugin-sleep2`, `plugin-example-db`, `plugin-init`, `poll-disable-event-plugin`, `popen1`, `restartdir`, `nocheckpoint` |
| Shells, terminal apps, and language/runtime smoke | `perl`, `python`, `bash`, `dash`, `zsh`, `readline`, `tcsh`, `script`, `vim`, `emacs`, `screen`, `java1`, `cilk1`, `matlab-nodisplay`, `openmp-1`, `openmp-2` |
| MPI smoke | `hellompich-n1`, `hellompich-n2`, `openmpi` |

## Ported With Explicit Limits

| Test | Status | Rationale |
| --- | --- | --- |
| `command-json-bcheckpoint` | Ported with `cycles=1` | This validates `dmtcp_command --json --bcheckpoint` against a live worker with one checkpoint/restart cycle. |
| `command-json-kill`, `command-json-quit` | Ported with `cycles=0` | These validate `dmtcp_command --json` completion behavior against live workers without running checkpoint/restart cycles. |
| `coordinator-exit-on-last` | Ported with `cycles=0` | This validates `--exit-on-last` against a real worker without adding a checkpoint/restart cycle. |
| `coordinator-replacement-worker` | Ported with `cycles=0` | This validates that a real replacement worker can join after one live worker disconnects, without adding a checkpoint/restart cycle. |
| `coordinator-reject-restart-while-running` | Ported with `cycles=0` | This validates coordinator rejection of a restart worker while the original computation is still running. It creates a checkpoint image first, then attempts `dmtcp_restart` before killing the original worker. |
| `coordinator-barrier` | Ported with `cycles=1` | This is a focused real-worker cross-check for normal two-worker coordinator barrier release. |
| `checkpoint-header` | Ported with `cycles=1` | This validates the fixed bootstrap records in an uncompressed checkpoint image without adding a second restart cycle. |
| `restart-debug-pause` | Ported with `cycles=1` | This validates that `dmtcp_restart --debug-restart-pause 1` pauses before the restarted worker rejoins the coordinator. The harness kills the paused restart after the bounded check so the suite cannot hang. |
| `ckptdir-flag` | Ported with `cycles=1` | This validates that launcher `--ckptdir` writes checkpoint images outside the default per-test checkpoint directory and that restart can consume them. |
| `ckpt-signal-flag` | Ported with `cycles=1` | This validates that launcher `--ckpt-signal` works for a normal checkpoint/restart cycle. |
| `gzip-flag` | Ported with `cycles=1`, disabled on AArch64 | This validates that explicit launcher `--gzip` creates a gzip checkpoint image where the platform supports gzip checkpoints. AArch64 is blocked because `dmtcp_launch` forces gzip off there. |
| `no-gzip-flag` | Ported with `cycles=1` | This validates that explicit launcher `--no-gzip` creates a plain checkpoint image. |
| `tmpdir-env` | Ported with `cycles=1` | This validates that `DMTCP_TMPDIR` can point at a private per-test directory. |
| `unique-ckpt-env` | Ported with `cycles=1` | This validates that `DMTCP_UNIQUE_CKPT_PLUGIN=1` places checkpoint images in unique checkpoint subdirectories. |
| `unique-ckpt-flag` | Ported with `cycles=1` | This validates that launcher `--enable-unique-checkpoint-filenames` places checkpoint images in unique checkpoint subdirectories. |
| `dmtcp1-m32` | Built-artifact-gated | Authoritative only when the multilib build produces `test/dmtcp1-m32`. |
| `readline`, `selinux1`, `cma`, `cilk1`, `pthread_atfork1`, `pthread_atfork2` | Built-artifact-gated | Configure and Makefile rules still decide whether these binaries can be built, but the Python registry now filters them by the resulting `test/<name>` artifact instead of duplicating those configure or architecture checks. |
| `openmp-1`, `openmp-2` | Built-artifact-gated, slow | These use the default two checkpoint/restart cycles and keep the old harness's `S=3*DEFAULT_S` checkpoint settle delay because checkpointing too early can race active OpenMP startup work. |
| `ssh1` | Configure-flag-gated, slow | Authoritative only when localhost SSH is available; the old harness used extra checkpoint settle time for this external-service case. |
| `nocheckpoint` | Ported with `cycles=1` | The second post-restart checkpoint needs separate debugging; keep the single-cycle behavior explicit. |
| `matlab-nodisplay` | Configure-flag-gated | The command path is discovered by configure and carried on the `TestSpec`; the registry filters it with `HAS_MATLAB`. |
| `tcsh`, `dash`, `zsh` | Path-gated | The harness registers these shell tests directly and filters them out when their absolute command path is missing. |
| `script` | Path-gated, slow | Uses a per-test work directory for the transcript artifact so the migrated harness does not write `dmtcp-test-typescript.tmp` in the repository root. |
| `vim` | Configure-flag-gated, PTY, slow, disabled on AArch64 | Uses explicit PTY launch and restart with the old harness's `-X -u DEFAULTS -i NONE` arguments. Ubuntu 24.04 AArch64 `vim` is built with PAC and fails under DMTCP on that host class, so the registry keeps the x64 coverage while blocking `AARCH64_HOST`. |
| `emacs` | Path-gated, PTY, slow | Uses explicit PTY launch and restart so `emacs -nw` has a controlling terminal. It preserves the old harness's `S=40*DEFAULT_S` checkpoint settle delay and `DMTCP_GZIP=0` setting, but uses `-Q` instead of only `--no-init-file` to avoid host site-start/native-compilation temp-file churn. |
| `screen` | Path-gated, PTY, slow | Uses explicit PTY launch and restart plus a private per-test `SCREENDIR` with `0700` permissions. This avoids depending on a host `/run/screen` directory that may be unavailable or unwritable in CI. |
| `hellompich-n1`, `hellompich-n2`, `openmpi` | Configure-flag-gated and built-artifact-gated | MPI smoke tests require configure-discovered launchers and the built MPI test binary; `openmpi` keeps the old `[5, 6]` peer-count allowance. |
| `java1` | Configure-flag-gated and required-file-gated | Requires both `HAS_JAVA`/`HAS_JAVAC` and the built `test/java1.class` artifact. |
| `shared-memory3` | Ported, slow | This old-disabled test now passes two checkpoint/restart cycles on the current host. It keeps the old harness's `S=10*DEFAULT_S` checkpoint settle delay. |

Logging-specific limits:

- `logging-runtime`: `cycles=1`; validates trace/restart logs.
- `logging-quiet`: `cycles=0`; validates quiet suppression.
- `logging-overrides`: `cycles=0`; validates component overrides.

## Slow Timing Parity

The old harness used `--slow` as a repeatable timing multiplier, not as a
separate test-selection class. The new harness preserves that behavior:
`./test/autotest.py --slow` multiplies checkpoint settle time and command
timeouts by 5, and repeating `--slow` multiplies by 5 again.

Tests that had explicit longer `S` timing in `autotest_old.py` are tagged
`slow` in the new registry so they can be listed or selected with
`./test/autotest.py --list --tag slow`.

## Deferred Old-Harness Tests

| Test or group | Status | Rationale / next step |
| --- | --- | --- |
| `gcl` | Deferred | The old runner reproduces a checkpoint-time failure when GCL is installed. Direct `/usr/bin/gcl` exits cleanly on stdin EOF in a non-interactive command, so the current command is not a stable checkpoint target. Keep it out of `make check` until a deterministic GCL workload is defined. |

## Old-Harness Disabled Tests

These were already disabled or commented in `autotest_old.py`; they are not new
regressions in the harness migration:

- `stack-growsdown`
- `posix-mq2`: still fails after the first restart. The second checkpoint can leave both workers suspended in POSIX MQ precheckpoint drain with no new checkpoint image. Investigate `mq_notify` restart/rearm and nonblocking or single-owner queue draining before re-enabling.
- `timer2`: still fails under DMTCP before checkpoint. The timer helper passes a virtual TID to the kernel `SIGEV_THREAD_ID` path, which returns `EINVAL`; fix helper real-TID handling and restart semantics before re-enabling.

## Porting Checklist

When moving a deferred test into the new harness, record:

- required command path, library, or configure probe
- expected peer counts
- environment overrides
- checkpoint/restart cycle count
- host-sensitive skip reason
- artifact needs, especially PTY, MPI, or external-service logs
- whether the test passes on `main` before treating it as a branch gate
