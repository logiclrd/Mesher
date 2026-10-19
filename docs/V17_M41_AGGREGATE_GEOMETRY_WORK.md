# V17-M41 — whole-Step winding, collision and arrangement work accounting

**Status: accepted in PR #92 (CI recovery from PR #91).** Substantive
[Actions #39288853723](https://github.com/logiclrd/Mesher/actions/runs/39288853723)
passed Linux GCC, Linux Clang and Windows MSVC warning-as-error builds,
all 23 CPU test suites and applicable benchmark stages. Exact
documentation-final PR-head and independent merged-main CI are still
required before reporting the milestone merged-complete. This is the
second flat independently completable precursor to ORIGINAL M13,
following M40 merged to main in [PR #90](https://github.com/logiclrd/Mesher/pull/90)
(commit `3b596657d3c5f4703bfd681c6b4ff20eec9439fa`).
Do NOT close the original M13 or the M41 checkbox until all 23 suites
and applicable benchmarks are green on final PR head AND merged-main CI.

## Actual measured operation units, not guessed geometry size

The new `include/mesher/exact_stage_work.hpp` defines a
per-executing-thread, nested-restoring `ScopedExactStageWork` meter.
Every invoked `RegionTopologyIndex::triangleIntersections()`
records its REAL query, visited BVH boxes, partitions, candidate
triangles and attempted exact narrow-phase predicates. Every
`RegionTopologyIndex::positiveWindingAt()` records its own REAL
query, BVH box/partition traversals, triangle candidates and
signed ray tests (including the near-surface pass). A normal or
exceptional return charges the actual captured local statistics
through a noexcept scope-end observer. Finite local query limits are
unchanged; no observer synthesizes successful classification if a
query or a local budget refuses.

`buildMultiSectionCutPlan()` measures its actual arrangement
pairwise section tests and graph/scaffold/triangulation predicates
from the existing counters, including incomplete or refused
plan attempts. Existing per-plan graph-edge, junction and triangle
budgets remain enforced. Single-curve conformance correctly
consumes ZERO multi-curve arrangement-work units.

Each genuine 4D physicalExact `stamp` or queued
`mergeExactContacts` scopes one meter over the FULL unpublished
proposal: post-inflate original V13 contact discovery, c1/c2/c3
occupancy and original coverage, M03 uncut retirement, d2 seam
refinement/weld, final residual local source collision, and the
independent full M26 source-authentication replay plus first-original
terminal fidelity. Reusing immutable evidence incurs no new query
charge; a real repeated certificate proof that REEXECUTES queries
does count again. Separate simultaneous worker threads cannot
share or corrupt measured budgets. Scope terminates at the exact
pre-CAS admission, so no post-publication work is silently appended
after the aggregate check.

## Individually finite aggregate category guards

M41 adds opt-in original-root aggregate limits to
`SculptCollisionSettings::exactStageLimits` for twelve distinct
work dimensions:

- Collision: query count, BVH boxes, partitions, candidate triangles,
  and exact narrow-phase tests
- Signed winding: query count, BVH boxes, partitions, triangle
  candidates, and tested rays
- Multi-curve planar arrangement: pairwise tests and predicates

Each work dimension has its own nonnegative finite ceiling; it
cannot borrow another category's unused budget. Exact equality
passes for a fully certified supported final candidate and ONE
less refuses the otherwise identical candidate. Work addition
guards all `size_t` overflow, saturating on overflow and requiring
a fail-closed refusal before CAS. Overflow can never wrap and pass
as a small workload. The accounting is an additional guard;
V13/c1/c2/c3/M03/d2's own separately bounded local scopes and
M40 cloned-COW/ID/dirty resource guards still apply.

These are attempted computation work bounds, NOT maximum RSS,
CPU wall-time guarantees, two-sided Hausdorff fidelity, full
CCD, or universal arbitrary three-body Boolean support.
The `SculptStepResourceWork::exactStage` field forwards real
measured counts through the existing asynchronous worker
`Status.stepResources`. An early invalid scope with no query
reports zero, while a complete but disallowed workload reports
the ACTUAL attempted counts alongside `refused=true`,
`published=false`, and the category-specific refusal reason.
M23 treats any refused Clean as Unknown, interrupting quiet
hysteresis without granting a Boolean weld.

## Real regression evidence, to be confirmed by CI

`tests/async_sculpt_tests.cpp:
testM41ExactWholeStepQueryAccounting()` uses the existing
72-source mirrored overlapping-brush protected M39/M37 source
with a genuinely normal-driven c1/c2/c3/M03/d2 physical union.
It independently measures all winding/collision work, validates
full exact-budget acceptance and one-under refusal for each
query, narrow-phase, triangle, ray and BVH box/partition/candidate
dimension without root CAS, source-authentication history, mirror,
renderer-dirty or undo changes. A second separate authentic
source-certified queued two-tetra exact union exercises the same
complete independently metered acceptance and refusal boundaries.

`tests/revision_collision_tests.cpp:
testCrossingMultipleSectionsPerOriginalTriangle()` reuses the
real three closed-solid two-transverse-section crossing
fixture. Its complete read-only source-conforming plan must
measure nonzero TRUE pairwise arrangement intersections and
scaffold/ear predicates; exact limits accept, and each
separate one-under limit refuses the measured work. No
partial candidate, source-owner rewrite or publication is
licensed by these measurement-only checks. This is genuine
multi-curve PLAN acceptance, not an unsupported assertion that
an arbitrary triple-body joined FINAL Boolean is already
supported.

Once CI is green, final milestone evidence should record
exact successful GCC/Clang/MSVC run SHAs and full CPU and
benchmark results; check ONLY M41, leaving M42 and original
M13, M14–M15, V18 and V19 open.

## Accepted substantive evidence and no-scope-expansion boundary

Original review [PR #91](https://github.com/logiclrd/Mesher/pull/91)
exposed only MSVC warning C4456: a new scope accounting guard called
`record` hid an existing local face `record`. The spelling was
corrected consistently to `queryAccounting`, without modifying
source predicates, work amounts or acceptance thresholds.
[PR #92](https://github.com/logiclrd/Mesher/pull/92) repeats the
FULL suite at exactly the corrected SHA
`231915a6fc8d172889f742f066bf2720cd4e29b7`, with all three
compilers green in [Actions #39288853723](https://github.com/logiclrd/Mesher/actions/runs/39288853723).
The test includes genuine 72-source mirrored physical unions,
independent authenticated queued c1/c2/c3/M03/d2, exact equality
and separately rejected one-under each of the 10 collision/winding
operation ceilings, plus original three-closed-solid multi-section
conforming arrangement predicate and pair-test exact/one-under
checks. M40 copied payload, source provenance, stable original
FaceId intent, signed cavity/mirror proof, M23 hysteresis, original
M12 and all 23 existing CPU suites are preserved.

This second flat acceptance peer does not close M42
contour/join/feature resource diagnostics, the ORIGINAL M13
integrated resource review, M14 fault-injected post-CAS work,
M15 actual desktop UI, V18 or V19.
