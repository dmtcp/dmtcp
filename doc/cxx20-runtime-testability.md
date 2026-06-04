# C++20 Runtime Testability Roadmap

## Goal

Upgrade DMTCP's runtime maintainability after the C++20 baseline by building a
stronger test foundation first, then using it to drive process metadata,
thread-state, assertion/logging, and initialization cleanup.

The priority order is:

1. Testability
2. Maintainability
3. Runtime overhead reduction

Runtime overhead remains a design guardrail. New core runtime code should avoid
unnecessary allocation, complex library calls, wrapper reentry, and fragile
restart-path dependencies. But broad performance work should wait until the
test foundation can catch regressions.

The intended reader is a DMTCP maintainer preparing the implementation series.
After reading this document, they should be able to split the work into
bisectable commits and know which invariants must be tested before each runtime
cleanup lands.

## Global Constraints

- Maintain git bisectability. Each commit must build, and behavior-changing
  commits must carry focused tests or be immediately preceded by them.
- Keep commits logical. Do not mix C++20 cleanup, test harness work, and
  checkpoint/restart semantic changes in one commit.
- Prefer clean end-state architecture over compatibility scaffolding. This
  project does not need checkpoint-image or old internal API compatibility.
- Keep generated Autotools files synchronized with the source changes that
  require them.
- When new test targets or build probes touch Autotools inputs, plan for local
  Autoconf/Automake version mismatches explicitly. If regeneration cannot use
  the checked-in toolchain version, record that in the commit notes and keep
  generated-file diffs minimal and tied to the source change that requires
  them.
- Avoid dynamic allocation and complex APIs in fragile contexts such as early
  constructors, wrappers, checkpoint suspend, restart, TLS restore, and fatal
  diagnostic paths.
- Keep public plugin ABI decisions separate from checkpoint-image bootstrap
  format decisions.
- Maintain a fragile-context matrix as the work proceeds. At minimum, classify
  early constructors, preload wrappers, signal handlers, checkpoint suspend,
  checkpoint serialization, restart before memory restore, TLS restore,
  coordinator I/O, and fatal diagnostics by whether they permit allocation,
  wrapped libc calls, locks, plugin events, and normal logging.
- Use focused integration tests as merge gates for runtime commits. Unit tests
  and synthetic coordinator clients are necessary but not sufficient for
  restart, TLS, signal, fork/exec, and wrapper-reentry behavior.

## Fragile-Context Matrix

The matrix below should be updated when a phase discovers a more precise rule.
It is intentionally conservative: a context marked "no" may still be able to
use a specific primitive after a local audit, but the burden is on that change
to explain why it is safe.

| Context | Allocation | Wrapped libc calls | Locks | Plugin events | Normal logging | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Early constructors and preload init | Avoid | Avoid | Avoid | Avoid | Avoid | Runtime state, TLS, and wrapper targets may not be initialized yet. |
| Preload wrappers | Avoid | Avoid reentry | Scoped only | Existing hooks only | Bounded only | Prefer `_real_*` calls and `WrapperLock`; avoid calling back into wrapped APIs unless intentional. |
| Signal handlers | No | No | No | No | Bounded only | Async-signal-safety dominates; diagnostics must not allocate or take ordinary locks. |
| Checkpoint suspend / presuspend | Avoid | Avoid | Scoped only | Yes | Bounded only | Threads may be stopped or converging on barriers; preserve event ordering. |
| Checkpoint serialization | Avoid | Avoid | Scoped only | Existing hooks only | Bounded only | Serializer paths should not depend on wrapper behavior or heap growth. |
| Restart before memory restore | No | No | No | No | Bounded only | Only fixed bootstrap data and raw restart primitives are trustworthy. |
| TLS restore | No | Avoid | No | No | Bounded only | Thread descriptors and per-thread state are being rebuilt. |
| Coordinator I/O | Yes | Yes | Yes | No | Yes | Coordinator is not preloaded runtime code, but protocol parsing must remain bounded and testable. |
| Fatal diagnostics | No | Avoid | No | No | Fixed buffer | ASSERT/WARNING output must work after allocator or runtime corruption. |
| Test harness helpers | Yes | Yes | Yes | N/A | Yes | Harness code should optimize for clarity, diagnostics, and cleanup reliability. |

