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
this table are authoritative when their corresponding probes enable them.

| Group | Ported tests |
| --- | --- |
| Harness and command protocol | `command-json-bcheckpoint`, `command-json-kill`, `command-json-quit`, `coordinator-exit-on-last` |
| Core smoke and process state | `dmtcp1`, `dmtcp2`, `dmtcp3`, `dmtcp4`, `dmtcp5`, `alarm`, `sched_test`, `coordinator-barrier`, `gettid`, `sigchild`, `rlimit-restore`, `rlimit-nofile`, `environ`, `realpath`, `forkexec`, `vfork1`, `vfork2`, `frisbee`, `checkpoint-header`, `gzip-invalid-env`, `cma`, `waitpid`, `waitid-syscall` |
| File, fd, and path behavior | `file1`, `file2`, `file3`, `stat`, `mmap1`, `mremap`, `poll`, `shared-fd1`, `shared-fd2`, `stale-fd`, `procfd1`, `epoll1`, `epoll2`, `gzip` |
| Threads and synchronization | `pthread1`, `pthread2`, `pthread3`, `pthread4`, `pthread5`, `pthread6`, `pthread_atfork1`, `pthread_atfork2`, `mutex1`, `mutex2`, `mutex3`, `mutex4`, `timer1`, `clock`, `gettimeofday` |
| IPC, sockets, and PTY smoke | `client-server`, `seqpacket`, `shared-memory1`, `shared-memory2`, `sysv-shm1`, `sysv-shm2`, `sysv-sem`, `sysv-msg`, `posix-mq1`, `posix-mq-close-untracked`, `pty1`, `pty2` |
| Plugins and events | `dlopen1`, `dlopen2`, `syscall-tester`, `presuspend`, `plugin-sleep2`, `plugin-init`, `poll-disable-event-plugin`, `popen1`, `restartdir`, `nocheckpoint` |
| Shells and language/runtime smoke | `perl`, `python`, `bash`, `dash`, `zsh`, `java1`, `openmp-1`, `openmp-2` |

## Ported With Explicit Limits

| Test | Status | Rationale |
| --- | --- | --- |
| `command-json-bcheckpoint` | Ported with `cycles=1` | This validates `dmtcp_command --json --bcheckpoint` against a live worker with one checkpoint/restart cycle. |
| `command-json-kill`, `command-json-quit` | Ported with `cycles=0` | These validate `dmtcp_command --json` completion behavior against live workers without running checkpoint/restart cycles. |
| `coordinator-exit-on-last` | Ported with `cycles=0` | This validates `--exit-on-last` against a real worker without adding a checkpoint/restart cycle. |
| `coordinator-barrier` | Ported with `cycles=1` | This is a focused real-worker cross-check for normal two-worker coordinator barrier release. |
| `gzip-invalid-env` | Ported with `cycles=1` | This verifies invalid `DMTCP_GZIP` handling and checkpoint-header validation without adding a second restart cycle. |
| `nocheckpoint` | Ported with `cycles=1` | The second post-restart checkpoint needs separate debugging; keep the single-cycle behavior explicit. |
| `openmp-1`, `openmp-2` | Ported with `cycles=1` | Same second-cycle limitation as `nocheckpoint`; do not hide it behind the default cycle count. |

## Deferred Old-Harness Tests

| Test or group | Status | Rationale / next step |
| --- | --- | --- |
| `dmtcp1-m32` | Shim | Multilib parity needs a deliberate 32-bit harness gate and host capability check. |
| `selinux1` | Shim | Host-policy-sensitive SELinux coverage; port only with explicit environment probing. |
| `plugin-example-db` | Deferred | Old harness already marks this as broken/FIXME; fix plugin test setup before porting. |
| `ssh1` | Shim | Requires localhost SSH setup and stricter external-service diagnostics. |
| `readline`, `tcsh`, `emacs`, `script`, `screen` | Shim | PTY/terminal/editor class needs deliberate harness support before it is authoritative coverage. |
| `vim` | Deferred | Known to fail on `main`; not a migration gate until separately triaged. |
| `cilk1`, `gcl`, `matlab-nodisplay` | Shim | External/legacy runtime availability; add only with precise configure probes and artifacts. |
| `hellompich-n1`, `hellompich-n2`, `openmpi` | Shim | MPI launchers need separate process-tree, coordinator, and artifact handling. |

## Old-Harness Disabled Tests

These were already disabled or commented in `autotest_old.py`; they are not new
regressions in the harness migration:

- `stack-growsdown`
- `shared-memory3`
- `posix-mq2`
- `timer2`

## Porting Checklist

When moving a deferred test into the new harness, record:

- required command/library/configure probe
- expected peer counts
- environment overrides
- checkpoint/restart cycle count
- host-sensitive skip reason
- artifact needs, especially PTY, MPI, or external-service logs
- whether the test passes on `main` before treating it as a branch gate
