# V17-M43 — genuine private contour and cap/zipper closure work admission

**Independently complete flat M42 prerequisite (NOT original M42).**
Implemented in [PR #93](https://github.com/logiclrd/Mesher/pull/93)
from M41 merged-main `0a831b20577eb10614068b4619f3c55942b75bc4`.
Substantive [Actions #39296825695](https://github.com/logiclrd/Mesher/actions/runs/39296825695)
has **GCC, Clang and MSVC** fully green: 23 CPU tests on each
compiler and applicable 100K/brush and GCC comparison benchmarks.
Documentation-final PR-head and independent merged-main CI remain
mandatory before claiming final merge acceptance.

## Original resource obligation and actual measured units

M42's full contour/join/feature/worker objective remains intact,
OPEN and independently downstream. Its first independently-completable
category M43 extends M41's already accepted bounded `ExactStageWork`
meter with **seven distinct, finite** contour/closure costs:

1. `contourEdgeProbes`: each actual removed-triangle directed edge
   visited by the read-only contour extraction, including non-boundary
   edges and incomplete candidates
2. `contourBoundaryEdges`: each actually matched original
   removed-versus-surviving directed boundary edge
3. `contourLoopVisits`: each genuinely inserted/visited contour
   boundary vertex in assembled oriented rings
4. `closureRingTests`: each original contour ring whose geometry
   is assessed for a fast cap/zipper decision
5. `closurePairCandidates`: each candidate pair of original rings
   actually considered for a zipper, including rejected ratios
6. `closureSampleTests`: each original arc-length correspondence
   squared-distance comparison, including both orientations and
   every shift; never substitute raw vertex-index parity
7. `closureDpTransitions`: each cap and eligible zipper alternative
   evaluated by the complete bounded subset DP

These increment at their actual work sites in
`src/repair_plan.cpp` and `src/repair_closure.cpp`, never
inferred from the eventual surviving contour plan. Repeated genuine
c3/uncut occupancy/contour revalidations count their real work again
when invoked. Scope is the WHOLE worker original-root proposal up to
its one allowed pre-CAS admission; nested thread-local restoration
isolates simultaneous workers. Even failed local plans and
exceptions preserve charges for work already attempted.

All seven have separate finite `ExactStageWorkLimits` ceilings
in `include/mesher/exact_stage_work.hpp`, with checked/saturating
`size_t` overflow refusal inherited from M41. Actual cost exactly
equal to a category's cap is permitted; one fewer unit refuses by
that **specific** category BEFORE publication. Local V15/V16 contour
shape and closure supports remain fail-closed. Neither measured work
nor a ready closure selects a Boolean deletion without independent
original c1/c2/c3/M03/d2 authority.

## Positive source fixtures and one-under refusals

`tests/repair_closure_tests.cpp::testMergingWallsAndArcLength`
uses the existing real closed two-prism unequal-density four-versus-
eight vertex contour fixture with a successful strictly original
read-only zipper selection. It directly observes all seven NONZERO
work buckets, including two actual ring evaluations, ONE
pair candidate and exactly `2 × 32 × 32` distance comparisons
for reversed/forward rotated resampling. Each exact individual
category accepts, each one-under refuses with its named reason and
the immutable original picker/BVH pointer unchanged.

`tests/async_sculpt_tests.cpp::
testM43WholeStepContourClosureAccounting` reproduces a REAL
72-FaceId original mirrored protected normal-driven physicalExact
source union and, separately, a real authenticated asymmetric queued
two-tetrahedron original-root c1/c2/c3/M03/d2 union. Each first
calibrates ALL genuine attempted per-category work from a complete
successful physical/queued Step, then accepts exactly those
complete measured category ceilings. For every category actually
used by that supported source it reduces ONLY that category by
one and requires a refused Step with the precise categorical reason,
unchanged old COW root, first-original M29 source certificate,
stable mirror pairs, renderer dirty partitions, publish counter,
undo/redo history, and full measured-but-unpublished attempted
costs. It never fabricates one-under work for a category that
the supported candidate did not actually execute. The separate
unequal-tessellation source fixture proves all seven categories
actually execute in the supported read-only closure planner.

## Explicit continuation and limitations

M43 does **not** claim to count seam trace/zipper/weld work
(M44), seven-point original protected witness or mirror/history
proof work (M45), or stage-wise timings, aggregate FIFO and
checkpoint telemetry (M46). Those were all preplanned as flat
independently-completable items in `todo.md` before starting
M43. Only then is the ORIGINAL M42 combined policy audited,
followed by ORIGINAL M13. M14 post-CAS failure injection,
M15 desktop switch and V18/V19 stay open.

All measured categories are deterministic *attempted operation counts*,
not hard maximum process RSS or CPU wall-time; the sampled-original
source proof is not an exact two-sided Hausdorff metric, and
multi-ring cap/zipper preparation does not imply universal arbitrary
three-body same-component Boolean joins.
