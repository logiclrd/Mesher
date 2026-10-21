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

**October 18 scope rebaseline:** M06 exposed independent c3 singleton contour, d2 disk/weld and original-root worker/history gates. The complete former M07 objective is retained as flat M07–M09; former M08–M13 objectives are renumbered M10–M15 without loss. Historical task tree and append-only DESIGN retain old identifiers.\n\nEach item below is a **standalone mergeable milestone** with its own positive/negative acceptance. No child numbering (M01a, M01b1, etc.). Dependencies permit later integration but never require subdividing this list. **M01–M10 and M16 are complete in their independently verified bounded geometry, provenance, original-root worker, and accepted identity scopes; M12–M15 and V17 as a whole remain open. M17 source certification, M18 first physical exact transaction and M11 bounded sixteen repeated symmetric physical unions have independently passed their acceptance scopes. M12 is NEXT; no desktop Merge geometry UI is enabled.** M16 remains an append-only peer completed after M10 and before M11, without renumbering any accepted historical milestone. See [coincident-vertex contract](docs/COINCIDENT_VERTEX_TOPOLOGY.md).

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
- [x] **V17-M16 — Source-proven coincident-geometry and seam-equivalence acceptance.**
  Coordinate equality alone NEVER implies VertexId equivalence; distinct exact-coordinate source vertices remain distinct through private conformance. Prove a proper, source-complete off-X=0 transverse section weld yielding one genuinely closed oriented manifold (plus mirrored off-plane control) while preserving unrelated exact-coordinate duplicates on and off X=0. Require exact original FaceId/interval provenance, independently classified c1/c2/c3/M03 removal, valid connected final vertex links, edge winding, mirror orbits and no residual collisions. Accidental isolated corner/edge touches, protected cavity coincidences, wrong/near-coordinate pseudo-welds, incompatible multi-sheet fans and resource exhaustion must refuse without any partial root, mirror/history/dirty journal or dropped worker input. Regress M04/M05/M08/M09 positives and preserve old readers/tombstones. Full acceptance cases and non-goals: [docs/COINCIDENT_VERTEX_TOPOLOGY.md](docs/COINCIDENT_VERTEX_TOPOLOGY.md). **Complete after M10 and before M11/M15; peer milestone, not a nested subtask.**
  **Accepted M16:** The real complete THREE-contact two-closed-tetra
  source graph translated completely off X=0 passes a genuine
  c1/c2/c3/d2 2→1 private closed oriented vertex-manifold and one
  ORIGINAL-root Step. Pre-join endpoint coordinates match EXACTLY but
  source sheets retain distinct stable VertexIds; only complete
  finite source interval provenance authorizes the terminal weld.
  Reversed complete original-contact order also passes. The already
  positive M05 reciprocal off-plane mirrored graphs now assert private
  source-separated IDs and full joined edge/vertex fans.
  Two otherwise valid tangent tetrahedra sharing one exact XYZ on
  OR off the plane keep separate stable IDs through no-op; four
  independent closed tetrahedral fans touching at one exact point
  remain separate and may not produce a Boolean four-fan; a 1e-7
  near-miss cannot be snapped. An unrelated exact-coordinate third
  source at the active region either remains independent of an
  otherwise-certified two-body join or causes a complete refusal.
  Missing original FaceId contacts, forged owning FaceId and finite
  seam budget exhaustion refuse without a publishable Step,
  retirement, mirror history or dirty journal. Existing M02–M10
  true positive/cavity/queued worker controls remain unchanged.
  Full GCC, Clang, Windows MSVC /W4 /WX, 23 CPU suites and
  applicable benchmarks passed at test-code head 4979b25
  (Actions pull-request run 39144616634); the acceptance register is
  docs/V17_M16_COINCIDENT_IDENTITY_ACCEPTANCE.md. This does NOT
  enable sculpt-triggered merging or generic imported topology.
- [x] **V17-M17 — Complete ORIGINAL-contact discovery for actual post-sculpt geometry.**
  Independently classify the *real* post-Inflate/refine/collapse unpublished original-root candidate, not selected old pairs or V16 scar contours. Enumerate the entire bounded V13 source-contact graph including reciprocal reflected halves and new moved/rewritten faces, distinguish clean/contact/ambiguous/over-budget, retain exact FaceId and candidate-root provenance, and make complete discovered pairs usable by the existing V17 exact private planner. Positive real physical candidate must have certified transverse original contacts; tangency, missing mirrored pair, ambiguous and budget cases refuse/diagnose without mutation. Read-only stage: no publication or live switch.
  **Accepted bounded M17 read-only certificate:** The complete post-
  Inflate candidate root (not the previous published root or dirty
  subset) is screened by strict finite V13 per-FaceId sections with
  a bounded all-live-vertex indexed query. Exactly mirrored whole-
  candidate orbits must be reciprocal and physically reflected.
  No ambiguity, tangency, coplanar overlap, missing symmetry or
  insufficient candidate/section/query work produces a partial
  consumable contact set. A real off-X=0 three-source-contact
  physically moved two-tetra geometry and its two reflected halves
  pass as complete source certificates; their entire candidate FaceId
  graphs equal independent all-live-face discovery. An actual
  normal-driven stamp moves initially separated closed tetrahedra
  INTO finite transverse contact; a subsequent real local edge
  split is re-assessed from its new terminal FaceIds, never from
  stale pre-remesh pairs. The original clean pre-stamp revision
  independently returns Clean; point-only tangent, exactly
  coincident coplanar closed shells and tight section/collision/
  whole-source work limits cannot certify a finite seam.
  Original picker, stable IDs, mirror journals, history and
  renderer are unchanged. The normative scope and tests live in
  docs/V17_M17_PHYSICAL_CONTACT_DISCOVERY.md.
  This is NOT a committed Boolean sculpt union; M18 owns the
  first true atomic physically triggered union, followed by the
  unchanged sixteen-alternating-input M11 acceptance.
- [x] **V17-M18 — One physical collision-triggered exact Boolean union in a single sculpt Step.**
  Consume M17's complete post-sculpt candidate V13 graph and V17's M04–M10/M16 full exact Step. Unlike V16 approximate surgery, accept only a true closed source-certified union; reconcile pre-union refine/collapse mirror deltas with exact final net IDs and correct ORIGINAL-root capture; stage complete renderer dirty/history/worker mirrors with ONE CAS, or roll back the entire stamp. Prove one actual physically induced positive asymmetric and reflected candidate (where supported), plus a refused invalid stimulus followed by a valid one, without enabling the desktop UI.
  **Accepted bounded M18:** An explicit/default-off
  `SculptCollisionSettings.physicalExact` policy now runs the
  full M17 source V13 certificate on the ACTUAL unpublished
  post-Inflate/refine/collapse root. Certified finite contacts
  alone enter M09's private c1/c2/c3/M03/d2 exact Boolean; a
  Clean physical Step proceeds without invented welding, while
  unsupported geometry/budget/mirror/validator input refuses the
  entire stamp. The candidate+cut+join remain PRIVATE and only
  one captured pre-stamp original-root CAS publishes final
  closed manifold, renderer dirty owners, net live VertexId/
  FaceId counts, one reversible history journal and checkpoint.
  The net mirror journal compares old Inflate with final staged
  Refiner/Collapser orbits, excluding IDs both minted and
  retired during one stamp (including X=0 fixed self-orbits).
  A true clean → normal-driven finite transverse collision
  creates a supported union on TWO initially disjoint closed
  tetrahedra. A physically moved off-X=0 three-original-contact
  source yields a 2→1 certified union. A bounded search of
  genuinely moved and locally SPLIT original source edges
  also produces a successful exact union after a real
  post-Inflate FaceId change; all unsupported alternatives
  roll back complete source/mirror/history state. A two-area
  reflected physical stamp produces a paired union, with
  an injected terminal-validator refusal on the FIRST input
  followed by a successful physical input on the SAME brush
  and checked undo/redo/durable mirrored checkpoint.
  The unchanged queued 4D worker receives a genuine physical
  stamp, not an explicit merge command, and emits ONE
  terminal root/dirty history transition, verified undo/redo
  and durable async recovery. Exhausted contact budgets
  refuse without root/dirty/map publication. GCC/Clang/MSVC
  /W4 /WX 23 CPU suites and applicable benchmarks passed at
  source test head f8e5a20 (Actions 39153631172), with
  additional mirror refusal/checkpoint and design commits
  checked by exact-final-head CI before merge.
  Source contract: docs/V17_M18_PHYSICAL_EXACT_STEP.md.
  The desktop UI remains default OFF; this does NOT satisfy
  M11's sixteen successful alternating physical symmetric
  Steps, M12 anchored policy, M13 aggregated work, M14
  post-CAS faults or M15 desktop acceptance.
- [x] **V17-M11 — Repeated actual sculpt-triggered classified symmetric unions.**
  After M10/M16's supported-domain and coincidence gates, real physical tower contact after Inflate/split/flip/collapse, successful repeated sixteen-Step unions, clean/no-contact continuation, rejected-input drain, immutable history and renderer roots.
  **Accepted bounded M11:** SIXTEEN independent REAL normal-driven
  original-contact Boolean joins on ONE persistent two-tower
  symmetric source history, not sixteen refusal-only stamps,
  explicit Merge commands or selected pre-existing contacts.
  Initially all 64 component tetrahedra are individually closed
  and the complete root has ZERO V13 intersections. Eight physical
  paired contact regions per tower are spaced in height and
  genuinely alternated. Every stamp moves one separate region;
  M17 discovers both complete source FaceId contact halves and
  M18 welds both into final oriented closed topology through ONE
  captured-original CAS. Every accepted Step is individually
  verified as globally collision-free with real FaceId retirement,
  net reciprocal mirror identity, nonempty old-to-final renderer
  dirty owners, one real history entry and stable prior picking
  root. All sixteen accepted original-root revisions undo and
  redo in order without exposing private conformance/cut/join.
  A separate SAME persistent asynchronous 4D worker drains
  sixteen queued physical begin/end pairs, with an interleaved
  physically clean no-op, injected post-exact validation refusal
  followed by a successful identical stamp, and exact FIFO
  completed/rejected/published accounting. Its final journal
  durably checkpoints only living reciprocal orbits, then
  undo/redo restores every terminal root. A BROAD real symmetric
  multi-region physical stamp exceeding a strict complete
  V13 source-section budget refuses ALL affected changes;
  an ordinary later single-region stamp on that SAME worker
  still completes a positive symmetric union. The original
  separate thirty-two same-patch repair/anchored-witness and
  old-root controls are preserved unchanged; they are NOT
  misreported as 32 successive Boolean joins. V16 remeshing
  and M18 physically split-plus-union positive coverage remain
  independently green. This proves a bounded repeated
  source-certified geometry domain, NOT arbitrary recursive
  merging of one already joined patch, universal N-way
  topology or live desktop activation.
  See docs/V17_M11_REPEATED_PHYSICAL_UNIONS.md.
- [x] **V17-M12 — Active original-anchored exposed-feature and hysteresis policy.**
  Cumulative geometry acceptance versus the first reference, intentional deformation exceptions, cavity protection, proven episode reset and stable repeated-contact decisions.
  **Original M12 accepted in PR #88, substantive CI #39272006623**:
  GCC/Clang/MSVC 23 CPU suites and applicable benchmarks green.
  Independently accepted M19–M23 and M35–M39/original M36 genuine
  local physicalExact and 4D protected policy reviewed criterion by
  criterion. NEW final-root M12 test reproduces two true source-proven
  same-neighborhood normal-driven joins and recaptures ALL 72 original
  FaceId vertex incidences, original shortest edge and SEVEN immutable
  witness XYZ; complete exact 7×living descendant post-d2 source work
  accepts, one-under, stale intermediate reference and forged
  accepted-transition certificate refuse. All forty first-root
  unscoped original FaceIds/XYZ, mirrored signed-winding 0/+1/0
  protected outer/cavity walls, original reflected source orbits and
  immutable old/intermediate/final picker roots survive.
  Original M36 independently proves ONE real async 4D worker history
  of actual two local physical joins, first accepted protected drift,
  cumulative second refusal, Unknown-interrupted Clean/three-NEW
  Clean closure, later source-exact recontact and FIFO recovery,
  atomic COW/mirror/dirty/history and pointer-exact undo/redo.
  Evidence: `docs/V17_M12_ORIGINAL_FINAL_ACCEPTANCE.md`.
  This is NOT universal three-body Boolean, two-sided continuous
  Hausdorff, exact CCD, or enabled desktop UI; M13–M15/V18/V19
  remain OPEN.
**M13 flat execution rebaseline — October 19, 2026.** The ORIGINAL
M13 objective and checkbox are unchanged and remain OPEN. Aggregate
per-Step resource admission, winding/collision/arrangement accounting,
contour/join/feature budgets, and worker telemetry must be accepted
independently instead of leaving partially implemented M13 open or
creating recursive subtasks. Record the COMPLETE ordered flat sequence
before beginning code:

- [x] **V17-M40 — Whole-private-Step COW resource admission and attempt accounting.**
  Establish one explicit conservative resource envelope across
  Inflate/refine/collapse, optional V13/c1/c2/c3/d2 exact operations
  and queued private exact operations (not merely a single internal
  RegionEdit). Aggregate ALL private cloned COW payload bytes with
  checked overflow, net changed-ID evidence and exact final dirty
  owner count before the ONE root CAS. Give success, one-under
  failure, timed/untimed source-refusal and unchanged COW/history/
  mirrored/checkpoint invariants. Expose honestly labeled attempted
  vs published counts and zero on unavailable evidence in both direct
  brush and async worker diagnostics. Opt-in policy only; no
  unbounded legacy default change, desktop still default off.
  **M40 tested in PR #89, substantive Actions #39277749517**:
  archived GCC/Clang/MSVC logs show 23/23 CPU tests passing,
  applicable benchmarks and no compiler errors; exact final-head
  and post-merge CI still gate merge. Physical original-source
  certified mirrored normal-driven and genuine queued source-exact
  union both accept all THREE resource caps exactly equal to actual
  full private COW copy bytes, changed ID references and final dirty
  owner counts. Each separate ONE-under physical and queued category
  refuses BEFORE root CAS with old M29 source, mirrors, history and
  renderer unchanged; early bad source intent reports no fabricated
  work, complete rejected candidate retains attempted costs.
  `Status.stepResources` forwards real FIFO accepted work.
  See `docs/V17_M40_PRIVATE_STEP_RESOURCE_ENVELOPE.md`.
- [x] **V17-M41 — Combined finite winding/collision/arrangement work.**
  Trace actual preflight, c1/c2/c3 original and post-cut source work,
  uncut winding, final local collision and arrangement/junction
  predicates. Propagate complete measured per-category work across
  private substeps of ONE original-root candidate without double
  charging identical evidence. Enforce bounded aggregate caps
  independent of local caps, including exact-pass vs one-under
  real positive physical and queued candidates. Any incomplete,
  ambiguous or exhausted category refuses atomically.
  **M41 original flat peer accepted PR #92, substantive Actions
  #39288853723**: all GCC/Clang/MSVC 23 CPU suites and benchmarks
  GREEN at exact corrected source SHA `231915a6fc8d172889f742f066bf2720cd4e29b7`.
  Count actual V13/M26 collision query/box/partition/candidate/narrow
  work, actual original signed-winding query/box/partition/triangle/ray
  work and genuine multi-section arrangement pair/predicate work.
  All 10 winding/collision per-category measured exact-boundary
  source-authenticated physical AND queued final-root union candidates
  accept; each separate one-under limit rejects atomically BEFORE
  CAS with unchanged old source/mirrors/dirty/history. Original
  three-closed-solid multi-curve read-only arrangement independently
  measures nonzero real pairs/predicates, exact-pass/one-under.
  Checked overflow and pre-CAS meter end preserve honest attempted
  diagnostics; no partial Boolean authority. Earlier PR #91 MSVC-only
  C4456 guard-name shadow was corrected, not hidden. Evidence
  `docs/V17_M41_AGGREGATE_GEOMETRY_WORK.md`; original M13, M42,
  M14/M15/V18/V19 remain OPEN.
