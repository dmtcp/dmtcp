# Single-DSO DMTCP Built-In Plugin Consolidation

This is the canonical design for folding DMTCP's built-in/internal plugin DSOs
into one preloaded `libdmtcp.so` while preserving logical plugin ownership and
external plugin loading.

## Status

The branch implements the PluginManager-based design described here. Earlier
internal drafts explored a separate built-in state layer and broad wrapper-event
machinery; those ideas are intentionally not part of the final architecture.

## Goals

The user application preloads one built-in DMTCP DSO: `libdmtcp.so`.
External plugins requested by the user remain DSOs and keep the public plugin
contract as much as practical.

Every internal DMTCP plugin remains a logical plugin even though its code is
linked into `libdmtcp.so`. That includes old in-core plugins such as Terminal,
ProcessInfo, syslog, rlimit/float, alarm, path translation, and unique-pid, as
well as newly folded plugins such as allocation, dynamic loader, event, file,
pty, socket, SysV IPC, timer, and PID.

Plugin-specific wrapper policy stays in the owning plugin. Core DMTCP owns only
shared mechanics that cannot belong to one plugin: the preload shape, real-call
helpers, core lifecycle control flow, protected-fd policy, and rare wrapper
composition points where more than one plugin must affect the same call.

Backward compatibility for separately preloaded built-in DSOs is not a goal.

## Non-Goals

Do not preserve the old built-in DSO chain.

Do not create a parallel built-in plugin registry outside PluginManager.

Do not add wrapper events for single-owner wrappers.

Do not move plugin policy into `src/wrappers.cpp` merely because the plugin is
now linked into `libdmtcp.so`.

Do not refactor `vfork` in this pass unless it is required to remove duplicate
exports.

## Design Principles

Single-owner wrappers stay with their owning plugin.

A wrapper moves to core only when it has real multi-plugin ownership or core
mechanics that cannot be expressed through one plugin.

Simple PID translation should use two public internal helpers:

- `dmtcp_pid_virtual_to_real(pid_t virtualPid)`
- `dmtcp_pid_real_to_virtual(pid_t realPid)`

Additional PID helpers are allowed only when the caller needs behavior that
cannot be expressed by those two helpers, such as fork/clone child mapping or
command-specific out-parameter translation. Each extra helper must have a
small caller set and a documented reason.

Existing DMTCP events for file descriptor lifecycle and path translation remain
the default mechanism. Open, close, dup, and related wrappers should not change
unless a concrete gap is found.

Legacy `DMTCP_PLUGIN_DISABLE_CKPT()` and `DMTCP_PLUGIN_ENABLE_CKPT()` regions
must be replaced with the modern RAII lock style:

- `WrapperLock` for ordinary wrappers
- `WrapperLockExcl` for fork/clone/exec-style global transitions
- `LibDlWrapperLock` for dynamic-loader wrappers

## Build Model

Folded built-ins are built as per-plugin internal libraries and then linked
into `libdmtcp.so`.

`src/Makefile.am` must not list every folded plugin source directly under
`__d_libdir__libdmtcp_so_SOURCES`. Instead, it should keep core sources in
`__d_libdir__libdmtcp_so_SOURCES`, define internal libraries for plugin units,
and add those libraries to `__d_libdir__libdmtcp_so_LIBADD`.

The plugin units should reflect logical plugin ownership, not historical DSO
packaging. For example, the IPC tree is not one unit for enable-state and
ownership purposes. It contains separate internal plugins:

- ssh
- event
- file
- pty
- socket

The exact Automake shape can use `noinst_LIBRARIES` or convenience libraries,
but the build graph must make these ownership boundaries visible.

Folded plugin libraries must be linked into `libdmtcp.so` under a
`--whole-archive` linker region. Many plugin objects only define interposition
wrappers and do not satisfy unresolved references during the ordinary archive
scan, so relying on normal archive extraction can silently drop wrappers. The
`--no-whole-archive` marker must appear before the larger core helper
archives, otherwise the link will also pull unrelated archive objects into the
preload DSO.

## PluginManager Model

The existing PluginManager harness is the single internal plugin harness.

`builtinplugins.cpp` and `builtinplugins.h` should be removed. They introduce a
second plugin model and treat newly folded plugins differently from old in-core
plugins. That split is architectural debt.

Each internal plugin should have descriptor metadata that PluginManager can use:

- descriptor function
- stable plugin name
- internal plugin id
- default enabled state
- whether the plugin's enable state is controlled by `DMTCP_<PLUGIN>_PLUGIN`
- cached enabled state

Old in-core plugins and newly folded plugins use the same metadata path.
Plugins without lifecycle behavior may still have descriptor metadata so
enable/disable and ownership are uniform.

