# V17-M46 — Honest cumulative direct, queued and asynchronous worker diagnostics

**Independent fourth prerequisite for original M42.** The sequence
was planned before work on M43: M43 contour/closure, M44
source-certified seam, M45 protected original/history, M46
worker telemetry, then independent ORIGINAL M42 and ORIGINAL
M13 acceptance reviews. M46 does not close those original gates.

Substantive [PR #96 CI #39328566938](https://github.com/logiclrd/Mesher/actions/runs/39328566938)
passed GCC and Clang Linux and Windows MSVC /W4 /WX, all 23 CPU
suites, repeated checkpoint/history stress and applicable
benchmarks. Exact final documentation HEAD and independent
merged-main CI are separately mandatory before merging.

## Two different resource concepts, neither inferred from final topology

`SculptStepResourceWork` retains per-physical/queued-attempt
diagnostics: source geometry counters, `privateCopiedBytes`
for all intermediate private COW work, `changedIdReferences`,
`finalDirtyPartitions`, actual attempted total milliseconds,
and distinct `evaluated`, `refused`, `published`, and
human-readable refusal. The 35 real `ExactStageWork` counters
from M41/M43/M44/M45 remain independent finite geometry
and source-proof operation counts; they saturate at MAX
rather than wrapping. M46 additionally measures individual
monotonic wall-clock stages at their execution sites:

- `initialGeometryMs`: private physical inflate/refine/collapse
  or queued V13 exact cut/closure/join preparation
- `sourceGeometryMs`: actual post-remesh source collision
  classification, mirror/validator or queued source journal
  preliminary decisions
- `originalProofMs`: certified FIRST-root source lifetime,
  seven-witness and history proof evaluation
- `admissionMs`: whole private COW resource and source
  geometry category finite-budget pre-CAS checks
- `publicationAndHistoryMs`: actual accepted root CAS,
  mirror journal and history/dirty publication

Stages genuinely skipped on rejection may remain zero. Their
observed time spans do not need to sum to the entire Step's
observed elapsed time, which also includes orchestration.
Wall clock is NOT a hard deterministic CPU-time bound.
Copied COW payload bytes are an attempted copying-work
**proxy, NOT process peak RSS** or live resident bytes.

## Current-session cumulative accounting

`SculptResourceTotals` is maintained at every normal physical
stamp and every queued exact Boolean result, whether a
successful publication, an unchanged physical candidate, or
a recoverable refusal. It reports total attempts, physical
and queued attempt counts, published/refused/unchanged
counts, provenance/geometry/finite-budget/stale refusal
classification and separate ATTEMPTED and PUBLISHED
COW copied bytes, changed stable-ID references, final dirty
partitions and *all 35* saturated `ExactStageWork` categories,
as well as cumulative observed attempted/published milliseconds.

A failed source geometry predicate or exhaustion may have
performed nonzero real work; that work remains in ATTEMPTED
and must never be counted in PUBLISHED. No counter is
Boolean deletion authority or geometric certification.
Undo/redo restore recorded immutable roots rather than
reexecuting a sculpt Step and therefore cannot bill the
old work again. A coarse refusal category is diagnostics
only; the exact failure message retains the specific
M40/M41/M45 gate and authentication reason.

## Real FIFO and checkpoint semantics

`AsyncSculptWorker::Status` now makes these totals visible
alongside the last actual per-Step timings and actual
synchronous/queued outcome. Worker-local monotone metrics
also count real undo and redo restorations, checkpoint
commands, observed snapshot publication events, size of
the latest coalesced renderer dirty-owner set and sum of
such per-publication owner set sizes. Existing bounded
history statistics remain authoritative for retained
revisions and COW change-byte budget, while existing
asynchronous checkpoint statistics distinguish completed,
failed, coalesced and requested file writes.

These are current process/session observations, not
checkpoint-authenticated facts. Saving an authenticated
source-backed current root writes the V13 original fidelity
**requirement**, but reading it from disk does NOT synthesize
the opaque first-original M29 authority nor resurrect
a previous session's work history. A new restored live
controller starts its own zeroed cumulative telemetry;
it may carry verified M29 source credentials ONLY from a
trusted in-memory handoff, not an opaque disk digest.

## True physical and queued acceptance regressions

`testM46CumulativeSourceWorkerDiagnostics` executes:

1. Genuine 72-original protected reflected normal-driven
   M39 physical exact union from the same imported original,
   direct and through live 4D worker Begin/End FIFO.
2. Genuine independent asymmetric source-certified queued
   original-root two-tetra c1/c2/c3/M03/d2 union, direct
   and through live queued Merge FIFO.
3. Exactly measured positive M45 first-original witness
   work versus ONE-UNDER real operation limit refusal in
   both physical and queued paths, with unchanged source
   root, mirror/dirty/history and zero published refused costs.
4. A stale queued original-root CAS request with real
   named stale refusal but no phantom copied-byte or
   source-work publication.
5. Live async worker undo/redo root and history cursor
   restoration without recharging geometry work, plus
   honest snapshot owner partition aggregation.
6. Durable original-backed queued checkpoint written by
   the actual worker, disk read-back preserving only
   the V13 protected-source requirement, never an
   unverified opaque M29 certificate or earlier work totals.

M46 does NOT claim bounded absolute CPU runtime, process
RSS, continuous two-sided Hausdorff, universal imported
Boolean topology success, source-equivalence from XYZ
coincidence or desktop Merge default enablement. The
original M42 integrated all-criteria same-transaction
review remains OPEN, followed by independently open
ORIGINAL M13, and M14/M15/V18/V19.
