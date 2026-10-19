# V17-M44 — Aggregate source-certified seam trace/refinement/join work

**Independently accepted flat M42 prerequisite**, following M43's verified
merge on [PR #93](https://github.com/logiclrd/Mesher/pull/93),
`main` commit `09a4270ab5941a707e308e25123a30923a716234`.
M44 substantive [Actions #39303521777](https://github.com/logiclrd/Mesher/actions/runs/39303521777)
is **SUCCESS** on Linux GCC, Linux Clang and Windows MSVC
warnings-as-errors, all 23 CPU tests and applicable 100K/brush and
comparison benchmarks. Final documentation-head and independent
merged-main three-compiler CI are separately required before
reporting M44 merged.

## Scope and exact work categories

M41/M43's thread-local nested-restoring `ExactStageWork` meter spans
one complete candidate original-root private Step and ends at its
PRE-CAS admission, before source-root publication, history or renderer
effects. M44 instruments SEVEN new independent actually consumed
seam-work categories in `src/repair_intersection_join.cpp`:

- `seamTraceProbes`: every actual original source boundary-edge
  inspected by `trace()` (including source-mismatched candidates
  and unsuccessful/partial scans), across independent join,
  optional refinement and revalidation
- `seamMatchedIntervals`: each opposite-winding, exact-XYZ
  original source-aligned boundary interval actually approved
  by the join, not a guessed correspondence of vertex positions
- `seamWeldComparisons`: each exact-point pair compared by the
  source-certified weld union-find, including redundant same-class
  comparisons, rather than only retained representatives
- `seamRefinePredicates`: every attempted unequal-density boundary
  interval coverage/splitting sweep predicate in the optional V17
  source-authorized seam subdivision reconciliation
- `seamRefineMatches`: each surviving interval whose new
  cross-sheet opposite orientation/exact endpoints are certified
  in the refinement output; a failed local bound does not return
  private authoritative roots
- `seamMirrorOrbitChecks`: individually checked historical and
  newly allocated reflected/fixed source-cut vertex orbits and
  source-proven reflected seam-equivalence members
- `seamFaceRewires`: each actually issued private `RegionEdit`
  replacement of a retained source-face from the certified join;
  not the final count of emitted new faces

Each count uses `ExactStageWork::add` checked saturation; overflow
fails closed and cannot appear as a small wrapped cost. Seven
separate finite `ExactStageWorkLimits` maxima are enforced by
M41's already existing `refuseExactStageWork` at final pre-CAS
admission, on physical and queued exact original-root operations.
An exactly equal ceiling accepts and any ONE less refuses that
otherwise positive fully source-certified candidate with an
explicit named category refusal. All local V13/c1/c2/c3/M03/d2
budgets still apply; no arbitrary closest-vertex or floating-point
seam proximity inference is introduced.

Actual repeated work in source revalidation is measured again
when performed; no final mesh size is substituted for a consumed
work count. Optional refinement may perform zero work in a
supported two-body join; a zero category is neither claimed as
positive nor artificially decremented. Actual attempted work
and refusal reasons remain in the complete direct
`SculptStepResourceWork::exactStage` and actual async
`Status.stepResources`, clearly distinct from the successful
publication counter and M40 COW copying proxy (not peak RSS).

## Complete real original-root acceptance

`testM44OriginalSeamTraceJoinWork` executes one genuine
72-first-original protected mirrored M37/M39 normal-driven
physical exact Boolean Step and separately a real asymmetric
authenticated queued c1/c2/c3/M03/d2 exact union.
Each candidate first succeeds, establishing its complete
actual nonzero seam-work costs. All seven ceilings are
calibrated to those EXACT work counts, which must
allow the same fully authenticated positive root to commit.
Every separate truly executed nonzero category is then
lowered by ONE, independently, and is required to refuse
for THAT specific category before CAS: no change to the
original COW root, M29 first-original lifetime credentials,
reflected stable-ID pair map, dirty renderer partitions,
completed union/publication counters or undo/redo history.
Unlike a synthetic budget calculator-only test, these are
real direct and queued source-owned final root attempts.

`testGenericCompleteThreeBodyTransverseGraph` in
`tests/revision_collision_tests.cpp` remains the genuine
12-contact three-closed-solid generic transverse graph fixture.
Its independently staged private `splitSingleCertifiedBoundaryForTest`
increases one original-source seam fan's density; the
opposite fan remains unsplit. The *genuine* optional
`reconcileCertifiedSeamSubdivisions` is instrumented and
must report nonzero actual trace probes, interval coverage/
split sweep predicates and independently certified matched
intervals. Exact all-positive refinement limits pass;
one-under real refinement-predicate or match limits produce
the correctly named refusal *before* any root publication.
The subsequent actual join must still yield the
certified one-component closed union in this bounded fixture.
Read-only demonstration of this supported certified triple
join does not imply universal triple-body or accidental
coincident-XYZ union support.

## Remaining scope

M44 does NOT close M45 protected-original seven-sample
features and history/mirror per-CAS work accounting,
M46 stage timings/worker cumulative status, the
ORIGINAL M42 simultaneous acceptance, or ORIGINAL M13
aggregate final review. The full flat M43→M44→M45→M46→
ORIGINAL M42→ORIGINAL M13 plan was recorded in `todo.md`
before M43 coding. M14/M15 and V18/V19 remain open.
No unsupported exact CCD, hard CPU duration/RSS bound,
universal imported Boolean success or proximity source
alias is claimed.
