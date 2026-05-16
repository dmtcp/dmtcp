# Internal Plugin Consolidation: Wrapper Order and TSAN Evidence Map

Reader: a DMTCP maintainer designing the internal-plugin consolidation work for S02 and S03. Post-read action: reconstruct today's preload, constructor, wrapper lookup, plugin registration, event-hook order, TSAN risk, and M004/R011 experiment plan from source citations before changing wrapper placement.

This file records source-backed current behavior and the S01 feasibility boundary for the TSAN concern. It does not claim runtime TSAN success; runtime sanitizer proof is reserved for the M004/R011 experiments named below. [gene-email.txt]

## Scope and R002 evidence boundary

R002 needs a source-backed map of the current DMTCP loader stack before design work relies on assumptions about wrapper order. The motivating note frames the desired long-term shape as DMTCP initialization happening early while DMTCP wrappers run late, close to libc; it also describes a TSAN experiment where TSAN's constructor runs first, DMTCP initialization is reached through a wrapper, and later DMTCP dlsym activity reaches a TSAN `dl_iterate_phdr` interceptor. [gene-email.txt]

This appendix therefore covers the current order of:

- `dmtcp_launch` construction of `LD_PRELOAD`.
- Placement of `DMTCP_PLUGIN` and a pre-existing user `LD_PRELOAD`.
- Built-in DMTCP plugin shared objects around `libdmtcp.so`.
- The `DMTCP_HIJACK_LIBS` capture point.
- The `libdmtcp.so` constructor path into wrapper preparation and plugin registration.
- `DMTCP_DECL_PLUGIN`, `NEXT_FNC(dmtcp_initialize_plugin)`, and event-hook ordering.
- The current `RTLD_NEXT` and `dl_iterate_phdr` risk surface, including what consolidation can reduce and what still needs runtime proof. [gene-email.txt]

## Current dmtcp_launch LD_PRELOAD construction

`dmtcp_launch` builds a colon-separated `preloadLibs` string in `setLDPreloadLibs`. The construction starts empty, prepends `DMTCP_PLUGIN` when present, copies that prefix to the 32-bit string, then appends enabled DMTCP libraries in the order of the `pluginInfo` table unless all plugins are disabled. The final variable written for normal execution is `LD_PRELOAD`; kernel-loader mode writes the same chain to `UH_PRELOAD` instead. [src/dmtcp_launch.cpp]

The normal enabled order in the table is:

1. Optional disabled-by-default plugin slots: `libdmtcp_modify-env.so`, optional unique-checkpoint names, and optional path virtualization.
2. Enabled wrapper/plugin libraries before the base library: `libdmtcp_alloc.so`, `libdmtcp_dl.so`, `libdmtcp_ipc.so`, `libdmtcp_svipc.so`, and `libdmtcp_timer.so`.
3. The base `libdmtcp.so`.
4. `libdmtcp_pid.so`, explicitly annotated as the plugin that must come last. [src/dmtcp_launch.cpp]

If `--no-plugins` or `--disable-all-plugins` is used, the same function bypasses that table and sets the preload chain to only `libdmtcp.so`. [src/dmtcp_launch.cpp]

## User DMTCP_PLUGIN and original LD_PRELOAD placement

`--with-plugin` stores its argument in `DMTCP_PLUGIN`, and `setLDPreloadLibs` appends that value before all enabled DMTCP built-ins. In search-order terms, a library named through `DMTCP_PLUGIN` is earlier than `libdmtcp_alloc.so`, `libdmtcp_dl.so`, `libdmtcp_ipc.so`, `libdmtcp_svipc.so`, `libdmtcp_timer.so`, `libdmtcp.so`, and `libdmtcp_pid.so`. [src/dmtcp_launch.cpp]

A pre-existing user `LD_PRELOAD` is handled differently. `dmtcp_launch` saves it in `DMTCP_ORIG_LD_PRELOAD` and appends it after the DMTCP-generated chain. The result is that the target process starts with DMTCP's generated libraries first and the user's original preload entries later. [src/dmtcp_launch.cpp]

Inside the target, `restoreUserLDPRELOAD` runs after DMTCP has hijacked startup and before user `main`. It destructively rewrites the process `LD_PRELOAD` string to either an empty string or the saved original user value, while tracing the generated hijack values. That makes the environment visible to the application look closer to the user's original state, but it does not change the dynamic linker's already-loaded order. [src/dmtcpworker.cpp]

