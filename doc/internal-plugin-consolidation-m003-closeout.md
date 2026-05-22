# M003 internal plugin consolidation closeout scope

## M003 scope statement

M003 owns the implementation closeout for consolidating the six built-in internal plugin boundaries into the `libdmtcp.so`-centered design: timer, SysV IPC, IPC, PID, alloc, and DL. Its evidence boundary is implementation, source/build ownership, stale-DSO absence, focused runtime behavior, disable-mode behavior, and the final broad-suite attempt after the static and focused gates have passed.

M003 does not re-own the earlier design and planning requirements from M001/M002, and S07 does not create a new implementation promise. S07 only records the validation-scope boundary so M003 is judged against the requirements it actually owns or directly supports.

## Final evidence map

| Evidence class | M003 closeout meaning | Durable repository-local anchors |
|---|---|---|
| Consolidated internal DSO ownership | The old timer, SysV IPC, IPC, PID, alloc, and DL plugin DSO boundaries no longer own the built-in implementation; the implementation is checked through final-state gates rather than planning prose. | `src/Makefile.am`, `src/plugin/Makefile.am`, `src/dmtcp_launch.cpp`, `src/execwrappers.cpp`, `test/verify-timer-built-in-gate.py`, `test/verify-svipc-built-in-gate.py`, `test/verify-ipc-built-in-gate.py`, `test/verify-pid-built-in-gate.py`, `test/verify-alloc-built-in-gate.py`, `test/verify-dl-built-in-gate.py` |
| Descriptor and wrapper architecture | Descriptor-bearing built-ins stay in PluginManager-owned internal rows without extending the public plugin ABI; alloc and DL remain wrapper-only groups with no synthetic descriptors. | `src/pluginmanager.cpp`, `include/dmtcp.h`, `doc/internal-plugin-consolidation-registration-architecture.md`, `test/verify-consolidation-registration-plan.py`, `test/verify-alloc-built-in-gate.py`, `test/verify-dl-built-in-gate.py` |
| Disable and launch/exec/restart behavior | Focused checks cover disable-all behavior, alloc/DL disable flags and env disables, launch preload construction, exec reconstruction, and restart argument propagation where the implementation changed those boundaries. | `src/dmtcp_launch.cpp`, `src/execwrappers.cpp`, `src/util_exec.cpp`, `test/verify-alloc-built-in-gate.py`, `test/verify-dl-built-in-gate.py`, `test/verify-consolidation-migration-plan.py` |
| Static-initialization and duplicate ownership | Static gates cover duplicate internal initializers, duplicate wrapper/helper ownership, PID static-initialization hazards, and final-state stale-row drift after the alloc/DL changes. | `test/verify-pid-built-in-gate.py`, `test/verify-consolidation-preflight.py`, `test/verify-consolidation-registration-plan.py`, `test/verify-consolidation-migration-plan.py` |
| Focused runtime behavior | M003 directly supports behavior preservation through focused timer, SysV IPC, IPC, PID, alloc startup/disable, DL, launch, exec, user preload smoke, and external-plugin smoke evidence. | `test/autotest.py`, `test/plugin-init.cpp`, `test/dlopen1.c`, `test/dlopen2.cpp`, `test/verify-dl-built-in-gate.py`, `test/verify-alloc-built-in-gate.py` |
| Broad suite evidence | `make check` remains a standalone batch signal after focused gates, but a host-caveated or partial result stays non-green and is not merged into static/focused success wording. | `test/autotest.py`, `doc/internal-plugin-consolidation-migration-plan.md` |
| Scope drift prevention | This closeout and verifier keep requirement ownership explicit so M004, M005, deferred, and out-of-scope requirements are not treated as M003 validation failures. | `doc/internal-plugin-consolidation-m003-closeout.md`, `test/verify-consolidation-closeout-scope.py` |

## Removed DSO absence evidence

M003 stale-DSO absence evidence must name all six former built-in internal plugin DSOs in the final absence surface:

- `libdmtcp_timer.so`
- `libdmtcp_svipc.so`
- `libdmtcp_ipc.so`
- `libdmtcp_pid.so`
- `libdmtcp_alloc.so`
- `libdmtcp_dl.so`

The absence evidence is not satisfied by checking only the launcher, only one Makefile, or only the last migrated wrapper group. The closeout surface must keep build ownership, generated preload/replay ownership, artifact scans, and final-state verifier output separable so a future regression names which old DSO boundary reappeared.

## D019 broad-suite policy

`make check` is run standalone. A non-green result is not called green. D019 classification is allowed only after static closeout gates and focused runtime tests pass.