The internal metadata is stored as trailing fields on
`DmtcpPluginDescriptor_t`. External plugins can continue to use aggregate
initializers that omit those trailing fields; zero-initialized metadata means
"not an internal plugin." This branch intentionally allows the descriptor ABI
to change because preserving separately preloaded built-in DSOs is not a goal.
Internal enable environment variable names are derived from the all-caps plugin
name in the descriptor, for example `PID` maps to `DMTCP_PID_PLUGIN`.

PluginManager registers enabled internal descriptors in explicit order.
External plugin registration remains chained through the public
`dmtcp_initialize_plugin()` ABI.

The target descriptor order is:

1. unique-ckpt
2. path translator
3. syslog
4. rlimit/float
5. alarm
6. terminal
7. coordinator API
8. process info
9. unique-pid
10. ssh
11. event
12. file
13. pty
14. socket
15. SysV IPC
16. timer
17. PID
18. allocation
19. dynamic loader

Unique-ckpt is first because its `DMTCP_EVENT_PRECHECKPOINT` hook updates the
checkpoint directory name, and later plugins should observe that final
directory when they serialize or reopen plugin-owned state.
PID remains last among lifecycle-owning and restart-sensitive descriptors
because restart and child-process virtualization depend on other descriptor
state already being restored. Allocation and dynamic-loader descriptors follow
PID only because they are wrapper-only metadata descriptors today. If testing
shows that an older core descriptor must remain after a folded descriptor, the
table should record that order explicitly with a short comment.

Allocation and dynamic-loader descriptors are metadata-only descriptors today:
their wrapper code is linked into `libdmtcp.so`, but they do not register
lifecycle hooks.

## Enable And Disable State

Built-in enable state belongs to PluginManager metadata, not to a separate
`builtinplugins.cpp` table.

The launcher still parses plugin enable/disable options and sets environment
variables before loading the target. `libdmtcp.so` reads those variables through
PluginManager metadata.

`--disable-all-plugins` disables internal plugin policy. It must not disable
core DMTCP mechanics required for interposition, initialization, checkpoint
coordination, protected fds, or exec replay.

The exemption is represented by descriptor metadata: plugins with
`internalPluginEnvControlled == 0` remain enabled under
`--disable-all-plugins`, while env-controlled policy plugins are disabled.
The core exemption set is path translator, syslog, rlimit/float, alarm,
terminal, coordinator API, process info, and unique-pid. Unique-ckpt is
intentionally env-controlled because it is opt-in checkpoint naming policy
derived from the configure default and runtime launcher flag, not a core
mechanic.

Single-owner wrappers should check their owning plugin state through a small
PluginManager-facing helper, for example `PluginManager::pluginEnabled(name)`
or an equivalent internal API. The exact API should be chosen to match existing
DMTCP style and avoid a second plugin registry.

## Wrapper Ownership

Most wrappers remain in plugin-owned files. Linking those files into
`libdmtcp.so` is enough to remove the built-in DSO chain.

### Single-Owner Wrappers

These wrappers should stay with their plugin owner and should not use wrapper
events:

| Wrapper Family | Owner |
| --- | --- |
| `malloc`, `calloc`, `realloc`, `free`, `memalign`, `posix_memalign`, `valloc`, `mmap`, `mmap64`, `munmap`, `mremap` | allocation |
| `dlopen`, `dlclose`, `dlsym` behavior | dynamic loader |
| `socket`, `connect`, `bind`, `listen`, `accept`, `accept4`, `setsockopt`, `socketpair`, hostname wrappers | socket |
| `poll`, `select`, `pselect`, `epoll_*`, `eventfd`, `signalfd`, `inotify_*` | event |
| `mq_open`, `mq_close`, `mq_send`, `mq_receive`, `mq_timedsend`, `mq_timedreceive` | file / POSIX MQ |
| `shmget`, `shmat`, `shmdt`, `semget`, `semop`, `semtimedop`, `msgget`, `msgsnd`, `msgrcv` | SysV IPC |
| `wait`, `waitpid`, `wait3`, `wait4`, `waitid` | PID |
| `process_vm_readv`, `process_vm_writev` | PID |
| PID identity and signal wrappers such as `getpid`, `getppid`, `gettid`, `kill`, `tkill`, `tgkill`, process group/session wrappers, scheduler wrappers | PID |

PID-owned blocking wait wrappers may internally poll with `WNOHANG` while
releasing `WrapperLock` between polls. That preserves checkpoint progress while
the user observes normal blocking `wait*()` behavior.

Timer wrappers remain in the timer plugin. When they need pid/tid translation,
they call `dmtcp_pid_virtual_to_real()` or `dmtcp_pid_real_to_virtual()`.

SysV IPC wrappers remain in the SysV IPC plugin. When they need pid fields in
out buffers translated, they call the minimal PID translation helpers directly
when possible. Command-specific helper expansion should be justified only when
the two generic helpers are insufficient.