## Built-in plugin shared-object order

The shared-object order is driven by two build surfaces. The top-level source build creates `libdmtcp.so` from the worker, wrapper, plugin-manager, and internal plugin source files, and links it with `libsyscallsreal.a` for real-function lookup support. [src/Makefile.am]

The plugin build creates separate shared objects for wrapper/plugin layers such as `libdmtcp_alloc.so`, `libdmtcp_dl.so`, `libdmtcp_ipc.so`, `libdmtcp_pid.so`, `libdmtcp_svipc.so`, and `libdmtcp_timer.so`. The `ipc`, `svipc`, `timer`, and `pid` shared objects include the source files that declare their DMTCP plugin descriptors. [src/plugin/Makefile.am]

`dmtcp_launch` is the runtime authority for ordering those artifacts in the default preload chain. Build order in the makefiles proves that the artifacts exist as separate shared objects; the `pluginInfo` table proves their default runtime placement relative to `libdmtcp.so`. [src/plugin/Makefile.am] [src/dmtcp_launch.cpp]

## ENV_VAR_HIJACK_LIBS capture point

`setLDPreloadLibs` writes `DMTCP_HIJACK_LIBS` after it has appended `DMTCP_PLUGIN` and enabled DMTCP libraries, but before it appends a pre-existing user `LD_PRELOAD`. The 32-bit companion value is captured at the same point when that branch is compiled. [src/dmtcp_launch.cpp]

That means `DMTCP_HIJACK_LIBS` represents the DMTCP-generated preload chain, not the final user-visible `LD_PRELOAD` after original user entries are appended. `restoreUserLDPRELOAD` later logs `DMTCP_HIJACK_LIBS`, its 32-bit variant, the saved original preload, and the rewritten `LD_PRELOAD` while hiding the DMTCP-generated value from the application environment before `main`. [src/dmtcpworker.cpp]

Immediate implication: any consolidation design that changes DMTCP's internal library order must preserve the distinction between the generated hijack chain and the restored user preload value. [src/dmtcp_launch.cpp] [src/dmtcpworker.cpp]

## libdmtcp constructor entry

`libdmtcp.so` enters through `dmtcp_initialize_entry_point`, declared with constructor priority 101. It exits immediately if initialization already happened, marks the process initialized, and then calls `dmtcp_initialize` before higher-level worker setup. [src/dmtcpworker.cpp]

After wrapper preparation, the constructor initializes Jalib, prepares atfork handling, marks the worker running, initializes the plugin manager, restores post-exec state or root-process logging, reads process metadata, restores the user-facing preload environment, initializes thread state, sends `DMTCP_EVENT_INIT`, initializes timezone state, and finally creates the checkpoint thread. [src/dmtcpworker.cpp]

The ordering matters for consolidation because wrapper lookup and plugin registration are not delayed until after `DMTCP_EVENT_INIT`; they happen inside the constructor path before the event is emitted. [src/dmtcpworker.cpp]

## dmtcp_initialize and dmtcp_prepare_wrappers

`dmtcp_initialize` is intentionally small: it calls `dmtcp_prepare_wrappers`. The worker comment says this happens at the very beginning of the DMTCP worker constructor path so DMTCP can use `_real_XXX` functions reliably later. [src/dmtcpworker.cpp]

`dmtcp_prepare_wrappers` initializes wrapper addresses once. It calls `initialize_libc_wrappers`, and that function iterates over DMTCP libc wrappers with a macro that resolves each real function through `dlsym_default_internal_flag_handler(RTLD_NEXT, ...)`. The prepared addresses are cached in `dmtcp_real_func_addr`; later `_real_*` helpers use those cached pointers or trigger preparation if a pointer is still missing. [src/syscallsreal.c]

`NEXT_FNC(func)` has a related lazy path. On first use for a given function it calls `dmtcp_initialize` if available, then resolves the next implementation with `dmtcp_dlsym(RTLD_NEXT, #func)` and caches the result. [include/dmtcp.h]

Observed order: constructor entry calls eager wrapper preparation, and plugin-chain helper calls can also force wrapper preparation before resolving their own `RTLD_NEXT` target. [src/dmtcpworker.cpp] [src/syscallsreal.c] [include/dmtcp.h]

## PluginManager initialization and registration chain

