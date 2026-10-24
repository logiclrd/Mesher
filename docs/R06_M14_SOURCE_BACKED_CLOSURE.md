# R06-M14 — Source-backed mixed closure working topology

**Architectural decision (2026-10-23).** The R03 post-excision
`RegionTopologyIndex` is the immutable authority for all surviving
`VertexId`, `FaceId`, positions, edge incidence, ownership and spatial
lookups. A cap or zipper is a proposed **transactional edit** to that same
topology, not a detached copy of a small ring neighborhood which must later
be synchronized to the real mesh.

## Problem

Currently `MixedClosureStaging` copies immediate R03 surviving triangle
records and positions into `faces` and `positions`. Refinement mutates
these local values, then replays them into a private COW view. It can split
an *already tracked* shared edge correctly using one midpoint ID for both
incident staged faces. However, the tracked survivor collection is seeded
only by `RepairContour::survivingFaces`. Other original surviving edges
and faces are absent, even if a proposed cap/zipper encounters them in 3D.
The M13 `NEW_VERTEX_INSIDE_SURVIVOR_EDGE` reports demonstrate geometric
nonconformity in untrusted preview results; they do not by themselves prove
that a recognized R03 boundary edge was split twice with different IDs.

## Target model

1. Hold `shared_ptr<const RegionTopologyIndex>` to the **exact R03 cut**.
   The source root remains immutable and alive throughout a repair.
2. Reference existing surviving faces/vertices by permanent IDs only.
   Resolve reads from one source-backed view plus **transaction-owned
   overrides**. Newly proposed faces/vertices have transaction-local
   identity until permanent ID allocation.
3. Provide one effective `face(id)`, `vertex(id)`, `edgeSides(edge)`
   and bounded spatial lookup surface. Reads after an edit observe the
   pending edit; unchanged geometry reads from the source. Do not
   maintain a second independently mutable copy of survivor topology.
4. A topological `splitEdge(A,B)` discovers its *effective* incident
   faces, preflights both sides (and entire reflected orbit), introduces
   one midpoint `VertexId`, replaces all affected faces in the
   transaction, and updates the effective adjacency. There is no
   separate cap/survivor synchronization pass. A source FaceId may
   require one or more new child FaceIds, preserving tombstones and owner
   provenance.
5. The editable footprint may grow through bounded discovery of source
   geometry. The generator's **patch plan must explicitly include every
   intended attachment and required surviving-face subdivision**; source
   edges are updated using shared VertexIds. Never infer attachment merely
   from an XYZ coincidence. A geometric self-intersection is not proof
   of nonmanifold topology.
6. Materialize the complete private COW candidate, then validate **closed,
   oriented topological two-manifold connectivity** (two oppositely oriented
   faces per edge, a single closed fan per vertex), established geometry
   quality constraints, reflection policy and finite work. If the candidate
   fails, discard it. If it passes, atomically publish the already-validated
   immutable revision. No intermediate cut, cap or zipper root is published.
   Geometric intersection audits provide diagnostic/quality evidence, not
   a global zero-intersection admission requirement.

## Flat implementation sequence

- **M14A — Source-backed read foundation and identity tests.** Give
  staging a strong reference to its originating R03 index and an
  authoritative read-through interface for existing survivor IDs;
  model edits as transaction-local overrides. First migrate read-only
  boundary/survivor discovery and test whole-mesh access, stable
  source identity, source immutability and correct override precedence.
  Do not change closure decisions or claim that refinement is already
  unified until the remaining migration completes.
- **M14B — Migrate cap/zipper staging and refinement to the same
  effective working topology.** Remove redundant mutable survivor
  copies from refinement; keep only existing FaceIds and transactional
  changes. Execute face/vertex queries against effective topology,
  reusing COW ownership and budgets. Preserve historical exact replay
  results and all quality floors. This is a bounded independent
  refactor; no new collision-repair policy.
- **M14C — Unified conforming edge splitting.** Operate on effective
  incident-face sets including newly proposed and source survivor
  faces, expand write/read halo as required, and preflight mirrored
  orbit, child quality, winding, stable IDs and budget atomically.
  Regression: splitting a real R03 rim edge leaves one shared
  midpoint ID across surviving and new triangles; splitting an edge
  beyond the original tracked neighborhood updates *both* real
  survivor sides or refuses before mutation.
- **M14D — Diagnostic provenance, without new acceptance policy.** Link
  opt-in M13 seam witnesses to the exact R03 source and final patch:
  original versus transaction-added VertexIds, whether a witness edge
  existed as a one-sided rim or two-sided survivor edge in R03, and
  whether the final mesh represents the claimed connection by indexed
  shared incidence. Deduplicate bounded witness records. Do not infer
  patch *intent* from proximity and do not refuse on an incidental
  crossing or prior self-intersection.
- **M14E — Explicit patch-plan integration contract.** Ensure every
  generator-requested attachment includes necessary survivor edits in
  the same transaction (using M14C conforming edge operations). Materialize
  the consolidated candidate, audit closed manifold topology, and discard
  on topological failure; retain quality and resource budgets. Do not
  manufacture welds based on geometric tolerance or mandate globally
  intersection-free output.
- **M14F — Exact archive and GPU acceptance.** Keep immutable
  `.r06case` bytes, test all five exact replays with area and
  proximity classifiers, preserve reviewed CLOSED 2/4/5 and honest
  REFUSED 1/3 until certified recovery, compare M13 geometric witness
  counts, verify on physical Vulkan with W and exact overlays, run
  combined GCC/Clang/MSVC CI. R07/R08 publication stays separate.