## Approach

Use a test-foundation-first sequence:

1. Establish a new test harness, minimal structured coordinator command output,
   C++ unit-test support, and synthetic coordinator worker tests.
2. Redesign process checkpoint bootstrap metadata after tests can validate the
   restart path.
3. Clean up core thread state ownership before moving plugin-specific state.
4. Replace allocation-heavy ASSERT/WARNING paths with fixed-buffer diagnostics.
5. Simplify process and thread initialization architecture.
6. Apply selective C++20 source modernization in small, tested commits.

This sequencing is slower than jumping directly into runtime refactors, but it
reduces the risk of breaking checkpoint/restart behavior without a useful
failure signal.

## Phase 1: Test Foundation

Replace the current monolithic `test/autotest.py` with a clean, compact Python
harness.

Planned structure:

- `test/autotest.py`: supported CLI and registry entry point.
- `test/autotest_old.py`: frozen copy of the old runner for reference and
  one-off validation only.
- `test/dmtcp_test_harness.py`: coordinator/process/checkpoint/restart helpers,
  result model, cleanup, and artifact capture.
- `test/dmtcp_test_cases.py`: default checkpoint/restart test class and
  registered tests.
- Optional diagnostics module only if artifact handling becomes too large for
  the harness file.

Rules:

- New tests go only into the new harness.
- The old runner is frozen as `test/autotest_old.py` for reference and one-off
  validation. CI and normal `make check` should use the new harness.
- The new harness must explicitly track old-suite parity gaps instead of
  pretending unsupported classes are covered. Direct `dmtcp_launch` cases can
  be ported as `TestSpec` entries. PTY-backed terminal/editor tests, MPI
  launchers, and interactive external tools need deliberate harness support
  before they become authoritative new-harness coverage.
- Treat shim support as the planned path for hard old-suite classes, not as an
  emergency fallback. The `make check` switch should be blocked only by the
  port-now set and any shim-backed classes that are intentionally part of the
  new authoritative suite; known-main failures and environment-specific classes
  stay deferred until separately triaged.
- Tests that are known to fail on `main`, such as current terminal/editor
  cases around `vim`, are not migration gates for this project. Leave them out
  of the authoritative new-harness set until they are separately triaged.
- If an old direct test only passes for a single checkpoint/restart cycle under
  the new harness, keep that `cycles=1` limitation explicit in the `TestSpec`
  and treat the second post-restart checkpoint as follow-up coverage to debug.
- Simple tests should be one-liners or near one-liners using a default test
  class with fields such as name, command, peer count, environment, plugins,
  timeout, tags, checkpoint mode, and restart mode.
- Complex tests can subclass or use hooks, but adding ordinary checkpoint/
  restart coverage should not require editing many files.
- Preserve a parity checklist while porting: test name, tags, required
  commands/libraries, environment probes, multilib expectations, stack or ulimit
  requirements, plugin requirements, and known host-sensitive skips.
- Keep `test/autotest-parity.md` updated as that checklist so unsupported or
  known-main-failing old tests are visible without becoming migration gates.
- Start sequential by default. Only add parallel execution after port allocation,
  process-group cleanup, coordinator teardown, and artifact retention are
  reliable enough to avoid CI flakes.

Diagnostics should be first-class output:

- per-test temporary directory
- command transcript
- coordinator stdout/stderr and status
- worker stdout/stderr
- checkpoint image list
- restart logs
- failure phase, such as launch, checkpoint, restart, timeout, or cleanup
- process-group teardown result and leftover-process detection
- retained temporary directory on failure

Runtime integration gates should be named early, even if some are implemented
after the harness skeleton:

