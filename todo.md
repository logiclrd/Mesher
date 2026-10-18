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
- [x] **V12 — Persistent history and durable checkpoints.**
  A RevisionHistory retains immutable indexed roots directly with no full
  mesh copies, and records only dirty partition IDs and added/removed
  stable-ID symmetry pairs per successful Step. Undo/redo restores root,
  mirrored correspondences, exact vertex/face counts and V9 dirty updates
  together; a new edit after undo discards its redo branch. History retains
  at most 64 revision roots and a 32 MiB budget of accumulated COW-cloned
  vertex/face payload bytes (whichever limit is reached first), keeping
  the latest root even after an exceptionally large edit. Old evicted
  roots are passed to V11's bounded retirement thread. This estimate
  does not measure true process RSS, retained readers or shared tree nodes.
  Ctrl+Z undoes one geometry-changing Step; Ctrl+Y or Ctrl+Shift+Z redoes.
  They operate only between strokes and are serialized by the worker
  command queue, preserving the existing exact 4D sample semantics.
  A checksummed binary .mshr checkpoint records every live partition and
  stable vertex/face ID, identity allocator high-water marks, persistent
  deletion tombstones, the original remeshing reference vertex count,
  and symmetrical correspondence pairs. Reading a
  checkpoint rebuilds immutable connectivity, spatial and picking indices
  once at startup; it validates record limits and rejects truncated,
  corrupt or unsupported files. Subsequent new IDs cannot collide with
  deleted, rolled-back or abandoned historical IDs. Background
  AsyncCheckpointWriter holds an immutable snapshot and writes the entire
  mesh via a temporary file, flushes, then replaces the destination,
  coalescing multiple pending save requests to the latest revision.
  The sculpt worker never walks or serializes the entire mesh on Ctrl+S.
  --checkpoint FILE names the save target (default mesher-checkpoint.mshr),
  while --recover FILE resumes the active worker from recovered topology
  and symmetry rather than generating a new sphere. A failed checkpoint
  preserves the last complete destination and reports errors in checkpoint
  diagnostics, not as sculpt worker failure. Previous in-memory history
  is not serialized: a restarted session begins a fresh bounded history
  from the checkpoint. On POSIX the checkpoint file and containing
  directory are synced; Windows calls FlushFileBuffers on the complete
  temporary file before replacement. Rename durability and sudden-power-loss
  guarantees remain filesystem/platform specific.
  The eighteenth CPU suite checks history truncation/branching, stable
  mirror reversal, checkpoint replacement/corruption/truncation,
  allocator/tombstone round-trips, asynchronous coalesced writes and
  recovery of exact ray hits. Extended worker tests cover actual
  undo/redo, real Ctrl+S-style save, restart and a further 4D Step.
  GPU presentation and native input shortcuts still warrant device tests.

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
  live roots, clone payload bytes and publication latency. V12 adds bounded
  root history, reversible mirror deltas, asynchronous stable-ID checkpointing
  and full recovery. Further painting/import/render ergonomics remain open.


## Self-intersection-aware sculpting (proposed V13–V19)

**Design:** [Sculpt collision and topology repair](docs/SCULPT_COLLISION_REPAIR.md).
The agreed model is positive winding (`w > 0`) on the **complete
post-deformation candidate revision**, not on the pre-stamp revision and not
the odd/even or generic nonzero winding rule. A two-sheet crossing removes
the buried parts of **both original smooth surfaces**; density-aware
ladder/zipper seams connect the surviving boundary contours, not an
arbitrary bridge across the enclosed red region. See the design note for
the exact upper/lower red-arc correspondence in the reference drawing.
The live V12 path remains unchanged until a later integration milestone.

Each task below is independently testable/mergeable, with no incomplete
topology published, no dropped 4D Steps, old roots immutable, stable IDs
never reused, symmetry seam shared, brush radius/density independent,
existing tests preserved, and GCC/Clang/MSVC warnings-as-errors CI.

- [x] **V13 — Immutable post-state triangle proximity/intersection queries.**
  Extend revision-specific partition BVHs with triangle-AABB candidate
  queries and robust triangle-triangle static overlap tests. Given a
  private proposed revision plus changed face IDs, find changed/changed
  and changed/unchanged intersections within the *new* revision, including
  topologically distant partitions and simultaneous symmetric movement.
  Deduplicate pair IDs; distinguish legitimate adjacent contacts from
  improper overlaps and cover coplanar/tangent/near-degenerate cases.
  Compare against exhaustive test fixtures. CPU-only API/tests, no live
  geometry modification; assert bounded normal-case index work and retain
  previous revision pointer identity.
  Implemented as `RegionTopologyIndex::triangleIntersections()` with
  immutable revision-specific BVH AABB traversal, static SAT/coplanar
  contact checks, stable-ID pair canonicalization and ambiguity reporting.
  A dedicated nineteenth CTest checks shared-feature adjacency,
  overlapping/tangent faces, simultaneous mirrored closed-solid collision,
  exhaustive face-pair comparison, index pruning, and old-root retention.
  Detection is an opt-in CPU API only; the brush/worker does not invoke it
  yet. Numerical predicates are conservative double-precision tests, not
  exact arithmetic or a claim of complete adversarial robustness.
- [x] **V14 — Positive-winding solid and fragment classification.**
  Add reference-accurate signed winding/occupancy evaluation on whole
  *candidate* geometry and accelerated local queries; verify 0 vs 1,
  1 vs 2, and negative-winding regions. Classify exposed vs buried
  portions on both sides of crossing triangles without assuming each
  whole triangle has one classification. Identify fully redundant
  isolated internal components; explicitly flag ambiguous intentional
  cavities instead of silently deleting them. Unit-test both-sheets-
  moved, X=0 symmetry, original smooth surface continuation and
  uncertainty handling. No destructive topology changes.
  Implemented via `positiveWindingAt()` (two agreeing signed
  ray-crossing counts over revision-specific persistent BVHs, with a
  rare full-mesh solid-angle reference fallback), independently checked
  by `referenceWindingAt()`. `classifyFaceFragments()` reports
  bounded-depth barycentric *sample patches*, including separately
  classified exposed/buried portions of the same original triangle;
  they are NOT exact intersection-trimmed fragments or permission for
  destructive face deletion. `classifyClosedComponent()` walks a
  bounded closed connected component, rejects intersections/ambiguous
  samples, labels redundant closed shells, and reports enclosed cavity
  boundaries as policy-dependent. The twentieth CPU CTest exercises
  winding 0/1/2/-1, simultaneous symmetric movement, old/new revision
  immutability, mixed patches, nested bubble/cavity classification,
  on-surface uncertainty and BVH pruning. No live worker behavior
  changes. V15 still owns actual cut and seam planning.
