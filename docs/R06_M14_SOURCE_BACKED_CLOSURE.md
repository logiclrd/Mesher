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
5. The editable footprint may grow through bounded discovery of nearby
   source geometry. A geometric coincidence is not an automatic welding
   instruction: classify authentic shared attachment vs unrelated
   crossing/overlap. If a safe conforming edit cannot be established,
   refuse without publication.
6. Commit exactly one private COW revision **after** complete quality,
   manifold, 3D-contact, mirror and finite-budget checks; maintain
   R01–R06 untrusted-preview vs certified-result distinction. Worker CAS,
   checkpoint and history remain unchanged.

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
- **M14D — Geometric encounter classification.** Link saved M13
  vertex-on-edge/crossing witnesses to provenance: existing/new vertex,
  actual R03 rim, original survivor face/edge, proposed patch face,
  prior split ancestry and mirror orbit. Deduplicate witnesses and
  distinguish intended attachment from incidental crossing.
- **M14E — Bounded geometric conformity recovery.** For proven
  intended attachment, propose an atomic conforming split/retessellation
  using M14C. For unrelated intersections, rework the cap front,
  expand R03 excision with material-side proof, or refuse. Do not weld
  independently indexed vertices by spatial tolerance.
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
are retained. Never certify from indexed manifoldness alone:
embedding/T-junction and 3D collision safety require their own checks.
