# Coordinator Synthetic/Real-Worker Coverage Ledger

This ledger keeps `coordinator_synthetic.py` honest. Synthetic workers exercise
the production coordinator socket protocol, but they are still model coverage:
a transition is authoritative only after at least one real-worker test drives
the same coordinator behavior through `dmtcp_launch` and `dmtcp_command --json`.

## Rules

- Keep this file in sync when adding synthetic coordinator tests.
- Mark a transition **real-worker-backed** only when the named integration test
  asserts the same coordinator state or command behavior.
- Leave transitions **model-only** when no real worker currently covers them.
  Model-only tests are useful, but they must not be used as the sole evidence
  for coordinator refactors.

## Current Coverage

| Coordinator behavior | Synthetic coverage | Real-worker coverage | Status |
| --- | --- | --- | --- |
| Coordinator option parsing rejects invalid values | `test_invalid_coord_port_option_exits_with_usage`, `test_invalid_coord_port_env_exits_with_error`, `test_invalid_timeout_option_exits_with_usage`, `test_invalid_stale_timeout_option_exits_with_usage`, `test_invalid_interval_option_exits_with_usage` | CLI-only coordinator validation; no worker required | Coordinator-only hardening |
| Coordinator writes status file at startup | `test_status_file_is_written_at_startup` | coordinator startup through harness tests uses the port file; no real worker state transition | Coordinator-only hardening |
| First worker join updates status | `test_single_synthetic_worker_join_updates_status` | `dmtcp1`, `dmtcp_command --json --status` in the harness | Real-worker-backed |
| Multiple workers join same computation | `test_two_synthetic_workers_join_same_computation` | multi-peer harness specs such as `dmtcp5`, `sched_test`, `shared-fd1` | Real-worker-backed |
| Replacement worker after disconnect | `test_replacement_worker_can_join_after_peer_disconnects` | none yet | Model-only |
| Reject wrong or stale computation group | `test_new_worker_with_existing_computation_group_is_rejected`, `test_restart_worker_with_wrong_computation_group_is_rejected` | none yet | Model-only |
| Reject restart worker while running/checkpointing | `test_restart_worker_is_rejected_while_computation_is_running`, `test_restart_worker_is_rejected_while_checkpoint_is_active` | none yet | Model-only |
| Normal two-worker barrier release | `test_two_synthetic_workers_release_same_barrier` | `coordinator-barrier` drives a two-peer real-worker checkpoint/restart cycle through `dmtcp_launch` and `dmtcp_command --json --checkpoint` | Real-worker-backed |
| Barrier disconnect, desynchronization, and duplicate barrier requests | `test_barrier_waiter_releases_when_peer_disconnects`, `test_mismatched_barrier_disconnects_offending_worker`, `test_duplicate_barrier_from_same_worker_does_not_release` | no explicit real-worker assertion yet | Model-only |
| Restart quorum and peer mismatch | `test_restarting_workers_release_barrier_only_after_restart_quorum`, `test_restart_worker_with_peer_count_mismatch_is_rejected` | multi-peer restart specs exercise quorum success; mismatch rejection is not covered | Partially real-worker-backed |
| Checkpoint command reaches workers | `test_checkpoint_command_reaches_synthetic_worker` | default harness checkpoint cycle for every `TestSpec`; `command-json-bcheckpoint` covers the blocking JSON checkpoint path | Real-worker-backed |
| Duplicate, concurrent, or slow checkpoint requests | `test_checkpoint_command_rejects_second_request_while_active`, `test_worker_update_during_checkpoint_gets_duplicate_request`, `test_timeout_exits_during_slow_checkpoint` | none yet | Model-only |
| Kill and quit commands | `test_kill_command_reaches_synthetic_worker`, `test_kill_command_during_checkpoint_reaches_worker`, `test_quit_command_kills_workers_and_stops_coordinator` | `command-json-kill` asserts `dmtcp_command --json --kill` against a live `dmtcp_launch` worker; `command-json-quit` asserts `dmtcp_command --json --quit` stops a live worker and coordinator | Real-worker-backed |
| New worker during checkpoint/restart | `test_new_worker_during_checkpoint_receives_checkpoint_request`, `test_new_worker_is_rejected_while_restart_is_active` | fork/exec-style tests cover process arrival during normal execution, not these coordinator edge states | Model-only |
| Exit-on-last | `test_exit_on_last_stops_coordinator_after_worker_disconnect` | `coordinator-exit-on-last` runs a live `dmtcp_launch` worker under a coordinator started with `--exit-on-last`, then asserts that `dmtcp_command --json --kill` lets the worker and coordinator exit | Real-worker-backed |
| KVDB request/response | `test_kvdb_request_round_trip` | plugin flows may use KVDB indirectly; no direct real-worker assertion yet | Model-only |
| Invalid protocol, oversized extra bytes, partial messages, half-open sockets | `test_invalid_magic_worker_is_rejected`, `test_oversized_extra_bytes_worker_is_rejected`, `test_invalid_message_size_worker_is_rejected`, `test_partial_worker_message_does_not_start_computation`, `test_unexpected_worker_message_disconnects_worker`, `test_idle_half_open_connection_does_not_block_status` | none expected from normal real workers | Synthetic-only protocol hardening |

## Next Real-Worker Additions

- Add a small real-worker barrier plugin or harness hook before treating
  barrier generation tests as authoritative coordinator coverage.
- Add a restart peer-mismatch integration test if coordinator restart rejection
  behavior is refactored.
