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
- [x] **V6 — Transactional local edge splitting and flipping.**
  Added VersionedLocalRefiner over V5's immutable LocalStampBaseline and
  V4's persistent stable-ID edge/face/vertex connectivity. Scan only edges
  in the prior stamp's affected faces; split only when a length exceeds
  the original nominal target AND grew from the immutable pre-stamp state.
  Each split replaces both incident triangles and inserts both new triangles
  in one validated RegionEdit; mirrored edge splits are one all-or-nothing
  transaction. Never rebuild or compact the entire mesh; new vertex/face IDs
  are permanent and the persistent indices advance by local topology deltas.
  A bounded-size overflow partition allocator handles initially full
  partitions without breaking the 256-element (or configured) capacity.
  Candidates for geometric flips come only from the split neighborhood.
  The original radial-orientation, normal, quality, and valence conditions
  remain in place; paired mirrored flips commit atomically. The incremental
  result lists changed stable face IDs, inserted vertex IDs, cloned partition
  and directory-path counts, and only newly inserted mirror correspondences.
  VersionedInflateBrush accepts these additional mirror pairs without
  copying the entire symmetry map before subsequent stamps.
  The new thirteenth CTest suite covers cross-partition splitting at a
  capacity of one, validated manifold orientation, persistent old revisions,
  exact mirrored new face winding and midpoint positions, multiple
  deformation/split/flip rounds, and paired-V5-stamp continuation.
  The legacy flat LocalRefiner remains untouched until V8's complete worker
  cutover; the flat collapsed-mesh refiner path and baseline remain in place
  until V7/V8. Stable-ID topology cannot be exported with the original V2
  FlatMeshIndex; the test adapter constructs full current flat geometry.
  These checks do not prove collision freedom or globally optimal geometry.
- [x] **V7 — Transactional local collapse without global compaction.**
  VersionedDirectionalCollapser consumes V5's immutable before-stamp index
  and checks only shortening edges from the stamp's affected-face halo.
  Stable-ID vertex and edge neighborhoods replace full-triangle scans for
  the exact old two-face link condition. A candidate is staged in one
  RegionEdit, including mirrored counterpart removal and seam preference;
  updated face orientations, triangle areas, outward winding and closed
  vertex fans are validated before publication. Accepted collapses remove
  only affected face/vertex records with O(log N) persistent index deltas,
  never copying a whole trial mesh, renumbering vertex/face IDs, compacting
  arrays, or rebuilding global adjacency. The version-scoped index retains
  immutable tombstone records for deleted IDs; earlier snapshots still
  contain original live geometry, and IDs remain permanently unallocated.
  Output tracks retired vertices/faces, rewritten faces, old-to-new
  partition/directory copy counts and newly deleted mirror pairs. Both
  VersionedInflateBrush and VersionedLocalRefiner consume these pair deltas
  without rebuilding their entire symmetry maps.
  Fourteenth CPU suite verifies no-op on stationary short edges, actual
  controlled collapse across partitions, closed genus-zero orientation,
  old-revision persistence, index agreement with independent full rebuild,
  literal tombstone records and nonreuse, legacy-compatible symmetrical
  shrinking/collapse, seam constraints, repeated local trials and next
  mirrored Inflate/refiner handoff. No full-mesh pass is required during
  each accepted operation. The live flat DirectionalCollapser and worker
  remain unchanged until V8; V11 will bound CPU/GPU retirement and retained
  history costs. This does not promise prevention of self-intersection.
