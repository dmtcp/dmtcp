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

## Current New-Harness Coverage

The new harness currently covers the direct checkpoint/restart tests listed by:

```sh
./test/autotest.py --list
```

That includes the main direct runtime, file, fd, process, pthread, mutex, timer,
event, SysV IPC, POSIX MQ, plugin-init, shell, restart-directory, and selected
language/runtime smoke tests.

## Ported With Explicit Limits

| Test | Status | Rationale |
| --- | --- | --- |
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