- [x] **V15 — Local approximate excision and boundary pairing plan.**
  Given V13/14 outputs, generate a private plan selecting removable faces
  and surviving contours at current **local** density (not brush-radius
  density); preserve as many existing vertices and fine details as
  possible. Pair matching boundaries, orientations and seam sides for
  simple and multi-loop intersections; handle/diagnose ambiguous or
  unpairable junctions. Bound estimated geometric deviation, size and
  work, checking the '~2 pixels' intuition as a conditional target, not
  a guarantee. Tests inspect plan geometry/IDs; do not publish repairs.
  Implemented as two read-only APIs: `planApproximateExcision()` grows
  bounded tentative hidden-face sets from V13 post-state collisions and
  V14 barycentric winding samples (plus optional isolated component
  seeds); `planRemovalContours()` traces retained-face-directed closed
  boundary loops and computes density-scaled globally consistent
  zipper pairings. It rejects branching/open boundaries, ambiguous
  samples, excessive loop gaps, equivalent matchings and processing
  beyond explicit budgets. The reported face-edge maximum and seam
  gaps are quality indicators, NOT rigorous Hausdorff error bounds;
  sampled mostly-hidden triangles may include visible portions.
  `Ready` is a proposal only: V16 must validate actual surgery.
  A twenty-first CPU CTest covers two crossing seams, multiple local
  seam pairs, greedy-matching counterexample, complete redundant
  bubble without a seam, cavity preservation and conservative failures.
  No live worker changes or published repairs.
- [x] **V15.1 — Topology-aware cap-vs-zipper selection (read-only).**
  Closing a torus handle and merging two colliding walls present the
  same pair of directed rings but require different connections.
  Instead of categorizing collision types, compare the *estimated
  areas* of constructing two separate caps versus a zipper. For
  approximately planar, compact and coaxial rings compute polygon
  enclosed areas A_i, area-weighted centroids C_i, effective radii
  r_i=sqrt(A_i/pi), d=|C_1-C_2|. Estimate
  A_caps=A_1+A_2 and A_zip=pi*(r_1+r_2)*hypot(d,r_1-r_2);
  prefer smaller total area by a meaningful relative margin. No
  minimum-radius clamp or tuned dimensioned score.
  Candidate zipper quality still uses independently resampled
  *absolute arc length* (cyclic offsets/reversal, max and mean
  correspondence distances), never matching original vertex indices.
  A bounded global DP decides which rings to zip and which to cap,
  including odd ring counts. Flag near-ties, nonplanar/distorted,
  unsupported off-axis and over-limit cases as ambiguous instead of
  guessing. Explicit closed redundant bubbles still require no seam.
  Add torus pinch (cap destroys handle) and nonpinched tunnel (zip
  preserves handle), overlapping walls (zip), small distant rings
  (caps), unequal radii, uneven vertex spacing and ambiguity fixtures.
  This is *advisory topology planning only*; the V16 actual cap/zip
  surfaces may differ and MUST be validated independently.
- [x] **V16a — Private transactional zipper/cap surgery on non-symmetric regions.**
  Implemented `applyRepairSurgery()` on a private, complete immutable
  candidate revision. Refuses stale/incompatible V15.1 plans; removes
  stable face IDs, retires fully orphaned old vertices, preserves
  surviving positions/identities, and returns a committed but UNPUBLISHED
  `RegionTopologyIndex` and `RevisionCommit`. Caller must perform
  an optimistic `publishIfCurrent()` separately; exceptions publish
  nothing. Supports planar ear-clipped independent caps and dynamic-
  programming zipper strips with physically measured, independent
  arc-length advance. Phase lag between fronts is bounded to avoid
  twisting/crossing triangles. Long boundary edges are subdivided
  according to local median edge lengths; each adjacent surviving
  triangle is split within the same RegionEdit transaction to avoid
  T-junctions. Quality/fan/budget checks and RegionEdit's oriented
  two-manifold and vertex-link invariants precede private commit; V13
  rechecks triangles affected by newly created scar geometry after
  commit and before return. 23rd CPU suite tests caps, separated
  surface merge, unequal-density boundary subdivision, rollback,
  stable IDs and atomic repository publication. An actual pinched-
  torus closure additionally verifies Euler characteristic 0 -> 2.
  Fixed V13's tolerance-expanded coplanar edge clipping, which
  previously mistook lawful shared-vertex contact for penetration.
- [x] **V16b — Atomic mirrored caps/zippers, shared X=0 seam, and stronger validation.**
  RegionEdit now provides explicit reflected vertex/face addition, mirrored
  replacement/removal, and exactly one self-mirrored X=0 seam vertex ID.
  Paired-operation errors poison the private transaction rather than
  risking publication of a half-reflected edit; commit checks mirror
  mapping, symmetric vertex positions, face existence and reversed winding.
  `applyMirroredRepairSurgery()` checks full vertex involution and
  face/contour/closure reflection, refuses incomplete or ambiguous
  symmetry, and builds mirrored cap pairs or matching zipper pairs.
  For two rings reflecting into each other, the zipper is constructed
  on BOTH sides of a SINGLE shared X=0 seam ring; no coincident duplicate
  sheets. Long mirrored contour edges are subdivided in reflected pairs
  together with both adjacent retained triangles, without T-junctions
  or unpaired new vertices. Entire reflected isolated components can
  be retired atomically. Both mirrored and ordinary private surgery
  now verify all nearby surviving faces against the complete post-state
  collision BVH and enforce bounded one-sided scar-to-excised-surface
  deviation in *local* edge-length units. Over-budget or ambiguous
  geometry fails privately. Tests cover reflected caps, mirrored zipper,
  unequal edge density, x=0 unique seam IDs, mirrored full retirement,
  negative mirror maps, local drift rejection, and multiple sequential
  repairs preserving older immutable roots.
