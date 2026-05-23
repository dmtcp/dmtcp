# Single-DSO Built-In Plugin Consolidation Implementation Plan

> **For agentic workers:** Use this as the canonical implementation plan for
> the single-DSO internal plugin consolidation. There are no date- or
> version-suffixed plan variants; update this file when the plan changes.

## Goal

Consolidate DMTCP built-in/internal plugin DSOs into one preloaded
`libdmtcp.so` while preserving logical plugin ownership, checkpoint/restart
behavior, runtime enable/disable controls, and external plugin loading.

The final shape keeps PluginManager as the only internal plugin harness. Folded
plugins are linked into `libdmtcp.so` through plugin-owned internal libraries.
Single-owner wrappers remain in plugin-owned source files. Shared wrapper
control flow is reserved for real multi-plugin collision points.

## Source Map

- `doc/superpowers/specs/libdmtcp-single-dso-design.md`: canonical design and
  invariants.
- `src/Makefile.am`: per-plugin internal libraries, `libdmtcp.so` link shape,
  whole-archive region for wrapper-only plugin archives, and stale folded DSO
  cleanup.
- `src/pluginmanager.cpp`, `src/pluginmanager.h`, `src/plugininfo.cpp`,
  `src/plugininfo.h`: internal plugin metadata, descriptor order, cached
  enable state, and descriptor registration.
- `include/dmtcp.h`: `DmtcpPluginDescriptor_t` internal metadata fields.
- `include/util.h`, `src/util_misc.cpp`: shared environment parsing helper.
- `src/constants.h`, `src/dmtcp_launch.cpp`, `src/util_exec.cpp`,
  `src/execwrappers.cpp`: launcher and exec propagation of internal plugin
  enable state.
- `src/wrappers.cpp`, `src/syscallsreal.c`, `src/syscallwrappers.h`: shared
  wrapper mechanics, `syscall()` dispatch, `mq_notify`, and remaining core
  composition points.
- `src/plugin/alloc`, `src/plugin/dl`, `src/plugin/event`, `src/plugin/file`,
  `src/plugin/socket`, `src/plugin/ssh`, `src/plugin/svipc`,
  `src/plugin/timer`, `src/plugin/pid`, `src/plugin/unique-ckpt`: folded
  plugin-owned logic.
- `AGENTS.md`, `TESTING.md`, `PROJECTS.md`: durable agent guidance, testing
  guidance, and known follow-up gaps.

## Implementation Phases

### 1. Documentation And Guardrails

- Land the design and implementation plan before code changes.
- Keep the docs unversioned: one canonical design doc and one canonical
  implementation plan.
- Record future-agent guidance in `AGENTS.md`, test strategy in `TESTING.md`,
  and broader known gaps in `PROJECTS.md`.
- Do not add temporary `test/verify*` scripts unless they will be maintained as
  part of a durable test path.

### 2. Build And PluginManager Infrastructure

- Define each folded plugin as a visible internal library in `src/Makefile.am`.
- Link the plugin internal libraries into `libdmtcp.so` under a
  `--whole-archive` region so wrapper-only objects are not dropped by archive
  extraction.
- Keep core sources in `__d_libdir__libdmtcp_so_SOURCES`; do not flatten all
  plugin sources into one large core source block.
- Extend `DmtcpPluginDescriptor_t` with internal plugin metadata instead of
  adding a separate built-in plugin registry.
- Derive each controlled internal plugin environment variable from the all-caps
  plugin name: `PID` maps to `DMTCP_PID_PLUGIN`.
- Cache internal plugin enable state in PluginManager initialization.
- Remove any separate `builtinplugins.*` harness.

### 3. Runtime Enable/Disable State

- Preserve existing launcher plugin controls and add runtime flag/env handling
  for folded internal plugins where needed.
- Keep `--disable-all-plugins` scoped to plugin policy. It must not disable
  core DMTCP mechanics needed for interposition, initialization, checkpoint
  coordination, protected fds, or exec replay.
- Keep old in-core plugins and newly folded plugins on the same descriptor and
  enable-state path.

### 4. Wrapper Ownership Folds