`PluginManager::initialize` creates the singleton plugin manager on first use and then calls `dmtcp_initialize_plugin` if that symbol is present. The registration chain is therefore entered during `libdmtcp.so` constructor execution, before `DMTCP_EVENT_INIT`. [src/pluginmanager.cpp] [src/dmtcpworker.cpp]

`dmtcp_register_plugin` appends a descriptor to `pluginInfos`. The `libdmtcp.so` implementation of `dmtcp_initialize_plugin` registers in-built descriptors in this order: path translator, syslog, rlimit-float, alarm, terminal, coordinator API, process info, and unique pid. It then obtains `NEXT_FNC(dmtcp_initialize_plugin)` and calls it if present. [src/pluginmanager.cpp]

Because the chain uses the dynamic linker's next-symbol lookup, registration order follows the loaded `dmtcp_initialize_plugin` chain rather than a separate hard-coded list in `PluginManager`. The `pluginInfo` preload order in `dmtcp_launch` determines where external plugin shared objects, `libdmtcp.so`, and the PID plugin sit in that chain. [src/pluginmanager.cpp] [src/dmtcp_launch.cpp]

## DMTCP_DECL_PLUGIN and NEXT_FNC chaining

`DMTCP_DECL_PLUGIN(descr)` defines a `dmtcp_initialize_plugin` function that registers the descriptor and then calls `NEXT_FNC(dmtcp_initialize_plugin)`. This is the standard plugin-chain pattern for shared objects using the public DMTCP plugin declaration macro. [include/dmtcp.h]

The IPC plugin uses a hand-written `dmtcp_initialize_plugin` rather than the macro. It initializes its SSH, event, file, pty, and socket subplugins in an order chosen for file-descriptor restoration dependencies, then calls `NEXT_FNC(dmtcp_initialize_plugin)`. [src/plugin/ipc/ipc.cpp]

The timer, SysV IPC, and PID plugins declare descriptors and use `DMTCP_DECL_PLUGIN`. Their descriptor names are `timer`, `sysvipc`, and `pid`, and their event hooks are registered through the macro-generated chain. [src/plugin/timer/timerlist.cpp] [src/plugin/svipc/sysvipc.cpp] [src/plugin/pid/pid.cpp] [include/dmtcp.h]

Current registration consequences:

- A plugin loaded before `libdmtcp.so` can register before the in-built descriptors if its `dmtcp_initialize_plugin` is reached first through symbol interposition and `NEXT_FNC` chaining. [include/dmtcp.h] [src/pluginmanager.cpp]
- `libdmtcp_pid.so` is placed after `libdmtcp.so` in `dmtcp_launch`, so its macro-generated registration is reached after the base library's position in the chain. [src/dmtcp_launch.cpp] [src/plugin/pid/pid.cpp]
- IPC subplugin initialization order is local to `libdmtcp_ipc.so` and is not the same as the shared-object preload order. [src/plugin/ipc/ipc.cpp]

## Forward versus reverse event order

`PluginManager::eventHook` always calls `PluginManager::initialize` first, so event delivery depends on the registration chain having populated `pluginInfos`. [src/pluginmanager.cpp]

The following events are delivered in plugin registration order: `DMTCP_EVENT_INIT`, `DMTCP_EVENT_RUNNING`, `DMTCP_EVENT_PRE_EXEC`, `DMTCP_EVENT_POST_EXEC`, `DMTCP_EVENT_ATFORK_PREPARE`, `DMTCP_EVENT_VFORK_PREPARE`, `DMTCP_EVENT_PTHREAD_START`, open/reopen/close/dup fd events, `DMTCP_EVENT_VIRTUAL_TO_REAL_PATH`, `DMTCP_EVENT_PRESUSPEND`, and `DMTCP_EVENT_PRECHECKPOINT`. The source comment describes these as the natural order for pre-checkpoint layered behavior. [src/pluginmanager.cpp]

The following events are delivered in reverse registration order: exit and pthread-exit events, atfork and vfork parent/child/failed events, `DMTCP_EVENT_REAL_TO_VIRTUAL_PATH`, `DMTCP_EVENT_RESUME`, `DMTCP_EVENT_RESTART`, and `DMTCP_EVENT_THREAD_RESUME`. The source comment says reverse ordering during resume/restart is required for layered software. [src/pluginmanager.cpp]