- [x] **V16c1 — Self-reflected cap, multi-edge survivor splitting and conditional reverse fidelity.**
  A single ring which reflects onto itself is now cappable when its
  directed boundary has exactly two existing X=0 seam vertices and one
  uninterrupted positive-X arc. One half is triangulated as a disk
  against the chord joining the two shared seam vertices; its other
  half is constructed by reflected face pairs, never duplicate
  coincident seam sheets. The old self-mirrored deleted pole is
  retired exactly once, with coherent historical revision and
  manifold checks. A slightly nonplanar symmetric contour is also
  covered. For ordinary AND mirrored repairs, multiple boundary
  edges belonging to one original surviving triangle are now
  subdivided together in one transaction; the original face ID is
  retained and all additional subtriangles remain properly oriented.
  The pure two-/three-edge routine is independently tested for
  winding, area and edge incidence. Added opt-in
  `requireReverseSurfaceFidelity` and
  `maximumReverseDeviationInEdges`: for explicitly feature-bearing
  excisions only, also check original triangle vertices/centroids
  against the final local retained/new surface in local-edge units.
  This reverse guard is NOT automatically enabled for intentionally
  buried red faces, whose deletion correctly destroys their surface.
  Both directions remain sampled estimates, not certified Hausdorff
  distances. All committed surgery continues to be private.
- [x] **V16c2a — Isolated warped/concave caps and explicit X=0 preflight.**
  A single cut ring cannot select a zipper: it has no partner, so the
  frustum model's strict planarity/compactness assumptions are unnecessary.
  Use a SEPARATE conservative `maximumSingleCapPlanarityInRadii=0.75`
  and `maximumSingleCapIsoperimetricRatio=3.0` for this case only.
  Actual ear-clipped cap faces still require simple projected geometry,
  positive nondegenerate oriented triangles, valid manifold links,
  quantitative surface deviation, and residual-collision checks on the
  private revision. Non-adjacent projected contour edges are tested
  before ear clipping; a crossing/overlap refuses the repair.
  A concave prism opening and a substantially nonplanar saddle-shaped
  ring now close correctly; stronger warping and even mildly warped
  MULTI-ring topology choices remain safely ambiguous.
  Added deterministic pure symmetry-plane classification and an early
  mirrored-repair preflight: straddling edges without X=0 vertex IDs,
  existing boundary edges lying in the plane, near-zero-but-not-exact
  plane positions and self-reflected loops with more than two plane
  contacts are explicitly rejected rather than inventing duplicate
  seam vertices or unsafe orientation. Tests cover these cases.
- [x] **V16c2b1 — Conforming X=0 edge normalization for self-reflected incident triangles.**
  Mirrored meshes may contain edges whose endpoints reflect into each
  other, and whose two incident triangles each reflect onto THEMSELVES.
  The old paired-face RegionEdit interface cannot operate on such a
  face: there is no distinct pre-existing mirrored partner ID.
  Added `RegionEdit::splitSelfMirroredFace()`: given an exact new
  self-mirrored X=0 midpoint on the crossing edge, retain the old
  triangle's stable FaceId for one oriented half, and introduce a
  fresh FaceId for its opposite reflected half. Record them as an
  ordinary mirror pair for local edge/vertex-fan validation.
  `normalizeSelfMirroredPlaneEdge()` normalizes BOTH incident
  self-reflected triangles in the SAME private RegionEdit transaction
  using ONE shared seam vertex, checks complete proposed-revision
  triangle intersections, and returns the still-unpublished revision
  for optimistic publication. It refuses incomplete mirror maps,
  non-manifold edges, wrong midpoint coordinates, and inappropriate
  asymmetric face topology. Tetrahedral symmetry fixtures verify
  one seam ID, a closed four-triangle fan, Euler invariance, existing
  FaceIds, immutable old snapshots and atomic compare-and-swap
  publication. No live or autonomous recovery behavior was enabled.
- [x] **V16c2b2 — Private crossing normalization, re-planning and closure.**
  `repairMirroredContoursAcrossPlane(original,originalPlan,mirrors,...)`
  validates the ORIGINAL immutable plan and mirror involution, detects
  exactly mirrored X=0 crossings of its directed removal boundaries,
  and, within explicit work limits, applies the V16c2b1 conforming
  two-face normalizer to EACH eligible boundary-crossing edge. It
  advances through unpublished immutable revisions, extends the
  excision set whenever a split original deleted face gains a new
  reflected half FaceId, and inserts one self-mirrored ID per crossing
  into the mirror map. It re-runs V15.1 closure planning on the fully
  normalized revision and checks original contour order, cap/zipper
  topology and partnering are preserved. Only then does it use the
  validated mirrored cap/zipper surgery against that new plan.
  All intermediate revisions remain private: the caller may publish
  ONLY the final index with `publishIfCurrent(original,index)`,
  producing one atomic user Step rather than one Step per split.
  Budgets include normalization vertex/face work; unsupported
  nonmirror crossing edges, more than two combined seam contacts,
  ambiguous re-plans and asymmetric excision fail closed.
  Regression fixtures: one-crossing triangular rim and two-crossing
  four-sided symmetric rim, stable FaceIds and newly deleted split
  faces, closed manifold, immutable old root, one final CAS,
  exhausted budget and forged-plan refusal.
