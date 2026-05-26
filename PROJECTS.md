* Unit testing
* Coordinator UT
* Statemachine
* C++ 20
* Logging update
* JASSERT/JTRACE upate.
* string\_view
* Parallel make check
* Init dmtcp
* Ckpt-thread
* Pluginize other components.

## Wrapper consolidation follow-ups

These are known gaps observed during the single-DSO internal plugin
consolidation work. They were not folded into the consolidation commits because
they are broader than the reviewed change, appear to predate the branch, or
need focused tests before changing behavior.

* POSIX MQ notification lifecycle
  * `mq_notify` registration replacement and deregistration can leak wrapper
    state. Fixing this should be paired with a POSIX MQ test that covers
    registration, re-registration, `NULL` deregistration, and restart.
  * `SIGEV_THREAD` notification behavior depends on libc copying the
    `sigevent` before returning and invoking the wrapped callback trampoline.
    Add a runtime test before changing this path.
* Newer PID/syscall surface
  * Audit pidfd APIs, `clone3`, and other newer process syscalls for virtual
    PID/TID translation coverage. This is a compatibility expansion, not a
    regression from wrapper consolidation.
* Old inactive wrapper/source stubs
  * Revisit older tmpfile/dead-code stubs and remove or document them. Keep
    this separate from plugin folding so history stays bisectable.
* Test clean target
  * `make clean` can print an error from the pathtranslator test cleanup when
    its fixture directory is absent, although the top-level clean exits
    successfully. Fix the cleanup rule so clean output is actionable.
* Full-suite baseline
  * Full `make check` has known host-sensitive failures on this aarch64 test
    machine. Keep using focused smoke tests for wrapper changes, but maintain a
    separate baseline note for failures that reproduce on main.