- single-process checkpoint/restart smoke
- multi-thread checkpoint/restart
- `gettid`/PID-TID virtualization smoke
- fork and exec restart cases
- presuspend/signal-path case
- TLS restore ordering case
- restart reconnect to the coordinator
- gzip or piped checkpoint image restart if supported by the current suite

If event-order bugs become hard to diagnose, add a tiny test plugin that records
checkpoint/restart event order for the harness to assert. Do not build a broad
mocking framework unless these targeted tests prove insufficient.

## Phase 2: Minimal `dmtcp_command --json`

Add an experimental JSON mode to support the new harness and user diagnostics.
This is not initially a stable public CLI contract.

Initial scope:

- status
- checkpoint request
- blocking checkpoint result if supported
- quit/kill coordinator
- structured error code and message
- coordinator unavailable
- coordinator host/port echoed where useful for diagnostics

The implementation can emit JSON directly. No external JSON dependency is
needed. Python tests can parse the output with the standard library.

The initial schema should be small and versioned. Include fields such as:

- `schema_version`
- `type`
- `ok`
- `error_code`
- `error_message`
- `phase`
- `coordinator_host`
- `coordinator_port`

The new harness should treat malformed JSON, mixed human/JSON output, and
non-zero exits as structured failures with captured raw output.
Centralize JSON parsing and schema validation in one harness helper so schema
changes are localized. The harness should assert `schema_version` from day one
instead of accepting best-effort payloads.

## Phase 3: C++ Unit Tests

Add a tiny in-repo C++ unit-test framework with no third-party dependency.

Targets:

- pure helper functions
- path/string/numeric parsing
- virtual ID tables
- serializer invariants
- plugin manager metadata helpers
- checkpoint bootstrap struct layout tests, initially against the current
  header and expanded when Phase 5 introduces conversion helpers
- fixed-buffer formatter utilities once Phase 7 adds them
- C-mode compile checks for any public bootstrap header consumed by restart
  code written in C

Execution:

- Direct Makefile target, such as `make check-unit`.
- New Python harness can also run/report unit binaries for unified CI output.

Unit tests should avoid preload, coordinator sockets, restart, and signal-
sensitive paths unless the test is explicitly about those mechanisms.

## Phase 4: Synthetic Coordinator Worker Tests

Before refactoring coordinator internals, add tests that drive the real
`dmtcp_coordinator` through the real socket protocol using synthetic workers.
Synthetic clients must use the production message layout and serialization
rules, not a parallel test-only protocol.

Coverage targets:

- new worker join
- multi-worker join
- disconnect and reconnect
- checkpoint request when workers are running
- barrier release only when all expected workers reach the same barrier
- worker disconnect while waiting at a barrier
- restart quorum and `RESTARTING` workers
- reject wrong computation group
- reject new workers while checkpoint/restart is active
- duplicate `DMT_DO_CHECKPOINT` handling for fork/exec race behavior
- kill, quit, and exit-on-last paths
- KVDB request/response if it stays simple
- peer count mismatch on restart
- simultaneous checkpoint and restart/control requests
- barrier generation desynchronization
- slow worker or timeout while checkpointing
- half-open sockets and partial message reads
- protocol version, magic, or computation-id mismatch on connect

After this coverage exists, extracting coordinator internals into a cleaner
`CoordinatorCore` can be evaluated. Keep the synthetic worker tests even after
any extraction so in-process state-machine tests do not drift from the real
protocol. For each modeled state transition, keep at least one thin real-worker
cross-check through `dmtcp_launch` and `dmtcp_command --json` so synthetic tests
cannot silently diverge from deployed behavior.
No coordinator state transition should be covered only by synthetic clients:
each modeled transition needs at least one real-worker assertion before it is
treated as authoritative coverage.

## Phase 5: Process Metadata And Checkpoint Bootstrap

No checkpoint-image backward compatibility is required for this project.