Each milestone requires independent observable tests; split an oversized
item into flat peer tasks in `todo.md` **before** implementing any part.

## Nonnegotiable constraints

Original R03 rims are actual directed survivor edges. A split of such
an edge is one shared topology operation—not two reconstructed points.
Stable IDs and mirrored orbits are authoritative. Source surviving
quality >= 0.012, normal new cap/zipper quality >= 0.10 (only existing
narrow proven rim-tip exception), winding, connected manifold vertex
fans, finite work, complete rollback and no sealed interior cavities
are retained. Minor self-intersections may PRE-EXIST in the known-good
published mesh and may also arise during a legitimate brush/closure
sequence. They do not by themselves invalidate the topology. The
existing strict rejection of any new triangle contact in selected R06
paths is a policy mismatch to review separately; no incidental
intersection should automatically be treated as proof of a bad seam.

## Clarified proposal/validation/publication contract (2026-10-23)

**Single writer.** `AsyncSculptWorker::run` executes sculpt stamps,
merge, undo and redo serially on the worker; the renderer consumes
immutable snapshots. A queued command cannot change the current
authoritative root during cap/zipper calculations. A user-selected
merge snapshot may become stale *before* its queued command starts,
so source-token admission still matters. The final
`publishIfCurrent(expected,final)` is a defensive single-writer
invariant assertion, not a conflict-resolution/rebase mechanism.

**Generator contract.** Intersection detection and R03 excision are
private, temporary steps. The cap/zipper generator may construct
intermediate nonmanifold geometry, but it must eventually propose
a *complete patch*: new/retired/modified faces and vertices, including
survivor subdivisions necessary for every intended attachment. The
source-backed transaction applies that proposal as ONE consolidated
private revision. Its final topology—not the intermediate geometry—is
the object of manifold validation.

**Admission.** The complete candidate is accepted only when all
required topological, quality, symmetry and bounded-work checks pass.
Topological manifoldness uses exactly two opposite-oriented face sides
per surviving edge and one cyclic incident-face link per vertex. The
local region-edit validator plus full private-closed manifold audit
already implement this kind of test. On failure, do not publish; on
success, atomically publish the finished revision and advance history
and auxiliary state. There is no concurrent mesh update to rebase.

**Self-intersections versus topology.** The published *starting* mesh
is known-good in connectivity, not guaranteed free of all 3D
self-intersections. Small transient crossings may already be present
or appear during successive strokes. Crossing edges, coincident XYZ
with different VertexIds, and geometric point-on-edge reports are
useful **diagnostics**, but a geometric encounter is not automatically
a topological hole or a mandate to weld; it must not be converted into
a blanket intersection-free publication rule. A generator that actually
expects to attach at the middle of a survivor edge must explicitly
split that edge and use the resulting shared VertexId. If it omits
these edits, the consolidated topology will contain unmatched edges
and fail manifold validation.

**M14D/E refinement.** Use M13 geometric witnesses to explain which
source/new IDs and original rim/interior edges were involved and to
diagnose potentially incomplete patch plans. Keep 3D-contact evidence
separate from the definitive manifold validator. M14D is observation
only; any change to old R06 collision refusal behavior needs its own
explicitly tested admission-policy milestone. M14E concerns coherent
patch construction and consolidation, not general-purpose Boolean
removal of all 3D intersections.

## M14E implementation details (2026-10-23)

The final mixed closure now calls `planPrivateMixedClosurePatch` to
produce an explicit source-bound ledger of (a) modified original
FaceIds, (b) newly generated survivor children of conforming splits,
(c) new cap/zipper faces, (d) original-vertex position overrides, and
(e) reserved transaction-new VertexIds with their exact owner and XYZ.
Before writing final faces, this plan rejects duplicate/foreign
survivor handles, original edits omitted from the staged worklist,
unregistered vertex references, absent reserved new vertices and
foreign R03 source identity. A new survivor child has `stableId=0`
and `kind=Survivor`; it counts as a new face but must NOT be colored
as a cap or zipper. This distinction was reinforced by regression.

`applyPrivateMixedClosurePatch` applies the entire ledger to a
single COW view, assigning new permanent FaceIds, after which the
caller commits exactly once and validates the consolidated
`RegionTopologyIndex` with `validatePrivateClosedManifold`. A
failed patch or manifold audit cannot reach worker publication,
persistent history or checkpoints; there is no concurrent update
writer and no source revision rebasing. New vertex ID reservations
occur inside the same private COW view during refinement, before
the final ledger materialization; they are expressly recorded and
verified, never published alone.

The M14E standalone regression includes a real shared-edge split
with two changed survivor FaceIds plus two NEW survivor children,
missing override refusal, missing reserved vertex refusal,
missing generated face refusal by manifold audit, finite budget,
foreign source identity, immutable original and two intersecting
but individually manifold tetrahedral shells accepted by the
**topological** manifold validator.

**Outstanding policy distinction:** the selected legacy R06 spatial
front path still contains a strict post-closure 3D triangle-contact
rejection. M14E changes neither its behavior nor the historical
archived-case admission. It is a known mismatch with the newly
agreed best-effort self-intersection policy and should be assessed
under an explicit, independently tested admission-policy milestone;
it is not automatically resolved by the topological patch refactor.