- [x] **V8 — Publish revisions from the asynchronous sculpt worker.**
  The active AsyncSculptWorker now owns a VersionedSculptBrush that runs V5
  local Inflate, V6 directional split/flip and V7 local collapse over
  persistent immutable indexed revisions. BrushStroke still executes the
  original exact 4D Step queue and applies every queued sample (including
  mouse-down, stationary stamps and the release endpoint) in its own worker
  thread. The callback's Mesh is an empty sampler placeholder, never an
  editable full mesh. Each completed stamp atomically publishes just the
  final matching revision/index pair, never its intermediate topology;
  coalesced snapshots contain only shared pointers plus stamp counts.
  Old consumer-held revision roots remain valid while stamps proceed.
  New mirrored vertex pairs and retired pairs are handed off incrementally
  between Inflate/Refine/Collapse, and local copy statistics are reported.
  The existing Vulkan viewport still requires a full flat Mesh: V8 adapts
  **only consumed/coalesced** revision roots on the viewport thread, with
  explicit viewport_flatten_ms and viewport_flatten_bytes diagnostics.
  This remaining whole-mesh CPU conversion and GPU upload cost is V9 work;
  it may still cause visible frame stalls on very large meshes, but never
  forces the sculpt worker to duplicate geometry for every publication.
  Expanded async CPU tests compare exact stamp counts and geometry to the
  existing synchronous reference, check retained old revisions and no
  lost input with deliberately unread snapshots, exercise enabled
  split/flip/collapse, and verify initial ordering plus new stable-ID
  vertex/face additions in the viewport compatibility adapter. Desktop
  input and demo-mode behavior remain unchanged. V11 will address bounded
  graph retirement and memory telemetry under extreme stroke loads.
- [x] **V9 — Dirty-partition Vulkan rendering.**
  The interactive Vulkan renderer now owns one solid and one wire vertex
  buffer per face-owning partition. A CPU-side PartitionRenderCache builds
  each partition only once initially, then regenerates only dirty owners
  using the exact immutable revision from V8. Flat face normals read the
  referenced vertices across all partitions; shared wire edges are owned
  by the incident face with the smallest stable FaceId, never duplicated
  or omitted at partition boundaries. Each V5/V6/V7 stamp computes its
  affected before/after vertex/face halo and owner partition IDs without
  scanning all mesh partitions. The async worker unions dirty IDs both
  across pending stamps and across coalesced unconsumed snapshots, including
  the immediately applied mouse-down stamp.
  After waiting for the previous graphics-submission fence, Vulkan replaces
  only affected per-partition allocations and retires any deleted/empty
  partition buffers. Unchanged GPU buffer handles, CPU staging geometry and
  mesh partition identities remain intact. Swapchain resize still uses its
  existing device-idle path; shutdown waits for the device before destroying
  all retained partition buffers. Static floor and HUD geometry stay in
  their old dedicated buffers, and synchronous demos retain the original
  full-mesh renderer. Indexed picking uses the *displayed* revision for
  consistency, remaining linear until V10's accelerated hierarchy.
  V9 removes V8's full-mesh viewport conversion from the live path and
  reports dirty_partitions, dirty_faces, dirty_upload_bytes and
  mesh_upload_ms for each displayed revision. The fifteenth CPU CTest
  compares incremental partition geometry against an independent full
  rebuild, including local movement, split/collapse, globally unique
  cross-partition wire edges, retained unchanged buffer pointers and
  coalesced revisions. Interactive Vulkan window behavior itself is not
  GPU-integration-tested by headless CI; manual tests remain important.
  Draw submission still visits partition buffer handles per frame and
  uses one graphics fence, so V11 will refine retirement and large-scale
  lifetime/performance telemetry.