Keep the checkpoint bootstrap POD type public and readable in `include/dmtcp.h`
because MANA and similar external tools may include `dmtcp.h` and inspect the
checkpoint image directly. Treat this as checkpoint-image/bootstrap ABI, not as
the external plugin ABI.

Do not add accessor helpers initially. The struct should be clear enough for
direct field access, and there is currently only one known direct external
consumer class.

Bootstrap invariants:

- The checkpoint image starts with two identical bootstrap records: one for
  `dmtcp_restart` and one for `mtcp_restart` after exec.
- Restart readers must validate magic/signature, format version, header size,
  architecture, word size, endian fields, and any reserved fields that must be
  zero.
- Gzip, pipe, and seek-based restart paths must be audited before the record
  size or layout changes.
- The bootstrap struct must remain C-compatible. Do not use C++-only types,
  default member initializers, constructors, or non-fixed-width field types in
  the part consumed by C restart code.
- The default should be to keep the bootstrap record fixed-size and
  page-aligned unless an audit proves that alignment and 4096-byte sizing no
  longer serve restart mechanics. If the size changes, it must be
  self-describing with a bounded `header_size`.

Redesign the bootstrap header to contain only data needed before restored
memory can be trusted:

- magic/signature
- checkpoint format version
- header size
- architecture, word-size, and endian sanity fields
- restore buffer region
- saved brk
- end of stack
- vDSO/VVAR regions and validation fields
- post-restart entry point
- process identity and orchestration fields needed by `dmtcp_restart`, such as
  `upid`, `uppid`, `compGroup`, `numPeers`, process name, and session/process
  tree fields still required before memory restore

Keep rich process, thread, and plugin metadata in restored memory. Do not add a
JSON checkpoint metadata section for this work; it would add parser complexity
to a fragile restart path without clear value.

Replace `ProcessInfo : DmtcpCkptHeader` with explicit runtime process state and
a snapshot/conversion step that prepares the bootstrap header. The snapshot
conversion runs during checkpoint serialization, so it must follow suspend-time
fragile-context rules: no dynamic allocation, no wrapped libc calls, no plugin
events, and no conversion work that depends on unsuspended application threads.
Prefer filling a preallocated header buffer owned by the process runtime state.

Before changing fields, publish a field matrix that classifies each current
bootstrap field as:

- required by `dmtcp_restart` before memory restore
- required by `mtcp_restart` before memory restore
- required only after restored memory is available
- obsolete or derivable
- externally consumed by MANA or another known wrapper around restart

Current `DmtcpCkptHeader` field matrix:

| Field group | Current pre-restore consumer | Classification | Migration note |
| --- | --- | --- | --- |
| `ckptSignature`, `headerSize`, `headerVersion`, `wordSize`, `endianMarker` | `dmtcp_restart`, `mtcp_restart`, MANA lower-half-style restart wrappers | Required before memory restore | Keep in the public C-compatible bootstrap prefix. Validate before trusting any other field. |
| `restoreBuf` | `dmtcp_restart` passes the region to `mtcp_restart`; `mtcp_restart` uses it to stage restore code | Required before memory restore | Keep in the bootstrap prefix until restore staging is redesigned. |
| `savedBrk` | `mtcp_restart`/restore path | Required before memory restore | Keep in bootstrap; it is part of reconstructing the process address-space shape. |
| `endOfStack` | `mtcp_restart` and MANA lower-half memory restore logic | Required before memory restore | Keep in bootstrap unless stack detection is replaced with per-area metadata. |
| `postRestartAddr` | `mtcp_restart` and MANA lower-half call into DMTCP after memory restore | Required by restart handoff | Keep in bootstrap. It is consumed after memory is restored but before normal runtime control resumes. |
| `elfType` | `dmtcp_restart` chooses compatible restart path | Required before memory restore | Keep in bootstrap or replace with an architecture field that covers the same decision. |
| `vdso`, `vvar`, `vvarVClock`, `clock_gettime_offset`, `getcpu_offset`, `gettimeofday_offset`, `time_offset` | `dmtcp_restart` validates vDSO offset compatibility; `writeckpt` and `mtcp_restart` use vDSO/VVAR location semantics | Required before memory restore for current vDSO policy | Freeze current semantics during the first split. Any semantic change needs separate vDSO/VVAR restart tests. |
| `upid`, `uppid`, `compGroup` | `dmtcp_restart` rebuilds process identity and reconnects to the coordinator | Required before memory restore | Keep in bootstrap while `dmtcp_restart` orchestrates process tree reconstruction. |
| `pid`, `ppid`, `sid`, `gid`, `fgid`, `isRootOfProcessTree` | `dmtcp_restart` process-tree/session reconstruction; MANA lower-half session restore uses `pid`/`sid` | Required before memory restore | Keep initially. Later migration needs restart tests for dependent children, orphan roots, process groups, and session leaders. |
| `numPeers` | `dmtcp_restart` restart orchestration and coordinator reconnect | Required before memory restore | Keep until restart quorum/orchestration metadata is separated from process runtime state. |
| `procname`, `procSelfExe` | `dmtcp_restart` coordinator reconnect, process display, and restart target selection | Required before memory restore | Keep initially; if moved, provide an equally early fixed-size process identity block. |
| `padding` | No intended reader | Reserved | Keep zeroed and validate if it is promoted to a formal reserved region. |

Fields not listed above are runtime `ProcessInfo` members outside the public
bootstrap struct today. They can move into restored runtime state independently
as long as checkpoint serialization still writes a complete bootstrap snapshot.

If vDSO/VVAR fields are not the focus of the first redesign, freeze their
current semantics and only move them mechanically. If their semantics change,
split that work into its own subsection and require restart tests that exercise
vDSO/VVAR validation.

MANA and similar consumers should either be updated in lockstep with the header
format change or protected by a loud format failure. A layout-changing commit
should bump the checkpoint format version, and should consider bumping the
signature/magic if an old consumer could otherwise misread the new layout.

Validation:

- Audit every current `DmtcpCkptHeader` consumer before removing or moving a
  field.
- Test both bootstrap-record reads.
- Test uncompressed checkpoint images and any gzip/pipe restart paths supported
  by the suite.
- Unit-test header layout, version/signature validation, and ProcessInfo-to-
  bootstrap conversion.
- Compile the public bootstrap header in C mode.
- Gate the redesign with multi-thread and restart-path smoke tests, not just
  struct layout tests.
- Add a small external-consumer-style test if it stays simple. Otherwise,
  document MANA as a known direct consumer and update it separately.

## Phase 6: ThreadInfo And ThreadList Cleanup

`ThreadInfo` and `ThreadList` should be separated conceptually.

`ThreadInfo` is per-thread state. `ThreadList` is lifecycle/list coordination.
The current thread structure mixes at least three concerns:

- core per-thread state: real pid/tid, virtual pid/tid when available, wrapper
  lock count, and fixed diagnostic buffer
- checkpoint/restart thread state: saved context, TLS restore data, signal
  masks, clone flags, ptid/ctid, and saved stack
- ThreadList bookkeeping: active-list links, lifecycle state, and checkpoint-
  thread coordination

First milestone:

- Add a core thread-state chunk inside the existing thread structure without
  physically splitting all fields at once. Avoid a broad rename until restart
  ordering is covered by tests.
- Ensure `ThreadInfo` exists before ASSERT/WARNING can emit.
- Use a preallocated initial-thread object, for example:
  - `static ThreadInfo motherofallThreadInfo`
  - `__thread ThreadInfo *curThread = NULL`
  - early bootstrap initialization sets `curThread = &motherofallThreadInfo`
- Define the lifecycle for the initial thread, clone-created application
  threads, the DMTCP checkpoint thread, and post-restart resumed threads before
  migrating ASSERT/WARNING call sites.
