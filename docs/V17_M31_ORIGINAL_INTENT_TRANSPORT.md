# V17-M31 — Caller-verified first-original intent transport

**Accepted:** PR #76; substantive GCC/Clang/MSVC [Actions #39179461146](https://github.com/logiclrd/Mesher/actions/runs/39179461146), all 23 CPU tests and applicable benchmarks.

**Flat peer of V17-M30, which remains open.** The implementation plan was
recorded in `todo.md` before changing code. M32 physical pre-CAS,
M33 explicit union pre-CAS, M34 integrated worker acceptance, the existing
original M30 goal, and the original M22 integration are still open.

## Independent boundary

The actual 4D worker previously conveyed only physical areas in Begin and
only expected root, original contact pairs and work limits in Merge. No
first-original FaceId nomination could traverse the worker queue. Inferring
intent from brush XYZ, candidate displacement, reflected proximity or an
unrelated coincident surface would violate M21's source-ownership proof.

M31 adds explicit `std::vector<FaceId>` caller choices to each Begin
command and (separately) each explicit union command. The worker transports
those values in FIFO order, moves them to the brush at Begin, and never
derives them from post-Inflate/Boolean geometry. A subsequent Begin replaces
the prior stroke's scope. Merge owns its own scope, not the last stroke's.

Fresh physicalExact construction captures every permanent first-original
FaceId and seven original samples once using M19/M27, under a finite input
witness cap. It retains a separate original mirrored VertexId map; current
post-remesh mirror maps can retire, add, or change live identities without
rewriting the first-root authority. An authenticated trusted in-memory
handoff may retain original mirror pairing separately from current pairs.
Neither terminal-only V12/V13 disk reload nor an unverified checkpoint
can create a new first-root certificate.

A nonempty caller scope is checked by M21's opaque original intent
declaration against that immutable first-root authority, original
reflected face closure (including X=0 singletons), duplicate/alien ID
refusal and a finite source-count cap. A bad physical Begin produces a
recoverable rejected stamp, not a worker-killing exception. A bad queued
Merge refuses before preparing a cut; a later legitimate FIFO command
remains eligible. Empty source selection means no **declared** sculpt
exemption, not permission to invent intended faces later.

## Invariants and current limit

**M31 validates source *inputs*, not terminal geometry.** Existing worker
publication still does not attach M28/M29 next-lifetime proof or call the
complete M21/M27 first-original descendant audit immediately before CAS.
Do not interpret a valid M31 nomination or a normal Step's existing
history entry as a completed physicalExact fidelity guarantee. M32/M33
must re-bind to the **direct prior published** opaque source lifetime on
every candidate and apply the complete original-edge threshold/work gate
before final CAS. M34 verifies joint asynchronous acceptance. M14 remains
responsible for injected post-CAS exceptional-failure atomicity.

Legacy callers that send no explicit intent keep their existing behavior
until M32/M33 activate the complete required policy. This intermediate
peer is independently covered by source-input/queue/rejection tests; it
must not be mistaken for original M30 completion.

The desktop Merge geometry option is still disabled/default off.
PhysicalExact and approximate V16 enabled remain mutually exclusive.
Unauthenticated protected V13 disk recovery still refuses.