- [x] **V16c2b3 — Exact reflected-edge and cap-plane topology decisions.**
  A crossing edge and its DISTINCT reflected edge meet at exactly
  the SAME world-space point of X=0. Unless they are the SAME original
  topological edge orbit (the V16c2b1/b2 case), the input already has
  non-adjacent crossing edges; mirroring two independent seam midpoints
  or welding their branches would conceal the intersection or make a
  nonmanifold vertex. The normalize/re-plan path now distinguishes
  self-reflected, distinct-reflected and unmapped crossing-edge
  orbits, verifies that a distinct reflected edge exists and refuses
  it with an explicit pre-existing-intersection diagnostic, before
  changing any private geometry. An embedded reflection-symmetric
  cap DISK has one nonbranching fixed arc, with exactly two
  TRANSVERSE contacts of its boundary with X=0. A contour tangent
  to X=0 is not an extra transverse crossing; >2 transverse
  contacts on one ring cannot be resolved by blindly pairing
  chords and remain fail-closed. Pure geometry regressions check
  orbit identities, exact coincident plane-crossing points,
  crossing-vs-tangent counts, and one-versus-four contact topology.
  A concrete reflected-tetrahedron pair demonstrates safe refusal
  without changing the old root. A positive two-component fixture
  confirms that FOUR total contacts distributed across TWO
  independent, valid rings still undergo atomic normalize/re-plan/
  cap surgery, preserving Euler characteristic and original IDs.
- [x] **V16d1 — Sampled surface provenance and bounded exposed-feature reverse fidelity.**
  Reuse the V14 original-revision fragment classifier to count
  `Exposed`, `ReversedBoundary`, `Buried`, `Outside` and `Ambiguous`
  patches of each proposed removed face. Expose a read-only
  `inspectRemovalSurfaceProvenance()` with bounded total fragment
  work; the counts describe SAMPLES, not certified geometric
  partitions or trustworthy permission to delete a face.
  Add an explicit `requireExposedFragmentFidelity` option distinct
  from the earlier all-faces `requireReverseSurfaceFidelity` mode:
  sample ORIGINAL barycentric patch centroids from exposed and reversed
  boundary samples only, verify distance to repaired+surviving local
  triangles at original local mesh scale, and allow buried/outside
  fragments to disappear. Any ambiguous fragment refuses privately;
  mutually exclusive modes refuse contradictory policy. Hard bounds
  apply to classification samples and total nearest-triangle
  evaluations; return sample counts, geometric distance and actual
  work. The default mode remains unchanged and opt-in; no implicit
  assumption that all `planRemovalContours` deletion requests have
  sound winding provenance. Tests compare a precisely reconstructed
  exposed cube cap against a redundant fully buried inner shell,
  cover invalid policy/work limits and preserved historical roots.
  Stress eight independent near-coplanar cube cap repairs within
  a single private operation; check contour pairing, manifold Euler
  characteristic, V13 residual collisions and bounded refusal.
- [x] **V16d2a — Explicit classification-aware repair policy and mixed/long-history stress.**
  Introduce `applyClassifiedExcisionRepair()`: the non-symmetric,
  private wrapper calls V13/V14/V15 `planApproximateExcision` on the
  CURRENT immutable candidate, never trusts a caller's low-level
  `planRemovalContours` deletion set, and requires the caller to
  specify `ClassifiedFeaturePolicy::{TopologyOnly,
  PreserveSampledExposedFeatures}`. The preservation variant sets
  V16d1's optional exposed/reversed-fragment proximity guard, not
  an automatic two-sided Hausdorff constraint. Return the original
  planner's NoChanges/Ambiguous/Budget status with NO new revision if
  not Ready; even a Ready candidate must complete normal transactional
  topology, collision and surface-drift validation. No publication.
  Conflicting manual fidelity flags are explicitly rejected; mirrored
  workers still require a separate proven symmetric caller path.
  Regression from V14 overlapping cubes identifies ONE original
  triangle with both exposed and buried fragments and checks per-face
  fragment-work limits. Classification-aware nested-shell tests
  distinguish no-op vs explicitly redundant component excision and
  verify both selectable fidelity policies. Additional stress runs
  five sequential cap repairs while keeping all historic immutable
  revisions alive, rejects stale original FaceIds at each step, and
  exercises near-coplanar/tangent/intersecting multi-shell proposals.
  Full cross-platform CPU/benchmark CI required before merge.
- [x] **V16d2b — Original-surface witness audit and repeated deep-contact stress.**
  Add read-only `sampleSurfaceWitnesses(original,faceIds,maxWitnesses)`:
  seven world-space witness points per original triangle (vertices,
  edge midpoints and centroid). Those points remain immutable even
  after original FaceIds are retired in later revisions. Add
  `auditSurfaceWitnesses(current,originalWitnesses,candidateFaceIds,
  originalEdgeLength,maxDistanceTests)`: measure each early-revision
  witness against its closest triangle on an EXPLICIT later surface
  patch and report maximum, mean and RMS distances normalized by
  original local edge length. Work is limited by a checked
  witness-count x unique-candidate-face product; nonfinite data,
  invalid scales, retired candidate FaceIds, and empty targets
  fail closed. This is a DISCRETE cross-revision erosion witness,
  never a continuous Hausdorff bound or an autonomous constraint
  on sculpt-worker publication.
  Regress thirty-two successive cap replacements of the SAME
  exterior patch, each with a new set of stable FaceIds, checking
  original-anchored zero drift, closed genus-zero topology,
  old-root stability, atomic per-step publication, all held
  33 historical immutable generations, and hard test budgets.
  A wrong opposite-face audit deliberately produces a nonzero
  erosion signal, detecting incorrect surface selection. Stress
  several configurations of deeply intersecting offset shells
  through V13/V14/V15 candidate planning; unsupported folded
  topology refuses privately and accepted scars must pass
  post-state collision verification.