- Utility binaries should use the same diagnostic-buffer interface as
  `libdmtcp.so`. Prefer sharing the same core thread context type; if a smaller
  sibling type is required, keep the formatter interface explicit so the
  implementations do not drift.
- Resolved design decision for the first implementation: restored TLS is not
  treated as authoritative for `curThread` until restart code explicitly
  re-establishes it.
- Do not blindly trust restored TLS to provide the correct `curThread` during
  restart fixup. Restart should explicitly re-establish the current thread
  context before generic diagnostics depend on it. Until that ordering is
  proven for every restart path, the ASSERT/WARNING implementation must retain
  a fixed-size static thread-local fallback buffer for cases where no
  `ThreadInfo` provider is available, including utility binaries.

Lifecycle questions that must be answered in the first milestone:

- Where is per-thread state for clone-created threads allocated, and can that
  allocation happen before the new thread can emit diagnostics?
- How does the checkpoint thread identify its own state without confusing
  application-thread lifecycle bookkeeping?
- Which fields are real kernel identifiers and which are virtual identifiers
  visible to application code?

Do not add MANA/user slots now. Do not pull built-in plugin TLS into the first
ThreadInfo cleanup. Migrate plugin-specific thread state later, one plugin or
concern per commit, with targeted tests.

## Phase 7: ASSERT/WARNING And Logging

Replace allocation-heavy `jalib/jassert` usage with a DMTCP-owned diagnostic
system.

Initial scope:

- Move new assertion/logging implementation to `src/util_assert.h` and
  `src/util_assert.cpp` or equivalent.
- Replace ASSERT and WARNING first.
- Defer high-volume TRACE migration.
- Treat ASSERT as WARNING plus termination.
- Keep old `JASSERT`, `JWARNING`, and `JTRACE` temporarily while call sites
  migrate subsystem-by-subsystem.
- Track the migration explicitly by subsystem and fragile-context risk so the
  old diagnostic system is retired deliberately instead of lingering after the
  low-risk call sites are converted.

Preferred API:

```c++
ASSERT(fd >= 0, "open failed: path={} fd={}", path, fd);
WARNING(ret == 0, "setsockopt failed: fd={} opt={}", fd, opt);
ASSERT_ERRNO(fd >= 0, "open failed: path={}", path);
WARNING_ERRNO(ret == 0, "retrying syscall: fd={}", fd);
ASSERT_EQ(expected, actual);
ASSERT_NULL(ptr);
ASSERT_NOT_NULL(ptr);
ASSERT_FALSE(flag);
ASSERT_GT(count, 0);
```

Convenience macros should include the useful comparison/null forms:

- `ASSERT_TRUE`
- `ASSERT_FALSE`
- `ASSERT_EQ`
- `ASSERT_NE`
- `ASSERT_LT`
- `ASSERT_LE`
- `ASSERT_GT`
- `ASSERT_GE`
- `ASSERT_NULL`
- `ASSERT_NOT_NULL`

All macros should route through one formatter and emit path.

Do not use `std::format`. It may allocate and has uneven support across older
libstdc++ versions.

Formatter requirements:

- append into `curThread->core.assertBuffer[4096]`
- macros save `errno` before evaluating diagnostic arguments, pass the captured
  value to the formatter, and restore `errno` before returning from WARNING or
  terminating from ASSERT
- no dynamic allocation
- no wrapped libc calls on the emit path; write through `_real_write` or an
  equivalent syscall-level helper
- fatal ASSERT exit must not consult environment variables; use a fixed raw
  exit code on this path and keep environment-controlled launcher/restart
  failure policy outside the diagnostic formatter
- `{}` default formatting
- `{:x}` and `{:#x}` hex formatting
- simple width forms such as `{:08x}` if straightforward
- strings, chars, booleans, signed integers, unsigned integers, and pointers
- errno code/text helpers for `*_ERRNO`
- explicit truncation marker with stable boundary behavior, guaranteed to fit
  inside the fixed buffer
