# Plugin consolidation preflight

Reader: a DMTCP maintainer or M003 implementation agent preparing to fold DMTCP-owned internal plugin shared objects into `libdmtcp.so`.

Post-read action: block or approve the next implementation slice by checking the enable-state model, wrapper ownership matrix, duplicate-symbol command classes, stale-artifact gates, rollback gates, and requirement ownership below.

This document is a contract proof, not runtime proof. S01 names source-backed blockers and commands that later implementation work must run after source moves and builds exist. It does not assert that a migrated build, checkpoint/restart run, sanitizer run, or full batch test has already succeeded.

## Source evidence boundary

The preflight cites only visible repository paths. It does not rely on generated build output, installed library directories, local branch notes, hidden planning state, or environment values. The source-map verifier for this boundary is:

```sh
python3 test/verify-consolidation-preflight.py source-map
```

Primary evidence paths for the S01 contract:

- `doc/internal-plugin-consolidation-design-plan.md` for the accepted final architecture, milestone handoff split, and deferred runtime proof boundary.
- `doc/internal-plugin-consolidation-migration-plan.md` for the stage order, rollback model, duplicate-symbol gates, stale-artifact gates, focused tests, and implementation command classes.
- `doc/internal-plugin-consolidation-registration-architecture.md` for PluginManager-owned built-in registration, descriptor accessors, external ABI preservation, IPC ordering, pid-last ordering, and alloc/DL wrapper-only behavior.
- `doc/internal-plugin-consolidation-wrapper-order-tsan.md` for current preload order, `DMTCP_HIJACK_LIBS` capture, `DMTCP_DECL_PLUGIN` chain behavior, `RTLD_NEXT` risk, and the M004 sanitizer boundary.
- `src/dmtcp_launch.cpp` for `DMTCP_PLUGIN` placement, `--disable-all-plugins`, default built-in library order, `DMTCP_ALLOC_PLUGIN`, `DMTCP_DL_PLUGIN`, `DMTCP_HIJACK_LIBS`, `DMTCP_HIJACK_LIBS_M32`, and `DMTCP_ORIG_LD_PRELOAD` handling.
- `src/constants.h` for the concrete environment variable names used by launch, restart, and exec propagation.
- `src/util_exec.cpp` for restart argument propagation through `Util::getDmtcpArgs`.
- `src/execwrappers.cpp` for exec-time preload reconstruction through `getUpdatedLdPreload` and `patchUserEnv`.
- `src/pluginmanager.cpp` and `src/pluginmanager.h` for descriptor append order, `dmtcp_register_plugin`, the current `libdmtcp.so` `dmtcp_initialize_plugin` chain node, and forward/reverse event delivery.
- `include/dmtcp.h` for the unchanged external `DMTCP_DECL_PLUGIN` ABI, `NEXT_FNC`, and `DmtcpPluginDescriptor_t`.
- `src/Makefile.am` for the current `libdmtcp.so` source boundary.
- `src/plugin/Makefile.am` for the current internal plugin DSO boundaries.
- `test/sysv-shm1.c`, `test/sysv-shm2.c`, `test/sysv-sem.c`, `test/sysv-msg.c`, `test/timer1.c`, `test/dlopen1.c`, `test/dlopen2.cpp`, and `test/plugin-init.cpp` for later focused validation anchors.

The integration verifier for cross references is:

```sh
python3 test/verify-consolidation-preflight.py integration
```

## Built-in enable state

The current enable-state model is launch-owned first, then restart and exec propagation preserve what launch decided.