- [x] **V16 — Transactional density-aware zipper/cap surgery.**
  Implement a RegionEdit-based private operation consuming a V15 plan:
  remove red/buried faces, generate either selected independently
  triangulated caps or an oriented watertight density-aware zipper,
  handling mismatched vertex counts and local edge lengths. Zippers
  MUST advance by physical arc length (not vertex-index parity);
  subdivide long edges where necessary and simultaneously retriangulate
  their surviving adjacent faces to avoid T-junctions. Reject extreme
  triangle shapes and high-degree hubs, preserve high-resolution detail,
  and avoid requiring a global minimum-radius regularizer. Verify
  cap/zipper topology for handle removal vs preserved handles,
  retire IDs/tombstones, and update mirrored pairs + single shared X=0
  seam atomically. **Qualified V16 final acceptance:** the tested PRIVATE supported subset
  and non-destructive refusal boundary are recorded in
  [docs/V16_FINAL_ACCEPTANCE.md](docs/V16_FINAL_ACCEPTANCE.md).
  This does NOT activate live geometry merging. Tested geometry currently covers
  selected capped/zippered manifold repairs but not arbitrary
  deeply folded multicomponent surface retopology. The new
  original-revision witness audit detects sampled cumulative
  erosion, not a guaranteed continuous two-sided Hausdorff
  envelope. Live integration must choose a policy for ambiguous
  winding, source-feature retention, overlapping strokes and
  preserved symmetrical topology, account for all committed
  partitions/IDs, and keep one compare-and-swap per input Step.
  Distinct reflected crossing edges remain pre-existing
  intersections, not independent seam midpoints. V17 owns
  live brush integration and its repeated-contact tests. V17 must
  bound exceptional full-mesh winding fallbacks and pathological
  collision discovery, recover rejected Steps without stopping the
  worker or dropping queued input, stage mirror/auxiliary state until
  final publication, and check the full changed candidate for residual
  collisions beyond just the newly constructed scar.

## V17 — Flat remaining milestones (authoritative rebaseline)

The old seven-level V17 task genealogy has been **retired as a planning hierarchy**, not discarded. Its complete original text, historical checkbox status, regressions, caveats and decisions are archived verbatim in [docs/V17_LEGACY_TASK_TREE.md](docs/V17_LEGACY_TASK_TREE.md). The authoritative detailed acceptance criteria and historical-ID traceability matrix are in [docs/V17_MILESTONES.md](docs/V17_MILESTONES.md); DESIGN.md preserves the chronological engineering record. The implementation through PR #50 (`28c419f`) is preserved and does not need to be reimplemented.

**October 18 scope rebaseline:** M06 exposed independent c3 singleton contour, d2 disk/weld and original-root worker/history gates. The complete former M07 objective is retained as flat M07–M09; former M08–M13 objectives are renumbered M10–M15 without loss. Historical task tree and append-only DESIGN retain old identifiers.\n\nEach item below is a **standalone mergeable milestone** with its own positive/negative acceptance. No child numbering (M01a, M01b1, etc.). Dependencies permit later integration but never require subdividing this list. **M01–M09 are complete in their independently accepted private cut/c3/join and terminal original-root worker scopes; M10–M16 and V17 as a whole remain open.** Newly added flat peer M16 is scheduled after M10 and **before M11**, without renumbering accepted existing IDs. See [coincident-vertex contract](docs/COINCIDENT_VERTEX_TOPOLOGY.md).

- [x] **V17-M01 — Canonical source-proven multi-sheet/triple-plane junction identities.**
  Construct original-ordered, finite-V13-verified three-plane points
  from THREE distinct source-face IDs and THREE original pair segments.
  Each source face gets its OWN cut point index but the exact SAME 3D
  position, with nondegenerate oriented fan/area and c1/c2 verified.
  Complete 12- and 16-contact closed-tetra fixtures now independently
  pass canonical-point source orbit tests, order reversal and hard
  triple-enumeration budget refusal. The 12-contact fixture can now
  ALSO pass existing strict d2/d3 full union without changing the
  joiner; that is additional observed coverage, not M04 completion.
  Caller contact/section journal ordering is preserved after
  deterministic source-pair construction. Unsupported graze, uncertain
  endpoint, nontransverse and near-parallel cases still refuse.
  NO automatic worker/desktop Merge geometry activation.
- [x] **V17-M02 — Exact surviving-seam interval matching across unequal subdivisions.**
  Compare original V13 source-paired seam vertices by exact 3D coordinates,
  not independently rounded scalar projections. An explicit
  `reconcileCertifiedSeamSubdivisions` private preflight sweeps directed
  c3 survivor intervals, accepts truly matched buried gaps, refuses
  one-sided or overlapping ownership and stages additional split
  vertices at precisely existing OPPOSITE-SHEET V13 boundary points
  when the two sheets have unequal edge subdivision. It splits BOTH
  incident triangles per affected source edge in one unpublished
  RegionEdit, preserves every original VertexId position, verifies
  oriented area/fans, independently redoes c1/c2/c3 on all new child
  FaceIds, and then proves exact endpoint pairing, opposite direction,
  unique original contact ownership and complete source boundary
  coverage. Hard work/inserted vertex/face limits fail closed.
  A full 12-contact THREE-shell fixture with deliberately unequal
  REAL private child edge density is positive through refinement
  and existing exact d2 seam join; no-op, missing original contacts,
  mirrored-new-split refusal, hard budgets and historical roots are
  checked. This is a self-contained PRIVATE adapter: M04 still owns
  integrating the additional COW edit into ONE original-root net
  Step/history journal; M05 owns mirrored NEW refinement orbits.
  Production V15 thresholds, protected coordinates and desktop
  Merge geometry remain unchanged.
