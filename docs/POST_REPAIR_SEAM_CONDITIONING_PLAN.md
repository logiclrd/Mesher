# Post-repair seam conditioning — next independent development arc

Status: **planning / draft**. Baseline is the completed R01–R07 integration
merged by [PR #162](https://github.com/logiclrd/Mesher/pull/162) at
`9c8d1effac9015c8422671d1b0c90a2187a1aaa0`.
All geometry implementation in this branch must be reviewed separately
from that stable baseline.

## Why this work is separate

The October 2026 Release acceptance exercise established that default
sphere/torus/hollow configurations can undergo repeated mirrored and
non-mirrored self-contacts, certified zippers, advancing-front caps, inner
island removal, and continued editing without visible misclassification.
The existing pipeline sometimes **refuses** attempted stamps rather than
publishing uncertified topology; keeping that behavior fail-closed is an
essential invariant, not a reason to reopen the completed integration.

The open issue is **avoidably irregular geometry near fresh repair seams**:
a zipper can produce tightly compressed/elongated triangles adjacent to the
surviving mesh. Some later ordinary Inflate stamps are accepted and the
seam becomes visually uniform; others encounter a geometric/quality refusal.
An October 28 screenshot progression shows stamps 151 -> 153 -> 155,
cumulative certified repairs 4 -> 5 -> 6, vertices 6207 -> 6161 -> 6148,
and zero refusals. **Do not treat that sequence as proof that the ordinary
refiner alone smoothed a fixed zipper**; two more private repairs occurred
between those screenshots.

The ordinary `VersionedLocalRefiner` currently considers edges that
**grew** and exceeded its maximum length within the stamp. Optional flips
are selected after successful splits; the directional collapser considers
edges that **shrank** below a minimum ratio within the stamp. Neither
provides a general, dedicated post-repair seam-quality pass.

## Known refusal classes (previous 2467-stamp long run)

69 successful repairs, 26 refused stamps grouped in 10 unchanged-root
episodes:
- M15A effective edge would exceed two incident faces: 10 stamps.
- M22B Poisson-settled advancing-front self-contact: 8 stamps.
- M22B no admissible mirrored-front operation: 2 stamps.
- M17B zipper split would worsen child quality: 3 stamps.
- M04C ambiguous burial/material certificate: 2 stamps.
- M04B cap under minimum quality after bounded refinement: 1 stamp.

The two most frequent classes now have **diagnostic witnesses only**:
M15A logs occupied edge, attempted face, source faces and staged ordinals;
M22B logs the intersecting settled triangle pair, indices and coordinates.
Their checks have *not* been relaxed.

## First investigation: exact one-stamp post-zipper trace

Use the F8 diagnostic operation on the R07 baseline, which executes one
worker stamp with no timed 4D input segments, one attempted source
transaction and a displayed-snapshot gate before the next request.
Mirrored F8 still contains the whole reflected pair as **one** stamp.
Record before/after immutable revision identity, stamp counter, repaired
and rejected counters, brush point/radius, exact M03 plan, successful
cap/zipper verdict, and any M15A/M22B geometry witnesses.

Classify the FIRST bad step as one of:
1. Initial certified zipper is valid but has suboptimal local triangle shape.
2. Next Inflate creates an independently poor triangle or new contact.
3. Post-stamp refiner/collapser misses a legal improvement.
4. Another valid AUTO repair is required (not merely mesh smoothing).
5. A true fail-closed ambiguity or budget exhaustion that must remain a refusal.

Avoid diagnosing by comparing frames whose repaired counter differs.

## Candidate design, not yet approved or implemented

A bounded, source-provenance-aware **post-closure conditioning** pass,
restricted to fresh cap/zipper faces and their immediate surviving
one-ring (possibly two rings if a witnessed case requires it).
Evaluate legal geometry-improving operations on private candidate
revisions: mirror-orbit-aware edge flip, edge collapse and constrained
vertex reposition (and carefully justified re-triangulation).
Require genuine improvement in minimum/bottom-decile quality or
unhealthy curvature; avoid blind repeated subdivision of slivers.
Respect X=0 fixed points, source IDs, owner capacity, survivor splits,
patch boundary conformity and exact mirrored-orbit correspondence.

Conditioning is never permission to invalidate or bypass the final
manifold, effective-edge incidence, collision, material/burial,
surface-orientation, finite-work or atomic-publication checks.
An unhelpful modification must roll back to the initial certified
closure; it must not turn a successful repair into a refusal merely
because optional polish could not improve it.

Performance: keep scope/local work finite, measure time spent by phase.
Past Release builds showed around hundreds of milliseconds for complex
repairs; do not make sustained sculpting depend on expensive global
relaxation. Existing repair stutter is a later independent optimization.

## Test policy and acceptance criteria

1. Keep R01–R07 baseline geometry and mirrored cap/zipper tests green on
   Linux/GCC, Linux/Clang and Windows/MSVC.
2. New tests assert desired behavior and safety invariants. **Do not
   codify known-broken current refusal output as a passing regression.**
   For a demonstrated failure, first create an explicitly red test or
   wait until the geometry fix exists, then lock in the corrected result.
3. When a witnessed seam repair admits a legal smoother result, the
   accepted source must be a certified watertight oriented manifold,
   preserve exact reflection, remain collision/material-correct, and
   not introduce poor new triangles.
4. If no such modification is legal, preserve the existing certified
   closure unchanged, or correctly refuse if the initial closure itself
   was uncertifiable. Never fabricate a weld or delete interior material
   without the required evidence.
5. One-stamp F8 experiments show each intermediate result; denied stamps
   remain private and later deliberate stamps can continue.
6. Run a controlled Release-mode GPU acceptance sequence on sphere,
   torus, and hollow sphere, both across X=0 and as separate mirrored
   contact events, including post-repair continued inflation.
7. Record quantitative seam-quality distributions and repair cost, not
   just a visually smooth final frame.

## Working arrangement

- Base: `main` at merge commit `9c8d1effac9015c8422671d1b0c90a2187a1aaa0`.
- New feature branch: `post-repair-seam-conditioning`.
- The merged [PR #162](https://github.com/logiclrd/Mesher/pull/162)
  must remain closed; do not rewrite it or push new work to `r06-integration`.
- Future code changes and their affirmative tests belong on this branch
  and its own PR. This document is the initial research brief, not a
  claim that geometry conditioning has already been implemented.