The concrete plugin hooks that receive these events include timer, SysV IPC, and PID hook functions. Their source files show descriptors pointing at `timer_event_hook`, `sysvipc_event_hook`, and `pid_event_hook`, so event-order changes would affect these plugin subsystems directly. [src/plugin/timer/timerlist.cpp] [src/plugin/svipc/sysvipc.cpp] [src/plugin/pid/pid.cpp]

## Current dlsym RTLD_NEXT and dl_iterate_phdr risk

The current wrapper-lookup design still uses next-symbol resolution. The historical comment in `src/syscallsreal.c` describes the current approach as resolving `_real_XYZ` through `dlsym(RTLD_NEXT, "XYZ")`, then explains known recursion and library-order hazards around malloc hooks and pthread symbols. The current implementation path uses `dlsym_default_internal_flag_handler(RTLD_NEXT, ...)` to fill the real-function table during wrapper preparation. [src/syscallsreal.c]

`NEXT_FNC` also resolves via `dmtcp_dlsym(RTLD_NEXT, #func)` after forcing `dmtcp_initialize` if available. The public plugin declaration macro uses that mechanism for `dmtcp_initialize_plugin`, so registration-chain traversal itself depends on DMTCP's next-symbol machinery. [include/dmtcp.h]

DMTCP has two relevant dlsym surfaces. The internal `dmtcp_dlsym` wrapper path accepts `RTLD_NEXT` and `RTLD_DEFAULT`, takes DMTCP's wrapper lock, computes the caller return address, and delegates to `dlsym_default_internal_flag_handler`. That lower-level handler obtains the caller's `link_map` with `dladdr1`, starts after the caller for `RTLD_NEXT`, and walks `l_next` until a matching default symbol is found. The design note for this machinery explains that DMTCP is trying to return the default version that the dynamic linker would choose, rather than whichever version libc `dlsym` returns. [src/dmtcp_dlsym_wrappers.cpp] [src/dmtcp_dlsym.cpp] [doc/dmtcp_dlsym.txt]

The external `dlsym` wrapper in `src/dlwrappers.cpp` has a separate `RTLD_NEXT` path: `dlsym_with_rtld_next` walks loaded objects with `dl_iterate_phdr`, records matching symbol instances, and returns the second match as the next function. Its comment states that this assumes the first match is the wrapper function and that the caller starts from that library. If the special path fails, the wrapper falls back through `NEXT_FNC(dlsym)`. [src/dlwrappers.cpp] [include/dmtcp.h]

The TSAN note reports a concrete concern: TSAN's constructor can run before DMTCP's constructor, then TSAN can call `dlsym(RTLD_NEXT, ...)`, DMTCP's `dlsym` wrapper can lead to `dl_iterate_phdr`, and TSAN may intercept that call before DMTCP has a protective `dl_iterate_phdr` wrapper or direct libc bypass. That is a source-backed risk statement and reproduction clue, not proof that any consolidation design solves the case. [gene-email.txt] [src/dlwrappers.cpp]

## R002 coverage checklist

| R002 prompt | Source-backed answer | S01 status |
|---|---|---|
| Constructor order | `libdmtcp.so` enters at constructor priority 101 through `dmtcp_initialize_entry_point`, calls `dmtcp_initialize`, prepares wrappers, initializes `PluginManager`, restores user-visible preload state, then emits `DMTCP_EVENT_INIT`. TSAN may still have its own earlier constructor when the target is built with TSAN. [src/dmtcpworker.cpp] [gene-email.txt] | Mapped; runtime TSAN proof deferred. |
| Wrapper order | `dmtcp_launch` currently preloads multiple DMTCP wrapper/plugin DSOs before `libdmtcp.so` and places `libdmtcp_pid.so` after it; user `DMTCP_PLUGIN` entries precede those built-ins, while an original user `LD_PRELOAD` is appended after the DMTCP-generated chain. [src/dmtcp_launch.cpp] | Mapped; consolidation can reduce DMTCP's own DSO stack. |
| TSAN interaction | Gene's stack shows `__tsan::Initialize` before DMTCP, then a TSAN interception path reaching DMTCP's `dlsym` wrapper and `___interceptor_dl_iterate_phdr`; the source confirms DMTCP's external `dlsym(RTLD_NEXT)` path calls `dl_iterate_phdr`. [gene-email.txt] [src/dlwrappers.cpp] | Feasible concern; not solved by documentation alone. |
| dlsym(RTLD_NEXT) | Internal DMTCP lookup uses `dmtcp_dlsym` and a caller-based `link_map` walk; the external `dlsym` wrapper has its own `RTLD_NEXT` special case and falls back through `NEXT_FNC(dlsym)`. [src/dmtcp_dlsym_wrappers.cpp] [src/dmtcp_dlsym.cpp] [include/dmtcp.h] [src/dlwrappers.cpp] | Requires design care; consolidation does not remove every `RTLD_NEXT` dependency. |
| dl_iterate_phdr | The risky direct call is in `dlsym_with_rtld_next`; S01 can say why TSAN may intercept it, but only M004/R011 can prove whether a DMTCP wrapper, direct libc lookup, or link-map replacement avoids the interceptor without breaking symbol semantics. [src/dlwrappers.cpp] [gene-email.txt] [src/dmtcp_dlsym.cpp] | Follow-up experiment required. |

