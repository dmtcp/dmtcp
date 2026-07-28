# Checkpoint-Time Socket Discovery

## Status

Approved architecture for implementation stacked on the thread and TLS
refactoring work.

## Motivation

The socket plugin currently learns about sockets by wrapping lifecycle calls
such as `socket()`, `bind()`, `listen()`, `connect()`, `accept()`, and
`socketpair()`.  It maintains a parallel socket state machine throughout
normal execution.

This model is difficult to maintain and can become stale when socket activity
bypasses a wrapper.  Examples include libc-internal calls, direct system calls,
inherited descriptors, and descriptors received from another process.

DMTCP needs socket state only while preparing a checkpoint and restart.
Therefore, the kernel should be the source of truth after application threads
have been quiesced.

## Goals

- Discover and classify every open socket descriptor held by the current
  process at checkpoint time.
- Remove runtime tracking from socket lifecycle operations.
- Query expensive socket state only once for a socket shared by multiple
  DMTCP processes.
- Preserve checkpoint, resume, and restart behavior for the supported socket
  set.
- Restore the effective listen backlog.
- Match peers that belong to the same DMTCP computation when their observed
  endpoint tuples agree.
- Warn about connections that cannot be restored; replace them with dead
  sockets at restart time only.

## Non-Goals

- Exhaustive support for every Linux socket family or protocol.
- Preservation of every protocol-specific or write-only socket option.
- Transparent restoration of connections to external processes.
- Replacing the existing concepts of queue draining, refill, connection
  rewiring, or shared-descriptor redistribution.
- Preservation of file descriptors carried in in-flight `SCM_RIGHTS`
  messages.
- Peer matching across NAT, transparent proxies, or service meshes that cause
  the two endpoints to observe different address tuples.

## Supported Socket Set

The initial design covers:

- IPv4 TCP stream sockets
- IPv6 TCP stream sockets
- UNIX stream sockets
- UNIX sequenced-packet sockets
- Raw Netlink sockets
- Duplicated descriptors
- Descriptors shared by multiple DMTCP processes

Datagram, packet, SCTP, and other protocol-specific sockets are outside the
initial scope.

## Architecture

The socket plugin constructs a fresh, transient socket snapshot for every
checkpoint.  It does not retain a live socket state machine between
checkpoints.

Within this design, a logical socket is one kernel socket object, an endpoint
is one side of a connected socket, and an alias is a descriptor that refers to
that logical socket.

Application threads must be suspended before discovery begins.  This
stabilizes the process descriptor table while DMTCP inspects it.  Remote peers
and the kernel may still advance protocol state, so discovery treats
in-progress connections separately.

Discovery uses the following sources in priority order:

1. `/proc/self/fd` enumerates descriptors and identifies socket inodes.
2. `getsockopt()`, `getsockname()`, and `getpeername()` provide information
   available directly from each descriptor.
3. `/proc/self/net/*` or `SOCK_DIAG` supplies information not available
   directly from the descriptor.  `SOCK_DIAG` is required for facts such as
   the effective listen backlog or an anonymous UNIX peer inode.

Linux documents `/proc/net/tcp` as deprecated in favor of socket diagnostics.
The design therefore avoids depending exclusively on procfs network-table
formats:
<https://docs.kernel.org/networking/proc_net_tcp.html>.

## Discovery And Ownership

Discovery is divided into a cheap ownership phase and a leader-only inspection
phase.  DMTCP excludes its protected descriptors and any transient descriptors
opened by discovery itself.

1. Each process enumerates `/proc/self/fd` and groups descriptors that refer to
   the same socket inode.  It records per-descriptor flags such as
   `FD_CLOEXEC`.
2. Each process participates once per logical socket in a checkpoint-scoped
   coordinator election.
3. Only the elected checkpoint leader performs detailed socket queries,
   resolves in-progress connections, and derives a checkpoint-scoped endpoint
   identity from the DMTCP host identity and socket inode.
4. Followers retain their local descriptor aliases and a reference to the
   leader-owned logical socket.
5. The leader publishes its endpoint identity and matching data.
6. All processes enter the existing global publication barrier.
7. Leaders match peers and classify unmatched sockets.
8. Processes enter the existing local post-matching barrier.
9. Leaders drain supported connection queues.  Followers do not inspect or
   drain the shared socket.
10. Each process serializes its aliases and its reference to the leader-owned
   endpoint.

The election uses an atomic coordinator key-value operation named
`GET_OR_SET`.  This is a required extension to the coordinator API, not the
existing non-atomic `SET` operation.  Its key identifies the checkpoint
generation, host, and socket inode.  Its value identifies the candidate
process using DMTCP's unique process identity.  Linux allocates socket inodes
from a host-wide socket filesystem, so host identity and socket inode identify
the shared kernel object even when processes use different network
namespaces.

- The first caller stores its value and receives no previous value.  It is the
  checkpoint leader.
- Later callers receive the previously stored value.  They are followers.