| Surface | Current source-backed behavior | Consolidation contract |
|---|---|---|
| `DMTCP_PLUGIN` | `src/dmtcp_launch.cpp` prepends user-provided plugin entries before the DMTCP-generated chain. | External plugins still register before DMTCP internals through the public chain. Moving internals into `libdmtcp.so` must not put built-ins ahead of user `DMTCP_PLUGIN` descriptors. |
| `--disable-all-plugins` | `src/dmtcp_launch.cpp` reduces the generated chain to `libdmtcp.so` when all plugins are disabled. | This remains the global hard disable. Descriptor-bearing built-ins are skipped or made inert by the engine, and wrapper-only groups fast-pass rather than requiring separate DSOs. |
| `DMTCP_ALLOC_PLUGIN` | `src/dmtcp_launch.cpp` maps `DMTCP_ALLOC_PLUGIN=0` to `enableAllocPlugin = false`; `src/util_exec.cpp` maps the same state back to `--disable-alloc-plugin`. | Alloc remains wrapper-only alloc and DL class behavior: no descriptor row, no event hook, and disabled wrappers fast-pass to real allocation and mapping functions. |
| `DMTCP_DL_PLUGIN` | `src/dmtcp_launch.cpp` maps `DMTCP_DL_PLUGIN=0` to `enableDlPlugin = false`; `src/util_exec.cpp` maps the same state back to `--disable-dl-plugin`. | DL remains wrapper-only alloc and DL class behavior: no descriptor row, no event hook, disabled wrappers fast-pass, and `RTLD_NEXT` sensitivity remains a later validation gate. |
| `DMTCP_HIJACK_LIBS` | `src/dmtcp_launch.cpp` captures the DMTCP-generated preload chain after enabled built-ins are appended and before the original user preload is appended. | After each source move, the generated chain must not name a removed DSO. The shorter chain is expected only after the new `libdmtcp.so` owner exists. |
| `DMTCP_HIJACK_LIBS_M32` | The 32-bit companion generated chain is captured alongside `DMTCP_HIJACK_LIBS` on supported architectures. | A stage that removes an internal DSO updates both generated-chain paths by changing the same launch authority rather than leaving a 32-bit stale entry. |
| `DMTCP_ORIG_LD_PRELOAD` | `src/dmtcp_launch.cpp` saves the original user `LD_PRELOAD`, and `src/execwrappers.cpp` preserves user preload intent while rebuilding DMTCP preload state during exec. | Consolidation must keep generated-chain state separate from user preload restoration; a removed DSO must not be reintroduced through exec-time preload reconstruction. |
| restart argument propagation | `src/util_exec.cpp` carries `--with-plugin`, `--disable-alloc-plugin`, and `--disable-dl-plugin` from protected environment state. There are no timer, SysV IPC, IPC, or PID restart disable flags. | Alloc and DL keep their visible restart flags. Descriptor-bearing built-ins do not gain new user flags unless a later requirement explicitly adds them. |
| exec-time preload reconstruction | `src/execwrappers.cpp` reads `DMTCP_HIJACK_LIBS`, `DMTCP_HIJACK_LIBS_M32`, and `DMTCP_ORIG_LD_PRELOAD` to rebuild `LD_PRELOAD` for exec. | After a stage, exec replay must match launch ownership: no old DSO, no missing `libdmtcp.so`, and original user preload still appended only as user state. |
| descriptor-bearing built-ins | Timer, SysV IPC, IPC, and PID currently register descriptors through separate DSO chain nodes or a handwritten initializer; `src/pluginmanager.cpp` registers core descriptors in `libdmtcp.so`. | Future ownership is a PluginManager-owned built-in descriptor table invoked from the `libdmtcp.so` chain node. External ABI remains the unchanged external DMTCP_DECL_PLUGIN ABI. |
| wrapper-only alloc and DL | Alloc and DL currently provide wrappers but no `DmtcpPluginDescriptor_t` rows. | They remain wrapper-only alloc and DL groups after their DSOs disappear. They must not gain descriptor accessors or PluginManager rows. |

Default descriptor order after consolidation remains a behavior contract: external `DMTCP_PLUGIN` descriptors first, IPC subplugins in forward order `ssh,event,file,pty,socket`, then SysV IPC, then timer, then existing core `libdmtcp.so` descriptors, and PID last. Reverse event delivery in `PluginManager::eventHook` still depends on this order for restart and resume behavior.

Hard blocker: a source move is not allowed until launch enable state, restart argument propagation, exec-time preload reconstruction, descriptor-bearing built-ins, wrapper-only alloc and DL, and unchanged external `DMTCP_DECL_PLUGIN` ABI are all explicitly accounted for in the stage diff.

## Wrapper ownership

The matrix is intentionally stage based. Each row has one current owner, one future owner, collision risk, audit target, and hard stop rule. Ambiguous owner language blocks implementation because a half-migrated stage can leave both an old DSO and `libdmtcp.so` exporting the same registration or wrapper symbol.