## TSAN feasibility assessment

Feasibility answer for S01: consolidation is plausibly helpful for Gene's TSAN scenario because it can reduce DMTCP's own preload layering and make the DMTCP constructor/plugin-registration plan explicit, but it is not sufficient evidence that TSAN will run safely. The known failure path is not only that DMTCP has too many internal libraries; it is also that TSAN can run first, use `dlsym(RTLD_NEXT, ...)`, enter DMTCP's external `dlsym` wrapper, and then intercept DMTCP's direct `dl_iterate_phdr` call. [gene-email.txt] [src/dlwrappers.cpp]

The constructor side is encouraging but incomplete. DMTCP's constructor path is explicit and early once `libdmtcp.so` is loaded: priority 101, guard against repeat initialization, wrapper preparation, plugin-manager initialization, then event delivery. However, Gene's motivating experiment says TSAN's constructor can run before `dmtcp_initialize_entry_point`; S01 has not executed that binary under a sanitizer and therefore cannot claim the external constructor ordering is fixed. [src/dmtcpworker.cpp] [gene-email.txt]

The wrapper side remains the high-risk unknown. Internal wrapper preparation uses DMTCP's link-map based default-symbol search rather than calling the public `dlsym` symbol, but the external `dlsym` wrapper still handles `RTLD_NEXT` by calling `dl_iterate_phdr`. TSAN's interceptor stack in the email lines up with that source path. [src/syscallsreal.c] [src/dmtcp_dlsym_wrappers.cpp] [src/dlwrappers.cpp] [gene-email.txt]

S01 conclusion: internal-plugin consolidation should be treated as a prerequisite simplification, not the TSAN fix. The TSAN-safe design must also decide whether DMTCP should avoid `dl_iterate_phdr` in this path, wrap it first, or bypass interceptors through a verified libc/linker route. [src/dlwrappers.cpp] [src/dmtcp_dlsym.cpp] [gene-email.txt]

## dlsym RTLD_NEXT behavior under DMTCP

DMTCP's internal real-function lookup is already more specific than a raw libc `dlsym` call. `dmtcp_dlsym` and `dmtcp_dlvsym` handle `RTLD_NEXT` and `RTLD_DEFAULT` by computing the caller return address, invoking `dlsym_default_internal_flag_handler`, and printing version-debug information when configured. The handler obtains a `link_map` for that caller and, for `RTLD_NEXT`, begins at `map->l_next`. [src/dmtcp_dlsym_wrappers.cpp] [src/dmtcp_dlsym.cpp]

That behavior supports DMTCP's wrappers and plugin chain because `NEXT_FNC(func)` forces `dmtcp_initialize` when available and then caches `dmtcp_dlsym(RTLD_NEXT, #func)`. `DMTCP_DECL_PLUGIN` uses `NEXT_FNC(dmtcp_initialize_plugin)`, so plugin registration order is also tied to this next-symbol contract. [include/dmtcp.h] [src/pluginmanager.cpp]

The external `dlsym` wrapper is different. It exists for target/application calls to `dlsym`, has a special `RTLD_NEXT` branch that calls `dlsym_with_rtld_next`, and only then falls through to `NEXT_FNC(dlsym)`. The comments acknowledge that this special branch searches from the beginning of the link map using `dl_iterate_phdr` and assumes the first match is the wrapper and the second match is the desired next target. [src/dlwrappers.cpp]