**M42 flat implementation rebaseline — October 19, 2026.** The
original M42 objective remains fully intact and OPEN. Its contour,
seam/join, protected-feature/history, and cumulative worker telemetry
are separate independently complete categories. Plan the FULL flat
sequence NOW, before beginning M42 implementation:

- [x] **V17-M43 — Actual original-root contour and closure selection work.**
  Meter real per-removed-face contour extraction/edge traversals,
  loop and cap/zipper selection predicates consumed by repeated
  private c3/uncut and d2 source reconfirmations. Add independently
  finite aggregate per-category admission in a single direct/queued
  original-root Step, exact-vs-one-under for actual supported physical
  and queued joins and independent read-only contour fixtures.
  Fail-closed atomic COW/source/mirror/dirty/history refusal.
  **M43 independently accepted substantive PR #93,
  Actions #39296825695:** Linux GCC, Linux Clang, Windows MSVC
  /W4 /WX all 23 CPU suites and applicable benchmarks GREEN.
  Seven actual source-contour/closure operation types, measured
  across whole private original physical/queued exact Steps,
  separately finite with checked saturating overflow. Genuine
  unequal-density two-ring zipper proves all seven NONZERO
  category counts, exact all-category bounds and independent
  one-under categorical refusal without changing root.
  True mirrored protected M39 source physical and real source-
  authenticated asymmetric queued c1/c2/c3/M03/d2 complete
  original-root unions independently verify exact-bound
  acceptance and each actually nonzero one-under Step rejection
  before CAS with source/M29 proof, mirrors, COW, dirty and
  history unchanged. All seven attempted costs in worker
  per-Step status, not peak RSS or deterministic wall time.
  `docs/V17_M43_CONTOUR_CLOSURE_WORK.md`.
  M44–M46, original M42, original M13, M14/M15, V18/V19
  remain OPEN. Documentation-head and merged-main CI gate merge.
- [x] **V17-M44 — Combined source-certified seam tracing, zipper and join work.**
  Meter all actual original-root seam trace comparisons, matching
  intervals, source-owned join/weld predicates, asymmetric optional
  seam refinement and net mirrored-orbit checks; enforce separately
  finite aggregate caps on genuine direct and queued unions. Test
  exact-bound positive and one-under category-specific refusal with
  unchanged accepted source, mirror and checkpoint identity.
  **M44 independently accepted substantive PR #94, Actions
  #39303521777:** all GCC, Clang and Windows MSVC warnings-as-
  errors 23 CPU suites and applicable benchmarks GREEN.
  SEVEN real complete private original-source seam categories
  (actual trace probes, opposite certified source intervals,
  exact welded point comparisons, optional unequal subdivision
  predicates and matched intervals, source-fixed/mirrored orbit
  checks, retained-face rewires) have independent checked/
  saturating aggregate bounds before ONE physical/queued CAS.
  Genuine protected 72-source symmetric normal-driven exact union
  and separately authenticated queued c1/c2/c3/M03/d2 positive
  accept exact actual work; each exercised category independently
  ONE-under refuses the named category while old source/M29,
  mirror map, COW, dirty and history stay identical. Genuine
  3-solid 12-contact UNEQUAL source fan regression independently
  proves actual optional refinement predicate and matched source
  interval work, exact/one-under and correctly certified joined
  closure without global proximity welding. See
  `docs/V17_M44_SEAM_TRACE_JOIN_WORK.md`. M45/M46, ORIGINAL M42,
  ORIGINAL M13, M14/M15/V18/V19 still OPEN; exact final-head and
  merged-main CI remain mandatory.
- [x] **V17-M45 — Complete protected-original feature and pre-CAS history evidence.**
  Meter actual SEVEN-point × living-descendant closest-triangle
  checks across all repeated source proof audits and intended
  exposed/reversed cavity feature decisions (never infer from a
  last revision). Meter authenticated history-transition and source
  mirror/orbit verification work; individually exact-vs-one-under
  guard actual positive physical/queued original-root Steps, preserving
  first-reference lifetime and refusing before root CAS.
  **M45 accepted substantive PR #95, Actions #39318060899:** Linux
  GCC, Clang and Windows MSVC /W4 /WX passed all 23 CPU suites
  and applicable benchmarks. Nine independent finite saturated
  categories count actual first-original anchor, live descendant,
  certified retirement, seven-point-to-owned-child closest-triangle,
  explicit intent, protected Exposed/ReversedBoundary, original
  reciprocal mirror orbit, and repeatedly executed PRE-CAS M29
  publication root/transition work. Real 72-first-original
  reflected protected normal-driven physical union and separate
  authenticated asymmetric queued exact source union calibrate
  exact limits and independent one-under refusal of each truly
  nonzero category, without COW/source/mirror/dirty/history changes.
  Repeated direct original audits charge each real reexecution;
  failed local preflight creates no phantom witness tests.
  See docs/V17_M45_ORIGINAL_FEATURE_HISTORY_WORK.md.
  M46, ORIGINAL M42/M13 and M14/M15/V18/V19 remain OPEN.
  Final exact PR-head and independent merged-main CI required.
- [x] **V17-M46 — Honest direct and queued worker cumulative diagnostics.**
  Expose separately measured stage wall times (observations, not
  deterministic upper bounds), attempted vs published per-category
  copy-work, source/geometry/category refusals, COW memory proxies,
  completed/rejected worker cumulative counters and bounded
  history/dirty evidence across real asynchronous FIFO including
  checkpoint save/restore and undo/redo. Verified exact/one-under
  acceptance without treating attempted bytes as peak RSS, or
  an unverified persisted opaque credential as authenticated.
  **M46 independently accepted substantive PR #96,
  Actions #39328566938:** GCC, Clang and Windows MSVC /W4 /WX
  all 23 CPU suites, checkpoint stress and applicable benchmarks
  GREEN. Per-original-root Step observed wall stages separately
  cover initial physical/remesh or queued exact geometry, source
  topology/intent decisions, immutable original witness/lifetime
  proof, finite admission and the actual history/publication
  work; these are observations, NEVER hard runtime limits.
  Genuine attempted and published M40 copying bytes (private
  COW work proxy, NOT peak RSS), changed ID references, bounded
  dirty partitions and ALL 35 independent saturated M41-M45
  source/geometry counters accumulate separately. Outcome and
  coarse failure categories distinguish provenance, geometry,
  finite work and stale expected root. A refused Step cannot
  earn published counters. Live FIFO also reports actual
  snapshot publication/dirty owner events, undo and redo
  restorations, checkpoint queue commands and asynchronous
  completed/coalesced/failed writer totals alongside existing
  bounded history. Trusted in-memory M29 provenance survives
  read-only checkpoint handoff; V13 disk read-back carries only
  the required-fidelity flag and NEVER fabricates an opaque
  credential or previously attempted session telemetry.
  Genuine mirrored 72-original protected physical Step and
  separate asymmetric original-authenticated queued exact union
  calibrate exact-vs-one-under M45 witness budget admission.
  Stale queued root and async undo/redo/checkpoint across actual
  immutable roots also tested. See
  `docs/V17_M46_WORKER_DIAGNOSTICS.md`.
  ORIGINAL M42 and ORIGINAL M13 still OPEN, as are M14/M15
  and V18/V19. Exact final-head and independent merged-main
  green still required before reporting M46 merged.
**Then execute ORIGINAL M42** separately: simultaneous criterion-by-
criterion integration of M43–M46 plus accepted M40/M41 on the SAME
genuine direct and queued protected Step/hysteresis history, with
its own regression/evidence, all 23 CPU suites, GCC/Clang/MSVC
final-head and independently merged-main green. Check ONLY the
original M42 checkbox after this, then ORIGINAL M13 integrated
resource/diagnostic acceptance. Original M13, M14/M15 and V18/V19
remain separate, OPEN; no weakening of first-source provenance,
exact interval/CCD promises or unsupported three-body welding.