- [x] **V17-M03 — Safe retirement authorization for wholly buried uncut original faces.**
  Added `planUncutFaceRetirement`, a strict READ-ONLY independent
  original-source connected-component/whole-face classifier. It recomputes
  c2 COMPLETE original V13 coverage and c1 actual child occupancy,
  derives child removal/protection directly from positive signed winding
  (not caller-supplied V15 contours), traverses the FULL bounded original
  face-connected uncut patch around the genuine cut sources, proves no
  unreported V13 contact on EACH original candidate face and checks
  unchanged source FaceIds, owners and exact vertex positions. Four
  interior points at two signed offsets on each side must agree on
  homogeneous unit winding, with NO full-mesh fallback. Buried/Outside
  originals are authorized; Exposed/ReversedBoundary stay protected.
  The original and c1/c2/c3 child sets combine into one sorted FaceId
  retirement plan, subject to a fresh V15 contour quality check.
  Independent aggregate collision, adjacency, winding and face budgets
  refuse without leaking partial authorization. A real 1-above/3-below
  two-tetra fixture authorizes its previously uncut buried base;
  missing V13 pairs, strict work caps and a disconnected inverted
  internal cavity are negative controls. No actual topology deletion
  or one-root d3 publication is performed (M04), and new mirror
  retirement orbits remain fail-closed (M05/M06).
- [x] **V17-M04 — Full closed three-body Boolean union with one original-root terminal Step.**
  Compose the canonical M01 triple-plane source-point cut, the M02
  opposite-sheet exact section subdivision alignment and the M03
  independently classified untouched buried-original FaceId retirement
  into a single original-root immutable Step. A private extra M02
  RegionEdit, when required, remains invisible and its new FaceId/
  VertexId allocations and COW costs enter only the NET composed
  history/dirty/retirement journal. The M02 source interval preflight
  now reasons about the same M03-authorized removal boundary as d2:
  an uncut buried face must not manufacture a false unmatched seam.
  The final d2 joins only exact source-paired opposite section edges;
  it retires both genuine child fragments and authorized unchanged
  faces and protects exposed and reversed cavity boundaries. The
  complete closed THREE tetrahedron fixtures with 12 generic and 16
  orthogonal proper transverse original V13 pairs both pass one
  connected edge/vertex-manifold, final changed-halo and whole-final
  surface collision audit, original-source coordinate fidelity and
  one original-root CAS, balanced permanent ID/dirty journal, ONE
  undo/redo history transition and durable checkpoint/restore.
  The one-above/three-below tetrahedron additionally deletes its
  independently authorized uncut buried base while preserving a
  separate reversed cavity shell. Source pair omission, insufficient
  net-ID/refinement budgets, unsupported historical 21-pair grazing
  contact cases and stale original-root CAS fail closed with no
  publishable cut or intermediate root. Mirrored triple-region
  refinement, X=0 fixed-plane seams and automatic sculpt integration
  remain gated by M05, M06 and M11 respectively. Default desktop
  Merge geometry remains disabled; no universal Boolean claim.
- [x] **V17-M05 — Off-plane mirrored multi-section Boolean acceptance.**
  Complete 24-source-contact GENERIC and 32-contact ORTHOGONAL
  reflected triple-tetrahedron groups pass genuine finite V13 contact
  discovery and close both sides into exactly TWO disconnected,
  oriented, edge/vertex-manifold terminal unions, in ONE original-root
  private Step. The mirrored planar arrangement now recognizes real
  source-shared endpoint POINT IDs rather than rejecting harmless
  t=u=1+epsilon in a reflected projection; distinct near-coincident
  endpoints STILL refuse. Each new or historic vertex has an exact
  reciprocal off-plane mirror orbit, each surviving child FaceId has
  exactly one reverse-winding reflected counterpart, and no unrelated
  overlapping source sheets are welded by world-coordinate proximity.
  M03 untouched-original authorization now requires a bounded
  ORIGINAL reciprocal three-vertex FaceId map for BOTH removable and
  protected faces; paired one-above/three-below buried bases retire
  while both independently enclosed reversed cavities survive.
  The original-root terminal join includes that same certified
  paired untouched-source retirement and verifies protected original
  faces even when no additional deletion is eligible. Hard M03
  face-component budgets, missing original contact, missing or
  noninvolutive/wrong mirror map, unpaired child source face orbit,
  zero mirrored pair work and net-ID budget refuse with NO final
  root, history or mirror journal. Tests re-audit ALL surviving final
  faces for intersections, all NEW/HISTORICAL mirror point/face
  involutions, one CAS/stale replay, one undo/redo transition and
  checkpoint tombstones/orbits. A complete 24-contact mirrored
  union is also exercised through the queued async worker,
  checkpoint reload and synchronized three-controller mirror maps.
  Support remains STRICTLY OFF X=0 (M06); optional unequal-density
  mirrored NEW extra-refinement split staging that lacks proven
  paired COW orbits remains fail-closed (M10 supported-domain
  boundary), not silently replaced by nearest-point matching.
  Automatic brush-triggered merging and the desktop toggle remain off.
- [x] **V17-M06 — Independently certified X=0 source-cut and invariant child-fan topology.**
  A full two-closed-tetra fixture with THREE truly transverse original
  V13 source pairs (the initial tangent vertex was deliberately moved
  away from the supporting plane) now produces a complete privately
  committed, locally manifold conforming cut and c2 source coverage.
  Both true mirror-paired off-plane points AND single new permanent
  self-mirrored X=0 source-owned VertexIds are included, with exact
  original corner coordinates, true original FaceId provenance, and
  complete distinct/singleton reflected reverse-winding child FaceId
  orbits. The mirrored arrangement recognizes canonical source-proven
  X=0 fixed points, correcting ONLY an independently computed 5.55e-17
  residual X after proving the ORIGINAL invariant edge/face orbit.
  It constructs each invariant source triangle's positive half-disk
  once and reflects the full fan, using RegionEdit's one-ID seam
  vertex, paired/singleton child allocation, full local fan/edge
  validation and immutable COW commit. Missing original contacts
  refuse independent c2 certification; incomplete source mirror
  involutions and zero mirrored pairing work refuse private
  conformance, with no source-root mutation or half-published state.
  The original and candidate picking roots, all original corners,
  inserted orbit cardinality, original contact/child coverage and
  protected old reader identity are tested. This milestone NEVER
  deletes classified faces, authorizes a d2 weld, publishes one
  original-root Step, or updates worker mirror maps. The earlier
  attempt to enable that NEXT layer uncovered c3's
  'Conflicting child-contour vertex mirror mapping'; this remains
  explicitly open under V17-M07, whose terminal code changes were
  withheld from M06 so unsupported Boolean joins still refuse.