The informal dlsym document explains why DMTCP implemented default-symbol lookup at all: libc `dlsym` can choose an older versioned symbol, while DMTCP wants the dynamic linker's default-version behavior. Any S02/S03 design that removes or bypasses DMTCP's current `RTLD_NEXT` surfaces must preserve that default-version requirement or explicitly narrow the affected call sites. [doc/dmtcp_dlsym.txt] [src/dmtcp_dlsym.cpp]

## dl_iterate_phdr feasibility and bypass options

Why DMTCP can call `dl_iterate_phdr` today: the external `dlsym` wrapper's `RTLD_NEXT` branch explicitly invokes `dlsym_with_rtld_next`, and that helper calls `dl_iterate_phdr(callback, NULL)` before falling back to `NEXT_FNC(dlsym)` if no result was found. [src/dlwrappers.cpp] [include/dmtcp.h]

Why TSAN may intercept it: Gene's reproduction has TSAN's constructor entering first and shows `___interceptor_dl_iterate_phdr` above `dlsym_with_rtld_next` in the stack. Since this DMTCP path calls `dl_iterate_phdr` by symbol name from inside the wrapper, a sanitizer-provided interceptor earlier in the active search/interposition path can see that call unless DMTCP has a verified bypass. [gene-email.txt] [src/dlwrappers.cpp]

Bypass option A is to remove the `dl_iterate_phdr` dependency from this `dlsym(RTLD_NEXT)` branch and reuse or adapt the existing caller-based `link_map` walk. That would align this external wrapper more closely with `dmtcp_dlsym`, but it must prove it still handles the target application's intended caller semantics rather than DMTCP's own wrapper frame. [src/dmtcp_dlsym.cpp] [src/dlwrappers.cpp]

Bypass option B is to add a DMTCP-controlled `dl_iterate_phdr` wrapper or real-function path that reaches libc/loader behavior before TSAN's interceptor. Gene explicitly suggests this as an experiment, but it has a strict ordering requirement: the DMTCP wrapper or bypass must be first for the relevant call and still allow the rest of DMTCP to run late enough near libc for ordinary wrappers. [gene-email.txt] [src/dlwrappers.cpp]

Bypass option C is to keep the current branch but constrain when it is enabled. That would be narrower than a full lookup rewrite, but it must be reconciled with the current `dlsym` wrapper motivation around target applications that patch wrapped symbols and with the `libdmtcp_dl.so` wrapper layer that protects `dlopen`/`dlclose` behavior through its own locks and `NEXT_FNC` calls. [src/dlwrappers.cpp] [src/plugin/dl/dlwrappers.cpp] [include/dmtcp.h]

## What consolidation solves

Consolidation can reduce DMTCP's own preload layering. Today `dmtcp_launch` injects several enabled DMTCP plugin DSOs before `libdmtcp.so`, then `libdmtcp_pid.so` after it; a single internal plugin body would reduce the number of DMTCP-owned link-map nodes that compete with each other before reaching libc. [src/dmtcp_launch.cpp]

Consolidation can also make constructor and plugin registration architecture more explicit. Instead of relying on multiple `dmtcp_initialize_plugin` definitions chained through `NEXT_FNC`, S02 can choose built-in registration order inside one process-local registration surface while preserving the forward and reverse event-order semantics that `PluginManager::eventHook` currently enforces. [include/dmtcp.h] [src/pluginmanager.cpp]

Finally, consolidation can clarify the difference between the generated DMTCP hijack chain and the restored user-facing `LD_PRELOAD`. That helps S03 migration planning because `DMTCP_HIJACK_LIBS` is captured before original user preloads are appended, while `restoreUserLDPRELOAD` only changes environment visibility after the dynamic linker has already loaded the objects. [src/dmtcp_launch.cpp] [src/dmtcpworker.cpp]

## What consolidation cannot prove or solve in S01

Consolidation cannot prove TSAN constructor safety. Gene's concern depends on the target being built with TSAN, TSAN's constructor order, and TSAN's interceptor behavior during DMTCP initialization; S01 only maps source paths and does not run that dynamic scenario. [gene-email.txt] [src/dmtcpworker.cpp]

