# V17-M34 — Integrated protected worker FIFO acceptance

**Milestone:** Independently complete M34 peer; original V17-M30 and
V17-M22 remain OPEN.
**Substantive CI:** [Actions #39187712935](https://github.com/logiclrd/Mesher/actions/runs/39187712935),
full GCC/Clang/MSVC warnings-as-errors, all 23 CPU suites and
applicable benchmark checks **PASSED**.
**Source:** `tests/async_sculpt_tests.cpp`,
`src/async_sculpt.cpp`, PR #79.

## Objectives and exact protected boundary

Run M31 FIRST-original caller scopes, M32 full actual
Inflate/refine/collapse/optional d2 pre-CAS protected policy, and
M33 explicit queued c1/c2/c3/M03/d2 before-CAS protected policy
together on real asynchronous work, not as independent private
certificates. Keep one immutable FIRST-original root's FaceIds,
seven witnesses and local edge scales; preserve c3/M03-certified
Buried/Outside owners, no independent coincident owner substitution,
and preserve the original global reflected ID involution, including
X=0 self-mirror singleton semantics. Refuse before root CAS, history,
controller mirrors and renderer/picker dirty publication on stale,
foreign, insufficient work or cumulative erosion.

## Real mixed-API positive and refusal

The integration fixture has two really intersecting closed tetrahedra
and one separate, intact closed octahedron. A deliberately insufficient
`maxNetChangedIds=1` exact command and then a fully supported exact
union are queued back-to-back with the SAME initial root and
original tetra FaceId intention. The former refuses, the latter
commits ONE full certified c1/c2/c3/M03/d2 final revision. Publishing
a local physical stamp BEFORE the original crossing has been joined
correctly fails closed because the unrelated pre-existing source
intersection is not automatically clean. Thus the integration
correctly starts with the intended queued join, not an artificially
cleaned input.

An independently prepared pair of genuine physical Inflate/refine/
collapse private stages from the *certified post-union source
lifetime* measures the protected octa faces' first-original
seven-per-living-child deviation in each source's own edge units.
Its first movement and its cumulative second movement bracket a
finite threshold. The REAL worker first accepts unscoped octa
movement while explicitly continuing to name the original
tetra source ownership previously intentionally cut. It then
refuses an identical second octa move because the cumulative
FIRST-original protected maximum would be exceeded. No fabricated
history successor, dirty partition or interim renderer root is
published. An exact request referencing the earlier root refuses
stale, and a subsequent deliberate original FaceId physical stroke
succeeds after that refusal.

Across all accepted steps, the latest UI mailbox snapshot must
match the actual terminal root and dirty render owners; old
COW pointer/BVH readers remain unchanged. Three alternating
exact/physical roots survive exact independent undo and redo,
and the final checkpoint retains only finished geometry plus
the protected-original provenance-required marker. A V13 binary
read alone cannot authenticate a forgotten FIRST-original source.

## Mirrored X=0 and owner independence

The existing genuine complete three-contact X=0 original-revision
symmetric source-exact join is first accepted as an M33 queued
command, after rejecting a partial contact set and a half-reflected
original caller FaceId orbit. M34 immediately follows that terminal
with an authentic symmetric 4D physical Begin/End on the SAME
protected worker. Its source-lifetime predecessor, terminal
reflected/self-mirrored vertices, dirty publication and mixed
undo/redo COW pointer identities remain exact. Existing M11
sixteen genuinely accepted physical exact mirrored joins remain
positive. The CPU source/repair suites continue to reject
coplanar coincident independent shells as bogus source intervals
and retain all original FaceId/cavity ownership distinctions.
No XYZ-nearest or X=0 special-only source owner fallback is allowed.

## Discovered real reservation / renderer race

The previous `AsyncSculptWorker::run` checked only
`!stroke_.active() && reserved_.load()` before clearing the
producer reservation. An unrelated queued Merge/Undo/Checkpoint
iteration could observe the producer's NEW atomic reservation
before that Begin was consumed, publish an OLD root and drop the
reservation. The next stamp could privately publish newer history
but fail to force a new UI snapshot, especially when 8 ms mailbox
coalescing suppressed its Begin publication. Integration
diagnostics confirmed old renderer `completedStamps=2` alongside
new `history.undoDepth=3`. This is a genuine race, not a
permitted renderer lag after the stroke reports drained.

M34 introduces a worker-owned `workerOwnsStartedStroke` token,
set ONLY after `stroke_.startTimed` accepts that specific
queued Begin. Only after the actual End, no active or pending
samples and a forced final terminal snapshot does the worker
clear the token and producer's `reserved_`. A new Begin
waiting in the command FIFO cannot be prematurely retired by
an earlier idle command. The strict terminal snapshot/dirty
integration assertion now passes across compilers.

## What remains OPEN

The ORIGINAL M30 acceptance checkbox and ORIGINAL M22 joint
M28–M30 acceptance still require their planned independent final
review; M23 and original M12 remain separately open.
M14 fault-injected exceptional post-CAS atomicity and M15
desktop UI/explicit Merge Geometry default-disabled opt-in
are NOT part of M34; M13, V18/V19 also remain separate.
This remains bounded sampled source fidelity, not continuous
Hausdorff, universal imported-mesh Boolean, or general CCD.