- [x] **V17-M07 — Source-certified X=0 c3 singleton/paired contour and cavity-safe retirement.**
  Independently re-run c1 positive signed winding and full V13/c2 source
  coverage on M06's exact three-contact invariant private cut. Certify
  singleton and reciprocal stable vertex, child FaceId and reflected
  contour orbits from original face/edge provenance, without manufacturing
  extra seam loops or equating unrelated coincident sheets. Positive
  strictly valid c3 removal/protection and contour plan on this exact
  fixture; preserve exposed/reversed cavity geometry and source roots.
  Missing contacts, wrong/self/noninvolutive mirrors, branched fans,
  grazing and exhausted contour/classifier budgets refuse atomically.
  READ-ONLY authority; no weld, original-root Step or UI.
  **Accepted M07:** The FULL three-original-contact M06 X=0 fixture
  now passes genuine private c1/c2/c3 Ready and checks exact self/paired
  child FaceId/VertexId source orbits, all source-proven fixed vertices,
  unique reflected (including invariant) contour orbits with two fixed
  ring anchors, complete authorized/protected children and no altered
  published source root. Broken FaceId singleton journal, dropped new
  self-orbit, missing original section, and hard work exhaustion refuse
  with NO partial approval. Complete 23-suite CPU/benchmark CI passed
  Linux GCC, Linux Clang and Windows MSVC at code head 9b4a0a6.
  This is deliberately NOT a Boolean weld or history publication;
  M08 and M09 remain positive acceptance gates.
