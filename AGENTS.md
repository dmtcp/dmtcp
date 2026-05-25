# Agent Notes

This repository is DMTCP, a checkpoint/restart system built around
`LD_PRELOAD` wrappers, a plugin manager, and checkpoint/restart events. Treat
changes as runtime-sensitive: wrapper reentry, early restart state, and plugin
enablement can affect correctness in ways that normal library code does not.

## Working Safely

- Preserve user changes. Inspect the worktree before editing, stay within the
  requested files, and do not revert unrelated changes.
- Avoid destructive git commands unless the user explicitly asks for them.
- Use `rg`/`rg --files` for search and follow nearby code patterns before
  introducing new abstractions.
- Keep changes narrow. DMTCP has many cross-cutting wrappers; a small-looking
  change can affect unrelated processes at checkpoint or restart.

## Architecture Conventions

- DMTCP behavior is split across preload wrappers, core runtime code, the
  plugin manager, and plugin-owned modules. Preserve those boundaries unless
  there is a clear cross-plugin reason to move code.
- Internal plugins are built into `libdmtcp.so`, but they should still be
  modeled as plugins through PluginManager descriptors. External plugins remain
  DSOs using the public ABI in `include/dmtcp.h`.
- Keep plugin-specific business logic in plugin-owned modules. Move a
  top-level/core wrapper only when multiple plugins need to affect the same
  wrapper behavior.
- Enable or disable internal plugin behavior at runtime through PluginManager
  descriptor state, launcher options, or environment controls rather than
  hard-wiring one global behavior.
- New wrapper code should use the modern `WrapperLock` pattern instead of the
  legacy `DMTCP_PLUGIN_DISABLE_CKPT` / `DMTCP_PLUGIN_ENABLE_CKPT` locking style.
  Be careful with wrappers that can synchronously invoke user signal handlers:
  kill-family wrappers have historically avoided wrapper locks because a
  signal-handler `longjmp()` can skip lock release.
- For simple PID/TID virtualization, prefer existing helpers such as
  `dmtcp_pid_virtual_to_real` and `dmtcp_pid_real_to_virtual` before adding new
  translation logic.
- Avoid calling wrapped functions from fragile internal paths, especially during
  early init, restart, TLS restore, or code that may already be inside a
  wrapper. Use `_real_*` helpers where wrapper reentry would be dangerous.
- The generic syscall wrapper should dispatch to libc where practical, so the
  normal wrapper for that libc function owns the behavior and plugin hooks stay
  centralized. For example, `SYS_socket` should call `socket()` and
  `SYS_wait4` should call `wait4()` unless a documented recursion or ABI issue
  requires `_real_syscall()`.
- DMTCP supports multiple libc, compiler, and architecture combinations. Be
  cautious with glibc-private struct fields, GCC/Clang codegen assumptions,
  32-bit builds, and architecture-specific syscall availability.
- Useful background docs live under `doc/`, including plugin, restart,
  fork/exec, PID/TID, thread-creation, multi-architecture, and coordinator
  behavior notes. Check them before changing long-standing runtime mechanics.

## Build And Test

- Common local build:
  - `./configure`
  - `make -j"$(nproc)"`
- Quick checkpoint/restart smoke test:
  - `./test/autotest.py -v dmtcp1`
- Broader verification:
  - `make check`
- After touching PluginManager metadata, Makefiles, launcher preload logic, or
  wrapper ownership, do a targeted source/build-layout audit for descriptor
  ordering, preload lists, stale built-in DSOs, and duplicate wrapper ownership.
- Run the quick smoke test before broad suites when changing wrappers, plugin
  behavior, restart paths, PID/TID virtualization, or syscall handling.
- DMTCP checkpoint/restart tests need local coordinator connectivity. Sandboxed
  runs can fail with coordinator connection errors unrelated to the code. If a
  test fails that way, rerun the relevant smoke test with local coordinator
  access before treating it as a code failure.
- `make check` is most useful after the smoke test is clean.

## Generated Files

- This repo may track Autotools-generated files. When changing `Makefile.am`,
  `configure.ac`, or related build inputs, check whether generated files need to
  be regenerated and kept in sync with the source build definitions.

## Debugging Guidance

- When debugging checkpoint/restart behavior, separate build failures,
  coordinator/connectivity failures, checkpoint-time failures, and restart-time
  failures before changing code.
- Look for wrapper reentry and event-ordering problems when failures occur in
  early init or restart paths.
- Prefer existing plugin manager state, event hooks, and virtualization helpers
  over ad hoc globals or duplicate translation tables.
- Use DMTCP logging and trace knobs when available instead of adding one-off
  prints; preserve logs from failing checkpoint/restart runs so ordering issues
  can be reconstructed.
- MPI, `UH_PRELOAD`, and heterogeneous deployment environments can have stricter
  preload, compiler, and ABI assumptions than a local smoke test. Treat changes
  to preload lists, wrapper exports, public descriptors, and generated build
  metadata as compatibility-sensitive.
- CodeRabbit or other automated reviews are useful input, not requirements.
  Verify each claim against this tree and distinguish new regressions from
  pre-existing gaps such as pidfd/clone3 coverage or older dead-code stubs.
- Keep notes generic and durable in this file. Do not record branch-specific
  regressions, temporary local fixes, stack traces, commit hashes, or bisect
  instructions here.