- Fold allocation wrappers without wrapper events; wrappers check the ALLOC
  plugin enable state directly.
- Fold dynamic-loader wrappers as plugin-owned code using `LibDlWrapperLock`.
- Fold IPC as separate plugin units: ssh, event, file, pty, and socket.
- Move shared IPC files from the historical IPC subtree to `src/plugin` or the
  owning plugin directory when needed.
- Fold SysV IPC after auditing pid-field translation. Prefer inline calls to
  `dmtcp_pid_real_to_virtual()` and `dmtcp_pid_virtual_to_real()` for command
  out fields.
- Fold timer wrappers and use PID helpers for `SIGEV_THREAD_ID` or CPU-clock
  pid/tid translation.
- Fold PID last among restart-sensitive plugins. Move wait and
  `process_vm_*` wrappers into PID ownership, simplify PID helper declarations,
  and keep helper expansion narrow.
- Fold unique-ckpt into `libdmtcp.so` while preserving its configure-time
  default and runtime launcher/env controls.

### 5. Shared Wrapper Composition

- Keep `open`, `close`, dup, and related fd lifecycle behavior on existing
  DMTCP event paths unless a concrete gap is found.
- Keep `syscall()` as a dispatcher to libc wrapper functions whenever possible.
  Use `_real_syscall()` only for documented recursion or ABI exceptions.
- Treat `mq_notify` as an explicit composition point because POSIX MQ and PID
  both need to mutate the same call.
- Preserve `fork` lifecycle mechanics in core and call minimal PID helpers for
  PID virtualization state. Ignore `vfork` for new design work in this pass.
- Replace migrated uses of `DMTCP_PLUGIN_DISABLE_CKPT()` and
  `DMTCP_PLUGIN_ENABLE_CKPT()` with `WrapperLock`, `WrapperLockExcl`, or
  `LibDlWrapperLock`.

### 6. Source Cleanup

- Remove stale folded built-in DSOs from launch/preload construction and
  packaging manifests.
- Remove unbuilt duplicate wrapper sources unless they are intentionally kept
  as documented references.
- Keep `src/plugin` as the uniform location for folded internal plugin code.
- Keep external plugins as separately loaded DSOs.

## Commit Boundaries

Keep commits logical and bisectable:

1. Canonical design and implementation docs.
2. Internal plugin enable-state infrastructure.
3. Wrapper lock modernization.
4. `syscall()` dispatcher normalization.
5. Allocation fold.
6. Dynamic-loader fold.
7. IPC plugin-unit fold.
8. SysV IPC fold.
9. Timer fold.
10. PID fold.
11. Unique-ckpt fold.
12. Durable agent/testing/follow-up docs.

Build-system updates that are required for a plugin fold should land in that
plugin's fold commit. Infrastructure that affects all folds should land before
folding starts.

## Verification

Run a fresh build after autosquashing or rebasing:

```sh
make -j"$(nproc)"
```

Run focused checkpoint/restart smoke tests before broader suites:

```sh
./test/autotest.py -v dmtcp1 dmtcp2 waitpid gettid
```

For IPC/plugin-sensitive changes, add representative coverage:

```sh
./test/autotest.py -v file1 file2 file3 shared-memory1 shared-memory2 sysv-shm1 sysv-shm2 sysv-sem sysv-msg posix-mq1 pty1 pty2 timer1
```

When the branch is otherwise stable, run:

```sh
make check
```

Pair runtime tests with source/build-layout audits for:

- stale folded built-in DSOs in `lib/dmtcp`, launcher preload tables, and
  Debian packaging manifests
- internal plugin descriptor order in PluginManager
- duplicate exported wrapper ownership
- duplicate `_real_*` helpers before PID link-in
- use of legacy checkpoint lock macros in migrated wrappers
- `DMTCP_HIJACK_LIBS` and `DMTCP_HIJACK_LIBS_M32` preserving one built-in DSO
  plus requested external plugin DSOs

## Known Gaps

Broader or pre-existing gaps should not be folded into the consolidation
commits unless they block correctness. Track those in `PROJECTS.md` with enough
context for a future focused change.