Consolidation cannot automatically remove `RTLD_NEXT`. `NEXT_FNC`, `DMTCP_DECL_PLUGIN`, wrapper preparation, `libdmtcp_dl.so`'s `dlopen`/`dlclose` wrappers, and the external `dlsym` wrapper all still depend on next-symbol or real-function lookup semantics in different ways. [include/dmtcp.h] [src/syscallsreal.c] [src/plugin/dl/dlwrappers.cpp] [src/dlwrappers.cpp]

Consolidation cannot by itself make `dl_iterate_phdr` sanitizer-safe. The risky call is in the external `dlsym` wrapper implementation, not merely in the fact that multiple DMTCP plugin DSOs exist. If that call remains, a TSAN interceptor may still be on the path; if it is removed or bypassed, default-version and caller-search semantics must be preserved. [src/dlwrappers.cpp] [src/dmtcp_dlsym.cpp] [doc/dmtcp_dlsym.txt]

## Likely gaps before design

1. Exact TSAN constructor ordering under `bin/dmtcp_launch` is still empirical. Source shows DMTCP constructor priority 101, but TSAN's constructor and interceptor order come from the target's sanitizer runtime and must be observed with breakpoints. [src/dmtcpworker.cpp] [gene-email.txt]
2. The external `dlsym(RTLD_NEXT)` wrapper has caller-semantics assumptions that are explicitly weaker than glibc's stack-based behavior. A link-map rewrite must prove which caller object should be used when TSAN or another library calls `dlsym` through DMTCP. [src/dlwrappers.cpp] [src/dmtcp_dlsym.cpp]
3. The `libdmtcp_dl.so` wrapper layer has checkpoint-locking and `dlopen`/constructor constraints that may survive consolidation even if the shared object disappears. Built-in registration must preserve those constraints, not only move code. [src/plugin/dl/dlwrappers.cpp] [include/dmtcp.h]
4. Event-order compatibility must be preserved for plugins that rely on forward pre-checkpoint delivery and reverse resume/restart delivery. Changing registration order is therefore a behavioral migration, not just a link-step cleanup. [src/pluginmanager.cpp]

## M004/R011 follow-up experiment matrix

| Experiment | Purpose | Source anchor | Expected pass signal | Expected fail signal |
|---|---|---|---|---|
| Tiny `-fsanitize=thread` target under `bin/dmtcp_launch` | Reproduce Gene's scenario on a minimal dynamically linked target and confirm whether the current branch reaches the sanitizer/DMTCP interaction. | Gene provides the compile and launch shape and identifies TSAN as the motivating target. [gene-email.txt] | The target starts under `bin/dmtcp_launch`, reaches both TSAN and DMTCP breakpoints, and any crash stack is captured with the wrapper path named. | Startup crashes or loops before both breakpoints, or the stack cannot identify whether DMTCP's `dlsym` wrapper was involved. |
| Breakpoint/order confirmation for `__tsan::Initialize` and `dmtcp_initialize_entry_point` | Establish external constructor order rather than assuming source priority controls sanitizer order. | DMTCP constructor entry is `dmtcp_initialize_entry_point`; Gene names `__tsan::Initialize` as the first breakpoint. [src/dmtcpworker.cpp] [gene-email.txt] | The debugger transcript records which breakpoint fires first and whether DMTCP initialization begins from a TSAN-triggered wrapper path. | Breakpoints are optimized out, never reached, or show an order that invalidates the hypothesized crash path. |
| `dlsym/RTLD_NEXT` link-map behavior | Compare DMTCP's current external `dlsym` wrapper result with the internal `dmtcp_dlsym` caller-based link-map walk for representative symbols such as allocator/string functions. | External wrapper uses `dl_iterate_phdr`; internal lookup uses `dladdr1`, `link_map`, and `l_next`. [src/dlwrappers.cpp] [src/dmtcp_dlsym.cpp] [src/dmtcp_dlsym_wrappers.cpp] | For chosen symbols, the experiment records the caller object, selected link-map node, and returned address with no recursion. | Returned addresses differ without explanation, or either path recurses through TSAN/DMTCP unexpectedly. |
| `dl_iterate_phdr` interception or bypass prototype | Decide whether to wrap `dl_iterate_phdr`, bypass it, or replace it for the external `dlsym(RTLD_NEXT)` branch. | Risky call is in `dlsym_with_rtld_next`; Gene suggests a DMTCP wrapper or forced libc call. [src/dlwrappers.cpp] [gene-email.txt] | Prototype avoids `___interceptor_dl_iterate_phdr` during DMTCP initialization while preserving intended `dlsym(RTLD_NEXT)` results. | TSAN still intercepts the call, DMTCP loops/crashes, or symbol lookup semantics regress. |
| Default-version preservation check | Ensure any replacement for the external branch does not discard DMTCP's default-version lookup requirement. | The dlsym note explains DMTCP's default-version motivation and the implementation lives in `src/dmtcp_dlsym.cpp`. [doc/dmtcp_dlsym.txt] [src/dmtcp_dlsym.cpp] | Versioned-symbol probes return the dynamic-linker default symbol expected by DMTCP's current implementation. | Replacement returns an older/hidden symbol or skips an intended interposed symbol. |

