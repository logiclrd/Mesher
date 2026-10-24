# R06-M15 — Staging-owned topology editor (design locked 2026-10-24)

## Problem and strict scope

The M14C shared-edge helper only ensures conformity **when it is actually
called on an indexed edge**. M14E copies all already-requested edits into
one COW revision and checks manifoldness. Neither forces cap/zipper generators
to use shared topology primitives, nor treats the yellow R03 open boundary
as a *live* evolving path. M13 exact-case diagnostics still show 382 and
356 geometric point-on-edge witnesses for refusal-1 and refusal-3,
respectively, in **untrusted warning previews**; those are not proven
indexed-manifold failures or automatic permissions to weld.

M15 makes `MixedClosureStaging` the sole write interface for the evolving
private mesh topology. The immutable R03 open revision is its base, not an
already closed candidate. The stage starts with the R03 survivors and their
one-sided directed boundary loops; cap/zip, insertion and refinement then
alter that *same effective topology*. Intermediate open boundary is allowed.
Only finalized consolidated candidate is tested for closed oriented
2-manifold topology, quality and finite work. No publication in R06.
The single sculpt worker remains the sole mesh writer; there is no rebase
against competing publishers.

## API semantics (normative)

- `addVertex(position, owner)` allocates a **new** identity even at an
  already-used XYZ (no implicit coordinate welding), then records its owner,
  position and private reservation. It never creates a disconnected
  survivor identity by copying an original ID.
- `ensureFace(a,b,c, kind, owner, targetEdge)` means the oriented triangle
  shall exist. Equal *cyclic* vertex rotations are identical. An existing
  identically oriented triangle is a no-op and returns its same handle;
  opposite orientation of the same three IDs is a hard error. New triangles
  are checked for unique nonzero IDs, owner/limits and for not creating an
  edge with >2 incident faces or two equal edge directions. The operation
  may legitimately open a temporary closure front; finalization is stricter.
  A source survivor, a new cap/zip triangle and their mixed VertexIds use
  the same incidence ledger and rules. No coordinate-based overlap test
  silently converts a face request into a stitch.
- `splitEdge(a,b,fraction)` is a **topology mutation** on the currently
  existing indexed edge AB. Require 0<fraction<1; its repeated invocation
  after splitting AB **must throw** (AB no longer exists). The replacement
  edges AM and MB are *not* an alias for old AB. Before any modification,
  discover and preflight every effective incident face (including original
  survivors beyond the initial tracked contour halo, original overrides,
  and new closure faces), budgets, child geometry and mirror orbits.
  Allocate exactly ONE new M ID and split all incident faces using M in the
  same atomic logical operation; retain original FaceIds for parent
  replacements, introduce children as newly planned faces. If AB is an
  original R03 one-sided boundary, one-sided splitting is legal and the
  boundary becomes ...A-M-B...; if AB has a survivor and cap side, both
  sides are subdivided. Never leave a one-sided partial topology edit.
- Current boundary loops are **effective edge incidence**, not stale
  independent R03 contour snapshots. Their directed walks reflect an
  initial split even before any cap is inserted; inserting a closing face
  removes its now-paired edge from the yellow boundary and incorporates
  the face's remaining unpaired edges, if any. Derived walks must not
  pretend to be closed if the intermediate front is a chain or a
  nonmanifold fork; expose this as invalid/intermediate rather than
  returning a stale loop. Reflection orbits update as one transaction.
- `finalize()` builds ONE private COW consolidated revision, validates
  all required topological/quality/mirror/finite-work invariants, and
  returns a certified immutable revision or throws without publication.
  The R06 inspection `QUALITY_WARNING_PREVIEW` remains visibly untrusted.
  Preserve strict legacy spatial-front contact refusal until separately
  reviewed; geometric self-intersections alone do not define manifoldness.

## Encapsulation and implementation constraints

Do not leave alternate public routes to mutate face triangles,
`MixedClosureSourceView::overrideFace`, face/edge storage or contour lists
once the migration is complete. Geometry algorithms can inspect read-only
working topology but must invoke stage mutations. Before closing that API,
migrate *all* cap/zipper generators, finite edge refinement, diagonal
flips, interior cap-fan reduction and any topology-changing helper. Direct
XYZ motion by fairing is a separate position-edit API with its own quality
and reflection preflight. Reuse M14C finite mirror-orbit and minimum quality
preflights; do not silently change caps q>=.10, survivor q>=.012,
512 mandatory split budget, or existing acceptance policy.

Identity remains indexed, not inferred from nearest coordinate. A caller
cannot force an incidental new vertex lying near an unrelated survivor edge
to become a weld. To attach there, it must explicitly request a split on
the actual surviving edge and use the returned M when building its faces.
Edge existence and all cyclic face lookups use effective, not original,
topology. Internal boundary walkers must account for all source open
components, not merely an initial yellow display loop.

