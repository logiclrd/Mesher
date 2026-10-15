# Mesher development tasks

These are independently completable milestones. Each must leave the existing
synchronous demos and the asynchronous interactive Inflate path working, preserve
exact 4D Step sampling and symmetric seam behavior, and pass GCC, Clang and MSVC
warnings-as-errors CI before merging. Historical README/DESIGN sections may
describe previous implementations; check source and recent sections first.

## Versioned, partitioned mesh architecture

**Nonnegotiable contract:** A published `MeshRevision` and everything reachable
through its partition graph are immutable. A `MutableMeshView` copies only the
partitions it edits; unchanged partitions keep **the same object pointers**.
Committing does not copy, enumerate or otherwise scan the whole mesh or
partition directory. Publication swaps a single version root. A reader may keep
an older revision while the worker edits or publishes a newer one.

- [x] **V1 — Immutable versioned partition store (first milestone).**
  Introduce `MeshRevision`, `MutableMeshView`, `MeshRepository` and a persistent
  16-way radix directory. First access for editing clones a partition;
  later accesses reuse that working copy. Commit copies only root-to-leaf
  paths of edited partitions (nine nodes for one 32-bit partition ID),
  shares every unchanged partition and returns the new revision independently
  of atomic publication. Test partition pointer identity, old-revision
  immutability, branching, deletion, no-op commits, commit copy counts,
  and concurrent readers. This is intentionally **not yet wired to sculpt**.
  Mutable partition references must not escape their owning transaction.
- [x] **V2 — Partition construction and stable element identities.**
  Added partitionFlatMesh, reconstructFlatMesh and validatePartitionedMesh.
  Monotonically allocated nonzero vertex/face IDs belong to a shared identity
  domain; branches, abandoned transactions and deletions never reuse IDs.
  Deterministic spatial Morton grouping builds bounded partitions (default
  at most 256 vertices AND 256 faces each). Faces reference vertex IDs across
  partitions. FlatMeshIndex retains original ordering only for lossless
  compatibility export; vertex bit patterns, faces, winding, and shared
  symmetric seam identities are regression-tested.
  Import/export full traversals are explicitly outside the commit path.
  Fast ID lookup, transactional topology changes, production rendering and
  exporting newly inserted/deleted topology remain future tasks (V3/V4/V8).
- [x] **V3 — Region edit transactions and boundary integrity.**
  Added RegionTopologyIndex and RegionEdit. A one-time O(N) immutable topology
  lookup discovers seed vertices, their incident faces, adjacent read-only
  faces and cross-partition references. RegionEdit pre-clones precisely the
  write halo; read-only halo partitions remain shared. Its scoped operations
  support moves, paired mirrored moves/seam constraints, existing mirrored
  face pairs, single/multiple face replacements, local inserts and deletes.
  Transaction validation checks local geometric degeneracy, missing vertices,
  all changed edge incidences/orientations and one-ring fan connectivity;
  only a fully validated revision can be returned for atomic publication.
  A compare-and-swap publication option rejects stale-base transactions.
  Explicit per-partition element caps prevent unbounded growth during local
  editing. Regression tests cover valid edge flips/face subdivision across
  partitions, invalid half-edits, retained old revisions and mirror/seam edits.
  **Deliberate limits:** V3 rebuilds its immutable topology lookup O(N) per
  revision (V4 makes it incremental), and mirrored split/collapse insertion
  and removal operations are reserved for V6/V7. Neither full snapshot
  rebuilding nor GPU dirty-region uploading is attempted here (V8/V9).
  Local validation does not prove collision freedom or global manifoldness.
