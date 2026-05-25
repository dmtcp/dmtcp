# DMTCP Testing Guidance

DMTCP tests need to cover more than ordinary compile failures.  The highest
risk areas are wrapper interposition, plugin ordering, enable/disable state,
checkpoint/restart behavior, generated build metadata, and external-plugin ABI
compatibility.  Tests should be layered so cheap static failures are caught
before long checkpoint/restart suites run.

## Test Layers

Use three layers of tests:

1. Static verifier tests

   These should validate generated/build metadata, plugin descriptor order,
   wrapper ownership, and packaging expectations without launching DMTCP.
   They should run quickly and provide clear source-level failure messages.

2. Unit or component tests

   These should exercise small helper contracts directly, especially:
   plugin enable/disable parsing, PID virtual-to-real and real-to-virtual
   translation helpers, descriptor lookup, pluginmanager ordering, and wrapper
   dispatch decisions.  Prefer tests that do not require a coordinator.

3. Runtime integration tests

   These must launch real DMTCP jobs and perform checkpoint/restart cycles.
   They are required for wrapper interposition, restart ordering, thread state,
   process state, and external plugin compatibility.

## Regression Coverage

Every internal plugin should have at least one runtime smoke test for its
observable behavior. Tests should cover both ordinary launch behavior and
checkpoint/restart behavior when the plugin owns restart-sensitive state.

PID-related folding needs explicit coverage for:

- `getpid()`, `getppid()`, `gettid()`, `syscall(SYS_get*)`, `kill()`,
  `tkill()`, and `tgkill()` returning/translating virtual IDs correctly.
- Checkpoint and restart after restart, not just first-generation restart.
- Fork, exec, wait, pthread creation, pthread cancellation, and pthread signal
  wrappers.
- Multi-process restart cases where all peers connect to the coordinator and
  transition from restart to running.
- Real-kernel PID/TID use inside DMTCP internals, especially raw syscalls.

Plugin enable/disable behavior needs runtime coverage for:

- A default launch.
- `--disable-all-plugins`.
- Disabling one folded plugin at a time.
- Disabling PID while exercising pthread and scheduler wrappers that are now
  exported by `libdmtcp.so`.
- The equivalent environment-variable controls used by `dmtcp_launch`.

External plugin compatibility needs coverage for:

- A simple external plugin compiled against the current public header.
- A compatibility fixture that uses the previous public descriptor shape, or an
  explicit compile/runtime failure if the ABI is intentionally broken and the
  plugin API version has changed.
- Event delivery order relative to built-in plugins.

Unique checkpoint naming needs coverage for:

- The configure-time default.
- The runtime enable flag.
- The runtime disable flag.
- Restart scripts and checkpoint directories across multiple generations.

Build and packaging coverage should include:

- Generated `Makefile.in` and other Autotools outputs when build inputs change.
- `make clean` behavior for removed or folded plugin DSOs.
- 32-bit or multi-architecture builds when wrapper exports, syscall handling,
  public structs, or generated preload metadata changes.
- Configure-flag matrices for optional features such as unique checkpoint
  naming, fast restart paths, and architecture-specific syscall availability.

## Useful Smoke Sets

For quick local validation, start with:

```sh
./test/autotest.py -v dmtcp1 dmtcp2 dmtcp3 dmtcp4 dmtcp5 gettid
```

For PID and thread coverage, add:

```sh
./test/autotest.py -v forkexec waitpid pthread1 pthread2 pthread3 pthread4 pthread5 pthread6 pthread_atfork1 pthread_atfork2
```

For IPC/plugin coverage, add representative tests from:

```sh
./test/autotest.py -v file1 file2 file3 shared-memory1 shared-memory2 sysv-shm1 sysv-shm2 sysv-sem sysv-msg posix-mq1 pty1 pty2 timer1
```

Passing `dmtcp1` alone is not enough for folded-plugin changes. Pair runtime
tests with a targeted source/build-layout audit when the change affects
PluginManager metadata, Makefiles, launcher preload logic, wrapper ownership,
or folded built-in DSO layout.

Autotest options worth remembering:

- `-v` prints test progress and the underlying command.
- Pass explicit test names to keep smoke runs small while debugging.
- Preserve failing output directories and logs before rerunning a hanging test.

## Test Design Notes

Tests should verify observable behavior, not only source layout. A source audit
can show that a wrapper is compiled into `libdmtcp.so`, but only a runtime test
can prove that it preserves the right virtual ID, calls the real syscall at the
right time, and resumes correctly after restart.

When a bug is found through `git bisect`, add the regression test near the
commit that fixes the bug when practical.  If the test needs infrastructure
that does not yet exist, document the missing fixture and add the smallest
available smoke test first.

Keep tests deterministic.  Avoid arbitrary sleeps when a coordinator status,
file creation, process exit, or explicit pipe protocol can be used instead.

When adding a new runtime regression, put the small test program near similar
tests and wire it into the autotest list that already exercises that feature.
When adding source-layout checks, make the failure message name the stale file,
missing symbol, or unexpected descriptor order directly so the fix is obvious.

For failures in restart hangs, capture:

- The exact autotest command.
- The generated checkpoint directory.
- Coordinator status output.
- Relevant `dmtcpworker` and coordinator logs.
- `ps`, `/proc/<pid>/task/*/wchan`, and current syscall information for stuck
  processes.
- A gdb backtrace when symbols and stacks are usable.

This evidence should be enough for a future agent to distinguish a plugin-order
bug from a wrapper translation bug or a coordinator protocol bug.

For review-driven fixes, include a concrete reproducer whenever practical:
the failing autotest command, audit command, or smallest standalone program
that demonstrates the wrapper behavior. If a review finding is a known
pre-existing coverage gap, document the gap and avoid mixing broad cleanup into
an unrelated bugfix.

Coordinator access and filesystem behavior can differ under agent sandboxes.
If a checkpoint/restart test fails with coordinator connection errors or
permission-looking failures, rerun the smallest failing command with ordinary
local coordinator access before treating it as a code regression.
