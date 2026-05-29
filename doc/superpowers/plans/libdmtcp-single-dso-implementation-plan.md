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

### 7. Plugin-Owned Real-Wrapper Initialization

Goal: keep plugin-only `_real_*` surfaces inside the owning plugin while
retaining the explicit initialization shape of `syscallwrappers.h` /
`syscallsreal.c` for wrappers that benefit from it.

Recommended direction: add plugin-owned real-wrapper initialization callbacks
for built-in/internal plugins, with a lazy plugin-local fallback for wrappers
that can execute before PluginManager has fully initialized.

Approach A, local `NEXT_FNC` macros in plugin wrapper headers:

- Pros: simplest implementation, no new init ordering rules, no public or
  internal descriptor changes, and each wrapper lazily resolves exactly the
  libc symbol it needs.
- Pros: robust for early wrappers because `NEXT_FNC` already handles the
  "initialize DMTCP if needed" path at the call site.
- Cons: repeated macro definitions spread across plugin headers, no single
  per-plugin audit point for required real symbols, and no startup-time
  validation that a required symbol can be resolved.
- Cons: each call site owns its static cache independently, which keeps the
  design simple but makes it harder to reason about symbol initialization as a
  plugin-owned subsystem.

Approach B, central `syscallwrappers.h` / `syscallsreal.c` for all folded
wrappers:

- Pros: one familiar initialization mechanism, one place to inspect real-call
  definitions, and no plugin-local boilerplate.
- Pros: easy for common wrappers and core-owned wrapper composition points.
- Cons: weakens plugin boundaries by making plugin-private `_real_*` helpers
  globally visible to core DMTCP.
- Cons: encourages accidental core calls into plugin-owned wrapper surfaces and
  grows the shared real-call table with single-owner plugin details.

Approach C, plugin-owned callback plus plugin-local function pointer table:

- Pros: keeps boundaries crisp: plugin-only real symbols live under
  `src/plugin/<owner>/`, while common wrappers remain in
  `syscallwrappers.h` / `syscallsreal.c`.
- Pros: gives each plugin one explicit real-symbol inventory and one
  initialization point, making future audits and generated checks easier.
- Pros: lets PluginManager pre-initialize enabled built-ins while wrappers keep
  a plugin-local lazy fallback for early calls.
- Cons: adds modest boilerplate per plugin: a real-wrapper header, a source
  file or private block that owns function pointers, and an init callback.
- Cons: initialization order must be documented. Allocation and dynamic-loader
  wrappers may need to keep direct lazy `NEXT_FNC` lookup until a callback path
  can be proven free of allocation/dlsym recursion.
- Cons: if the callback is added to public `DmtcpPluginDescriptor_t`, it
  changes the external plugin ABI. Prefer an internal-only callback field on
  `InternalPluginEntry` or a PluginManager-private table unless external
  plugins need the feature.

Implementation sketch for Approach C:

1. Add an internal callback field to `InternalPluginEntry` in
   `src/pluginmanager.cpp`:

   ```c++
   typedef void (*InternalPluginInitRealWrappersFn)();

   struct InternalPluginEntry {
     DmtcpPluginDescriptor_t *descriptor;
     InternalPluginInitRealWrappersFn initRealWrappers;
     bool enabled;
   };
   ```

2. During internal plugin initialization, call `initRealWrappers` for each
   built-in plugin that has one. Do this before descriptor registration so
   plugin event hooks observe initialized real-wrapper tables. Keep this
   internal-only; external plugin initialization remains chained through
   `dmtcp_initialize_plugin()`.

3. For each plugin, add a plugin-owned real wrapper unit. Example for the event
   plugin:

   ```c++
   // src/plugin/event/eventrealwrappers.h
   #pragma once
   #include <poll.h>
   #include <sys/select.h>

   void dmtcp_event_init_real_wrappers();
   int dmtcp_event_real_poll(struct pollfd *fds, nfds_t nfds, int timeout);
   int dmtcp_event_real_select(int nfds,
                               fd_set *readfds,
                               fd_set *writefds,
                               fd_set *exceptfds,
                               struct timeval *timeout);
   ```

   ```c++
   // src/plugin/event/eventrealwrappers.cpp
   #include "dmtcp.h"
   #include "eventrealwrappers.h"

   static int (*real_poll_fn)(struct pollfd *, nfds_t, int);
   static int (*real_select_fn)(int, fd_set *, fd_set *, fd_set *,
                                struct timeval *);

   void
   dmtcp_event_init_real_wrappers()
   {
     if (real_poll_fn == NULL) {
       real_poll_fn = NEXT_FNC(poll);
     }
     if (real_select_fn == NULL) {
       real_select_fn = NEXT_FNC(select);
     }
   }

   int
   dmtcp_event_real_poll(struct pollfd *fds, nfds_t nfds, int timeout)
   {
     dmtcp_event_init_real_wrappers();
     return real_poll_fn(fds, nfds, timeout);
   }
   ```

4. Convert plugin wrappers one plugin at a time from `_real_*` macros to
   plugin-local `dmtcp_<plugin>_real_<function>()` helpers. Keep each conversion
   behavior-neutral and include one focused smoke test for that wrapper family.

5. Leave these cases out of the first callback pass unless there is a specific
   reason to include them:

   - allocation wrappers, because allocator real lookup participates in
     bootstrap and recursion-sensitive paths
   - dynamic-loader wrappers, because `dlopen` / `dlclose` interact with
     dlsym and `LibDlWrapperLock`
   - common wrapper composition points such as `mq_notify`, which should stay
     on the shared/core path by design

6. Verification for each plugin conversion starts with a fresh build:

   ```sh
   make -j"$(nproc)"
   ```

   Then run the focused smoke command for the plugin being converted:

   ```sh
   ./test/autotest.py -v poll epoll1
   ./test/autotest.py -v pty1 pty2
   ./test/autotest.py -v client-server seqpacket
   ./test/autotest.py -v file1 file2 shared-fd1 posix-mq1
   ./test/autotest.py -v sysv-shm1 sysv-shm2 sysv-sem sysv-msg
   ./test/autotest.py -v timer1 clock
   ```

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