| Stage | Current owner | Future owner | Collision risk | Audit target | Hard stop rule |
|---|---|---|---|---|---|
| timer | `libdmtcp_timer.so` from `src/plugin/Makefile.am`, loaded through `src/dmtcp_launch.cpp`, with descriptor state in `src/plugin/timer/timerlist.cpp` and wrappers in `src/plugin/timer/timerwrappers.cpp`. | `libdmtcp.so`, with a PluginManager-owned timer descriptor accessor and timer wrappers linked into the core source boundary. | Duplicate `dmtcp_initialize_plugin` or duplicate timer wrappers such as `timer_create`, `timer_delete`, `timer_settime`, `timer_gettime`, and `timer_getoverrun`. | `nm -D --defined-only` and `readelf -Ws` over DMTCP shared objects after a build; source audit for no `DMTCP_DECL_PLUGIN` in the moved timer descriptor file. | Stop if `libdmtcp_timer.so` remains in the launch chain, plugin source list, build output, install output, or symbol table after timer ownership moves. |
| SysV IPC | `libdmtcp_svipc.so` from `src/plugin/Makefile.am`, with descriptor state in `src/plugin/svipc/sysvipc.cpp` and wrappers in `src/plugin/svipc/sysvipcwrappers.cpp`. | `libdmtcp.so`, with a PluginManager-owned SysV IPC descriptor accessor and SysV shared-memory, semaphore, and message-queue wrappers linked into the core source boundary. | Duplicate initializer or duplicate wrappers for `shmget`, `shmat`, `shmdt`, `shmctl`, `semget`, `semop`, `semtimedop`, `semctl`, `msgget`, `msgsnd`, `msgrcv`, and `msgctl`; `IPC_PRIVATE` behavior is a focused runtime gate, not an S01 result. | Symbol audit after a build plus focused M003 command class for `test/sysv-shm1.c`, `test/sysv-shm2.c`, `test/sysv-sem.c`, and `test/sysv-msg.c`. | Stop if `libdmtcp_svipc.so` remains loadable, if exec replay still names it, or if the implementation cannot name the `IPC_PRIVATE` focused gate before the IPC stage starts. |
| IPC | `libdmtcp_ipc.so` from `src/plugin/Makefile.am`, with a handwritten initializer in `src/plugin/ipc/ipc.cpp` and subplugin descriptors for SSH, event, file, pty, and socket. | `libdmtcp.so`, with five explicit PluginManager-owned rows in forward order `ssh,event,file,pty,socket`. | Duplicate `dmtcp_initialize_plugin`, duplicate IPC/file/socket/POSIX message queue wrappers, or an order regression that breaks reverse restart order `socket,pty,file,event,ssh`. | Static descriptor-order audit plus `nm -D --defined-only` and `readelf -Ws` after a build for initializer and wrapper ownership. | Stop if the five rows are not explicit, if reverse restart order is not derivable, or if `libdmtcp_ipc.so` remains in launch, build, or install ownership after IPC moves. |
| PID | `libdmtcp_pid.so` from `src/plugin/Makefile.am`, loaded last through `src/dmtcp_launch.cpp`, with descriptor state in `src/plugin/pid/pid.cpp` and wrapper surfaces in `src/plugin/pid/pidwrappers.cpp` and `src/plugin/pid/pid_miscwrappers.cpp`. | `libdmtcp.so`, with the PID descriptor row still last after external plugins, IPC, SysV IPC, timer, and core descriptors. | Duplicate PID virtualisation wrappers and unresolved cross-stage ownership for fork, vfork, fcntl, syscall, wait, timer wrapper interactions, SysV IPC wrapper interactions, and POSIX mq_notify ownership before PID convergence. | Symbol audit for `getpid`, `getppid`, `kill`, wait-family wrappers, `fcntl`, `fork`, `vfork`, `syscall`, plus static review of `shmctl`, `semctl`, `msgctl`, `timer_create`, and `mq_notify` ownership. | Stop if PID is not last, if any listed cross-wrapper interaction has no owner, or if old `libdmtcp_pid.so` can still participate in the chain after the built-in PID row exists. |
| alloc | `libdmtcp_alloc.so` from `src/plugin/Makefile.am`, with allocation wrappers in `src/plugin/alloc/mallocwrappers.cpp` and mapping wrappers in `src/plugin/alloc/mmapwrappers.cpp`. | `libdmtcp.so` wrapper-only ownership, controlled by launch and restart enable state rather than PluginManager registration. | Duplicate wrappers for `calloc`, `malloc`, `free`, `realloc`, `memalign`, `valloc`, `mmap`, `mmap64`, `munmap`, and `mremap`, or accidental descriptor creation. | Duplicate-wrapper symbol audit after a build; source audit that there is no alloc descriptor row and no alloc `DmtcpPluginDescriptor_t`. | Stop if an alloc descriptor row appears, if disabled wrappers cannot fast-pass, or if `libdmtcp_alloc.so` remains loadable after alloc wrappers move. |
| DL | `libdmtcp_dl.so` from `src/plugin/Makefile.am`, with `dlopen` and `dlclose` wrappers in `src/plugin/dl/dlwrappers.cpp`. | `libdmtcp.so` wrapper-only ownership, controlled by launch and restart enable state rather than PluginManager registration. | Duplicate wrappers for `dlopen` and `dlclose`, accidental descriptor creation, or unreviewed `RTLD_NEXT` and sanitizer-sensitive lookup behavior. | Duplicate-wrapper symbol audit after a build; source audit that there is no DL descriptor row; focused M003 command class for `test/dlopen1.c` and `test/dlopen2.cpp`; M004 owns sanitizer and `dl_iterate_phdr` stress. | Stop if a DL descriptor row appears, if disabled wrappers cannot fast-pass, if old `libdmtcp_dl.so` remains loadable, or if `RTLD_NEXT` review is skipped before claiming DL completion. |