- no floating point initially
- format strings must be literals or otherwise constrained enough that
  placeholder mismatch behavior is deterministic and testable

Before migrating a subsystem, audit its existing diagnostic arguments. If a
current `JASSERT`/`JWARNING` path depends on floating-point formatting, either
defer that call site or add a separately tested formatter extension. Do not let
floating-point support become an implicit blocker hidden inside a broad
call-site migration commit.

Emit-path requirements:

- default to stderr or the existing DMTCP log fd policy, but document which one
  is authoritative
- define behavior when the log fd is closed or unavailable
- define fork, exec, and restart behavior for the diagnostic destination
- avoid allocation, locks, iostreams, locale APIs, and wrapped libc
- include reentrancy handling for an ASSERT/WARNING that interrupts another
  diagnostic in the same thread

Signal and fragile-context policy:

- Do not migrate signal-handler or checkpoint-suspend call sites to the generic
  formatter until they are audited.
- Either prove the formatter/emit subset used there is async-signal-safe, or add
  a minimal literal/integer-only fatal diagnostic path for those contexts.
- A WARNING that prints and continues does not need to be available in every
  signal context; the design must say which macros are legal there.
- Keep old `JASSERT`/`JWARNING` call sites in fragile paths until the new
  primitive has a verified replacement for that context.

Fatal behavior should emit diagnostics and then exit with the fixed raw ASSERT
failure code. The no-allocation fatal path must not consult `DMTCP_FAIL_RC`,
`DMTCP_ABORT_ON_FAILURE`, or any other environment-controlled policy. Backtrace
and `/proc/self/maps` dumping are not part of the first no-allocation primitive
unless later added behind explicit policy.

## Phase 8: Initialization Architecture

Only simplify initialization after the test foundation, process metadata,
core ThreadInfo cleanup, and initial ASSERT/WARNING work are in place.

Target process initialization phases:

1. real syscall/wrapper preparation
2. bootstrap ThreadInfo initialization
3. basic diagnostics setup
4. PluginManager descriptor/init setup
5. ProcessInfo/runtime process state setup
6. full ThreadList setup
7. plugin `DMTCP_EVENT_INIT`
8. checkpoint-thread creation

Make early-init constraints explicit:

- no logging before bootstrap ThreadInfo exists
- no plugin events before core thread/process state is ready
- no wrapped calls in fragile paths
- no dynamic allocation unless the phase explicitly allows it

Keep fork, exec, restart, and thread reinitialization as separate follow-up
milestones. They should not be bundled into the first init cleanup commit.

Phase 7 and Phase 8 should be interleaved carefully. It is acceptable to add
the formatter and bootstrap diagnostic buffer before the full init cleanup, but
broad call-site migration should wait until the initialization order guarantees
that a core thread context exists for every process path that can emit.

## Phase 9: Deferred C++20 Source Modernization

The C++20 build baseline should remain a base commit. Source modernization
should wait until the new test foundation is active.

Candidate modernization work:

- `std::string_view`, `starts_with`, and `ends_with` for internal parsing/path
  helpers
- `map::contains` and single-iterator lookup cleanup
- `std::from_chars` and `std::to_chars` for strict numeric parsing
- `std::span` overloads for internal buffer APIs while keeping raw pointer APIs
  where ABI or syscall-like shape matters
- allocator alias cleanup in `dmtcpalloc.h`
- `std::erase_if` for side-effect-free erase loops

Explicitly skip `std::atomic_ref` for this roadmap.

Avoid:

- `std::filesystem` in DMTCP runtime paths
- `std::jthread` and standard threading primitives in checkpoint/restart core
- coroutines and modules
- public C++20 types in external plugin ABI
- modernization inside mtcp restart code, checkpoint serialization, signal
  handlers, or wrapper hot paths unless the change is proven allocation-free and
  reentry-safe

Each modernization commit should be behavior-preserving, small, and backed by
relevant tests.

## Commit Sequencing