- [x] **V10 — Revision-consistent accelerated picking.**
  The RegionTopologyIndex now owns a structurally shared immutable two-level
  picking hierarchy: bounded triangle BVHs in each face-owning partition,
  below an eight-level 16-way persistent partition-ID directory whose
  nodes carry conservative ray-intersection bounding boxes. One-time
  import builds the hierarchy from existing face/vertex records. Every
  region edit gathers only owners of changed faces and the old/new incident
  faces of moved/added/deleted vertices; those partitions alone rebuild
  their BVHs, and only nine root-to-leaf directory nodes per affected
  partition are path-copied. Deleted/empty partition leaves are removed
  without recycling IDs; old roots and ray geometry remain immutable.
  Exact nearest positive double-sided ray-triangle intersection preserves
  the existing barycentric/determinant/distance tolerances, including
  edge and backface hits; box bounds expand conservatively to avoid
  false misses. Queries traverse the global and partition bounds and
  test only surviving candidate triangles instead of O(total faces).
  The existing viewport already pins the exact displayed revision for
  both drawing and ray selection: V10 switches that call to the BVH without
  modifying worker stamping, 4D Step queues, or backlogged stroke input.
  Picking diagnostics report boxes/partitions/triangles tested alongside
  picking_ms and pending input, exposing effective pruning interactively.
  The sixteenth CTest suite compares hits/misses and nearest intersection
  positions against independent exhaustive face traversal across camera
  rays, dense spheres, old retained revisions, divergent revision branches,
  cross-partition split/collapse, and no-op updates. It checks bounded
  touched owner counts, nine path-copy nodes per owner and absence of
  full-mesh walks in incremental picking updates. Initial import still
  traverses all faces once. Partition ID directory grouping is pragmatic
  rather than a globally optimal spatial hierarchy; large nonlinear
  deformations may weaken pruning but never change picking correctness.
  V11 still owns prolonged CPU/GPU reclamation and stress diagnostics.
- [x] **V11 — Bounded reclamation, diagnostics and stress tests.**
  Old viewport-held RegionTopologyIndex graphs and cache roots are handed to
  a dedicated RevisionRetirementQueue thread rather than dropping their last
  references inside the Vulkan update loop. The queue has a configurable
  hard capacity (64 roots in the viewport); saturation imposes explicit
  backpressure instead of allowing unbounded pending history. This is a
  deliberate bounded-memory/latency tradeoff: an overloaded retirement
  worker can still stall an enqueuing producer, and external undo/history
  readers can retain arbitrarily many roots until V12's history policy.
  The retirement worker drains on orderly shutdown and independent reader
  references remain valid until their actual last release.
  Superseded Vulkan vertex buffers enter a per-frame GPU retirement list
  instead of being destroyed while their old handles could be referenced by
  in-flight work. Draining happens after the existing graphics fence or
  final device idle, including resize and shutdown. Replacing a zero-length
  buffer also retires its old allocation rather than keeping invisible GPU
  memory. Diagnostics distinguish live/peak GPU allocation bytes, bytes
  queued for fence-safe retirement, cumulative freed bytes and fence wait
  duration. There is no new whole-mesh GPU walk per stamp.
  New CPU counters report live/peak immutable indexed roots, copied COW
  partition payload bytes, modified partition and directory node counts,
  actual coalesced publication latency and bounded retirement queue depth,
  peak and backpressure events. Clone bytes count copied vertex+face payload
  sizes, not allocator overhead or all retained persistent AVL/BVH objects;
  GPU resident accounting measures Vulkan allocation requirement sizes.
  The seventeenth CTest stresses 160 local indexed edits with a deliberately
  tiny retirement capacity, independent old readers, 60 concurrent
  publications/picks, no full traversals, a deliberately unconsumed
  asynchronous stroke, and shutdown with queued work while an immutable
  reader remains alive. GPU presentation/lifetime still requires physical
  Vulkan driver validation; headless CI can only compile that portion.
  Producer backpressure, GPU fence waits, shared external roots and
  asynchronous destructor cost remain surfaced, not hidden.
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
* V1 establishes storage semantics; V2 stable identities; V3 validated
  local edits; V4 persistent indices; V5 local Inflate and pre-stamp baselines;
  V6 transactional local splitting/flipping; V7 localized ID-preserving
  collapse with persistent tombstones; V8 publishes immutable roots from
  the active sculpt worker; V9 updates Vulkan face partitions locally.
  V10 keeps exact picking on a path-shared local triangle BVH.
  V11 bounds CPU retirement, adds fence-safe GPU retirement and measures
  live roots, clone payload bytes and publication latency. Durable revision
  history and checkpoints remain V12.

## Other remaining design goals

Keep brush radius independent of editing density, first-class vertex-colour
fidelity and UV-to-colour error budgets, local geometric validity/error limits,
import/export, painting tools, and large-model streaming as future requirements.
Do not silently simplify high-density scan detail as an optimization.
