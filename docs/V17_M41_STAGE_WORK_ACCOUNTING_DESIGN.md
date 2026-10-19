# V17-M41 — Finite winding/collision/arrangement work, explicit stage accounting

**Status: implementation design only, NOT completed or accepted.**
M40 source prerequisite: commit `85189a54fb8654d96a3776b7826ab3f7ac9bde7e`.
M40 CI/merge is currently blocked by GitHub Actions scheduler status;
[PR #90](https://github.com/logiclrd/Mesher/pull/90) carries the exact
final head in a distinct review concurrency group. This planning branch
starts from the very same unmerged M40 head. Rebase onto verified M40
`main` before any M41 implementation is accepted.

## Existing measured evidence (verified directly in source)

- `RegionTopologyIndex::CollisionStatistics` already counts bounded
  BVH box, partition, candidate triangle, narrow-phase and reported
  contact work. `RegionTopologyIndex::WindingStatistics` counts
  corresponding box, partition, triangles and rays. These are actual
  query operations; never substitute face count or changed partition
  count for measured work.
- `CutCoverageCertificate` exposes first-original discovery work,
  changed-child pair checks and edge-trace tests.
- `CutFragmentAssessment` exposes actual signed-winding queries and
  winding statistics. `UncutFaceRetirementProposal` separately
  accumulates original contact discovery and full component winding.
  Its intra-stage queries already decrement their own remaining
  budget; M41 must preserve those local guards.
- `buildMultiSectionCutPlan` counts `arrangementTests`,
  `arrangementEdges`, `arrangementPredicates` locally, including
  scaffold and triangulation predicates. Those counts are presently
  discarded instead of returned as immutable work evidence.
- `prepareExactCutStep` invokes materialized cut planning,
  optional seam refinement, certified child/uncut join and final
  candidate collision, then RECOMPUTES source-removal evidence before
  assembling the one publishable source-exact root. Merely summing
  final collision or last removal-result statistics would miss
  earlier actual work.

## Required implementation sequence (part of ONE flat M41 task)

1. Add optional, non-authorizing measured work fields to the
   conforming plan/candidate, coverage/occupancy and final exact
   preparation results for actual predicate and query operations.
   Keep the complete counters intact on positive results. Rejected
   outputs may expose *attempted cost*, but MUST retain no
   cut/root/history certificate. Do not fabricate costs that were
   not observed.
2. Propagate stage-local work through the existing private
   original-root cut → optional refine → join → final collision →
   full source revalidation chain. Associate every observation with
   the invocation that consumed the work, so identical cached
   evidence reused by reference is charged once, whereas an actual
   repeated bounded source query is charged again. Guard all
   size_t additions against overflow.
3. Enforce conservative configured aggregate limits for winding
   work, collision/narrow-phase work and arrangement predicates
   across the ONE complete candidate in addition to all existing
   local limits. These are not a shared interchangeable unit:
   winding rays, collision narrow-phase tests, arrangement pair
   intersections, ear/scaffold predicates, box/partition visits
   remain separately typed diagnostics and fail with specific
   category refusals. A low aggregate cap may refuse as soon as
   the actually consumed count crosses it; no partial publication
   or child-owner retirement authority escapes.
4. Use source-authenticated genuinely successful M33 asymmetric
   queued c1/c2/c3/M03/d2 and M37/M39 mirrored normal-driven
   physical exact positives. Measure actual complete work and run
   *exact-boundary* positives and independent one-under refusers
   for every nonzero category. Never accept a refusal from an
   unrelated earlier geometry or face count cap as proof of this
   category. Include at least one genuine multi-curve arrangement
   fixture before asserting nonzero arrangement counts.
5. Forward `SculptStepResourceWork`/worker status with observed
   category costs and categorical refusal reason. On an early
   source-invalid request report work as unavailable/zero, never
   `published=true`. On a measured budget refusal preserve
   attempted-cost diagnostics while reverting COW root, mirrored
   maps, source M29 proof, history, dirty owners and checkpoint
   bookkeeping. Refused Clean must be M23 Unknown.

## Explicit design constraints

- All positives remain normal-driven and original FaceId-certified;
  no global nearest-XYZ, source ownership inference or relaxed
  Boolean predicate.
- No new user-visible partial root or renderer snapshot is created.
  Every composite operation still permits ONLY one final indexed CAS
  after complete stage and original-fidelity preflight.
- Additive work counts measure work consumed, not peak RSS,
  deterministic wall-time, universal accuracy, or fixed cost for
  pathological imported meshes. Do not promise a total resource
  cap without validating all categories in final original M13.
- The entire M41 category, including positive/negative tests,
  documentation, and exact final PR-head and merged-main CI, must
  finish independently before checking its TODO marker. M42 will
  separately cover contour/join/feature/diagnostic category costs.
  The ORIGINAL M13 checkbox, M14–M15, V18 and V19 stay OPEN.