- [x] **V17-M08 — X=0 original-paired private d2 seam closure.**
  Consume M07 Ready source-certified retirement plus independently
  justified M03 whole-uncut-original retirement, if required. Weld
  only exact opposite surviving finite original V13 intervals; X=0
  self-class demands EVERY contributing ID be source-proven fixed,
  and a valid complete oriented topological disk/link. Preserve
  distinct off-plane paired classes, old exterior and true reverse
  cavity geometry. The M06 complete 3-contact 2→1 positive must
  produce one PRIVATE closed connected symmetric manifold with
  no residual collisions; reject unsupported 4+ sheets, tangencies,
  near-coordinate pseudo-weld, dangling edges and exhausted work.
  **Accepted private M08:** M07's same complete three-original V13-contact
  two-tetrahedron cut now passes strict c3→d2 positive seam intervals,
  one unpublished RegionEdit and a true 2→1 connected closed manifold.
  Each independently source-proven X=0 sheet vertex in a self-reflected
  equivalence class becomes one permanent stable representative; off-plane
  classes remain distinct reciprocal IDs. Independent whole-terminal
  FaceId/edge winding and connected vertex-link audits, no residual
  intersections, old-source exact XYZ/picking identity, real retired
  FaceId/VertexId tombstones and exact remaining mirror orbits pass.
  M03's independently verified untouched-original survey additionally
  accepts true self-paired original mirror orbits, and its optional
  join mode passes the same closed X=0 terminal private union.
  Missing source pairs, forged historical self-mapping and exhausted
  seam work refuse atomically. Test-only synthetic tetra V15 contour
  allowances are unchanged; GCC, Clang and MSVC 23-suite CI passed
  at code head fefa926 (Actions #39138641095). NO original-root CAS,
  history, worker journal or default UI activation is claimed.
- [x] **V17-M09 — X=0 original-root history, queued worker and mirror journals.**
  Compose M06 private cut, M07 c3 and M08 real d2 closed weld into ONE
  original-root immutable Step/CAS with true net stable-ID/tombstone
  self and reciprocal mirror deltas, owner dirty partitions and COW
  accounting. Require exactly one history undo/redo, durable .mshr
  checkpoint and all three worker mirror controllers synchronized.
  Stale roots, corrupt orbit maps, lost source pairs, work/quality
  refusals and queued conflicting commands cannot publish intermediate
  roots or partial journals. Existing M14 post-CAS fatal boundary
  and M11 automatic physical sculpt-merge activation stay separate.
  **Accepted original-root M09:** The complete three-original V13-contact
  X=0 two-tetra fixture now composes M06 source conformance, M07 c3
  and M08 private exact 2→1 d2 join into ONE net original-root Step.
  New live M06 fixed points produce one (id,id) added mirror journal
  entry; retired historical original fixed vertices produce one
  removed self-orbit entry; paired off-plane mirrors retain reciprocal
  IDs, and born-then-retired private IDs do not enter history.
  Exact old XYZ, final closed oriented manifold/vertex links, full
  collision-free output, protected cavity faces, dirty partitions,
  stable tombstones, net counts and COW cost remain checked.
  One CAS, stale replay refusal, one undo/redo, durable .mshr
  checkpoint and a queued async worker demonstrate synchronizing
  all three mirror controllers through a refused incomplete contact,
  a later successful Step, stale replay, undo/redo and reloaded worker.
  Fake historical fixed orbits and exhausted net ID work fail closed.
  Cross-platform code-head GCC/Clang/MSVC build, 23 CPU suites and
  benchmarks passed at 29fec56 (Actions 39139919037).
  No automatic brush-triggered union, live UI toggle or post-CAS
  fault-atomicity activation: those remain M11, M15 and M14.
- [x] **V17-M10 — Full-contact geometric boundary and fail-closed acceptance matrix.**
  Historical 21-pair nontransverse examples, fold/tangent/near-coplanar/nested/multi-region refusals, finite work/quality thresholds and tested complete-contact supported cases. Classify exact/near-coincident contact refusals without confusing source identity and mere geometric equality; M16 owns independently positive and negative generic identity acceptance.
  **Accepted bounded M10 matrix:** A dedicated source-owned acceptance
  register in docs/V17_M10_SUPPORTED_DOMAIN.md separates PROVEN 12-/16-
  contact 3→1, 24-/32-contact mirrored, three-contact X=0, M03 protected
  cavity and M02 alignment positives from the complete historical
  crossing/T 21-pair unsupported V13 graphs. Both original 21-pair
  cases and REVERSED contact order refuse before creating a public
  original-root Step; original 3 closed shells, picker, retirement,
  net ID/mirror/dirty journals and history remain unchanged.
  Vertex-only tangency, exact whole-shell coplanar coincidence and
  fully nested no-V13-contact shells refuse instead of inventing a
  seam, deduplicating XYZ or removing independent internal shells.
  Actual V16 near-tangent and rotated/sheared folded cube fixtures
  are re-probed with full original V13 contact discovery: any
  successful terminal union must have ONE connected oriented
  closed edge/vertex manifold with NO nonadjacent intersections,
  otherwise the entire original-root Step refuses with stage reason.
  Tightened otherwise-positive 12-contact final collision halo,
  source seam trace and net stable-ID budgets refuse with NO partial
  root. Production contact/V15 quality and source-coordinate policy
  is unchanged. GCC/Clang/MSVC warning-clean 23-suite and benchmark
  CI passed at test code head 79793ce (Actions 39142497184).
  This completes the bounded refusal/support CONTRACT, not arbitrary
  imported 3D topology, universal contact acceptance or automatic
  sculpt-driven unions. Coincident ID identity remains the explicit
  NEW M16 acceptance gate, NEXT before M11/M15.
- [ ] **V17-M16 — Source-proven coincident-geometry and seam-equivalence acceptance.**
  Coordinate equality alone NEVER implies VertexId equivalence; distinct exact-coordinate source vertices remain distinct through private conformance. Prove a proper, source-complete off-X=0 transverse section weld yielding one genuinely closed oriented manifold (plus mirrored off-plane control) while preserving unrelated exact-coordinate duplicates on and off X=0. Require exact original FaceId/interval provenance, independently classified c1/c2/c3/M03 removal, valid connected final vertex links, edge winding, mirror orbits and no residual collisions. Accidental isolated corner/edge touches, protected cavity coincidences, wrong/near-coordinate pseudo-welds, incompatible multi-sheet fans and resource exhaustion must refuse without any partial root, mirror/history/dirty journal or dropped worker input. Regress M04/M05/M08/M09 positives and preserve old readers/tombstones. Full acceptance cases and non-goals: [docs/COINCIDENT_VERTEX_TOPOLOGY.md](docs/COINCIDENT_VERTEX_TOPOLOGY.md). **Complete after M10 and before M11/M15; peer milestone, not a nested subtask.**
- [ ] **V17-M11 — Repeated actual sculpt-triggered classified symmetric unions.**
  After M10/M16's supported-domain and coincidence gates, real physical tower contact after Inflate/split/flip/collapse, successful repeated sixteen-Step unions, clean/no-contact continuation, rejected-input drain, immutable history and renderer roots.
- [ ] **V17-M12 — Active original-anchored exposed-feature and hysteresis policy.**
  Cumulative geometry acceptance versus the first reference, intentional deformation exceptions, cavity protection, proven episode reset and stable repeated-contact decisions.
- [ ] **V17-M13 — Aggregate per-Step work bounds and diagnostic telemetry.**
  Winding/collision/arrangement/contour/join/feature/resource budgets with exact-boundary refusals and honest completion, time, memory-proxy and rejection accounting.
- [ ] **V17-M14 — Post-CAS failure and auxiliary-state transaction acceptance.**
  Fault-injected commit/history/mirror/dirty/checkpoint consistency; one visible final root and undo transition or safe, completely reported failure.
- [ ] **V17-M15 — Default-off live Merge geometry switch and targeted desktop acceptance.**
  Explicit on-mode only after M16's source-proven coincidence/fan acceptance, with Linux/Windows Vulkan, symmetry, picking, undo/redo, storage and rejected-stroke checks. V18 Off-mode collision constraints and broader V19 acceptance remain separate.

- [ ] **V18 — Opt-in 'Merge geometry: Off' collision constraints.**
  On the same post-state triangle neighborhood, constrain normal
  displacement to prevent new non-adjacent crossings and maintain a
  mesh-scale (not brush-scale) clearance; retain tangential movement
  where valid. Do not perform unrelated cleanup of pre-existing bad
  imported geometry. Exercise moving **both** opposing sheets together,
  symmetry, grazing/sliding contact, and conservative large-stamp
  behavior. Exact swept-volume CCD remains deferred with a documented
  tunneling limitation. Keep the current live default until explicitly
  selected/validated.
- [ ] **V19 — 3D robustness, performance and desktop acceptance.**
  Stress multiple/near-tangent/coplanar intersection regions, tunnels,
  ambiguous enclosed shells, imported meshes with pre-existing unrelated
  exactly coincident positions or nonmanifold fans (never automatic global
  coordinate deduplication), very uneven/local high-density triangles,
  repeat zipper edits and pathological collision workloads. Add explicit
  accuracy/budget/failure diagnostics, performance measurement at
  5k/20k/100k and larger where practical, and user-observed Linux/Windows
  Vulkan tests including symmetry, picking, undo/redo and save/recover.
  Do not claim universal real-time complexity, exact CCD, all imported
  mesh semantics or strict total-memory bounds.

**Non-goals of V13–V19:** CGAL as a required hot-path dependency,
mandatory exact intersection-curve Boolean reconstruction, global
volumetric remeshing, global per-Step BVH rebuilds, unconditional
two-edge-length error guarantees, forced genus-zero preservation,
or treating an arbitrary enclosed cavity as automatically disposable.
Manifold/CGAL may be studied or used only as benchmark/reference
implementations where appropriate. All actual new behavior must be
separately verified before being marked complete.

## Other remaining design goals

Keep brush radius independent of editing density, first-class vertex-colour
fidelity and UV-to-colour error budgets, local geometric validity/error limits,
import/export, painting tools, and large-model streaming as future requirements.
Do not silently simplify high-density scan detail as an optimization.