### Existing Core FD Wrappers

Open, close, dup, and related fd lifecycle wrappers already use DMTCP event
handlers for path translation and fd bookkeeping. Keep this mechanism unless a
specific missing behavior requires change.

`fcntl` should be split by ownership. Core can retain the fd-dup bookkeeping
needed for `F_DUPFD` and `F_DUPFD_CLOEXEC`. PID-specific `F_SETOWN` and
`F_GETOWN` handling should live in PID-owned wrapper logic or a narrowly
documented composition point, not as broad PID policy in core.

## Wrapper Composition Points

A composition point is allowed only when more than one plugin must affect the
same wrapper and there is no existing DMTCP event that already models the
interaction.

Composition should start with direct helper calls, not a generic event bus.
If a wrapper-event mechanism is introduced later, dispatch each event through
the PluginManager path to every internal plugin and let uninterested plugins
ignore it. Do not require per-event registration in the first design.

Known composition points:

| Wrapper | Participants | Required Composition |
| --- | --- | --- |
| `mq_notify` | file / POSIX MQ, PID | Move to `src/wrappers.cpp` or another shared wrapper location. PID translates `SIGEV_THREAD_ID` tid. POSIX MQ wraps `SIGEV_THREAD` callback state and records notification state. |
| `timer_create` | timer, PID | Prefer keeping in timer plugin and calling `dmtcp_pid_virtual_to_real()` for `SIGEV_THREAD_ID`. Move to core only if duplicate exported wrappers cannot otherwise be removed. |
| `clock_getcpuclockid`, `pthread_getcpuclockid` | timer, PID | Prefer timer-owned wrappers that call PID translation helpers. |
| `shmctl`, `semctl`, `msgctl` | SysV IPC, PID | Prefer SysV IPC-owned wrappers that call PID translation helpers on command-specific out fields. |
| `fork`, `clone` | core lifecycle, PID | Core owns lifecycle/lock mechanics. PID exposes minimal fork/clone prepare, parent, and child helpers if generic pid translation is insufficient. |
| `syscall` | syscall dispatcher plus wrapper owners | Dispatch to the matching libc wrapper whenever possible. Use `_real_syscall()` only for documented recursion or ABI cases. |

`vfork` is out of scope for new design work in this pass. Preserve the current
native-vfork implementation path unless duplicate exports force a minimal
consolidation.

## Varargs, Re-Entrancy, And Short-Circuits

Wrappers must extract varargs exactly once before calling plugin-owned helper
logic. This applies to wrappers such as `open`, `fcntl`, `ioctl`, and
`syscall()`.

Handlers and helper paths must not recursively re-enter the same wrapper without
an explicit guard. Domain-specific recursion guards are allowed when the current
code already needs them, but the wrapper family must document the guard and why
it is safe.

Generic wrapper composition must not allow arbitrary plugins to skip the real
libc/syscall call. Most current behavior can be represented as argument
translation, post-call state updates, return-value translation, errno
translation, or wrapper-specific retry logic.

Explicit short-circuits are allowed only as local wrapper decisions with a
documented reason. Known categories include virtual identity reads, validation
paths that reject invalid operations, and deliberately unsupported wrappers that
must fail without touching the kernel.

## Syscall Wrapper Rule

The `syscall()` wrapper should decode known syscall arguments and call the
corresponding libc function, allowing that function's normal wrapper to own
policy. This keeps ownership in one place and avoids a second implementation of
the same behavior inside `syscall()`.

Examples:

- `SYS_socket` calls `socket()`, which is owned by the socket plugin.
- `SYS_shmctl` calls `shmctl()`, which is owned by the SysV IPC plugin.
- `SYS_process_vm_readv` calls `process_vm_readv()`, which is owned by PID.
- `SYS_wait4` calls `wait4()`, which is owned by PID.

Exceptions must be local and documented. The known exception is `SYS_shmdt`
while already inside the SysV IPC `shmdt()` wrapper; that path may call
`_real_syscall(SYS_shmdt, ...)` to avoid recursive interception caused by
external GOT interposition.

Unknown syscalls should fall through to `_real_syscall()`.

## PID Helper Surface

Start with this public internal helper surface:

```c++
pid_t dmtcp_pid_virtual_to_real(pid_t virtualPid);
pid_t dmtcp_pid_real_to_virtual(pid_t realPid);
```

Additional helpers require a short rationale in the header next to the
declaration. Expected candidates:

- fork/clone prepare, parent, and child hooks
- wait status translation if it cannot be expressed cleanly in the PID-owned
  wait wrappers without exposing internals
- `SIGEV_THREAD_ID` convenience only if direct access to the field is too
  platform-specific for non-PID wrappers

