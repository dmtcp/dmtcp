# Optional native read-only file references (RFC)

Normal `dmtcp_launch` keeps the existing embedded image format. Explicitly enable
references when the restart environment already provides immutable backing files:

```sh
dmtcp_launch --new-coordinator --coord-port 7779 --externalize-readonly \
  --readonly-include '/opt/app/assets/*.bin' \
  --readonly-exclude '/opt/app/lib/private.so' \
  --ckptdir /snapshots --interval 0 ./application

# Trigger checkpoint with the usual coordinator address/port options:
dmtcp_command --coord-port 7779 --bcheckpoint
# Stop the original instance before restoring it:
dmtcp_command --coord-port 7779 --quit
dmtcp_restart --new-coordinator --coord-port 7780 /snapshots/ckpt_application_*.dmtcp
```

The checkpoint writer compares eligible mapped bytes with the backing file and
omits their payload directly. No Python conversion step or second full image is
needed. Default candidates have `.so` in their basename. Includes add absolute
files, directories or full-path globs; excludes override defaults and includes.
Both selectors are repeatable and require the explicit enable flag. Literal
directories include descendants at path boundaries. Globs support `*`, `?` and
byte bracket classes/ranges; `*` can cross directory separators, and backslash
is literal. Quote globs so the shell does not expand them. Matching uses recorded
paths without resolving symlinks.

All candidates must remain private, non-writable, non-anonymous regular-file
mappings with a complete backing-file span and byte-identical contents. Deleted
files and guard-page ranges are retained. Modified and writable regions remain
embedded even if explicitly included. The comparison uses fixed buffers without
allocating memory in the checkpoint write loop. Gzip output is supported.

Reference images use a distinct signature rejected by older readers. Their
region headers retain original paths and offsets. Restart requires the original
immutable files; missing references abort. File hashes are NOT recorded or
verified by this native prototype. Changed-file negative tests detect an
application invariant failure, not loader-side integrity enforcement. Trust /
verification policy, relocation, final format/versioning and malformed-input
coverage need agreement before upstream support. Default checkpoint creation is
unchanged; the feature remains opt-in.

## Tests

The standard upstream entry points build and run these tests:

```sh
make tests
make check-unit          # native selector tests in unit/dmtcp_unit_tests
make check-harness-unit  # readiness helpers and native CLI arguments
make check-autotest      # includes the 20-case reference-image round-trip suite
make check               # all of the above suites
```

For a focused repeated round-trip run after `make tests`:

```sh
python3 test/readonly-ref/validate.py --repeats 3
```

`--prefix /path/to/install` can select installed runtime tools; fixture and
probe binaries are built by the test Makefile. The normal integration suite
runs each round-trip case once. Its test-only binary image inspector is gated
to x86_64 hosts with 4 KiB pages; the CLI and native selector tests are independent
of that inspector restriction.

The round-trip suite uses native commands exclusively: private coordinator,
checkpoint, SIGKILL the source, new-process restart, and repeated memory-state
checks. It covers default embedded images, references, include/exclude,
non-library files, private modifications, writable/shared/anonymous/deleted/
incomplete mappings, all-excluded payloads, gzip, inspection and checkpointing
again after restart. Test processes clean up their own process groups. The
Python image inspector is test-only and never converts an image.

The matching test exercises the allocation-free selector shared with the writer;
CLI tests reject missing/relative/newline/oversized selectors and selectors
without the enable flag. The C++ selector tests use the existing TestCase/assertion framework; Python
CLI and helper tests use unittest and are registered as CommandTestSpec entries.
Validation is on Linux x86_64/glibc 2.28 with 4 KiB pages.

## Distribution use case

Checkpointed processes can repeat immutable library pages already available in
a container image or node cache. References reduce incremental checkpoint
distribution volume and bandwidth demand. Targets without the matching files
must still receive them. A downstream 27B model-serving experiment reduced its
process-image component from 11.893 GiB to 5.347 GiB (55.0%), with weights
unchanged. That workload observation is separate from these CPU regressions and
is not a network-throughput or cold-start-latency claim.

Review follow-up: the probe and dirty-page assertions use the runtime page size,
and the fixture is compiled with that size as its ELF-array alignment. Image
readiness waits for a complete parsed image, including the gzip trailer, before
inspection or source shutdown. Deterministic helper tests withhold the trailer
and cover 4 KiB / 64 KiB dirty-file offsets. Actual round trips were run on a
4 KiB-page host; no 64 KiB-kernel validation is claimed.