The coordinator must clear the socket discovery namespace before a checkpoint
generation begins; this is another required coordinator change.  The election
itself requires no barrier and does not modify `F_SETOWN` or any other socket
state.  "No barrier" applies only to election; the existing global publication
barrier remains required before leaders query peer records.  A follower may
reach that barrier before its leader; it waits there until every process,
including all leaders, has completed publication.

## Socket Snapshot

The leader records only restart-relevant state in its checkpoint image:

- Domain, type, and protocol
- Kernel-observed lifecycle state
- Local and peer endpoints
- Effective listen backlog
- Shared open-file status and ownership flags from `F_GETFL`, `F_GETOWN`, and
  `F_GETSIG`
- Per-descriptor flags from `F_GETFD`
- A curated set of readable socket options
- Local descriptor aliases
- Cross-process sharing information
- Matched peer identity, when the peer is internal
- Restart disposition: recreate, rewire, or replace with a dead socket

Only the endpoint identity and matching data are published to the coordinator.
Followers record their local descriptor aliases and the identity needed to
receive the leader-restored descriptor.  At restart, the leader alone
recreates or rewires the logical socket and sends it through the existing
shared-descriptor redistribution path.  Followers receive that descriptor and
rebuild their local aliases.

The snapshot is serialized into the checkpoint image.  It is discarded after
checkpoint processing; the next checkpoint rebuilds it from current kernel
state.

## State Classification

The snapshot classifies supported sockets from observed kernel state rather
than from call history:

- Created but unbound
- Bound but not listening
- Listening
- Connected to an internal peer
- Connected to an unmatched or external peer
- Connection still in progress
- Failed or disconnected

The endpoint identity is unique within the checkpoint generation because
socket inodes are host-wide and the DMTCP host identity distinguishes hosts.
Both peers observe the same published pair after the barrier.  The original
distinction between the endpoint that called `connect()` and the endpoint
returned by `accept()` is not required.  After peer matching, DMTCP orders the
two endpoint identities.  The smaller identity takes the incoming restore role
and the larger identity takes the outgoing restore role.  These are
DMTCP-internal rewire roles; they do not claim to reconstruct the
application's original call history.

## Peer Matching

Leaders publish endpoint identities through a checkpoint-only coordinator
key-value namespace.  A global barrier ensures that all endpoint records are
available before queries begin.  This matching subsumes both the current
coordinator tuple exchange and the later in-band stream handshake used to
exchange connection identifiers.

- IPv4 and IPv6 TCP endpoints are identified by their directional local and
  peer address tuples.  A peer lookup uses the reversed tuple.
- UNIX endpoints use host-qualified socket and peer inode information.
- Anonymous UNIX sockets and socket pairs use peer inode information from
  socket diagnostics because names alone are insufficient.
- UNIX sequenced-packet sockets use the same inode matching as UNIX stream
  sockets and retain their separate sequenced-packet restore transport.
- Raw Netlink sockets do not participate in stream peer matching.

If a matching endpoint is present in the same computation, both records refer
to a shared restore identity.  Otherwise, the socket is classified as
unmatched.  Address rewriting that prevents reversed tuples from matching is
therefore classified as an external connection.  Restart-time publication of
temporary rewire addresses uses a separate namespace and occurs after peer
matching.

## Listen Backlog

Listening sockets restore the effective backlog reported by the kernel rather
than the original argument supplied to `listen()`.  This reflects limits such
as `somaxconn` that may have clamped the requested value.

`SOCK_DIAG` exposes the effective listen backlog for supported INET and UNIX
sockets:
<https://man7.org/linux/man-pages/man7/sock_diag.7.html>.

The backlog limit is restored, but connections still in the SYN queue or the
accept queue and not represented by an application descriptor are not
checkpointed.  A connected internal peer with no matching accepted descriptor
is classified as unmatched and receives the ordinary dead-socket treatment.

## Socket Options

Checkpoint discovery reads a curated set of common options required by the
supported socket set.  The set includes ordinary socket, TCP, IPv4, IPv6, UNIX,
and Netlink settings that have reliable query and restore semantics.  Read-only
options are not serialized.  Writable options are restored in three groups:
before bind, after bind but before connect or listen, and after connect,
listen, or rewire.

The design does not promise exhaustive option coverage.  In particular:

- Command-style operations such as multicast membership changes are not
  generally enumerable.
- File-descriptor-backed settings such as attached eBPF programs cannot be
  reconstructed from ordinary option queries.
- Security key material is not generally readable.
- Some getters expose an effective kernel value rather than the application's
  original input.  Receive and send buffer values are normalized so repeated
  checkpoints do not repeatedly apply the kernel's buffer-size adjustment.

Failure to read an optional setting produces a checkpoint warning and omits
that setting.  Essential socket identity or state that cannot be read makes the
socket unrestorable.  `F_GETOWN` process and process-group values are saved in
virtual form and translated explicitly by the socket plugin through the PID
plugin's public translation helpers when restored.

## Connections In Progress

The elected leader waits briefly for any connection that the kernel reports as
still in progress, regardless of whether the application originally requested
blocking or nonblocking behavior.

`DMTCP_SOCKET_CONNECT_WAIT_MS` controls the maximum wait in milliseconds.  The
default is `100`.  An invalid value produces a warning and uses the default.