Avoid broad helpers such as `dmtcp_pid_translate_shmctl_result()` unless the
caller genuinely needs PID-owned command decoding. Prefer plugin-owned command
decoding plus calls to the two generic helpers for each pid field.

## Wrapper Audit Categories

Before folding a plugin, classify each wrapper:

- pass-through with RAII wrapper lock only
- argument translation before the real call
- return-value translation after the real call
- out-parameter mutation after the real call
- errno preservation or translation
- retry or restart loop
- explicit short-circuit
- unsupported operation shim

This classification determines whether the wrapper stays plugin-owned, needs a
plugin helper from a shared wrapper, or requires a documented explicit
short-circuit.

## Error Handling

Core should preserve libc-visible behavior by default.

Save `errno` immediately after the real call. Post-call logic may intentionally
replace it only when the wrapper contract requires translated errno behavior. If
a plugin helper fails internally, it should either convert that failure into the
wrapper's expected libc-style failure or trigger an internal assertion when
continuing could corrupt checkpoint/restart state.

Plugin helpers must not leave partially updated context fields without also
setting a coherent return value and errno. For out parameters, helpers should
only modify memory that the wrapper contract says is valid.

## Stale Source Policy

When a plugin DSO is removed from the build, do not leave a second copy of the
old wrapper implementation as tracked, unbuilt source unless it is explicitly
kept as a reference file with a comment explaining that status.

For this consolidation, this applies at least to:

- `src/plugin/dl/dlwrappers.cpp`
- `src/plugin/pid/pid_miscwrappers.cpp`
- `src/plugin/pid/pid_syscallsreal.c`

Each file should either be built as part of the new internal-library shape,
moved into the owning plugin's linked sources, or removed.

## External Plugins

External plugins remain DSOs and continue to use the public DMTCP plugin ABI.

External plugins should not depend on old relative `LD_PRELOAD` ordering among
internal built-in DSOs, because those DSOs no longer exist. If an external
plugin depended on a built-in wrapper chain through `NEXT_FNC`, document a
migration recipe using public events or public helper APIs.

## Testing And Verification

Source/build-layout audits should cover:

- no folded built-in DSO remains in `lib/dmtcp`, launcher preload tables, or
  Debian install manifests
- folded plugin code is linked into `libdmtcp.so` through internal libraries
- duplicate exported wrappers are absent after each fold
- duplicate `_real_*` helpers are absent before PID link-in
- `DMTCP_HIJACK_LIBS` and `DMTCP_HIJACK_LIBS_M32` contain `libdmtcp.so` and
  requested external plugins, not folded built-in DSOs
- no newly migrated active wrapper uses `DMTCP_PLUGIN_DISABLE_CKPT()` or
  `DMTCP_PLUGIN_ENABLE_CKPT()`
- PluginManager internal descriptor order matches the explicit table in this
  spec
- plugin-specific disable flags and `--disable-all-plugins` suppress plugin
  policy while preserving core DMTCP mechanics

Run focused behavior tests for each plugin as it is folded. Run checkpoint and
restart tests for the plugin's behavior surface before considering that fold
complete.

## Implemented Shape

The implementation links internal plugin libraries into `libdmtcp.so` and uses
PluginManager metadata for internal descriptor registration and enable state.
Single-owner wrappers remain in plugin-owned files. Shared wrapper composition
in the final shape is limited to `fcntl`, `mq_notify`, fork PID helper
composition, and the documented `SYS_shmdt` recursion exception. `clone` was
left on the existing thread-creation guard path because no direct PID mapping
path needed consolidation.

## Migration Order

Implementation follows this order:

1. Introduce the per-plugin internal-library build shape.
2. Move enable state into PluginManager metadata and remove `builtinplugins.*`.
3. Split IPC ownership into ssh, event, file, pty, and socket.
4. Remove broad wrapper-event registration and keep single-owner wrappers in
   plugin files.
5. Consolidate true collision wrappers one by one, starting with `mq_notify`.
6. Move PID-only wrappers such as wait and `process_vm_*` back into PID
   ownership.
7. Audit and replace legacy checkpoint locks with RAII wrapper locks.
8. Collapse built-in preload to `libdmtcp.so` after ownership and duplicate
   export checks pass.

Each step should be committed separately and verified before the next step.

## Implementation Invariants

There is one built-in preload DSO.

There is one internal plugin harness: PluginManager.

Internal plugin boundaries are visible in the build graph.

Single-owner wrappers stay in their plugin.

Core wrapper composition is rare, documented, and justified by multiple
plugin participants.

PID exposes a minimal helper surface.

`syscall()` delegates to libc wrapper functions unless a documented recursion
or ABI exception requires `_real_syscall()`.

Existing DMTCP fd and path events remain the default for open/close behavior.

No active migrated wrapper uses legacy checkpoint lock macros.
