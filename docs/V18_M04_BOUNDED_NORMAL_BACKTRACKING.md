# V18-M04 — Finite private normal displacement backtracking

## Scope and boundary

M04 upgrades the explicitly enabled `Merge geometry: OFF` worker-only
collision policy from M01–M03's final-candidate admission/refusal
to bounded, **full post-remesh speculative backtracking**.
The ordinary desktop/default mode and V17 authenticated Merge ON
remain unchanged. No checkpoint schema or CLI/UI option changes;
those remain M72/M06. Original V18, M05–M07 and V19 stay open.

## What exactly is reduced

`VersionedInflateBrush::stamp` now accepts an optional
`normalFraction` in (0,1], default 1.0. It scales ONLY the
old-source-normal Inflate displacement `normal × strength × influence ×
curvatureMultiplier`. Original positions, physical brush radius,
symmetric area/partner correspondence, local remeshing settings
and any separately legal tangential movement are not modified.
The existing fraction-1 path is otherwise byte-for-byte identical
in ordinary/default, V17 physicalExact, and legacy callers.

When `SculptCollisionSettings::offModeConstraints.enabled` and its
`backtrack.enabled` are both true, `VersionedSculptBrush::stamp`
chooses an admissible fraction by testing, at most,
`maxTrials=5`, fractions 1, 1/2, 1/4, 1/8, 1/16. The hard maximum
configurable is 8 trials. Each candidate re-evaluates **all** of
Inflate, local split/flip refinement, directional collapse, newly
introduced mirror orbits, complete changed stable IDs, original
M01 collision contacts and M03 mesh-scale clearance against
the SAME fully staged immutable candidate root. It never mixes
one sheet's updated positions with the other sheet's previous
revision. Trial mirror staging is explicitly rolled back, including
every inserted midpoint mirror, before the next attempt.

Two independent refusal reasons may trigger attenuation:

1. M03 conclusively reports `TooClose` or `Contact` for the
   full private endpoint proposal. Smaller normal movement is
   tried without the unsafe root ever being published.
2. Even an apparently clear endpoint may tunnel through a surface
   during a large stamp. Therefore each moved source vertex has
   its attempted normal distance limited to **at most 0.5 times
   its shortest incident edge length in the immutable pre-stamp
   root**, entirely independent of brush radius. The limit is
   finite and editable but confined to (0,1]. An unsafe large
   proposal is backtracked as above.

No retry on `Uncertain` geometric evidence, invalid source,
unreliable numeric scale or exhausted finite work. If no candidate
passes, M04 forces one recoverable pre-CAS refusal; it never
quietly substitutes a zero-length "successful" edit. Valid
tangential-only candidate classifications from M03 remain
permitted when the independent normal edit is safe. Normal
attenuation cannot delete a separately applied tangential
component because Inflate's edit field is purely normal.
M04 neither adds an arbitrary tangential brush tool nor claims
all near-grazing trajectories can be proven safe.

## Actual finite CPU work / one terminal transaction

`SculptBacktrackWork` measures all trial counts, declined candidates,
chosen normal fraction, cumulative private copied bytes, changed-ID
references and historical/current Euclidean distance tests.
All trial index/edit work is real, even if those revisions are
later discarded, and all trial copy/reference counts are folded
into `SculptStepResourceWork.privateCopiedBytes` and
`changedIdReferences` for the ONE final original-root terminal
Step. Independent finite cumulative probe ceilings default to
512 MiB private copied data, 131072 changed-ID references, and
32768 triangle-distance evaluations. Each private M01/M03
BVH also enforces all of its existing six finite work ceilings.
The final complete Step is checked against the existing aggregate
`stepResources` limits (including probe work) before CAS.

A passing probe is NEVER published. After all its mirror changes
are rolled back, the **normal existing worker pipeline** fully
rebuilds the selected scale's Inflate, refine, collapse and
normal M01/M03 clearance; this final independently evaluated
candidate alone can perform the one CAS/history/renderer/mirror
transition. Failure/budget exhaustion still uses the existing
single recoverable refusal path and leaves pointer-identical
original indexed/picking root, empty dirty-frame set, unmodified
mirrors/undo history and fully draining 4D worker queue.
A post-CAS exception retains existing V17 M14 terminal
quarantine semantics.

This is *not* exact swept-volume CCD. The 0.5 mesh-local bound
conservatively limits per-stamp travel but cannot prove every
unseen thin-sheet trajectory safe, especially uneven or imported
geometry. A limit may refuse rather than authorize a step;
V19 owns broad imported/complex geometry robustness.

## Automated independent validation

`tests/offmode_backtracking_tests.cpp`, CTest
`offmode_bounded_normal_backtracking`, exercises:
- A real post-Inflate near-gap close approach on two independently
  indexed sheets. Full-strength is unsafe; halving and quartering
  must result in one safe positive normal move, with one CAS,
  history record and pointer-exact undo/redo.
- Actual cumulative copy accounting must include all discarded
  private trials and the final accepted stage.
- A single permitted trial rejects the dangerous full candidate
  with a recoverable undo/dirty-neutral refusal. The same brush
  subsequently publishes a valid outward Step.
- Zero speculative copy budget vetoes the edit with correct
  attempted work and budget-refusal counters; invalid trial
  configurations reject at construction.
- An endpoint-clear, locally huge normal displacement is
  reduced by the independent half-shortest-edge criterion.
  With only one trial it refuses rather than pretending the
  unseen path was proven safe.
- A real asynchronous 4D worker accepts a backtracked Step as
  exactly one published root/history transition and continues
  to a subsequent valid queued Step.

Older M02 explicit-intersection and M03 strict near-gap
**refusal** tests explicitly disable only M04 backtracking,
preserving their distinct original acceptance predicates.
All automatic retries are finite and private. Symmetry,
both opposing surfaces actively moving and independent fixed
X=0 orbits remain the **separately planned M05** integration.

M04 requires exact PR-head plus independent post-merge GCC,
Clang and Windows MSVC Vulkan-enabled CTest/stress/benchmarks.