## Duplicate symbol audit

These are command classes to run after a build in M003 or M004. They are not S01 execution results. Run them after each stage source move and again after stale-artifact cleanup.

Initializer uniqueness audit:

```sh
find src -type f -name 'libdmtcp*.so' -print0 \
  | xargs -0 -r nm -D --defined-only \
  | awk '$3 == "dmtcp_initialize_plugin" { print }'
```

Expected later pass signal: only the intended `libdmtcp.so` chain node remains for DMTCP internals, and no moved built-in DSO still exports an internal initializer.

Wrapper duplicate audit:

```sh
find src -type f -name 'libdmtcp*.so' -print0 \
  | xargs -0 -r nm -D --defined-only \
  | awk '{ print $3 }' \
  | grep -E '^(timer_create|timer_delete|timer_settime|timer_gettime|timer_getoverrun|shmget|shmat|shmdt|shmctl|semget|semop|semtimedop|semctl|msgget|msgsnd|msgrcv|msgctl|mq_open|mq_notify|mq_send|mq_receive|fork|vfork|fcntl|syscall|wait|waitpid|waitid|wait3|wait4|calloc|malloc|free|realloc|memalign|valloc|mmap|mmap64|munmap|mremap|dlopen|dlclose)$' \
  | sort | uniq -d
```

Expected later pass signal: no output for symbols that should have exactly one DMTCP-owned exporting artifact after the stage.

Readelf owner audit:

```sh
find src -type f -name 'libdmtcp*.so' -print0 \
  | xargs -0 -r readelf -Ws \
  | grep -E ' dmtcp_initialize_plugin$| (timer_create|shmget|semget|msgget|mq_notify|fork|vfork|fcntl|syscall|waitpid|malloc|mmap|dlopen|dlclose)$'
```

Expected later pass signal: every symbol listed by `readelf -Ws` has the single owner promised by the wrapper matrix for the completed stage.

Source registration audit:

```sh
grep -R "DMTCP_DECL_PLUGIN" \
  src/plugin/timer src/plugin/svipc src/plugin/ipc src/plugin/pid
```

Expected later pass signal: files for a stage already moved into `libdmtcp.so` no longer use `DMTCP_DECL_PLUGIN`; external plugins keep the public macro through `include/dmtcp.h`.

Hard blocker: if an audit command reports duplicate ownership after the source move, restore the old DSO boundary for that stage or complete the missing source/build/launch cleanup before continuing.

## Stale artifact cleanup

Removed internal DSOs must not survive in a build or install tree after their ownership moves into `libdmtcp.so`. The cleanup set is explicit:

- `libdmtcp_timer.so`
- `libdmtcp_svipc.so`
- `libdmtcp_ipc.so`
- `libdmtcp_pid.so`
- `libdmtcp_alloc.so`
- `libdmtcp_dl.so`

Stale-source gate:

```sh
grep -E 'libdmtcp_(timer|svipc|ipc|pid|alloc|dl)\.so' src/dmtcp_launch.cpp src/plugin/Makefile.am
```