## Handoff constraints for S02/S03/S04

S02 built-in registration architecture: do not treat consolidation as only a linker change. Registration order currently emerges from `dmtcp_initialize_plugin` interposition and `NEXT_FNC`; a built-in registry must intentionally preserve or revise the ordering of in-built descriptors, user plugins, PID, IPC subplugins, and forward/reverse event delivery. [include/dmtcp.h] [src/pluginmanager.cpp] [src/dmtcp_launch.cpp]

S03 migration order: migrate preload construction and registration in an order that keeps `DMTCP_HIJACK_LIBS`, original `LD_PRELOAD` restoration, constructor entry, and wrapper preparation observable and comparable against the current map. The `libdmtcp_dl.so` `dlopen`/`dlclose` locking semantics are part of that migration surface. [src/dmtcp_launch.cpp] [src/dmtcpworker.cpp] [src/plugin/dl/dlwrappers.cpp]

S04 final design synthesis: present consolidation as reducing DMTCP-owned link-map complexity, not as a TSAN guarantee. The final design should explicitly defer or include the M004/R011 runtime experiments for `__tsan::Initialize`, `dmtcp_initialize_entry_point`, `dlsym(RTLD_NEXT)`, and `dl_iterate_phdr` interception/bypass before claiming TSAN support. [gene-email.txt] [src/dlwrappers.cpp]

## Immediate consolidation implications for S02 and S03

S02 and S03 should treat the following as established current-order facts:

1. `DMTCP_PLUGIN` entries precede DMTCP's built-in plugin/shared-object chain in the initial preload string. [src/dmtcp_launch.cpp]
2. The original user `LD_PRELOAD` is appended after DMTCP's generated chain and later restored in the environment before user `main`; that restoration does not reorder loaded objects. [src/dmtcp_launch.cpp] [src/dmtcpworker.cpp]
3. `DMTCP_HIJACK_LIBS` captures the generated DMTCP chain before the original user preload is appended. [src/dmtcp_launch.cpp]
4. `libdmtcp.so` constructor priority 101 enters `dmtcp_initialize_entry_point`, prepares wrappers, initializes `PluginManager`, restores user preload visibility, and then emits `DMTCP_EVENT_INIT`. [src/dmtcpworker.cpp] [src/pluginmanager.cpp]
5. Registration order comes from the `dmtcp_initialize_plugin` chain and `NEXT_FNC`, while event delivery is forward for pre-checkpoint-style events and reverse for resume/restart-style events. [include/dmtcp.h] [src/pluginmanager.cpp]
6. Current wrapper and plugin-chain lookups still depend on `RTLD_NEXT` machinery; one DMTCP dlsym wrapper path walks `dl_iterate_phdr`. [src/syscallsreal.c] [include/dmtcp.h] [src/dlwrappers.cpp]
7. Consolidation should reduce DMTCP's internal preload layers and make registration deterministic, but S02/S03 must not claim that this alone proves TSAN-safe ordering or removes the need for the M004/R011 `dl_iterate_phdr` experiments. [gene-email.txt] [src/dlwrappers.cpp]

Narrow conclusion for S01: consolidation design must account for dynamic-linker search order, plugin-registration/event order, and sanitizer interposition. Moving internal plugin wrappers into one library reduces DMTCP's own preload layering, but it does not guarantee TSAN-safe wrapper ordering while the external `dlsym(RTLD_NEXT)` path can still reach `dl_iterate_phdr` and while constructor order with TSAN remains unproven at runtime. [gene-email.txt] [src/dmtcp_launch.cpp] [src/pluginmanager.cpp] [src/dlwrappers.cpp]