## Flat independently testable milestones

- **M15A — Staging topology contract and live rim reads:** add stage-owned
  source-bound creation and effective boundary/face lookup/ensure-face
  operations. Tests for initially open R03 rim, cyclic idempotence,
  reverse winding refusal, insertion that changes the active yellow
  boundary and wholly new/part-survivor faces.
- **M15B — Staging-owned split operation:** move current direct
  `applyMixedConformingEdgeSplit` mutation behind stage, including a
  one-sided original rim, exact two-sided existing edge and attached cap;
  fail a repeated split of AB; update live boundary walks and reflected
  orbits without partial edits. Test finite/quality refusal and
  original-root immutability.
- **M15C — Migrate all closure construction/refinement writers:** cap and
  zipper face creation and adaptive subdivision call stage operations,
  then migrate flips, fan collapse and direct face/vertex change helpers.
  Gate underlying storage for read-only external access and remove
  direct callers of low-level split and direct `faces.push_back()`.
  Keep M14E consolidation and full manifold admission.
- **M15D — Physical regression and CI acceptance:** test split-before-
  closure and split-after-closure equivalence, inspect 1/3 exact-case
  warnings with M13 audits and exact overlay, preserve the five archived
  case bytes/classifiers, GCC/Clang/MSVC CTest, user physical Vulkan
  inspection with W and golden interface. Record witness-count changes,
  not just PASS/REFUSED. No default policy loosening or PR merge.

**Acceptance honesty:** M15A/B are foundations, not proof that old
refusal-1/3 geometric witnesses disappear. Only after M15C and M15D
can the whole class of accidentally independent *intended shared-edge*
splits be considered architecturally prevented.

## Implementation checkpoint and unresolved defect (2026-10-24)

Accepted in [three-platform CI #40072521856](https://github.com/logiclrd/Mesher/actions/runs/40072521856):
M15A adds a source-bound staging editor, derived effective yellow
boundary loops (as one-sided directed walks), indexed `addVertex`, cyclic
idempotent `ensureFace`, reversed-face/overfull-edge refusal, and
initial R03 survivor tracking. M15B moves the actual refinement split
from the caller-local lambda into `MixedClosureStaging::splitEdge`.
A request on a nonexistent former A-B edge is **always an error**;
one-sided R03 rim and two-sided survivor/cap cases update all their
incident faces using exactly one new VertexId, including mirrored
orbit and quality/budget preflight. The regression explicitly tests
split-before-cap, split-after-cap, original source immutability, and
remote two-sided source geometry. M15C1 routes initial cap/zipper
triangle insertion through `ensureFace`; M15C2 routes interior
diagonal flips, fan retirement, and fairing through staging methods.

**M15C3/C4 accepted (2026-10-24):** `MixedClosureStaging`
now keeps its source override object, current face records, working
positions, vertex ownership, provisional identities and initial rim
paths **private**. The published accessors expose only `const` views,
and original `MixedClosureFace::setTriangle` now refuses direct edits
through a FaceId handle. Free mutable edge splitting/import helpers
were removed, and generators, patch planning, refinement, provisional
ID conversion and vertex retirement use the stage operations. Interior
quality flip/collapse/fairing implementations remain private helper
algorithms invoked by the stage (rather than by closure callers).
A deliberately truncated or duplicate source-face patch ledger fails
before private COW writes; independently constructed invalid candidate
topology still fails the final full manifold validator. This enforces
the source-backed staging API contract, not collision-free geometry.

[Actions #40073922184](https://github.com/logiclrd/Mesher/actions/runs/40073922184)
completed SUCCESS on GCC, Clang and Windows MSVC (GCC: 100/100
CPU tests); all five original exact R06 replay regressions retained.
`r06-integration` code head `436f1ce8` is the accepted build;
`main` remains unchanged. M15D physical inspection and actual
generator requests for intended survivor-edge attachment remain open.

**Crucial empirical result:** The exact M13 warning-preview census is
unchanged from M14: `refusal-1` still reports 382 geometric
T-junction candidates and 4 new/survivor crossing edges; `refusal-3`
reports 356 candidates and 14 crossings; indexed shared-interface
incidence and orientation errors are both zero. An incidental
point-on-edge is not automatically an intended weld. The cap/front
generators still need an explicit policy for when a new vertex
*intends* to attach to a surviving edge and must use a returned
split VertexId. Do not loosen the quality thresholds or silently
weld by XYZ to make these two exact cases pass. M15D is not
accepted until physical GPU inspection confirms the intended
attachment behavior.