Expected later pass signal: after a stage removes one DSO, that DSO no longer appears in the launch chain or plugin DSO source list. Remaining stages may still appear until they move.

Stale-build gate after a build:

```sh
find src -type f \( \
  -name 'libdmtcp_timer.so' -o \
  -name 'libdmtcp_svipc.so' -o \
  -name 'libdmtcp_ipc.so' -o \
  -name 'libdmtcp_pid.so' -o \
  -name 'libdmtcp_alloc.so' -o \
  -name 'libdmtcp_dl.so' \
\) -print
```

Expected later pass signal: the DSO removed by the completed stage is absent from build output before the next stage starts.

Stale-install gate after install:

```sh
find "$DESTDIR" -type f \( \
  -name 'libdmtcp_timer.so' -o \
  -name 'libdmtcp_svipc.so' -o \
  -name 'libdmtcp_ipc.so' -o \
  -name 'libdmtcp_pid.so' -o \
  -name 'libdmtcp_alloc.so' -o \
  -name 'libdmtcp_dl.so' \
\) -print
```

Expected later pass signal: the DSO removed by the completed stage is absent from the install root used by that verification run.

Hard blocker: if a removed DSO remains in build output, install output, `DMTCP_HIJACK_LIBS`, `DMTCP_HIJACK_LIBS_M32`, or exec-time preload reconstruction, do not start the next stage.

## Rollback gate

Each stage has one safe rollback point: the old separate DSO boundary. Keep that boundary until all stage gates land together:

1. Source ownership is moved into `src/Makefile.am` or intentionally remains wrapper-only for the stage.
2. The old DSO target is removed from `src/plugin/Makefile.am`.
3. `src/dmtcp_launch.cpp` no longer emits the old DSO in the generated chain.
4. `src/util_exec.cpp` still propagates the user-visible restart arguments that exist for the stage.
5. `src/execwrappers.cpp` replays the generated chain without reintroducing a removed DSO.
6. Descriptor stages have exactly one PluginManager-owned row or explicit row set.
7. Alloc and DL have no descriptor rows.
8. Duplicate-symbol, duplicate-wrapper, stale-artifact, and focused-test command classes are named and ready for the implementation milestone that can run them.

Rollback rule: if any gate fails, restore the old DSO source list and launch-chain entry for that stage before moving another stage. Do not ship a half-migrated state where both old and new artifacts own the same initializer or wrapper surface.

## Requirement coverage

| Requirement | S01 preflight status | Downstream proof gate |
|---|---|---|
| R003 | R003 is owned here at contract level. This document names the build-boundary, focused-test, full batch, duplicate-symbol, stale-artifact, and rollback gates that must exist before M003 source migration. | M003 must run build regeneration where needed, clean rebuild, focused tests, duplicate-symbol audits, stale-artifact audits, and final batch validation after implementation edits. |
| R006 | R006 is a downstream proof gate. S01 defines central built-in ownership: descriptor stages move to PluginManager-owned rows, while alloc and DL stay wrapper-only. | M003 must prove the central ownership model in source, symbol tables, launch state, and focused runtime behavior. |
| R007 | R007 is a downstream proof gate. S01 names startup and static-initialization risks, with `test/plugin-init.cpp` as a smoke anchor. | M003 must provide startup smoke evidence after descriptor or wrapper placement changes; M004 owns sanitizer-specific stress if constructor order is involved. |
| R008 | R008 is a downstream proof gate. S01 defines duplicate initializer and duplicate wrapper hard blockers. | M003 must provide `nm` or `readelf` evidence that moved stages have a single DMTCP-owned initializer or wrapper owner and no stale DSO copy. |

The preflight therefore owns inspection and blocking language. It does not own runtime success evidence for R006, R007, or R008.

## Diagnostics

Run these static checks whenever this document or the source-map verifier changes:

```sh
python3 test/verify-consolidation-preflight.py self-test
python3 test/verify-consolidation-preflight.py source-map
python3 test/verify-consolidation-preflight.py preflight-doc
python3 test/verify-consolidation-preflight.py integration
python3 test/verify-consolidation-preflight.py full
```

Expected S01 result: `preflight-doc` confirms the hard-stop document contract, `source-map` confirms current source anchors, and `integration` confirms the source cross references above. Later milestones must add runtime transcripts for the command classes; S01 only makes those missing transcripts impossible to confuse with completed runtime proof.