Under that policy, a documented broad-suite caveat can be carried as environment-sensitive evidence, but it cannot retroactively convert a non-green broad-suite transcript into a passing result. Static closeout verifiers and focused runtime tests should be recorded as separate command outputs before broad-suite classification is applied.

## Requirement scope table

| Requirement | M003 closeout classification | Owner / status | Evidence boundary |
|---|---|---|---|
| R001 | Already validated by M001 | Already validated by M001; not re-owned by S07 | M001 produced the accepted design plan. M003 may cite that design but does not revalidate the design-plan requirement. |
| R002 | Already validated by M001 | Already validated by M001; not re-owned by S07 | M001 analyzed constructor order, wrapper order, TSAN, `dlsym(RTLD_NEXT)`, and `dl_iterate_phdr`; M003 only preserves the handoff boundary. |
| R003 | Already validated by M002 | Already validated by M002; not re-owned by S07 | M002 decomposed the accepted design into staged implementation gates; M003 executes those gates rather than re-owning the decomposition artifact. |
| R004 | M003 covered | M003-owned implementation closeout | Covered by final ownership of timer, SysV IPC, IPC, PID, alloc, and DL inside the `libdmtcp.so`-centered implementation with old internal DSOs absent. |
| R005 | M003 directly supported | M003 plus M004 support boundary | Directly supported by focused checkpoint/restart and behavior gates plus standalone broad-suite evidence; sanitizer or loader-stress portions remain M004-owned where applicable. |
| R006 | M003 covered | M003-owned implementation closeout | Covered by central PluginManager descriptor ownership, wrapper-only alloc/DL behavior, duplicate-initializer prevention, and duplicate-wrapper/helper audits. |
| R007 | M003 directly supported | M003 implementation evidence with M004 sanitizer handoff | Directly supported by static-initialization review gates, including PID static-initialization hazard checks; sanitizer-specific startup stress remains outside M003. |
| R008 | M003 covered | M003-owned implementation closeout | Covered by engine-visible disable behavior for disable-all plus alloc and DL disable flags/env disables after those groups moved into `libdmtcp.so`. |
| R009 | Support-only smoke evidence | M005 owns full optional/external plugin evaluation | M003 provides ordinary user `LD_PRELOAD` and external-plugin smoke evidence only; complete optional/external plugin evaluation remains M005-owned. |
| R010 | M005-owned | M005-owned external plugin maintainer recipe | M003 does not deliver the external plugin update or compatibility recipe; the recipe remains an M005 deliverable. |
| R011 | M004-owned | M004-owned sanitizer and wrapper-order stress | M003 does not deliver sanitizer, deep `dlsym(RTLD_NEXT)`, `dl_iterate_phdr`, default-version lookup, or wrapper-stress runtime proof. |
| R012 | Deferred | Deferred TSAN checkpoint/restart guarantee | No TSAN-instrumented checkpoint/restart guarantee is made by M003; future evidence is required before validation. |
| R013 | Deferred | Deferred runtime-overhead threshold | M003 does not define or enforce a concrete runtime-overhead threshold; that remains deferred until a threshold is selected. |
| R014 | Support-only smoke evidence | M005 owns full optional/external plugin compatibility | M003 smoke evidence supports public ABI preservation, but full optional/external plugin compatibility and wrapper-autonomy validation remain M005-owned. |
| R015 | Out of scope | Out of scope | The coordinator-worker protocol unit-test harness and `test/autotest.py` refactor are not part of the internal-plugin consolidation closeout. |

## Explicit non-proofs and handoffs

M003 does not prove sanitizer, wrapper-stress, deep `dlsym(RTLD_NEXT)`, `dl_iterate_phdr`, default-version lookup, or full optional/external plugin compatibility. Those concerns remain mapped to M004 and M005 as follows:

- M004 owns sanitizer, TSAN, deep `dlsym(RTLD_NEXT)`, `dl_iterate_phdr`, default-version lookup, and wrapper-order stress evidence after the implementation exists.
- M005 owns optional/external plugin compatibility, wrapper autonomy outside the centralized internal wrapper set, and maintainer-facing update or compatibility recipes.
- Deferred requirements R012 and R013 require future owner decisions before they can be treated as validation gates.

## Verifier contract

Run the closeout verifier in two separate modes:

```sh
python3 test/verify-consolidation-closeout-scope.py self-test
python3 test/verify-consolidation-closeout-scope.py full
```

The verifier reads only explicit repository-relative doc/source/test paths, rejects hidden planning paths, validates the requirement-scope table above, requires every removed DSO in the absence section, and rejects wording that turns M004/M005-owned concerns into M003 proof claims.