- [x] **V17-M42 — Contour/join/feature bounds and full worker diagnostics.**
  Aggregate complete local contour/closure, seam trace/zipper/join,
  protected original feature/distance witness work, symmetric ID
  proof and history limits. Separately report measured stage timings,
  attempted budget costs, copied memory-proxy, actual accepted
  publication, refusal category and cumulative worker diagnostics,
  including direct/queued FIFO and checkpoint preservation.
  Exact vs one-under cases must fail for the intended category,
  not an incidental earlier stage. Never portray attempted copy bytes
  as peak RSS or CPU wall time as deterministic execution work.
  **Original M42 integrated acceptance, PR #97:** the actual
  M39 72-first-original reflected, protected-cavity normal-driven
  4D Step and independent authenticated asymmetric queued
  c1/c2/c3/M03/d2 two-tetra Boolean EACH calibrate all 35
  independent M41/M43/M44/M45 operation categories AND all THREE
  M40 private COW/changed-ID/final dirty-owner limits TOGETHER.
  On the SAME real complete source candidate, exact 38-category
  equality accepts one original-root CAS, while every genuinely
  nonzero independent category lowered by ONE refuses its own named
  category with all other 37 caps at exact equality. At the
  substantive cross-compiler gate, 32/35 physical and 31/35
  queued category counters were genuinely nonzero; absent optional
  pair/refinement/closure categories are covered by existing
  independently accepted M41/M43/M44 real-fixture regressions,
  NOT fabricated as positive source work. Real rejected attempts
  preserve immutable old COW/M29 first original, reflected stable
  mirror IDs, picker, renderer dirty, bounded undo/redo and accepted
  work totals; they still expose actually attempted work and
  exact failure reasons. Genuine accepted worker FIFO repeats
  calibrated physical and queued candidates; the protected
  4D worker subsequently executes actual Contact → Clean →
  Clean → Unknown hysteresis under simultaneously finite limits.
  The Unknown invalid original-source intent clears the quiet
  streak without a Clean credential, history append, dirty root
  or accepted resource charge. Worker undo/redo restores real
  original-root history without reexecuting work; stale queued
  original-root union preserves all successful counters, and a
  completed V13 disk checkpoint preserves provenance-REQUIRED
  without fabricating M29 opaque authority or previously recorded
  work. Stage times are observations; copied bytes are cumulative
  private COW work, NEVER process peak RSS. Substantive
  [Actions #39332363112](https://github.com/logiclrd/Mesher/actions/runs/39332363112)
  GREEN Linux GCC/Clang and Windows MSVC /W4 /WX, 23 CPU suites,
  repeated checkpoint stress and applicable benchmarks.
  `docs/V17_M42_ORIGINAL_INTEGRATED_ACCEPTANCE.md` records
  separate exact-head/merged-main gates. ORIGINAL M13 remains
  UNCHECKED, then M14/M15 and V18/V19 remain OPEN.
**Then execute ORIGINAL M13** as its own criterion-by-criterion
integrated acceptance with all V17-M40/M41/M42 examples together
in real 4D and queued one-root transactions, verified full
GCC/Clang/MSVC CI, its own evidence and ONLY the original
M13 checkbox checked. M14 fault-injected post-CAS consistency,
M15 desktop on-mode and V18/V19 remain separate and OPEN.
No automatic same-component multi-body support, exact CCD,
universal import success or hard RSS bound is implied.

- [x] **V17-M13 — Aggregate per-Step work bounds and diagnostic telemetry.**
  Winding/collision/arrangement/contour/join/feature/resource budgets with exact-boundary refusals and honest completion, time, memory-proxy and rejection accounting.
  **Original M13 separate integrated acceptance: PR #98,
  substantive [Actions #39335754023](https://github.com/logiclrd/Mesher/actions/runs/39335754023)
  GREEN GCC, Clang, Windows MSVC /W4 /WX, all 23 CPU suites,
  repeated checkpoint/worker shutdown stress and applicable
  benchmarks. A new independent per-attempt ledger reconciles
  EVERY one of the 35 M41/M43/M44/M45 geometry/source work
  counters and all M40 private COW-copied byte, stable-ID
  reference and dirty-owner work *separately* for attempted
  versus accepted publication. In a real reflected 72-original
  protected 4D source Contact → Clean → Clean → invalid-
  caller Unknown stream, all 38 limits are simultaneously
  finite and calibrated to the greatest true executed Step.
  Three original-root M29 Steps are genuinely published;
  the fourth early refuse has zero phantom geometric work,
  breaks Clean hysteresis, leaves root/mirrors/source and
  undo/redo unchanged, and increments only provenance
  refusal and attempted operation counts. The cumulative
  counts reconcile exactly with four independently captured
  true Step records, with observed whole and individual
  phase milliseconds finite/nonnegative but NEVER asserted
  as deterministic bounds. A separate genuinely computed
  physical M45 original-witness one-under proves nonzero
  attempted COW/geometry with zero published charge and
  unchanged first-root source. Independent source-certified
  queued c1/c2/c3/M03/d2 exact union accepts all 38
  positive ceilings; a stale original-root request charges
  zero private work, while a full candidate under one
  original-witness limit retains real attempted work with
  no source/history CAS. Actual FIFO queued accepted/stale,
  undo/redo and completed V13 checkpoint file I/O reconcile
  cumulative counters without duplicating expensive
  geometry or inventing an opaque M29 credential on disk
  restore. Copy-work bytes are NOT peak RSS; observed
  milliseconds are NOT an enforceable CPU wall limit.
  Source and geometry rules remain unchanged. See
  `docs/V17_M13_ORIGINAL_INTEGRATED_ACCEPTANCE.md`.
  Final exact-head PR and independent merged-main CI
  still mandatory before reporting merged-complete.
  ORIGINAL M14, M15, V18/V19 remain OPEN.
- [x] **V17-M14 — Post-CAS failure and auxiliary-state transaction acceptance.**
  Fault-injected commit/history/mirror/dirty/checkpoint consistency; one visible final root and undo transition or safe, completely reported failure.
  **M14 implemented, PR #99 — independent fault-injection gate:**
  The real 72-original protected reflected M39 physical
  original-root Step and independent asymmetric first-original
  authenticated c1/c2/c3/M03/d2 queued two-tetra Boolean
  each inject a deliberately thrown exception at four
  genuinely POST-CAS boundaries: after indexed root CAS,
  after mirror state changes, after M29 history append,
  and after auxiliary mirror commit. An irrevocable CAS
  is NEVER reported as a recoverable pre-CAS budget refusal,
  and attempted M40 COW and M41–M45 source work are counted
  separately from ZERO successful complete publications.
  Rather than forge an undo/redo transition from partial
  mirror/history/dirty updates, the affected controller is
  explicitly terminal/quarantined and rejects every future
  physical or queued Step, undo/redo, and checkpoint. Public
  snapshot access is denied while auxiliary publication
  is IN FLIGHT and afterward on failure. A real held-CAS
  two-thread regression confirms concurrent readers cannot
  obtain an intermediate root/checkpoint. Actual FIFO
  command failure reports terminal reason and attempted
  cost, drops uncertain renderer frames, and rejects
  additional enqueue requests. Independently, invalid
  checkpoint destination causes a genuinely FAILED async
  disk write without damaging the accepted indexed root,
  M29 source history or published resource totals; a
  subsequent valid checkpoint succeeds and V13 disk
  read-back still cannot fabricate opaque first-original
  credentials. Fault callback is EMPTY BY DEFAULT and
  never part of the desktop Merge policy. The fail-stop
  alternative meets M14's *safe, completely reported
  failure* criterion; no fictitious rollback of a
  committed CAS or guarantee about process crashes is
  claimed. Substantive [Actions #39340898434](https://github.com/logiclrd/Mesher/actions/runs/39340898434)
  succeeded GCC, Clang, Windows MSVC /W4 /WX, all 23 CPU
  suites, repeated checkpoint stress and applicable
  benchmarks. See `docs/V17_M14_POST_CAS_FAULT_ACCEPTANCE.md`.
  Exact final-head and independent merged-main CI remain
  separate mandatory release gates.
**V17-M15 desktop integration — flat planned prerequisites (October 19, 2026).**
The intact ORIGINAL M15 checkbox above stays OPEN until these are all
independently accepted. Unlike already verified headless queued
contacts, GLFW has no source-preserving input route or worker-side
bounded contact discovery. Do not route the UI through anonymous
unbounded `triangleIntersections`, silently infer arbitrary
first-original FaceIds from a later terminal, make an unverified
disk checkpoint an opaque M29 authority, or activate protected
source surgery in the default OFF sculpt path. Complete this
sequence in order; if unsupported geometry is encountered,
report a refusal rather than asserting universal Boolean support.

- [x] **V17-M47 — Bounded and source-authenticated desktop Merge request.**
  Introduce a worker-side, serialized discovery command with a
  finite candidate/query work budget and explicit source capture
  from the *initial trusted original*, not the mutable renderer
  or a later published root. Require a complete nonempty
  independently checked contact set, current expected root
  and valid M16/M29 original lineage before dispatching to the
  existing c1/c2/c3/M03/d2 exact Boolean. Reject empty, stale,
  over-budget, ambiguous, out-of-support and disk-unverified
  requests fail-closed with CPU regressions for real supported
  asymmetric and reflected sources, stale source, undo/redo
  and no geometry/picker/dirty/history mutation on refusal.
  **V17-M47 independently implemented PR #100:** an EXPLICIT
  `mergeDiscoveredContacts(expected,selectedFirstOriginalFaceIds,...)`
  command carries one immutable UI-selected current indexed root.
  After FIFO serialization, the worker first verifies the exact
  current root (stale requests refuse), validates every unique
  nonzero user-nominated FIRST-original FaceId against the
  immutable initial import (not any coincident later XYZ face),
  requires sealed in-memory M29 original-source authority, then
  performs an independently bounded COMPLETE read-only
  V13/M16 transverse contact assessment over ALL faces and
  vertices of the exact chosen root. The caller can only
  tighten the six query bounds, source face/vertex limits
  and section-work ceiling. Missing/ambiguous/no-contact,
  misnominated, exceeded-budget, unprotected OFF-mode,
  stale, or unverified V13 disk source refuses entirely;
  no truncated contact subset or speculative renderer
  dirty/history is published. Only fully certified contacts
  reach the existing original-root queued c1/c2/c3/M03/d2
  Boolean, original lineage/feature verifier and M40–M46
  finite work budget. True 2-tetra asymmetric and reflected
  (explicitly sealed original mirror maps, NOT the sphere
  vertex-order convention) source unions both publish one
  fully committed COW history root and source-authenticated
  renderer dirty partition revision. Stale source, deliberately
  invalid FaceId, missing source, small query allowance,
  clean no-contact source, OFF source and untrusted disk
  checkpoint all refuse; a refused budget query is followed
  by a positive request on the SAME worker. Actual undo/redo
  restores original picker root and one history entry without
  executing a second Boolean. The separately authenticated
  in-memory checkpoint constructor verifies supplied original
  FaceIds against its original sealed anchor set and
  acceptedTransitions==0; disk V13 can never supply it.
  No desktop mode is enabled yet: M48/M49 and original M15
  remain OPEN. Substantive [Actions #39345076059](https://github.com/logiclrd/Mesher/actions/runs/39345076059)
  passed GCC, Clang, Windows MSVC /W4 /WX, 23 CPU suites,
  repeated checkpoint stress and applicable benchmarks.
  See `docs/V17_M47_BOUNDED_LIVE_MERGE_DISCOVERY.md`.
  Exact final PR-head and separate merged-main CI remain
  mandatory before reporting this prerequisite merged-complete.
- [x] **V17-M48 — Explicit default-OFF Vulkan desktop Merge input and mode.**
  Extend the viewport interaction contract with a visible
  default-OFF, user-toggled Merge geometry mode and bounded
  async request route, without an invisible automatic union
  on ordinary mouse-down. Pick/capture the requested
  source from an immutable indexed revision and execute
  only after idle; OFF preserves legacy mouse Inflate
  semantics and no protected-source work. ON uses trusted
  source authority and refuses rather than substituting
  arbitrary XYZ-coincident faces. Show accepted/refused/
  pending and fatal-terminal status in title/HUD/console;
  no callbacks to Vulkan from worker or blocking full
  geometry traversals on the render/picking thread.
  **M48 implemented PR #101 — explicitly provisioned default-OFF desktop:**
  Normal invocation is unchanged and does NOT create a protected
  worker, synthesize source authority or issue any Boolean request.
  Only `--merge-geometry` on a fresh interactive in-memory source
  provisions the sealed M29 physicalExact worker, and even in that
  authorized session the Vulkan Merge selection MODE initially remains
  OFF. The command line rejects invalid demo/comparison modes and
  disk `--recover` without opaque M29 first-original credentials.
  Explicit M key toggles visible ON/OFF mode; while ON, ordinary LMB
  inflation is suppressed and each fresh LMB click ray-tests the
  displayed immutable revision's same persistent picking BVH for
  stable `FaceId`+hit. Up to 32 unique stable face nominations bind
  to EXACTLY one immutable displayed indexed root; switching OFF,
  Backspace clear or any published new index clears nominations.
  Enter explicitly sends that expected root plus nominated FaceIds
  to M47's serialized fully bounded worker contact/lineage validator
  and source Boolean, without a renderer-thread all-face/contact scan.
  Missing source, unsupported geometry, invalid original FaceIds
  and stale root continue to refuse, never substitute coincident
  XYZ. GPU overlay and window title show OFF/ON, selected count,
  accepted/refused/pending and fatal-worker status; console
  records selections, submission and eventual refusal. A separate
  atomic outstanding-command counter prevents falsely displaying
  zero pending jobs between enqueue and worker completion, and
  stops new user strokes during a pending Merge. Typed
  `firstRayFaceHit` is tested against the legacy position-only
  BVH ray pick using SAME geometry, and all 23 CPU suites plus
  full Vulkan GCC/Clang/MSVC compile gates apply.
  This is a restrictive EXPLICIT AUTHORIZATION, not an
  unqualified UI-only upgrade of an arbitrary older sculpt
  history to protected first-original credentials. In sessions
  launched with `--merge-geometry`, OFF mode selects Inflate
  but retains the explicitly requested protected physics
  worker. Normal launches keep the prior legacy worker.
  Human-observed Linux/Windows Vulkan interaction and
  mode/input end-to-end acceptance are independently planned
  M49 and ORIGINAL M15 and are NOT claimed by CI.
  See `docs/V17_M48_DESKTOP_OPT_IN_ACCEPTANCE.md`.
  Substantive CI [Actions #39348475445](https://github.com/logiclrd/Mesher/actions/runs/39348475445)
  started after the precise outstanding-command fix;
  exact documentation-final and independent merged-main
  full compiler matrix remain mandatory.
- [x] **V17-M49 — Targeted desktop integration and compiler acceptance.**
  Exercise a CPU-testable representation of the exact
  GLFW input/mode state machine, default OFF and
  enable-disable, current-root stale behavior, unsupported/
  ambiguous/no-contact refusal, symmetric/self-mirrored
  fixed-point, asymmetry, picking and dirty-owner render
  handoff, bounded undo/redo and V13 save/recover
  fail-closed provenance. Confirm full Linux GCC/Clang
  and Windows MSVC Vulkan build, 23 CPU suites and
  checkpoint stress. No claim of user-observed rendered
  Vulkan runtime validation from headless CI.
  Preserve separate V18 off-mode collision control and
  V19 broader human-observed end-to-end acceptance.

  **M49 separately implemented and validated, PR #102:**
  `DesktopMergeInput` and `DesktopMergeKeyEdges` are the
  EXACT testable C++ controllers called by the GLFW/Vulkan
  viewport—not an independent test-only surrogate. The
  editor always starts Merge mode OFF, including explicit
  `--merge-geometry` protected-authority sessions. An
  absent startup opt-in can never authorize mode ON.
  Real M-key rising edges toggle ON/OFF only while idle;
  held M/Enter/Backspace cannot toggle, submit or clear
  repeatedly. The ON-mode LMB source choice binds up
  to 32 UNIQUE FaceIds to one displayed immutable root;
  no-hit, zero/duplicate, busy, 33rd selection, toggle
  OFF, clear, new displayed revision and stale submit
  cases are CPU tested. Enter assembles one exact root
  and explicitly nominated immutable FaceIds and queues
  the actual M47 whole-root bounded source checker;
  none of these input cases scans all geometry on the
  Vulkan thread, auto-welds XYZ coincidences or changes
  the default OFF Inflate behavior. Genuine supported
  asymmetric and reciprocal reflected source unions
  via this same input controller and serialized FIFO
  each publish one source-authenticated COW root,
  history entry and bounded dirty-owner renderer
  snapshot. First-original FaceId outside the initial
  sealed source refuses atomically. Real undo/redo
  restores the prior indexed picker and accepted
  source without rebilling geometry. V13 saved/reloaded
  checkpoint preserves protected-source REQUIRED
  state but never forges sealed opaque M29 authority.
  Four additional REAL `Mesher` executable-level
  no-Vulkan-instance CI checks cover `--help`
  default OFF and three incompatible `--merge-geometry`
  combinations (demo, disk recovery, noninteractive).
  Windows hosted CI deliberately installs a stripped
  Vulkan SDK with no vulkan-1.dll; MSVC `Mesher`
  now delay-loads Vulkan so CLI argument refusal
  executes without a GPU/runtime, while actual
  interactive Vulkan rendering still requires a
  genuine Vulkan loader. This fixes the earlier
  Windows loader exception 0xc0000135, NOT by
  making a fake Vulkan runtime or weakening policy.
  Substantive [Actions #39365774949](https://github.com/logiclrd/Mesher/actions/runs/39365774949)
  GREEN Linux GCC, Linux Clang, Windows MSVC
  /W4 /WX, ALL **27/27 CPU+desktop CLI tests**,
  repeated 100-run checkpoint stress and applicable
  benchmarks. No headless CI run proves user-observed
  rendered desktop interaction. See
  `docs/V17_M49_DESKTOP_INTEGRATION_ACCEPTANCE.md`.
  The exact documentation-final PR-head CI and
  separate merged-main push CI remain required.
  ORIGINAL M15 is STILL UNCHECKED, followed
  separately by V18/V19.
**Then execute ORIGINAL V17-M15** integrated criterion-by-criterion
review only after M47–M49 have independent acceptance. Keep M15,
V18 and V19 unchecked until their own gates are satisfied.

**October 20 original M15 integrated acceptance replan (flat peers, before implementation).**
The M47→M48→M49 implementation and headless CI gates are complete. A
new reproducibility gap remains: the actual `Mesher --merge-geometry`
desktop begins with a generated sphere, whereas supported positive
explicit live Merge regression sources are constructed only in CPU
tests. Visible FaceId selection alone does not provide a reproducible
source-positive Vulkan session. Retain the **unchanged original M15**
checkbox and ALL Linux/Windows rendered desktop acceptance objectives.
Complete the following independent peers in order; do not misrepresent
headless tests as GPU observations or automatically nominate all source
faces, bypass original intent, or widen source geometry authorization:

- [x] **V17-M50 — Explicit asymmetric source-positive desktop fixture.**
  Add a command-line-only, deliberately labeled supported asymmetric
  two-tetra FIRST-original fixture, *requiring* `--merge-geometry` and
  asymmetric mode. Use identical independently source-certified geometry
  as the tested M47/M49 exact Boolean positive; give it a real sealed
  protected worker and render/pick through the ordinary production
  GLFW/Vulkan path. Legacy launch, disk recovery, unrelated modes and
  selection semantics stay unchanged. Add headless gate refusals,
  shared-fixture CPU positive worker/protected-root regression and
  reproducible interactive instructions. This peer does NOT claim that
  a human has observed a Vulkan render.
  Accepted source and regression evidence: PR #104 substantive head
  `826cfba0` Actions #39375926773 SUCCESS GCC/Clang/Windows MSVC,
  29 viewport-enabled CTests, 100-repeat checkpoint-history stress
  and applicable benchmarks. No rendered GPU acceptance is claimed.
  Dedicated record: `docs/V17_M50_DESKTOP_ASYMMETRIC_FIXTURE.md`.
  Recheck the exact final doc-head and independent merged-main CI.
- [x] **V17-M51 — Mirrored positive fixture and complete explicit source
  nomination from the rendered desktop.** Independently construct the
  test's reciprocal, original-proven reflected two-tetra source (not a
  synthetic sphere mirror map); provide a deliberate bounded user
  mechanism to nominate otherwise occluded first-original faces
  without automatically interpreting coordinate proximity as source
  identity. Preserve 32-ID cap, immutable displayed-root binding,
  root stale checks, ON input suppression, worker-only whole-contact
  discovery and all original mirror, protected intent and budget
  limits. Independently CPU-test complete asymmetric AND reflected
  UI-to-worker positive selection, no-hit/foreign/duplicate/refusal
  and real renderer mailbox/history/undo.
  Independently accepted in PR #105 on substantive source
  `073763fc`: [Actions #39378257678](https://github.com/logiclrd/Mesher/actions/runs/39378257678)
  SUCCESS full GCC, Clang, Windows MSVC /W4 /WX Vulkan builds, all
  32 viewport-enabled CTests (23 CPU and 9 real headless executable CLI),
  repeated checkpoint-history synchronization and applicable benchmarks.
  The runtime uses independently certified mirrored first-original
  stable IDs, ON-only rising-edge A selection of 8/16 exact original
  FaceIds, atomic <=32 quota and expected initial displayed root,
  same camera-scale Vulkan projection/CPU picking, and actual
  protected queued positive asymmetric/reflected unions with one
  dirty immutable frame and pointer-exact undo/redo. Full details:
  `docs/V17_M51_REFLECTED_DESKTOP_AND_NOMINATION.md`. Real
  *observed* Windows/Linux rendered GUI remains exclusively M52.
  Recheck the final documentation-head and independent merged-main CI.
- [x] **V17-M53 — Right-drag rigid viewport orbit with geometry-hit or centroid-Y-plane pivot.**
  Before M52 GPU observation, provide a production GLFW right-button
  drag without editing source geometry. At mouse-down ray-pick the
  *currently displayed immutable surface* using the inverse of the
  rendered scene transform; use the exact hit as the fixed pivot.
  If the ray misses geometry, intersect that ray with the XZ plane
  at the current geometry's centroid Y (handle parallel/no-forward
  intersection deterministically). Horizontal mouse delta rotates
  the scene about world Z; vertical delta about world X; sign chosen
  so nearest visible material follows cursor motion. Persist
  accumulated rotation and pivot across frames, freeze at release;
  handle resize, high-DPI, changed COW root, picking, Vulkan wire/
  shaded frame, grid and ordinary left Inflate/ON Merge without
  ever changing worker geometry/FaceIds or checkpoints. Shared
  CPU-only orbit/ray/pivot mathematical regression, Linux GCC/
  Clang and Windows MSVC full CTest matrix; M52 separately
  observes actual GPU/desktop ergonomics.
  Accepted: PR #106 substantive head `e64d739a`,
  [Actions #39455771691](https://github.com/logiclrd/Mesher/actions/runs/39455771691)
  SUCCESS Linux GCC, Linux Clang and Windows MSVC Vulkan-enabled
  /W4 /WX full CTest, threading/checkpoint stress and applicable
  benchmarks. Actual production GLFW RMB input uses exact indexed
  surface or current displayed vertex-centroid Y-plane pivot,
  source-inverse typed picker rays and render matrix. The new
  independent CPU ray/pivot tests all pass. Fixed both A-key
  literal-escaped newline diagnostics. Details:
  `docs/V17_M53_RIGHT_MOUSE_VIEWPORT_ORBIT.md`.
  Do NOT check M52 or M54 from this evidence.
- [x] **V17-M55 — Screen-parallel background pivot, Alt+RMB view-direction dolly, and drag-edge cursor wrapping.**
  Refine the production M53 viewport without changing its original-source
  mesh/CAS/history contracts. For a background RMB gesture, intersect the
  current *display-space* mouse ray with the plane through the
  scene-transformed CURRENT geometry centroid and perpendicular to
  the current centre-screen viewing direction; inverse-transform the
  resulting fixed pivot to original source coordinates. Preserve exact
  immutable BVH hits for foreground clicks, and finite parallel/behind
  ray refusals. At RMB press, latch Alt to select a different gesture:
  horizontal cursor delta moves the view forward/backward along the
  central viewing ray (no implicit lateral pan or geometry changes),
  rather than yaw/pitch. While either RMB gesture is active, wrap
  cursor crossings at the GLFW client-area edges to the opposite edge,
  with no synthetic giant movement from the warp, support DPI/resize,
  leave normal mouse motion untouched outside gestures, and degrade
  safely where the compositor refuses cursor warping. Independently
  CPU-test rotated/translated screen-parallel pivot geometry,
  unchanged source-pick inverse, dolly sign and magnitude,
  gesture latched behavior, and repeated boundary-wrap delta
  continuity. Full Vulkan-enabled GCC, Clang and MSVC matrix; do not
  represent a headless test as observed Windows/Linux graphics runtime.
  After acceptance resume M54 actual reflected-union topology audit,
  then M52 human-observed GPU acceptance and original M15.
  Accepted independently in PR #107 on substantive head `763573d9`:
  [Actions #39460369523](https://github.com/logiclrd/Mesher/actions/runs/39460369523)
  SUCCESS full Linux GCC, Linux Clang and Windows MSVC /W4 /WX
  Vulkan-enabled CI, 32 viewport-enabled CTests, repeated
  history/checkpoint threaded regression and applicable benchmarks.
  The actual viewport uses transformed-centroid screen-normal
  visual-ray background pivot, latched Alt-RMB centre-ray camera
  translation, and synthetic-delta-safe client-edge GLFW warp with
  optional compositor refusal fallback. Ordinary immutable original
  FaceId/worker paths remain unchanged. Detailed acceptance:
  `docs/V17_M55_SCREEN_PARALLEL_ALT_DOLLY_CURSOR_WRAP.md`.
  Final documentation PR-head and independently merged-main CI
  still gate repository integration; actual GPU host observation
  remains strictly M52, and suspected internal union edges M54.
- [x] **V17-M56 — Correct depth-biased wireframe leakage and expose solid-only diagnostic.**
  The user's second Linux reflected original/accepted screenshots and
  rotated observation identify apparent rear-surface edges passing in
  front of nearer solid. Source inspection reveals actual Vulkan
  D32_SFLOAT-preferred/D24 fallback depth, solid depth writes, wire
  depth test and no depth write, BUT `shaders/wire.vert` deliberately
  subtracts `0.00012 * gl_Position.w` from wire clip Z. This shifts
  every projected edge toward the eye by roughly thousands of depth
  quantization increments, so near/behind unrelated surfaces may
  fail to occlude true hidden edges. Independently fix wire/surface
  occlusion without changing model geometry or source proofs:
  remove globally applied wire clip-space pull and retain
  `VK_COMPARE_OP_LESS_OR_EQUAL` with solid-written depth, add
  explicit W rising-edge solid-only/wire overlay toggle to
  distinguish genuine cut geometry from apparently penetrating
  background wires, guard against accidental reintroduction of
  large shader bias, and document expected possible coplanar edge
  rasterization behavior and runtime actual-GPU check. Verify full
  Vulkan-enabled GCC/Clang/Windows MSVC warning-clean CI/CTest
  on exact PR head and independently merged main. **Do not** infer
  M51 accepted union is manifold; proceed with original M54
  independent edge-incidence/residual-geometry audit afterwards.
  Independently passed substantive PR #108 head `d788ce87`:
  [Actions #39464142614](https://github.com/logiclrd/Mesher/actions/runs/39464142614)
  SUCCESS full Vulkan-enabled Linux GCC, Linux Clang and Windows
  MSVC /W4 /WX tests (33 viewport-enabled CTests), repeated
  checkpoint/history stress and benchmarks.
  Wire clip-Z pull removed; solid-written depth and LEQUAL wire
  read-only comparison retained; W toggles solid-only/edges with
  window title and console diagnostics. Shader/pipeline regression
  CTest and detailed analysis:
  `docs/V17_M56_WIRE_DEPTH_OCCLUSION.md`.
  Does NOT satisfy M54 manifold or M52 observed GPU acceptance.
  Final documentation-head and independent merged-main CI remain
  separate repository integration gates.
- [x] **V17-M54 — Explicit accepted-union manifold and residual-intersection audit.**
  Investigate the reported reflected union where triangle edges
  visually plunge into its surface even though M51 worker reports
  `A 1 R 0 P 0`. Add an independent bounded CPU diagnostic on
  the actual accepted fixture roots, before/after and undo/redo:
  per-undirected-edge opposite-directed two-face incidence,
  component count and connected shells, orientability, residual
  nonadjacent triangle crossings / internal overlapping fragments,
  and evidence against false-positive edge-wire occlusion. Separate
  observed failure from presentation artifact; if structural defects
  exist, record independently completable follow-on repairs in
  todo.md BEFORE beginning any partial repair and keep original
  M52/M15 acceptance open until the defect is solved. Never treat
  worker acceptance counters as manifold proof.
  Measured directly in independent CTest
  `accepted_union_structural_audit` on the **actual** worker root
  submitted through M51 UI selection:
  reflected before: V16/E24/F16, four closed components,
  18 transverse triangle contacts; accepted: V32/E84/F56,
  two genus-zero closed components, equal signed volumes
  2.3434 each, ZERO open/nonmanifold/winding/degenerate/duplicate
  face failures and ZERO residual intersection or ambiguous pairs.
  Independent CPU partition renderer staging emits exactly
  84 distinct live original-root edges, ZERO missing, ghost
  or duplicate wires. One-Step undo and redo restore pointer-exact
  roots and repeat these counts. Asymmetric positive control:
  V8/E12/F8/two components/nine crossings → V16/E42/F28/
  one closed component/zero crossings. Logged in actual GCC
  [Actions #39471107991](https://github.com/logiclrd/Mesher/actions/runs/39471107991)
  alongside full GCC/Clang/Windows MSVC successful CTest, including
  34 registered viewport-enabled tests and history/checkpoint stress.
  These source/staging observations DO NOT establish GPU pixel
  occlusion. A further independently discovered floor/fixture
  intersection is planned in M57; actual rendered Linux and
  Windows Vulkan evidence remains M52 and original M15.
- [x] **V17-M57 — Prevent the fixed Z=-1.22 work floor from slicing source-positive tetrahedral fixtures.**
  Following M54's proven accepted topology and source-consistent
  render wires, diagnose an independent viewport model flaw:
  `buildDrawing()` unconditionally creates a **depth-writing**
  XY floor at Z=-1.22, originally below the unit sphere, while
  both current M50/M51 tetrahedral fixture sources extend to
  Z=-3 and therefore are PHYSICALLY intersected and occluded
  by that floor. Implement an explicit finite fixture/bounds-aware
  floor offset below the complete initial source geometry,
  preserve the legacy sphere default, and provide a deliberate
  diagnostic ability to hide/show the floor+grid without hiding
  the immutable COW solids. Prevent an old unpartitioned flat
  source mesh from accidentally overlaying COW revisions;
  protect W solid-only/edge display, physical picker rays and
  worker original identities, and avoid adding render-thread
  full-mesh traversals. CPU-test original and reflected
  fixture floor clearance and render geometry separation,
  actual GLFW/Vulkan CMake/compiler integration and regression
  including Windows MSVC /W4 /WX. Remain explicit that even
  this only fixes a **known viewport-plane intersection**;
  actual GPU occlusion and wire rasterization must still be
  human-observed in M52. M54 topology result stays unchanged.
  Acceptance: PR #110 substantive head `2a42f65f`,
  [Actions #39474626696](https://github.com/logiclrd/Mesher/actions/runs/39474626696)
  SUCCESS full Linux GCC, Linux Clang, Windows MSVC /W4 /WX
  Vulkan-enabled builds, **36 registered desktop CTests**,
  repeated checkpoint/history stress and benchmarks. Both real
  source-positive tetra fixtures are proven to have initial
  minimum Z=-3, while the separate scenic floor sits at Z=-3.5;
  unit-sphere legacy Z=-1.22 is preserved. Dedicated floor
  solid/wire GPU buffers, G hide/show key independent of W
  wireframe, and real separate draw guards preserve immutable
  COW solids. Windows CI initially caught an existing test-only
  race reading a history-depth status before its separately
  published snapshot. The bounded `testWorkerMirroredUnionMirrorHistoryAndCheckpoint`
  observer now waits for matching exact root **and** history;
  no production worker algorithm changed. Details:
  `docs/V17_M57_FLOOR_CLEARANCE.md`.
  Source geometry remains independently certified by M54.
  Human-observed actual Vulkan screen pixel behavior still M52,
  original M15 remains OPEN. Final PR-head and independently
  merged-main CI are separate integration gates.
- [x] **V17-M59 — Restore viewport startup for indexed COW meshes after scenic floor separation.**
  User-reported regression immediately after merged M57: opening normal Mesher
  throws `Empty vertex buffer` before drawing a frame. M57 correctly removed
  duplicated legacy geometry when `displayedIndex_` exists, but the Vulkan
  constructor still unconditionally calls `upload()` for the resulting
  empty legacy solid/wire arrays; `upload()` properly rejects zero-byte
  vertex buffers. Preserve the user's intervening independent
  literal-newline fix (main `18ed1278`) and isolate the solution from the
  ongoing V17-M58 antialiased wireframe PR. Guard optional legacy uploads,
  leave immutable partition initialization and real floor buffers untouched,
  and never bind the resulting null Vulkan buffer handles in the renderer.
  Add a real registered Vulkan-enabled CTest source-contract guard covering
  indexed startup, zero-count vertex draws, and the preserved strict upload
  API; run GCC, Clang and Windows MSVC /W4 /WX full CTest, stress and
  benchmark matrix on the final PR head and independently on merged main.
  This prevents the proven immediate exception; a true GPU runtime smoke
  test still requires human-observed M52, not unsupported headless claims.
  Accepted source and registered regression on PR #112 implementation
  head `286d46a5`:
  [Actions #39482201808](https://github.com/logiclrd/Mesher/actions/runs/39482201808)
  SUCCESS Linux GCC, Linux Clang and Windows MSVC /W4 /WX full
  Vulkan-enabled compilation, 37 registered CTests,
  repeated history/checkpoint stress and applicable benchmarks.
  Empty flat legacy arrays are skipped at both allocation and
  draw-time while real COW partitions still initialize and render.
  Strict empty `upload()` refusal remains intact. See
  `docs/V17_M59_EMPTY_OPTIONAL_GPU_BUFFERS.md`.
  Final documentation-exact PR head and independent merged-main
  builds remain separate gates; real GPU launch must be
  rechecked by the user and is not proved by this headless test.
- [x] **V17-M58 — Antialiased perceptibly thick mesh/grid wires without hidden-edge leak.**
  Real Linux post-M56 screenshot confirms correct depth ordering but
  native Vulkan 1-pixel LINE_LIST edges alias to broken subpixel
  segments in both COW mesh and G-controlled floor grid. Replace
  the device-dependent thin native line raster with a deterministic
  screen-space widened segment quad, smooth alpha coverage using
  fragment blending and physical framebuffer dimensions, with a
  consistent 2+ pixel nominal stroke across viewport DPI/resize.
  Preserve identical true projected vertex depth, existing solid
  depth-write and wire LEQUAL depth-test/no-write ordering; **never**
  restore M56's globally shifted clip Z or display rear hidden
  geometry over solid. Expand existing canonical owner wire segments
  only at bounded GPU upload/dirty-replacement time, not via a
  live full-revision scan; do not change stable FaceIds, immutable
  source mesh, cut geometry, original lifetime, mirror or work
  limits. Make G scenic grid and W wire-hide continue to operate
  independently. Handle zero-length, near-clip and direction
  degeneracies in shader without stretched screen artifacts.
  CPU test precise per-original-segment six-vertex quad encoding
  and source-buffer immutability, validate shader and Vulkan
  depth/blend/pipeline contract, and gate exact final PR head plus
  independent merged-main Vulkan-enabled GCC, Clang and Windows
  MSVC /W4 /WX full CPU/CLI/stress/benchmark CI. Real X11/Wayland
  and Windows driver pixel appearance still requires M52 observed
  acceptance; original M15 stays open.
  M58 reconciled on PR #111 against M59 startup fix and the
  user's independent literal-newline correction, preserving
  guarded indexed-COW empty legacy allocations and null draws.
  The exact integrated implementation head `f7308822` passed
  [Actions #39491852308](https://github.com/logiclrd/Mesher/actions/runs/39491852308):
  Linux GCC, Linux Clang and Windows MSVC /W4 /WX, Vulkan shader
  compilation, **39/39 desktop CTests** on GCC, repeated
  checkpoint/history stress and applicable benchmarks. The CPU
  six-corner AA endpoint encoding, M56 depth ordering, M57 floor
  separation and M59 optional-buffer startup source contracts pass.
  Independent merged-main CI is required after PR merge;
  real Linux/Windows visible GPU acceptance remains M52.
- [x] **V17-M60 — Consistent Z-up turntable RMB orbit without accumulated roll.**
  User-observed defect: repeated circular right-mouse motions currently
  compose noncommuting incremental Z/X rotations and cause unintended
  apparent 360-degree roll. Replace incremental free composition with
  a true 2-degree-of-freedom yaw-about-global-Z and pitch-about-fixed-
  display-X turntable orientation, with NO roll component and a finite
  non-inverting pitch clamp adapted to the fixed elevated camera view.
  Preserve M53 exact picked-source and M55 screen-parallel background
  pivot for each RMB gesture, accurate source inverse picking,
  previously accumulated view-relative dolly/translation, immutable
  mesh/history/worker/certificate and symmetric source identity.
  Preserve direction and HiDPI mouse sensitivity, genuine W/G display
  controls, Alt-RMB dolly latch, edge wrapping, and invalid input
  rejection. CPU tests must prove circular/closed gestures return to
  the original orientation without roll, yaw under arbitrary pitch,
  upright world Z throughout sustained 360-degree yaw, finite pitch
  clamping/reversal, non-origin source pivot stability, screen-parallel
  background ray and inverse picking after interleaved dolly/rotation,
  and unchanged invalid-input state. Three-platform Vulkan-enabled
  compiler/CTest/stress/benchmark CI must pass on exact final PR head
  and independently on merged main. Actual GPU interactions remain
  M52; original M15/V17 and V18/V19 remain open.
  PR #113 implementation head `3c596743` was independently accepted
  in [Actions #39498850898](https://github.com/logiclrd/Mesher/actions/runs/39498850898):
  Vulkan-enabled Linux GCC, Linux Clang and Windows MSVC /W4 /WX
  complete workflows GREEN. GCC registered/passed **39/39 CTests**
  including `mouse_picking` with hundreds of closed RMB gestures,
  upright 360-degree yaw, pitch limits, original pivot, post-dolly
  inverse picking and input validation, plus threaded history/stress
  and applicable benchmarks. Source geometry/worker remain unchanged.
  Final documented PR-head and independently merged-main CI are
  separate acceptance gates, and real GPU observation remains M52.
- [x] **V17-M61 — Ctrl-modified view-relative pan/dolly and exact upright pole views.**
  User requested Ctrl+LMB view-relative strafe (camera translation
  along displayed screen-right/up), move Alt+RMB dolly to Ctrl+RMB,
  reserve Alt for later inverse brush without activating brush today,
  and extend M60 turntable to full top/bottom 90-degree camera
  elevations. Preserve ordinary RMB orbit, unmodified LMB
  sculpt/first-original Merge FaceId selection, M55 background/
  surface pivots, existing cursor wrap, mode latching at button-down
  and no view gesture ever enqueuing worker strokes or Merge. Avoid
  toggling modalities mid-drag, preserve inverse source picking
  across arbitrary orbit/pan/dolly sequences, use real framebuffer
  pixels for stable DPI scale, clamp camera elevation at poles while
  never inverting the up vector, and reject invalid motion inputs.
  Unit tests must exercise axes, sign, original immutable pivot,
  near/at both poles and reverse, round trips, inverse picking,
  modifier latching/precedence, invalid input, and viewport source
  wiring. Verify full Vulkan GCC/Clang/Windows MSVC /W4 /WX CI at
  exact PR head and independent merged main. Real visual input on
  Linux/Windows Vulkan remains M52, original M15 and V17 open.
  PR #114 implementation `6a7f2535` passed
  [Actions #39503101018](https://github.com/logiclrd/Mesher/actions/runs/39503101018):
  full Vulkan-enabled Linux GCC/Clang and Windows MSVC /W4 /WX,
  all 40 registered CPU/CLI/source-contract CTests, history/checkpoint
  threading stress and applicable benchmarks. The initial X11 build
  found a colliding `None` macro; renamed enum to `Inactive` and
  reran the complete compiler matrix GREEN. Actual visual tests
  remain M52; final exact-head and merged-main CI separately required.
- [x] **V17-M62 — Calibrate AA stroke thickness and diagnose occlusion-side asymmetry.**
  User's real Linux screenshot confirms M58 removed subpixel gaps and
  preserves occlusion, but the presently 2-pixel HALFwidth with a
  one-pixel alpha ramp produces heavy/uneven strokes. Independently
  distinguish central pixel coverage and depth occlusion of widened
  quads (near an adjacent reversed-facing triangle), avoid naive
  globally halving width, and choose perceptually lighter physically
  pixel-based AA coverage while retaining sufficiently readable
  silhouette/cropped half-strokes. Add independent analytic coverage
  tests and shader/real Vulkan depth contract gates; explicitly
  document intrinsic one-sided depth occlusion vs a genuine stroke
  rasterization defect. Do NOT reintroduce the M56 unconditional
  clip-depth pull, expose backface/hidden edges, change permanent
  FaceId geometry, or replace source-certified topology. W/G remain
  independent, M59 startup regression protected, and genuine
  visual cross-driver approval remains M52; full PR-head and merged
  GCC/Clang/Windows Vulkan CI required. If exact symmetry on
  silhouette needs a separate renderer design, keep M62's acceptance
  strictly to proven improvement and pre-plan any necessary peer
  milestone before proceeding, rather than silently weakening safety.
  PR #115 implementation `dbf45018` passed
  [Actions #39503750610](https://github.com/logiclrd/Mesher/actions/runs/39503750610):
  full Vulkan-enabled Linux GCC, Linux Clang, and Windows MSVC /W4
  /WX build/test/stress/benchmark GREEN; **41/41 CTests** on GCC.
  Includes 1.5px halfwidth smooth coverage integration vs M58's
  2.0px baseline, rigorous CPU symmetric/full/half ink bounds,
  independent Vulkan source/depth contract, and intact M59 startup.
  The user's observed asymmetric *visible* width near adjacent
  nearer faces can be physically correct depth masking, so this
  peer does not claim unsupported exact silhouette symmetry.
  Full real Vulkan display confirmation remains M52; future
  surface-aware silhouette work would require separate proof and
  planning, not removal of LEQUAL occlusion. Final PR-head and
  independently merged-main CI required.
- [x] **V17-M63 — Reverse Ctrl+LMB strafe to grab-and-drag the world.**
  Actual Linux desktop observation after M61: user expects Ctrl+LMB
  right/down movement to move visible world right/down, rather than
  dragging the camera right/down. Reverse both screen-relative
  translation signs in the UI-only ViewportOrbit strafe helper,
  update CPU sign, axis, round-trip, HiDPI, invalid-input and
  orbit/dolly/inverse-picking regressions plus developer README.
  Preserve all modifier latching and Ctrl+RMB dolly, ordinary RMB
  yaw/pitch and unmodified LMB brush/FaceId selection. No mesh
  changes, no persistent camera state, and never alter worker,
  Boolean, history, or source certification. Check exact PR-head
  and independently merged-main Vulkan-enabled Linux GCC/Clang
  and Windows MSVC tests, stresses and benchmarks.
  PR #116 implementation `f3be09a5` passed
  [Actions #39506698986](https://github.com/logiclrd/Mesher/actions/runs/39506698986)
  on Vulkan-enabled Linux GCC, Clang, and Windows MSVC /W4 /WX,
  including **41/41 CTests** (mouse_picking updated world-grab
  signs, roundtrip, HiDPI and exact inverse source-space picker),
  repeated worker history/checkpoint stress and benchmarks.
  Exact documentation-head PR and merged-main CI remain
  independent gates; actual new polarity on Linux remains M52.
- [x] **V17-M64 — Diagnose the pole-dependent wire width and specify a depth-safe renderer replacement (planned prerequisite).**
  User's real Linux near-top and exactly-top screenshots show both
  geometry wires and planar floor grid suddenly appear thicker
  at the camera's +90-degree pole. The fixed 1.5px shader
  halfwidth does NOT change with pitch. Hypothesis: M58 GPU wire
  quads preserve the edge's centerline depth even where the quad
  is laterally displaced in screen space. At oblique angles an
  actual filled face has a depth gradient across the expanded
  footprint, so one side of the wire fails LEQUAL; exactly
  top-down the horizontal floor has zero depth slope and both
  sides pass. First reproduce/refute analytically with distinct
  sloped-plane/front-face and coplanar/backface controls, then
  design a certified visibility-aware correction that keeps
  consistently narrow antialiased strokes across pole entry,
  handles scene/world/floor and dirty COW partitions, and never
  globally pulls clip-Z forward or leaks hidden/rear wires.
  If safe fix requires new per-face GPU ownership/visibility or
  additional pipeline resources, preplan bounded independent
  follow-on milestones before coding; do not substitute a
  depth-bias shortcut. CPU/GLSL/Vulkan contracts and full
  three-platform CI, real Linux/Windows visual acceptance M52.
  REPLANNED as a flat prerequisite after source audit: no single
  globally shifted edge quad can correct both genuine hidden-surface
  occlusion and incorrect self-masking by its owning sloped face.
  M64 now independently proves depth-slope asymmetry (including
  near-pole and exactly-pole, front/rear and other occluders), audits
  solid triangle and floor grid staging and selects a no-depth-bias
  face-/surface-anchored coverage design. Implement and cleanly
  integrate through M67, M68 and M69 below, all planned BEFORE
  renderer modifications; preserve M64's original full objectives
  across this explicit sequence. A source-contract/CPU analytic
  regression and document are M64 deliverables, not a claim of
  Vulkan visual correction.
  M64 analytical/source-contract implementation at `bcd1c86b`
  passed [Actions #39514057701](https://github.com/logiclrd/Mesher/actions/runs/39514057701):
  Vulkan-enabled Linux GCC, Linux Clang, Windows MSVC /W4 /WX
  full CPU test, history stress and benchmark workflows GREEN.
  The CPU tests explicitly establish 2px zero-slope vs ~1px
  nonzero-slope ink, reversed normal-gradient masking, and
  preservation of genuine closer occlusion. No runtime shader,
  Vulkan draw, source geometry or worker code was changed.
  Next implementation peers M67, M68 and M69 remain OPEN until
  independently verified; M52 human observation remains OPEN.

- [x] **V17-M67 — Face-anchored antialiased mesh wires with correct per-fragment depth.**
  Derive screen-space triangle barycentric distance from the actual
  solid triangle raster (non-indexed 3-vertex mesh triangles), so
  edges shade only fragments of the DEPTH-VISIBLE owning solid face.
  No global line clip-Z pull, no shader interpolation of source
  edge-center depth outside the face; preserve original triangle
  COW staging, dirty partitions, reflected winding, source IDs,
  two-sided interior inspection, and source picking. Match M62's
  ~2px full/1px silhouette effective ink, W toggling, exact
  noperspective pixel coverage under near/pole obliquity, backface
  and occluding solid surfaces. Keep floor grid on legacy path
  until independently replaced by M68; avoid accidentally
  rendering the floor's internal triangulation diagonal. Add
  mathematically grounded shader/C++ contract and three-platform
  Vulkan-enabled CI, plus independent merged-main verification.
  M67 implementation head `f4a94ae4` passed
  [Actions #39515294505](https://github.com/logiclrd/Mesher/actions/runs/39515294505):
  full Vulkan Linux GCC/Clang and Windows MSVC /W4 /WX GREEN,
  including **45 CTests** on GCC and the new barycentric edge
  gradient/actual draw-path contracts. Historical M56/M57/M58/M62
  tests were deliberately updated to certify the split mesh
  surface/floor grid without weakening LEQUAL or W/G semantics.
  Floor still uses legacy quads until M68; real Linux/Windows
  device visual approval is M52, and M69 GPU cleanup remains open.
  Final documented-head and independent merged-main CI required.
- [x] **V17-M68 — Surface-anchored floor grid coverage at arbitrary camera elevation.**
  Replace the floor's independent clip-depth-flat AA quad overlay
  with view-projected distance to integer XY grid lines evaluated
  ON the visible floor fragment at its actual surface depth.
  Keep the same 1-unit grid and z-plane bounds, no diagonal
  from the floor's triangulation, G whole-floor visibility,
  W independent grid visibility, HiDPI consistency and M57
  source-relative floor clearance. Reject hidden floor through
  nearer mesh. CPU analytic shader/source contracts and full
  three-platform Vulkan-enabled CI plus independent merged-main.
  M68 implementation `b00067ee` passed
  [Actions #39515619434](https://github.com/logiclrd/Mesher/actions/runs/39515619434)
  full Vulkan-enabled Linux GCC, Clang and Windows MSVC /W4 /WX
  builds and tests GREEN, including **47 registered CTests** on GCC
  for integer-grid pixel distance and actual W/G floor draw routing.
  No fake floor-triangulation diagonal and no independent detached
  grid GPU draw, with no change to genuine mesh occlusion.
  M69 old GPU buffer/pipeline cleanup remains separately open,
  human-observed Linux/Windows pole and hidden-edge verification
  remains M52, original M15/parent V17 still open.
  Final documented PR-head and independent merged-main CI required.
- [x] **V17-M69 — Retire obsolete clip-depth-flat wire quads and certify whole viewport.**
  Only after M67 mesh AND M68 floor have independently passed,
  remove dead AA wire vertex buffers, uploads and pipelines;
  reconcile M56/M58/M59/M62 regression source contracts, COW
  cache/dirty owner behavior, indexed empty-startup guard,
  renderer performance/memory overhead and unchanged W/G input.
  Revalidate dense/sparse surfaces, manifold/interior arms,
  genuine hidden occlusion, oblique-to-90-degree continuity and
  reflected tetrahedral fixtures. Full three-platform compiler,
  CTest, stress/benchmark and independent merged-main CI;
  reserve final real Linux/Windows screenshots and visual
  approval for M52, original M15/V17 still open.
  PR #122 implementation `7d0e7122` passed
  [Actions #39516929872](https://github.com/logiclrd/Mesher/actions/runs/39516929872)
  full Vulkan-enabled GCC, Clang and Windows MSVC /W4 /WX, CPU
  CTests, thread/checkpoint stress and benchmark checks GREEN.
  PR head `a28e4bb2` then passed final three-platform
  [Actions #39517508753](https://github.com/logiclrd/Mesher/actions/runs/39517508753).
  The stale M68-parent branch is reconciled with M68's independently
  merged-main acceptance (PR #121, `aaa30999`) in this integration.
  M69 removes GPU wire quad staging/pipeline, but preserves CPU
  canonical source edge audits, COW zero-byte protection and W/G.
  Exact reconciled PR-head and independent merged-main CI remain
  explicit gates. Actual new pole-line appearance and M15/M52
  physical Linux/Windows approval remain OPEN.

- [x] **V17-M65 — Correct M11 asynchronous 16-undo/redo renderer snapshot observer race.**
  Independent merged-main MSVC Actions #39508068531 failed 1/41
  at `M11 each real FIFO physical union is independently undoable`,
  while the same unchanged M11 code passed on exact PR head,
  Linux GCC and Clang. The M11 asynchronous regression presently
  waits only for a history depth, then immediately calls
  `takeSnapshot()`. Worker history/status and renderer mailbox
  publication are distinct asynchronously ordered observables.
  Replace both M11 undo and redo history-only waits with bounded
  exact immutable-root, worker current-root and BOTH history depths
  synchronization; retain all sixteen positive physical unions,
  exact root equality per Step, and refusal/undo invariants.
  No production worker, queue, Boolean or geometry changes. Add
  per-step timeout diagnostics and verify complete Vulkan-enabled
  GCC/Clang/Windows MSVC /W4 /WX CTest, checkpoint stress and
  benchmarks at exact PR-head and independently merged-main SHA.
  M63's sign-only camera milestone is already merged; treat
  this as an unrelated test observer regression, not proof
  that a failed merged-main CI was successful. M64 wire issue,
  M52 real GPU observations and original M15 stay open.
  PR #117 implementation head `5b6d003e` passed
  [Actions #39509119650](https://github.com/logiclrd/Mesher/actions/runs/39509119650)
  on Vulkan-enabled Linux GCC/Clang and Windows MSVC /W4 /WX,
  **41/41 CTests** on all platforms including Windows' formerly
  flaky M11 `asynchronous_sculpting`, repeated thread/history
  stress and relevant benchmarks. Production code remains untouched.
  Final documentation-exact head and merged-main CI remain separate
  gates; M64 and M52 stay open.
- [ ] **V17-M66 — Optional backface culling with retained two-sided interior inspection (DEFERRED).**
  Real Linux desktop evidence (2026-10-20): when the camera is inside
  the sculpted intersecting arm, currently rendered backfaces reveal
  a surface protruding through the opposite wall, which is useful for
  auditing forthcoming interior fragment excision, ladder stitching
  and cavity closure. Preserve this two-sided inspection behavior by
  default: current Vulkan solid pipeline uses VK_CULL_MODE_NONE and
  CCW front-face winding. Future optional display-only toggle may
  cull backfaces (VK_CULL_MODE_BACK_BIT) to reduce raster/fragment work
  for some outside views, but NO claimed factor-of-two frame-time
  improvement without actual CPU/GPU measurements.
  Before implementing, plan solid pipeline state selection or
  portable non-extension-dependent variants; decide whether the
  floor/grid are independently culled, how AA wire quads remain
  visible only with their intended owning/visible surfaces, and
  how depth occlusion behaves on camera-inside and near-pole views.
  Do NOT directly apply triangle culling to orientation-dependent
  screen-space wire quads, expose rear wires after their occluding
  backfaces disappear, or reinstate blanket clip-depth bias.
  Verify original and reflected manifold winding (including symmetry),
  closed outside/inside fixture views, W/G independent controls,
  near/far occlusion, exact source-face picking and post-Boolean
  interior inspection. No permanent MeshRevision topology, source
  FaceId, GPU upload/cache identity, history or worker changes.
  Real Linux and Windows GPU visual/benchmark comparisons plus
  Vulkan-enabled GCC/Clang/MSVC CTest/stress/benchmark acceptance
  required when intentionally scheduled. NON-BLOCKING future UX:
  not a prerequisite for M64 wire-depth diagnosis, M52 physical
  acceptance, original M15/V17 completion, or V18/V19; implement
  only when explicitly prioritized.
- [ ] **V17-M52 — Targeted real GLFW/Vulkan desktop observation.**
  Execute and record reproducible **actual human-observed** Linux and
  Windows runtime acceptance on real Vulkan hosts (not headless CLI
  or mocked GPU), covering normal and explicitly armed default OFF,
  ON selection, asymmetric and reciprocal reflected source-positive
  full joins, visible rejection, roots/picking/dirt, physical strokes,
  one-Step undo/redo, checkpoint recovery's V13 source refusal and
  failed-save diagnostics. Keep raw OS/GPU/build/commands, actual
  observed outcomes and limitations; never mark passed by assumption.
  **Partial actual desktop feedback, 2026-10-20, after M69:** The
  user reports mesh/floor line appearance now consistently independent
  of camera elevation (including the formerly discontinuous 90-degree
  angle), although slightly heavier than personally preferred.
  New camera movement feels correct; W works on both sides of a
  surface; asymmetric and reflected symmetric Merge fixtures
  behave as expected. This is reported human visual/interaction
  evidence, NOT a measured benchmark or proof of specific FaceId
  counts, worker acceptance stamps, source-local rejection, undo/redo,
  save-failure recovery, or real Windows GPU behavior. The user
  did not provide the exact latest executable commit, driver and OS
  build in this report; confirm those before claiming an exact build
  reproducibility record. Preserve the remaining exact geometric,
  history, refusal and recovery criteria and leave M52 OPEN.
  Detailed current evidence and outstanding checks:
  `docs/V17_M52_DESKTOP_OBSERVATIONS.md`.
**Then ORIGINAL V17-M15:** Independently reconcile all previously
  stated physical source/first-original/winding/cavity, 38 resource
  limits, M23 episodes, M14 quarantine, immutable history and
  source-certification acceptance against M47–M52 evidence; document
  supported and refused cases. Only check original M15/parent V17
  after every genuinely rendered targeted criterion has been
  observed, and leave V18/V19 separately open.

- [ ] **V17-M15 — Default-off live Merge geometry switch and targeted desktop acceptance.**
  Explicit on-mode only after M16's source-proven coincidence/fan acceptance, with Linux/Windows Vulkan, symmetry, picking, undo/redo, storage and rejected-stroke checks. V18 Off-mode collision constraints and broader V19 acceptance remain separate.

**M12 flat execution rebaseline — October 18, 2026.** The original M12
objective/checkbox remains intact and OPEN. Its original-source provenance,
intended deformation, cumulative terminal acceptance and conclusive clean
episodes are separate independently complete gates. Sequence:
**M19 → M20 → M21 → M22 → M23 → M12 integrated acceptance**, before
M13–M15. All are flat peer IDs, no recursively nested tasks. Existing M13–M15
and V18/V19 objectives remain unchanged. Refusal alone is not a positive weld.

- [x] **V17-M19 — Fixed-original ID-bound surface anchor.**
  Capture an immutable original FaceId reference and its seven per-face
  witnesses and original edge scale. A bounded, read-only check accepts
  only the same living FaceId with identical original vertex incidence,
  so coincident unrelated sheets can never be substituted. Test actual
  geometric motion, source disappearance/rewrite, malformed source, and
  exact vs one-below-distance-work limits. Refuse unproven remeshing until
  M20. No physicalExact activation or publication in M19.
  Accepted in PR #67: read-only capture and audit of fixed source
  FaceIds and incidence, 14-point zero/drift real Inflate positives,
  exact vs one-below distance cap, stale-reference, duplicate and
  foreign-incidence refusals. Three-compiler substantive head CI
  #39159046998 succeeded; final PR-head check is required before merge.
  The original M12, M20–M23 and M13–M15 remain unchecked.
**M20 flat execution rebaseline — October 18, 2026.** M20's original
scope stays OPEN and unchanged; actual split, flip, collapse, exact
cut/join, and across-Step composition have distinct source-ownership
conditions. Record the ENTIRE planned sequence before any M24 code:
**M24 → M25 → M26 → M27 → original M20 integration → M21–M23 → M12**.
These are independent flat V17 peer IDs, not M20a descendants.
M13–M15, V18/V19 and accepted M01–M19 remain untouched.

- [x] **V17-M24 — Bounded, runtime-certified local split ancestry.**
  Emit immutable before/after per-face proof records ONLY after actual
  accepted RegionEdit split commits, including real reflected pairs and
  self-mirrored X=0 midpoints. Consume these to trace original protected
  FaceIds through repeated genuine split generations, retaining ALL
  living stable FaceIds (rewritten original + appended child). Verify
  exact source oriented subdivision and midpoint; source/target root
  chains, independent coincident IDs and exact work caps. Refuse
  missing/reordered/forged records, unproven flips or other ancestry,
  never derive ownership by nearby XYZ. No physicalExact policy or
  GUI authorization.
  **Accepted:** PR #68 real two-generation splits, mirrored and X=0
  fixed midpoint orbits, two separately valid coincident closed sheets,
  exact opposite permanent-edge incidence, complete staged FaceId and
  midpoint journals, root continuity, exact/one-below budget and
  stale/forged/missing/cross-source/mirror refusals. Substantive head
  Actions #39161099327 passed GCC/Clang/MSVC and 23 CPU suites.
  M25–M27 and the original M20 checkbox remain OPEN.
- [x] **V17-M25 — Safe flip and collapse source-transition proofs.**
  Extend lineage beyond M24's split-only positive domain with bounded
  committed flip/collapse records. A flip that transfers area between
  original owners requires a jointly certified source-patch domain,
  never a unilateral inferred FaceId; a collapse that retires protected
  Exposed/ReversedBoundary material must refuse absent M21 deliberate
  permission. Positive valid local topological edits and mirrored
  orbits; missing protected fragments and ambiguous ancestors refuse.
  **Accepted in PR #69, substantive Actions #39163332789:** real
  repeated sphere split+2→2 flip positive preserving unrelated
  original protected sources; actual asymmetric and paired mirrored
  collapse positive with immutable protected outer walls; source-ID
  quad boundary, same-original-owner protected transfer, reflected
  reciprocal/opposite-winding fan, collapse midpoint/seam, exact
  retired/rewritten FaceId and tombstone source journals.
  Real cross-original protected flip, unproven protected collapse
  deletion/movement, stale/missing/forged journals and one-under
  evidence caps refuse. Full 23-suite GCC/Clang/MSVC CI green.
  Deliberate cross-original protected remapping WITHOUT separately
  certified joint source partition remains unsupported/refused; no
  automatic ownership transfer or M21 intentional deformation.
  M26, M27, M20 and original M12 remain OPEN.
- [x] **V17-M26 — Source-exact c1/c2/c3/d2 child correspondence.**
  Bridge original FaceId → source-proven conforming-cut childSources
  through M02 subdivisions and surviving final d2 seam into a bounded
  terminal descendant record, honoring c3 protected Exposed and true
  ReversedBoundary versus Buried/Outside allowed retirement. No
  source-free cap/zipper matches, unrelated coincident-ID aliases,
  missing mirror/self-orbits or forged child source.
  **Accepted in PR #70, substantive Actions #39165079674:**
  ONLY a fully validated original-root Step captures immutable private
  source-owned conforming child ancestry, signed-winding typed
  Exposed/ReversedBoundary vs Buried/Outside, separately M03-certified
  original uncut retire/protect sets, and exact d2 source seam welds.
  A bounded READ-ONLY consumer independently reruns complete
  c1/c2/c3/M03 against the original source and finite cut limits,
  maps all first-source FaceIds to uniquely proven live terminal
  descendants, records entirely Buried/Outside origins separately,
  and verifies protected final XYZ, retired/added net journals,
  reflected face pairs and actual X=0 singleton fixed orbits.
  Genuine closed disjoint multi-section, three-body cavity/M03,
  mirrored uncut burial, and X=0 exact Step positives passed;
  forged/wrong classifications, missing child/paired source orbits,
  stale original, missing old involution and one-under evidence
  budget refuse. Full GCC/Clang/MSVC warnings-as-errors, 23 CTest
  suites and applicable benchmarks green on substantive head.
  M27 owns cumulative original-witness/Step composition, so
  M20 and original M12 remain OPEN; UI default unchanged.
- [x] **V17-M27 — Lifetime patch composition and bounded witness audit.**
  Compose accepted M24–M26 stage records across prior published Steps
  from ONE immutable first nominated original root, preserving root
  continuity, stable FaceIds, reflected/cavity identity, source-local
  original edge units and honest complete cost caps. Accept actual
  remesh and exact-join descendant positives and verify bounded
  seven-point original-to-live-patch audit. Refuse stale captures,
  missing/ambiguous ownership, falsely substituted coincident triangles,
  truncated work and incomplete protected faces.
  **Accepted in PR #71, substantive Actions #39167334610:** read-only
  first-original immutable source anchors and opaque verified lifetime
  roots/FaceId owners/c3-M03 retired originals/evidence counters;
  M24 split, M25 flip and protected-safe collapse, and M26 c1/c2/c3/
  M03/d2 transitions advance without rebasing original seven-point
  witnesses or shortest-edge normalization. Cumulative per-stage
  source-face evidence cap and complete preflight 7 * live descendant
  distance cap accept equality, refuse one below. Actual published
  two-generation split roots show increasing first-original drift;
  real accepted flip/collapse protect distant original detail; real
  exact joins, reversed cavities, mirrored M03 retirements and X=0
  fixed-point orbits preserve source ID ownership. True independent
  coincident closed sheets never substitute by XYZ; forged/stale
  roots, ownership swaps, altered first witnesses, missing retirement
  authority and counter resets refuse. All GCC/Clang/MSVC warnings-
  as-errors 23 CTest suites and applicable benchmark jobs green.
  This is finite one-sided evidence, NOT an active fidelity threshold,
  M21 intent policy, M22 CAS decision or continuous Hausdorff proof.
  Original M20 and M12 integration remain OPEN.
**V17-M20 integration — accepted after M24–M27.** The original
M20 checkbox below is the ONLY completion checkbox; this sentence
continues documenting its previously required joint-acceptance gate.
M21 still decides deliberate sculpt scope and M22 activates
the pre-CAS policy. PR #72 independently validates a real M26
c1/c2/c3/M03/d2 exact union FOLLOWED by M24 local split on the same
published terminal root, crossing two original-root CAS revisions
with the SAME first-original FaceIds and seven-point witness scale.
The same immutable lifetime preserves signed-winding buried/uncut
retirement, complete surviving patch ownership and cumulative
source/distance test caps. Missing/alien ownership, stale CAS,
one-under caps, forgery and coincident independent sheets refuse.
Substantive Actions #39168754914 passed GCC/Clang/MSVC with 23
CPU CTest suites and applicable benchmarks; only final review
documentation follows before the merge.

- [x] **V17-M20 — Source-certified remesh patch descendants.**
  Preserve original FaceId ownership through split/flip/collapse,
  rewritten/retired source faces and d2 exact-cut descendants, including
  stable IDs, reflected orbits, X=0 self IDs and independent coincident
  surfaces. Bounded descendant audit positives and missing/ambiguous/
  foreign ancestry fail-closed tests. No arbitrary nearest-face matching.
  **Integrated acceptance in PR #72:** all flat M24–M27 peers
  independently completed; source-oriented local splits, protected-safe
  face transfer and collapse refusals, true c3/M03 classified exact
  d2 children and cavity preservation, mirrored/X=0 source orbits,
  coincident independent permanent IDs, and opaque lifelong first-root
  ownership all compose without a proximity-based fallback. A
  genuinely committed exact union followed by genuine local remesh
  publishes as two CAS Steps on the SAME original-root lineage;
  the protected source count, certified Buried/Outside tombstones
  and finite seven-witness per-original patch metrics survive.
  Every independent prior missing/ambiguous/forged/reflected/one-below
  refusal stays green in 23 full CPU test suites on three compilers.
  This closes ONLY the original M20 *source provenance* objective.
  M21 intentional feature exemptions, M22 active terminal pre-CAS
  policy, M23 clean episodes and original M12 remain OPEN.
- [x] **V17-M21 — Explicit intentional feature scope and cavity policy.**
  A caller-designated intended brush displacement protects unaffected
  exposed/reversed-cavity surfaces and exempts only source-proven intended
  features. The decision honors complete signed-winding c1/c2/c3/M03
  Buried/Outside retirements. Prove nonmirrored/mirrored legitimate
  intent, preserved outer and cavity walls, and refusal for forged or
  ambiguous scope, without guessing intent from candidate distances.
  **Accepted PR #73, substantive Actions #39171683227:** caller
  explicitly lists FIRST-original intended FaceIds, bound to the
  pre-edit opaque source certificate; only the immediately succeeding
  M24/M25/M26-certified candidate is eligible. Geometric exemption
  applies ONLY to those named original owners; all remaining live
  Exposed/ReversedBoundary source descendants get the COMPLETE seven
  original-witness comparison with original-edge scales and bounded
  full-work preflight. Completely classified c3/M03 Buried/Outside
  retirement remains source-certified; neither intended scope nor
  proximity can invent disappearance. Symmetric stages MUST supply a
  reciprocal original VertexId involution and closed opposite-winding
  original FaceId intent orbits, including genuine X=0 singletons.
  Positive actual asymmetric and reflected X=0 displaced/split
  sculpt, completely independently coincident closed sources,
  real c3/M03 uncut buried originals with protected reversed-cavity
  components (asymmetric and paired mirror) are accepted.
  Partial/alien/duplicate source nomination, wrong coincident sheet,
  missing mirror pairs, stale/sibling predecessor, forged retirement,
  unintended protected drift, and one-under full-distance work
  refuse. GCC/Clang/MSVC warnings-as-errors, all 23 CPU suites and
  applicable benchmarks green. This is READ-ONLY M21 scope policy,
  NOT M22 pre-CAS activation, M23 clean episode or original M12
  final integration; UI Merge geometry stays default disabled.
**V17-M22 implementation plan (flat, independently completable; original M22 objective unchanged).**
  The actual worker physical Step contains several PRIVATE source transitions
  (Inflate → M24/M25 refine → protected-safe collapse → M26 d2 exact union)
  before ONE published CAS, whereas M21 requires ONE certified previous
  published lifetime. Do not paper over this by rebasing original witnesses,
  declaring intermediate roots publishable, or skipping unsupported parts.
  Work sequence recorded BEFORE implementation:
  - [x] **V17-M28 — One-stamp private source-transition bundle.**
    Certify full original-root physical stamp source evidence (including
    unchanged-incidence deliberate displacement and all subsequent M24/M25/
    M26 stages), verify the complete original FaceId → final protected
    descendants and mirrored source orbits, and seal ONE external successor
    certificate with the actual previous published root. Full stage cost and
    unsupported collapse/proof combinations refuse, not assume source.
    **Accepted PR #74, substantive Actions #39173794290:** source-bound
    complete Inflate vertex/face halo, actual M24/M25 refinements,
    conservative M25 collapse and source-exact c1/c2/c3/M03/d2 final
    join, combined through ONE immutable externally certified previous
    published root. Recompute all Inflate moved-source and full original
    face/vertex journal under finite limits; a genuinely unchanged
    collapse costs zero, whereas a real one requires complete proof.
    Every private revision is read-only and M27's cumulative FaceId
    owners, original seven witnesses, Buried/Outside authority, and
    source evidence work remain sealed. Positive two physical stamps
    measure cumulative original-reference drift, one real two-child
    M24 local split, and a genuinely physically triggered exact
    union all return an M21-compatible SINGLE external successor.
    One-under evidence/vertex caps, forged moved source, stale root,
    and excessive second unscoped erosion refuse. GCC/Clang/MSVC
    all 23 CTest suites and applicable benchmarks green.
    **M29/M30 and original M22 remain OPEN:** there is no active
    worker publication hook, durable first-original recovery or UI
    activation in M28.
  - [x] **V17-M29 — Durable lifetime publication state and recovery.**
    Keep immutable first-root source witnesses, intended-scope credentials,
    explicit certified buried retirements, and cumulative face work across
    accepted worker Steps; synchronize worker undo/redo, history eviction,
    checkpoint/recovery and stale generation. A recovered root without
    authenticated first-root ancestry must refuse opt-in fidelity rather
    than quietly reset its baseline. Prove private failed candidates change
    no identity, FIFO, history, mirror or dirty state.
    **Accepted PR #75, substantive Actions #39176592637:** an
    authenticated, opaque first-original lifetime is now an optional
    immutable field of bounded COW history entries. An actual new
    descendant must pass exact previous/terminal root, first-original
    owner, original seven-witness and predecessor-certificate checks
    BEFORE history mutates or discards redo. Undo/redo return the exact
    old/new lifetime with their permanent FaceId lineage, original
    root and accumulated work. Cumulative source anchors survive
    history eviction; opaque proofs use WEAK predecessor ownership
    so older COW roots do not stay resident merely through history
    ancestry. Forked redo requires a new authenticated child; stale,
    forged and missing credentials refuse before mutating history.
    In-memory trusted handoff can keep the full sealed certificate.
    Legacy version-12 disk checkpoints still load. A protected
    version-13 checkpoint stores a checksummed provenance-REQUIRED
    marker but NEVER invents certificate authority by hashing
    terminal geometry: disk reload without independent authenticated
    first-root ancestry refuses protected physicalExact recovery,
    and an unprotected session cannot silently acquire a fresh
    first-original baseline. This is intentionally a fail-closed
    disk-recovery boundary, NOT successful reconstitution of
    forgotten provenance; a later true resumable proof transcript
    would need its own validated design. Tests cover V12/V13 read,
    trusted in-memory handoff, protected recovered refusal,
    authentic multi-Step history, stale/forged/redo branches,
    root retirement and cumulative source work. Full GCC/Clang/
    MSVC warnings-as-errors, 23 CPU suites and benchmarks green.
    **M30 and original M22 remain OPEN:** no new active physical
    worker pre-CAS evaluator is enabled by M29, no desktop default
    change, and existing worker FIFO/mirror/dirty/rollback regressions
    stay green.
  **V17-M30 completion plan — flat execution peers (M30 objective unchanged).**
    Code review of the M29 worker confirms two distinct original-root CAS
    paths, a physical multi-private-stage M28 bundle and an explicit
    queued exact-cut path. Original-source intent does not yet cross the
    worker input boundary. Record the ENTIRE independently testable
    sequence before editing; never infer intent from candidate XYZ or
    silently make a new terminal root the first original:
  - [x] **V17-M31 — First-original intent transport and validation.**
    Add deliberate FIRST-original FaceId selections to each physical
    stroke and explicit queued union request, preserving FIFO order and
    separate source identity. Keep an immutable original-source and
    reflected-orbit authority for subsequent M21 declarations; reject
    foreign, half-orbit or unverified recovered authority. Show the
    caller choices cannot be synthesized from displaced candidate geometry.
    This milestone installs INPUT CONTRACTS, not an active final CAS gate.
    **Accepted PR #76, substantive Actions #39179461146:** exact
    FIRST-original FaceId input is carried independently through Begin
    and explicit queued Merge. Fresh opt-in source authority retains
    immutable first-root witnesses and reflected ID map, while unverified
    recovered terminal roots cannot create credentials. M21 prevalidates
    nonempty original source scope and reflected partner completeness.
    Invalid original/half-mirror inputs refuse privately, then valid
    physical and queued exact unions drain on the same worker. The
    23-suite GCC/Clang/MSVC warning-as-errors build and applicable
    benchmarks passed. Full terminal M28/M21 pre-CAS enforcement,
    M29 worker history lifetime publication, original M30 and M22
    stay UNCHECKED, alongside M32-M34.
  - [x] **V17-M32 — Certified physical-stamp terminal CAS gate.**
    Initialize source-authenticated M29 history from the actual original
    root; replay complete M28 Inflate/refine/collapse/optional d2 source
    evidence; evaluate predecessor-bound M21 intent and all original
    seven-witness descendant work on the EXACT terminal root before CAS.
    Reject cumulative unscoped erosion and one-under budgets, with no
    partial history/mirror/dirty/checkpoint changes. Preserve real M18
    physical unions and M11 sixteen positive mirrored tower unions.
    **Accepted in PR #77, substantive Actions #39182191436:** the
    actual physicalExact M28 source bundle and M21 fully bounded
    first-original seven-witness terminal test now run BEFORE the
    one original-root CAS. Fresh original M27 certificates initialize
    M29 history; each accepted stamp appends its one direct successor,
    exact root and trusted checkpoint lifetime. Genuine first permitted
    displacement then second cumulative unintended erosion refuse,
    as do one-under full 7*descendant witnesses and Inflate IDs, with
    same old indexed root, history/proof, dirty/mirrors and later FIFO.
    Caller intent across genuine sequential unions must explicitly
    continue naming earlier deliberately sculpted still-live original
    FaceIds; only authenticated wholly c3/M03 retired origins can be
    omitted from that active M21 scope. No proximity guessing, rebasing,
    threshold loosening or permanent inferred exemption. The M18 real
    physical source unions, M11 sixteen genuinely accepted mirrored
    tower unions, physical worker refusal/recovery, cavity/mirror guards,
    undo/redo, and protected V13 checkpoint boundary remain green
    on GCC/Clang/MSVC with 23 CPU suites and applicable benchmarks.
    Explicit protected queued source-exact CAS remains fail-closed
    pending independent M33; M34, the original M30, M22, M23, M12
    and unrelated later goals remain UNCHECKED. M14 exceptional
    post-CAS atomicity is not falsely claimed complete.
  - [x] **V17-M33 — Certified explicit queued source-exact CAS gate.**
    Bind explicit queued union intent to its actual expected previous
    published lifetime, independently certify genuine M26 cut/d2
    source-child survivors and retired Buried/Outside originals, enforce
    complete M21 terminal bounds and M29 matching history credentials
    before CAS; reject stale roots, missing/sibling/reflection or
    coincident owner substitutions without changing redo or maps.
    **Accepted PR #78, substantive Actions #39184683170:** genuine
    separately queued original-root exact c1/c2/c3/M03/d2 complete
    M26 survivor/retirement lineage now binds the exact M29 current
    published certificate and the explicit M21 original FaceId intent.
    Full seven-witness FIRST-original lifetime audit and BOTH configured
    and M29 append proof bounds execute before ANY mirror-controller
    mutation or root CAS. Missing/foreign/half-reflected/doubled/stale
    source inputs, incomplete X=0 contacts, bounded raw input,
    one-under M26 cumulative source work and one-under full witness
    work all refuse without publishing, evicting redo, changing mirrors,
    or forging protected checkpoint provenance. Genuine asymmetric
    and three-contact X=0 symmetric source-exact unions accept exactly
    one terminal history/snapshot; their matching original-root proof,
    dirty face-owner partitions, reflection orbits and undo/redo persist.
    Original M31/M32 physical positives and legacy untracked queued
    commands remain green. GCC/Clang/MSVC warnings-as-errors, 23 CPU
    suites and applicable benchmarks passed.
    **M34, original M30, original M22, M23 and original M12 remain OPEN.**
    No M14 post-CAS injected-fault or desktop M15 claims are made.
  - [x] **V17-M34 — Integrated protected worker FIFO acceptance.**
    Combine M31-M33 under real asynchronous 4D begin/end and explicit
    union queues: accepted intended union, small allowed first movement,
    cumulative rejected second movement, later valid FIFO progress,
    mirrored X=0 and independent coincident identity, reversed-cavity
    protection, exact terminal-only renderer/picker, dirty face owners,
    undo/redo/checkpoint requirement, finite caps, and M11 positives.
    Keep M14 post-CAS exception injection and M15 desktop UI separate.
    **Accepted in PR #79, substantive Actions #39187712935:** A single
    opt-in physicalExact asynchronous worker now proves a genuine
    caller-intended full original-root queued c1/c2/c3/M03/d2 union,
    recovers from a queued one-under exact budget refusal, then applies
    the first limited physically small 4D displacement to the same
    source-authenticated terminal. An equal individual second stroke
    exceeds FIRST-original cumulative protected drift and refuses
    without new CAS/history/dirty or partial mirror maps. A stale
    expected queued root also refuses; a subsequent explicitly scoped
    valid physical stroke still drains against the most recent accepted
    certificate. Terminal-only renderer/picker, preserved older root,
    three exact mixed-root undo/redo steps, and V13 disk checkpoint's
    unforgeable original-source requirement pass. The separate
    genuine three-contact protected X=0 queued exact Step now has a
    real reflected physical 4D successor and matching mirror/undo/redo
    acceptance. Existing independent coincident owner refusals,
    reversed-cavity source protection and M11 SIXTEEN positive mirrored
    physical unions remain green across all 23 CPU suites.
    M34 uncovered and FIXED a real worker publication race: the
    producer's atomic Begin reservation could previously be cleared
    by an unrelated worker iteration BEFORE that Begin was consumed,
    losing the pending terminal UI root after a rapid FIFO transition.
    A worker-local begun-stroke token now authorizes release ONLY
    after its real End, pending samples, and forced final publication.
    GCC/Clang/MSVC warnings-as-errors and applicable benchmarks green.
    **Original M30, M22, M23, M12 and M13-M15 remain OPEN**; M14
    fault-injected post-CAS exception atomicity and desktop opt-in
    were intentionally not claimed by this peer.
  - [x] **V17-M30 — Active full-terminal pre-CAS policy on physicalExact.**
    Wire the M21 intent declaration and complete first-original M27
    sampled audit to BOTH actual stamped physical exact and explicitly
    queued source-exact union worker paths, after final d2/remesh. Test a
    genuine intended union, first small permitted drift, second cumulative
    unacceptable erosion, rejected one-under cap/stale root, unchanged
    original pointer, checkpoint/undo history, mirror maps, dirty
    partitions and serialized work while later valid requests drain.
    Default-disabled UI and prior M11 sixteen real unions must remain green.
    **Original V17-M30 accepted in PR #80; substantive Actions
    #39192742674 passed all GCC/Clang/MSVC warning-as-errors builds,
    23 CPU suites, and applicable benchmarks.** Full M31 first-root
    caller FaceId intent reaches BOTH actual protected CAS paths; M32
    authenticates the entire Inflate -> M24/M25 refinement/collapse ->
    optional exact d2 physical chain and M33 authenticates complete
    queued c1/c2/c3/M03/d2 source-owned union, always measuring ALL
    surviving first-original living descendants in local first-edge
    units AFTER the final terminal is constructed. No private stage is
    separately published. The original-source certificate and true
    published predecessor remain immutable and history/checkpoint-bound.
    M34 demonstrates real accepted intended union on a single worker,
    finite one-under refusal followed by success, the first permissible
    physical drift and atomic rejection of cumulative second erosion,
    genuine stale queued root refusal, later scoped FIFO progress,
    dirty terminal renderer/picker, protected X=0 mirror identities,
    V13 disk provenance refusal and exact mixed undo/redo COW roots.
    Existing M11 sixteen genuine positive mirrored physical unions
    and independent coincident/cavity source refusals remain green.
    **Final M30 audit fix:** the physical path now preflights BOTH
    caller-configured M21 and actual default M29 history source-proof
    bounds BEFORE CAS, matching M33's queued path, so a caller override
    cannot create a post-CAS history proof failure. A real accepted
    physical Step verifies the immutable default M29 transition even
    under an intentionally more permissive M21 work limit.
    **Final M30 test fix:** M34's mixed worker failed intermittently
    on MSVC because rejected physicalExact stamps also increment the
    rejectedUnion counter; an absolute union-outcome wait could return
    BEFORE the stale Merge was processed. The corrected assertion
    waits for one ADDITIONAL outcome after the post-physical baseline,
    verifies the exact stale-root rejection, and retains the same
    old root/history/renderer mailbox. This catches genuine ordering
    regressions rather than merely extending a timeout.
    **The original V17-M22, M23 and original V17-M12 remain OPEN;
    M14 post-CAS injected exception guarantees and M15 desktop
    Merge Geometry UI remain separate.**
  **V17-M22 final integrated acceptance plan (fulfilled; original checkbox below).**
    Verify M28–M30 jointly, warnings-as-errors GCC/Clang/MSVC with existing
    23 suites, preserve M23 and original M12 as open. Only then check the
    ORIGINAL M22 checkbox, not this explanatory plan.
- [x] **V17-M22 — Active cumulative final physicalExact preservation.**
  Validate terminal post-remesh/post-d2 root before original-root CAS
  against first original-source witnesses and descendants in local
  edge units under shared finite audit cap. Prove accepted deliberate
  physical exact union, first permissible drift and atomic refusal of
  cumulative second erosion with unchanged previous pointer, histories,
  mirror map, dirty owners and FIFO; refuse one-under cap/stale root.
  **Original M22 accepted via PR #81; substantive Actions #39197216458.**
  Verified M28 private full original-source chain and exact post-d2 terminal,
  immutable first-original seven-point per-living-descendant witnesses and
  per-original local shortest-edge scale; M29 original lifetime/one direct
  successor matched to actual published root; M30 M21 caller-scoped original
  FaceId intent and BOTH configured and M29-default pre-CAS proof limits
  on actual physical and queued exact source joins. The supported real M18
  physical exact union and sixteen genuine M11 mirrored physical unions
  remain accepted. M32 normal-driven first small movement is accepted,
  identical second cumulative protected erosion refuses with unchanged
  old COW root/source/history/dirty; full 56 witness/55 refusal and
  complete six/one-under Inflate work are enforced. M33 true full
  source-owned c1/c2/c3/M03/d2 queued union, bounded one-under work,
  stale expected-root and unsupported original FaceId refusals remain
  fail-closed. M34 one real asynchronous FIFO accepts deliberate exact
  union, permissible protected physical movement, atomic cumulative
  second refusal and later explicitly intended progress, preserving
  original renderer/picker pointers, terminal-only dirty owners,
  three exact mixed undo/redo steps and protected checkpoint provenance.
  Dedicated M22 regression asserts a half-orbit physical command after
  a TRUE authenticated X=0 fixed-point queued join leaves the EXACT
  welded root, immutable original source certificate, reciprocal and
  singleton live mirror pairs, dirty state and history unchanged;
  normal protected union undo/redo still works afterwards.
  All GCC/Clang/MSVC warnings-as-errors builds, 23 CPU suites and
  applicable performance benchmarks are green. This is ONLY the
  ORIGINAL M22 source-fidelity terminal acceptance: **M23,
  original M12, M13–M15, V18/V19 remain OPEN**. It neither
  implements M23's three-conclusive-Clean episode state,
  asserts M14 injected post-CAS exceptional rollback, nor enables
  the default-disabled M15 desktop Merge geometry mode.
- [x] **V17-M23 — Conclusive contact-episode decisions.**
  Three consecutive fully classified Clean checks end an episode;
  Contact starts/extends it; uncertain, refusal and budget exhaustion
  cannot invent Clean. Episode state informs policy/diagnostics without
  caching weld approval or skipping each original V13/c1/c2/c3/d2/final
  collision/anchor audit. Test real physical FIFO contact/clean/refusal/
  recontact and preserve M11 genuine sixteen unions.
  **Accepted via PR #82; substantive CI #39200600909** passes
  GCC/Clang/MSVC warnings-as-errors, all 23 CPU suites and applicable
  benchmarks. Previous tracker improperly retained two Clean credits
  through Unknown. M23 resets consecutive quiet/contact runs on
  ambiguity, refusal or budget exhaustion; exactly three consecutive
  fully classified and otherwise accepted Clean checks end an episode.
  Contact starts/extends the episode even when a later Boolean weld
  refuses; an uncertain/refused Step cannot be promoted to Clean.
  PhysicalExact V13 and opt-in V16 complete-proposal observations
  are staged until downstream candidate validator and original-source
  M21/M29 terminal policy resolve. A proven Contact remains
  informative on downstream refusal, while Clean is credited ONLY
  when that proposed Step is otherwise accepted. Invalid caller
  original FaceId scope produces Unknown and resets quiet streak,
  never a guessed Clean. The tracker now exposes closedEpisodes
  as a diagnostic; episode status NEVER authorizes a weld or bypasses
  a future V13/c1/c2/c3/M03/d2/final collision/source/anchor audit.
  The actual symmetric two-region M11 4D FIFO test performs one true
  source-certified physical Contact union, two real separately
  published and V13-certified Clean exterior-corner strokes,
  a rejected foreign original FaceId scope (Unknown),
  then exactly three more real Clean stamps before closing the
  episode; the next real independently named tower union reopens
  a second episode and publishes precisely one new terminal.
  The live post-union vertex is selected by stable current
  topology, not a retired import coordinate. All accepted
  dirty renderer snapshots are consumed before testing the
  later refusal's zero new dirty owners. Real broad-overbudget
  M11 original source discovery reports Unknown, not Clean/Contact;
  the next valid physical union still opens a real episode.
  Original V16 hysteresis regression now enforces C,C,Unknown,
  C,C,C closure and verifies exactly one closed episode.
  All sixteen genuine M11 reflected physical unions remain green.
  **Original M12 and M13–M15, V18/V19 remain OPEN;**
  no desktop Merge Geometry mode was enabled, and M14
  post-CAS injected exceptional atomicity is not claimed.
**M12 integration rebaseline — October 19, 2026.** The original M12
objective and ONLY its original checkbox remain unchanged and OPEN.
Independent M19–M23 acceptance did not yet prove repeated TRUE physical
source unions on the SAME local patch with simultaneous deliberate
deformation, protected first-original exposed/reversed-cavity and mirrored
outer-wall bounds, episode closure and recovery. Record this finite flat
sequence BEFORE implementation:
- [x] **V17-M35 — Repeated genuinely physical same-patch source unions.**
  Construct a reproducible, initially collision-free multi-shell source
  in ONE overlapping local sculpt area, not the already accepted sixteen
  spatially separated M11 tower contacts. Under one genuine
  `physicalExact` brush/history, perform at least TWO independent
  normal-driven input Steps that each certify a NEW original c1/c2/c3/M03/d2
  Boolean union, with exact source ownership, one terminal CAS/history
  per union, immutable previous picker roots, mirror orbits and dirty
  partitions. Confirm the second contact actually shares the previously
  edited local area or joined component, rather than another distant tower.
  Keep complete finite work/refusal conditions; unsupported geometry must
  fail closed without weakening production source predicates.
  **Accepted in PR #83, substantive Actions #39204571996.** Reuse the
  source-authenticated M11 closed mirrored tetra pair fixture, but move
  the second independent source band into ONE LOCAL OVERLAPPING brush
  neighborhood rather than the distant second tower. The two intended
  positive-side brush spheres share actual original source vertex
  candidates; the initial source still has ZERO triangle intersections.
  On one real `physicalExact` brush, two successive normal-driven
  strokes independently create real supported original-source contact,
  perform two actual c1/c2/c3/M03/d2 source-certified Boolean joins
  with strictly increasing certified retirements, and publish TWO
  distinct terminal COW roots with separate M29 undo depths.
  Both source lifetimes refer to the SAME immutable first-original
  root; old pickable BVH survives, correct dirty partitions publish
  only terminal changes, exact live reflected vertex positions and
  mirror IDs remain valid. Undo twice and redo twice restore the
  exact predecessor/successor pointers. The Contact episode remains
  continuously active with two conclusive contacts, not cached
  source-weld authorization. All 23 CPU suites/benchmark checks pass
  under GCC/Clang/MSVC with warnings-as-errors. This local-overlap
  proof is not a claim that both contacts join the same component
  or an arbitrary 3-body Boolean; M36's simultaneous protected
  exposed/cavity, explicit-scope, budget/refusal/recovery and
  three-Clean integration is still OPEN. **Original M12 and M13–M15,
  V18/V19 remain unchecked.**
**M36 flat execution prerequisites — October 19, 2026.** The original
M36 and M12 checkboxes and objectives are preserved. M36's coherent,
simultaneous source/cavity/drift/FIFO scenario is too broad for one
independently reviewable implementation. Record the complete ordered
preparation **before starting** any partial code work:
- [x] **V17-M37 — Scoped same-patch physical positives with protected cavity/outer source.**
  Extend M35's two genuine positive local source unions with separate
  original mirrored outer and signed-winding reversed-cavity geometry;
  retain those sources OUTSIDE explicit FIRST-original FaceId intent.
  Prove complete unchanged protected FaceIds, source descendants,
  original anchors, reflection identity and rejected unscoped wall
  deformation with exact COW/source/mirror/history rollback. Preserve
  both positive normal-driven joins and the original sixteen M11 joins.
  **Accepted in PR #84, substantive Actions #39208440042**
  GCC/Clang/MSVC all green (CPU suites and applicable benchmarks).
  Verified two real normal-driven local symmetric source-certified
  unions with a deliberately scoped 32/72 first-original FaceId intent;
  40 unscoped original outer/cavity/remote-source faces and exact mirrored
  vertices unchanged. Actual signed winding proves the nested cavity
  and independent unscoped attempted outer and inward wall deformations
  both refuse atomically with old COW pointer/certificate/mirrors/
  dirty/history retained. Double undo/redo restores exact union roots.
  M38/M39 and the ORIGINAL M36/M12 final acceptance remain OPEN.
- [x] **V17-M38 — Same-patch complete source work and cumulative protected drift.**
  Against accepted M37 terminal sources, audit all seven first-original
  witnesses for each authenticated living descendant, per-original
  edge scales, full and exactly one-under limits (distinct from
  Inflate/source-evidence/history caps); measure the first acceptable
  real protected drift and reject the individually small second
  cumulative excess without any root/proof/mirror/history/dirty change.
  Include checkpoint and exact undo/redo evidence.
  **Accepted in PR #85, substantive Actions #39212090176**:
  all GCC/Clang/MSVC warning-as-errors builds, full 23 CPU suites and
  applicable benchmarks green. Against the TWO genuinely source-exact
  normal-driven M37 local union terminal roots, audited EXACT
  7 x sum(living source descendants) first-reference work, per-original
  seven fixed XYZ and own shortest-edge unit, exact full work positive
  and one-under read-only AND active physical pre-CAS source refusal.
  Genuine small mirrored protected outer wall physical motion first
  accepts with a measured intermediate cumulative tolerance; the
  second remains under the witness-work cap but is specifically refused
  for original Exposed/ReversedBoundary first-root drift. Refused
  operations retain exact COW/source/mirror/dirty/history identity,
  V13 disk-only provenance refuses unsupported reauthentication,
  and accepted source-root pointers undo/redo exactly. The drift
  wall is part of this same authenticated multishell source but
  separate from the overlapping-join brush influence; M39 must
  integrate true async FIFO episodes, and original M36/M12 remain
  distinct final integrated acceptance gates.
- [x] **V17-M39 — Protected overlapping-patch 4D episode and FIFO recovery.**
  Use the genuine same-patch physical-positive fixture through actual
  asynchronous Begin/End 4D input; demonstrate Contact, two accepted
  Clean, interrupted Unknown, three NEW accepted Clean closure,
  renewed genuine Contact and later accepted well-scoped FIFO progress,
  with source/mirror/renderer/history/checkpoint invariants on refusals.
  Keep X=0 fixed-point and independent coincident-owner cases independently
  evidenced; never claim second join reaches the same component without
  proof.
  **Accepted in PR #86, substantive Actions #39263238193**:
  GCC/Clang/MSVC warnings-as-errors, all 23 CPU suites and applicable
  benchmarks green. Actual 4D Begin/End on ONE protected M37
  source-proven multishell with two TRUE overlapping original
  local contact brushes and 32 explicitly scoped / 40 protected
  source FaceIds accepts two independent real physical exact
  c1/c2/c3/M03/d2 unions, Contact, Clean, Clean, refused Unknown,
  three NEW accepted Clean closure, second local recontact,
  second refused Unknown and late valid scoped Clean FIFO recovery.
  Eight accepted COW/dirty/history terminal root pointers restore
  by exact full undo/redo; both refusals add NO roots or dirty owners,
  and BEFORE/AFTER durable probes preserve mirror pairs, retired
  FaceId/VertexIds and V13 provenance markers. Original remote/
  exposed/reversed cavity owners and 0/+1/0 signed winding remain
  unchanged. Protected disk-only restart cannot forge M29 first-source
  proof. No source-owner matching by accidental coincident XYZ.
  Distinct original M36 joint protected-policy and M12 standalone
  final acceptance remain open. M39 does not claim arbitrary
  same-component three-body Boolean.
**Then execute ORIGINAL M36** as a separate criterion-by-criterion
integrated combined-scenario acceptance (with any necessary new bounded
test that proves the *simultaneous* policy) before checking ONLY M36.
Then the already planned ORIGINAL M12 final acceptance. M13–M15, V18/V19
remain separate and open.
- [x] **V17-M36 — Same-patch M19–M23 combined protected policy.**
  On the M35 positive sequence, preserve first-original FaceId/edge-scaled
  seven-witness evidence and signed-winding cavity/outer-wall ownership,
  explicitly scoped deliberate source movement, real symmetric orbits,
  cumulative tolerance (first small permitted then second over-limit
  refused), exact vs one-under bounded work, active 4D Contact/three
  consecutive accepted Clean/Unknown episode behavior, and late valid
  FIFO recovery. Require old root/source/history/mirrors/dirty unchanged
  for refusals; checkpoint provenance and undo/redo stay authoritative.
  Record independent positive and conservative refusal outcomes without
  conflating source identity with coincident XYZ.

  **Original M36 accepted in PR #87, substantive Actions #39268160154**:
  GCC/Clang/MSVC warnings-as-errors full 23 suites and applicable
  benchmarks green. ONE protected M37 FIRST-source worker performs
  genuine overlapping local physical first union, real SMALL accepted
  unscoped original reflected outer-wall drift, two accepted Clean,
  invalid original-source Unknown and cumulative SECOND protected wall
  drift refusal Unknown, THREE new accepted Clean closing episode one,
  another genuine local source-exact physical union reopening episode
  two, a late refused Unknown and valid scoped Clean FIFO recovery.
  Exact full and one-under 7x living original descendant witness work
  independently exercises authentic original-root post-d2 physical
  first-stamp source-proof pre-CAS; second drift fits work cap and
  explicitly refuses on original-edge Exposed/ReversedBoundary
  protected cumulative distance. All forty other first-original
  FaceId/incidence owners, mirrored 0/+1/0 cavity topology, reciprocal
  reflected live orbits, before/after V13 disk IDs, zero refused dirty,
  old COW/picker roots and all accepted FIFO pointer-exact undo/redo
  remain authoritative. One continuous actual 4D history; no source
  by coincident XYZ. See
  `docs/V17_M36_ORIGINAL_COMBINED_PROTECTED_ACCEPTANCE.md`.
  **Original M12 and M13–M15/V18/V19 remain OPEN.**
Then execute **original V17-M12 integrated acceptance** as a standalone
criterion-by-criterion review of M19–M23 plus M35–M36, with three-compiler
full CI and its own evidence document. Check ONLY the original M12
checkbox after final-root and post-merge verification. M13 telemetry,
M14 fault-injected post-CAS atomicity, M15 default-disabled desktop
switch, V18 and V19 remain independent and open.

**Final M12 integration (the ORIGINAL unchecked M12 checkbox above):**
  Close that existing checkbox only after M19–M23 pass together for
  same-patch repeated real physical unions, deliberate sculpt deformation,
  cumulative exposed and reversed-cavity fidelity, symmetric protected
  outer walls, budgets and recovery. Do NOT create a second M12 checkbox.
  No universal imported-mesh or continuous two-sided Hausdorff claim;
  desktop M15 remains default disabled.

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