Recommended commit groups:

These groups are intentionally finer-grained than the roadmap phases above. A
phase may map to several bisectable commits, and a commit should not pull in
work from a later phase just because the phase-level narrative mentions it.

1. C++20 build baseline.
2. Minimal `dmtcp_command --json`.
3. New harness skeleton and diagnostics model without switching `make check`.
4. Old-suite parity classification in the new harness; switch `make check`
   only after the port-now set and planned shim-backed classes are covered;
   freeze old runner as `test/autotest_old.py`.
5. C++ unit-test framework and Makefile integration.
6. Synthetic coordinator worker tests.
7. Process bootstrap header tests.
8. ProcessInfo/bootstrap header redesign.
9. Core ThreadInfo bootstrap and diagnostic buffer ownership.
10. Fixed-buffer formatter and ASSERT/WARNING primitive.
11. Incremental ASSERT/WARNING call-site migration, as a multi-commit series
    split by subsystem and fragile-context risk.
12. Initialization architecture cleanup.
13. Selective C++20 source modernization.

If a later bug is found, fix it with a `fixup!` commit against the introducing
commit and autosquash before publishing the final series.

## Review-Driven Implementation Guardrails

Use these guardrails as implementation gates for the review risks that are easy
to miss when work moves from roadmap to code:

- Before switching `make check` to the new harness, keep
  `test/autotest-parity.md` authoritative. Every old-suite class should be
  classified as port-now, shim, or deferred, and shim support is the planned
  path for PTY/editor, MPI, and external-runtime classes.
- Route all harness use of `dmtcp_command --json` through one parser that
  rejects unknown `schema_version` values. Schema churn should be a one-file
  harness change plus focused JSON tests.
- Treat synthetic coordinator tests as model coverage, not sufficient end-to-
  end coverage. Every coordinator transition that becomes authoritative needs
  at least one real-worker assertion through `dmtcp_launch` and
  `dmtcp_command --json`.
- During the first `DmtcpCkptHeader` split, freeze vDSO/VVAR semantics. Any
  semantic change to those fields belongs in a separate commit with targeted
  restart tests.
- Do not trust restored TLS to provide `curThread` until restart code
  explicitly re-establishes the current thread context. ASSERT/WARNING must
  retain a fixed fallback buffer until that ordering is proven across restart
  paths and utility binaries.
- Prefer one shared thread diagnostic-buffer interface for `libdmtcp.so` and
  utilities. If a smaller utility context becomes necessary, keep the formatter
  entry point explicit so buffer semantics do not drift.
- Track `JASSERT`/`JWARNING`/`JTRACE` migration by subsystem and fragile-context
  risk. Do not migrate signal-handler, suspend, serialization, or TLS-restore
  call sites until the replacement path is audited for that context.
- Before migrating a subsystem, scan its existing diagnostics for floating-
  point arguments. Either defer those call sites or add separately tested
  formatter support before the migration commit.
- Keep Autotools source and generated-file changes together. If the local
  generator versions differ from the checked-in files, call that out in the
  commit notes and keep generated diffs mechanical.

## Open Validation Questions

- How much of the current old test suite is environment-sensitive and should be
  encoded as tags/requirements in the new harness?
- Which real-worker assertion covers each synthetic coordinator state-machine
  transition, and are any remaining synthetic cases still exploratory rather
  than authoritative?
- After the first `DmtcpCkptHeader` field-matrix audit, which initially kept
  fields can move out of the bootstrap record without changing restart
  orchestration?
- Should MANA be updated in the same branch as a bootstrap layout change, or is
  a loud format/signature failure enough for the first DMTCP-side series?
- Which ThreadInfo fields can be moved without changing restart ordering?
- Which JASSERT behaviors, such as backtrace and memory-map dumping, should be
  preserved by policy after the fixed-buffer ASSERT path lands?
- Which ASSERT/WARNING subset is legal in signal handlers and checkpoint
  suspend paths?