- [x] **V4 — Versioned local spatial/connectivity indices.**
  RegionTopologyIndex now uses structurally shared immutable persistent AVL
  maps for stable vertex-ID lookup (including incident face links), face-ID
  lookup, oriented edge-face incidence, and spatial buckets. Each validated
  RegionEdit commit derives an updated index from just changed vertices/faces:
  it adjusts touched edge/vertex incidences and old/new spatial buckets, then
  path-copies O(log N) tree nodes per modified record. There is **no full mesh
  pass or overlay-chain walk** per revision. Each index owns its corresponding
  immutable MeshRevision, and IndexedMeshRepository publishes that pair with
  one atomic root compare-and-swap. Radius queries use indexed buckets plus
  exact geometric filtering; indexed ray picking matches the flat CPU picker
  (still O(all faces) until V10's BVH). A full O(N log N) index build remains
  necessary *once*, when importing an initially flat mesh. Tests compare
  incremental changes against independent rebuilds, assert bounded update
  work across 180 revisions, check cross-partition split/flip behavior,
  identity sharing, exact picking/spatial results, and concurrent readers.
  Existing sculpt/Vulkan paths remain unchanged until V5-V10.
- [x] **V5 — Local Inflate and per-stamp pre-edit baseline.**
  VersionedInflateBrush now evaluates maximum-of-areas influence, all unit
  face-normal sums and optional cotangent mean-curvature correction against
  an immutable indexed pre-stamp revision, gathering all movements before
  modifying any geometry and applying them through validated RegionEdit.
  Mirrored vertices move as paired transactions and shared X=0 seam vertices
  remain constrained to that plane. LocalStampBaseline retains the prior
  immutable index and lists only moved stable vertex IDs and incident faces;
  directional split/collapse code can query exact prior edge lengths and
  growth/shrinkage without a copied global position array. A no-hit stamp
  returns its original revision root.
  Existing AsyncSculptWorker still uses flat Mesh until V8, but its
  AdaptiveInflateBrush now skips the formerly unconditional whole-vertex
  pre-stamp copy when remeshing is disabled (the default), with per-stamp
  preStampCopiedVertices telemetry. Enabled legacy remeshing intentionally
  retains the full array baseline until V6/V7.
  Tests drive the same 4D-sampled stamps through original and versioned
  Inflate (including curvature), verify local edits and baseline semantics,
  mirrored updates, and legacy no-remesh behavior. Versioned sum order and
  enforced symmetry may differ by floating-point rounding, so compare within
  tight tolerances rather than bitwise identity. Interactive worker revision
  publication and dirty Vulkan uploads remain V8/V9.
- [ ] **V6 — Transactional local edge splitting and flipping.**
  Port `LocalRefiner` to stable IDs and partition-local topology changes.
  Validate one-ring and cross-boundary operations, preserved winding, mirrored
  pairs, and unchanged geometry outside the modified neighborhood.
- [ ] **V7 — Transactional local collapse without global compaction.**
  Port `DirectionalCollapser` to stable IDs/tombstones and validated local
  trials, removing global trial-mesh copies, index compaction and global
  adjacency rebuilds from normal local operation. Keep symmetric seam and
  manifold conditions; compare outcomes with prior regression fixtures.
- [ ] **V8 — Publish revisions from the asynchronous sculpt worker.**
  Replace full `Mesh` snapshot copies with atomically published immutable
  roots. Coalesce intermediate visual revisions but never skip Step stamps.
  Initially, adapt revisions to the existing whole-mesh viewport format if
  needed; make that remaining conversion cost explicit in diagnostics.
- [ ] **V9 — Dirty-partition Vulkan rendering.**
  Maintain renderable buffers per partition, rebuild/upload only changed
  partitions, and handle cross-partition normals/edges and GPU in-flight
  lifetimes correctly. Verify no stale or missing triangles during fast
  sculpt strokes, split/flip/collapse, resizing and shutdown.
- [ ] **V10 — Revision-consistent accelerated picking.**
  Introduce/update a triangle BVH or partition-local acceleration hierarchy;
  pick the same published revision being displayed. Quantify behavior during
  worker backlogs and retain exact mouse-path/stroke queue semantics.
- [ ] **V11 — Bounded reclamation, diagnostics and stress tests.**
  Add safe retirement/reclamation for expired CPU graphs without large
  UI-thread destructor pauses, and delay GPU destruction until work finishes.
  Instrument touched partitions, clone bytes, root-path copies, publication
  latency, retained revisions, retirement queue and memory use. Stress
  concurrent readers, rapid edits, long stalls, cancellation and shutdown.
- [ ] **V12 — Persistent history and durable checkpoints.**
  Build undo/redo and background serialization on immutable revisions while
  bounding history memory and defining recovery/GC policies. Never block
  local editing on a whole-mesh save.

### Acceptance and performance constraints

* Per-commit directory work is `O(D * H)` for `D` modified partitions and
  fixed radix depth `H=8`, plus at most one new root object. It is never
  `O(total partitions)` or `O(total vertices)`. A single edit copies nine
  directory nodes (root plus eight levels). This excludes the first-write
  cost of cloning each bounded-size data partition.
* Publishing a completed revision is an atomic pointer replacement. Readers
  may retain any older published version without blocking the sculpt worker.
* Future steps must bound partition size and mutation halo; editing an entire
  giant partition is not an acceptable substitute for local work.
* All changes preserve the current results unless a geometric algorithm
  alteration has been separately specified and tested. No false claims of
  self-intersection guarantees or vertex-colour preservation.
* V1 establishes storage semantics; V2 adds stable identities; V3 adds
  validated local edits; V4 adds persistent incremental indices; V5 adds
  local Inflate and immutable pre-stamp baselines. Transactional split/flip/
  collapse, worker integration, dirty Vulkan buffers, accelerated triangle
  picking, and durable undo remain explicit future milestones.

## Other remaining design goals

Keep brush radius independent of editing density, first-class vertex-colour
fidelity and UV-to-colour error budgets, local geometric validity/error limits,
import/export, painting tools, and large-model streaming as future requirements.
Do not silently simplify high-density scan detail as an optimization.