This intentionally replaces the current 60-second drain-time wait with a
bounded default.  Discovery waits for write readiness and reads `SO_ERROR`.
A connected peer or a terminal socket error ends the wait.  If the connection
remains in progress when the interval expires, checkpoint warns and records
dead-socket restoration.  Peer matching completes before queue draining.

## Checkpoint, Resume, And Restart

At checkpoint:

- Unsupported, unqueryable, unresolved, and unmatched sockets produce
  warnings.
- The original process sockets remain intact.
- Queue draining and refill preserve data for supported internal connections.
- Only the elected leader drains shared state.

On resume:

- Original sockets continue to be used.
- A socket marked unrestorable is not replaced, allowing the application to
  close or otherwise resolve it before a later checkpoint.

On restart:

- Unbound, bound, and listening sockets are recreated from the snapshot.
- Listening sockets use their effective saved backlog.
- Matched internal connections are rewired.
- Descriptor aliases and cross-process sharing are restored.
- Socket data and supported options are refilled.
- Unrestorable sockets are replaced with dead sockets and produce another
  warning.
- DMTCP never attempts to reconnect directly to an unmatched external peer.
- Raw Netlink sockets restore their local address, readable memberships, and
  supported options; they do not participate in stream rewiring or queue
  draining.  Memberships are queried with `NETLINK_LIST_MEMBERSHIPS`.

Restart preserves the existing rewire ordering: leaders create temporary
restore listeners and publish their addresses in the restart-only namespace;
all processes enter the global restart publication barrier; outgoing endpoints
query and reconnect; local barriers separate reconnection, refill, and resume.

## Failure Policy

The checkpoint continues when an individual socket cannot be restored.  DMTCP
must report enough information at checkpoint time for the user to identify the
descriptor and reason.  A socket preclassified as unrestorable is not an
assertion failure during restart.

The same socket produces a restart warning when it is replaced with a dead
socket.  A discovery infrastructure failure that prevents any process from
enumerating its descriptor table aborts the computation-wide checkpoint.
Failure of an optional procfs or diagnostic query, including access
restrictions, falls back to the remaining sources.  If an essential fact such
as listener backlog remains unavailable, the socket is marked unrestorable.

Dead-socket replacement never occurs during checkpoint or resume.  A
disconnect discovered while draining changes only the saved restart
disposition; the original descriptor remains in the resumed process.

Temporary drain-time changes such as clearing `O_ASYNC` are allowed.  They must
be undone during refill before application threads resume.

Checkpoint discovery runs only after DMTCP has completed any active vfork
lifecycle.  It therefore replaces the socket plugin's vfork-time connection
list cloning and its special scan for pre-existing launcher sockets.

When restart moves a process to a host where a saved local address, interface,
or scope is unavailable, the socket follows the same warning and
unrestorable-socket policy.

## Current And Proposed Architectures

| Concern | Wrapper-tracked state | Checkpoint-time discovery |
|---|---|---|
| Runtime cost | Plugin bookkeeping on socket operations | No lifecycle bookkeeping during normal execution |
| Source of truth | Reconstructed call history | Quiesced kernel state |
| Bypassed operations | Can leave stale or missing records | Open sockets visible through `/proc/self/fd` are discovered |
| State complexity | Continuous transition tracking | One classification per checkpoint |
| Descriptor lifecycle | Close, duplicate, exec, and vfork bookkeeping | Descriptor aliases rebuilt from the current table |
| Shared sockets | Election temporarily changes socket ownership | Atomic coordinator `GET_OR_SET` |
| Socket options | Setter arguments are available, but pointer and descriptor values remain unreliable | Curated queryable settings |
| Listen backlog | Requested `listen()` argument | Effective kernel backlog |
| Checkpoint cost | Metadata mostly precomputed | Leader performs kernel queries |
| Platform coupling | Depends on interposition | Depends on Linux procfs and selected diagnostics |
| Failure visibility | Stale records may fail late | Unsupported state is classified and warned about at checkpoint |

The proposed architecture moves work from every socket operation to
checkpoint time.  Only one process inspects a shared socket, limiting the
additional checkpoint cost.

## Validation Requirements

Validation must cover:

- Atomic `GET_OR_SET` behavior under concurrent coordinator clients
- IPv4 and IPv6 TCP connections
- Pathname, abstract, anonymous, and socketpair UNIX sockets
- UNIX sequenced-packet sockets
- Unbound, bound, and listening sockets
- Effective listen backlog restoration
- Descriptor duplication and cross-process sharing
- Leader-only drain and shared-descriptor redistribution
- Per-descriptor and shared open-file flags
- Sockets created through paths that bypass ordinary libc wrappers
- Sockets inherited from launchers
- Protected DMTCP descriptors being excluded from discovery
- Curated socket-option restoration
- Repeated checkpoint and resume cycles rebuilding fresh snapshots
- In-progress connection timeout and environment override
- External, unsupported, and unqueryable sockets remaining intact on resume
- Dead-socket replacement and warnings on restart
- Procfs and socket-diagnostic fallback and failure paths
- IPv6 restore behavior on hosts with IPv6 enabled
