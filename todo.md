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
- [x] **V17a — Fail-closed, no-full-mesh winding fallback for prospective live callers.**
  Add an explicit opt-out from V14's exceptional O(all-faces) solid-angle
  reference fallback, while keeping all established callers backward-
  compatible by default. Propagate the policy through fragment and
  closed-component classification, V15 classified planning, and V16
  exposed-feature provenance. Unresolved ray evidence becomes Ambiguous,
  not a deletion authorization; report fallback refusals in query stats.
  Regression includes deterministic grazing rays, ordinary sphere
  classification, redundant-shell planning and guarded cap provenance.
  This bounds ONE exceptional CPU path only. V17 still needs bounded
  collision enumeration, per-Step rejection/rollback and symmetric
  classified integration before any live activation.
- [x] **V17b — Bounded V13 collision discovery and private residual checks.**
  Add an optional per-query collision work budget that limits unique seed
  faces, BVH AABB boxes, visited partitions, overlapping triangle
  candidates, unique narrow-phase triangle pairs and reported contacts.
  Exhaustion throws length_error before exceeding its counter; no partial
  collision list is returned. V15 maps incomplete collision discovery
  (including isolated-component verification) to BudgetExceeded with
  measured query statistics, while V16 uses a separate bounded private
  residual-collision check. Existing offline callers retain unbounded
  defaults; the future live policy must choose finite values explicitly.
  Regression covers exact-limit success and six one-under-limit refusals,
  empty-query behavior, V15 conservative refusals and V16 immutable
  snapshot safety. Per-query limits are not total stroke time/RSS bounds:
  winding-ray BVH work, repeated component queries, worker rollback and
  symmetry-aware live classification remain V17 work.
- [x] **V17c — Recoverable prepublication Step refusal and mirror-delta rollback.**
  Add an optional validator of the full private post-Inflate/refine/collapse
  Step and exact changed vertices/faces; accept by default. A nonempty
  refusal reason, bounded-work exception, or explicitly invalid geometry
  from the validator rejects only that Step: the 4D sampler consumes the
  original input and moves on, but no geometry root, undo entry, render
  dirty delta, checkpoint mirror map, or partition-copy counter advances.
  Refiner/collapser journal only touched mirror pairs and reverse changes
  on rejection, without copying the whole correspondence map per Step.
  Worker status exposes rejected-Step count and last diagnostic; unrelated
  unexpected exceptions continue to report fatal errors instead of being
  silently ignored. Regress symmetric splitting followed by veto/accept,
  exact undo/redo/identity restoration, and a worker that rejects the first
  two input samples yet drains and publishes later Steps. No live merge
  policy or new UI setting is activated here. Full commit-stage failure
  atomicity and collision/feature validators remain subsequent V17 work.
- [x] **V17d1 — Private complete-candidate classified collision integration (non-symmetric).**
  Provide a CPU-opt-in collision policy that inspects the COMPLETE
  post-Inflate/refine/collapse candidate at the V17c prepublication
  boundary. V13 discovery has hard finite per-query ceilings independent
  of legacy default settings; V14's global solid-angle fallback is OFF;
  V15 classification is required before V16 private removal and exposed-
  feature preservation. After surgery, V13 re-checks every changed vertex,
  changed/retired/rewritten face, new scar face and contour survivor against
  the COMPLETE repaired revision, not only the scar. Failure rejects the
  Step without publication or history/dirty changes. Accepted non-mirrored
  surgery updates vertex/face counts, dirty renderer partitions, copied
  payload, single root CAS and one history entry. Symmetric no-contact
  Steps can pass; any classified symmetric surgery requirement still
  refuses rather than publishing unmatched mirror correspondence. The
  desktop Merge geometry switch remains disabled; tests cover private
  redundant closed shell, full-surface clean verification, budget and
  symmetry refusal, and opt-in ordinary asynchronous input.
  **V17d2 remains:** classification-authorized mirrored surgery and
  crossing normalization with complete mirror-ID creation/retirement
  staging, final residual collision checks, repeat-contact/hysteresis and
  real multi-Step symmetrical merger acceptance before UI activation.
- [x] **V17d2 — Mirror-aware private surgery transaction (supported classified cases).**
  Resolve a V15 classification-approved mirrored removal through V16's
  capped/zippered mirrored surgeon, including supported shared-X=0 plane
  crossing normalization, entirely privately. Only collisions that admit a
  complete mirrored validated result may commit; ambiguous or unsupported
  reflection/crossing configurations refuse the original Step. Net new
  mirror IDs are matched by EXACT reflected coordinates (unique off-plane
  partners and one self-pair per X=0 seam vertex); retired IDs require
  the entire existing mirror orbit to disappear. The V17c refiner/collapser
  journals stage these added/retired pairs before any validator/CAS, and
  undo them on rejection. Accepted repairs publish ONE root and history
  transition, adding the same pair deltas to Inflate, undo/redo and durable
  checkpoints. Normalization's temporary faces and vertices are eliminated
  from net counts; its committed COW work is included in accounting.
  Regression covers genuinely redundant mirrored interior shells, veto
  rollback, accepted mirror retirement, undo/redo and disk checkpoint
  restoration, a shared-plane zipper's self-mirror seam correspondence,
  and missing/inconsistent-orbit refusal. Existing UI stays unchanged.
  **V17 completion still requires:** repeated *classified* symmetric tower
  merges from real contact, ongoing sampled witness-erosion and contact
  hysteresis policies, aggregate work budgets, stronger post-CAS exception
  atomicity and desktop on/off control. V16's supported surgery fixture
  alone does not prove arbitrary overlapping shells can classify as repairable.
- [x] **V17e1 — Physical repeat-contact acceptance and anchored witness harness.**
  Exercise real symmetric two-tower strokes, not hand-selected excision
  plans: verify the pre-classification Inflate proposal actually creates
  triangle contact, then drive sixteen complete Steps alternating towers.
  Every Step must be V13/V14-classified (never cached), publish one fully
  collision-free immutable root or reject without history/mirror/dirty
  changes, and retain all held old picking roots. Anchor 56 exposed outer-
  wall witnesses in the ORIGINAL revision and audit their absolute geometric
  distance to every subsequent full revision (maxDistanceTests bounded in
  the test) rather than to the immediately preceding snapshot. Retain the
  previous 32-repeat same-patch exposed-cap test as the scar drift control.
  Add a read-only diagnostic contact episode tracker to the brush and
  worker status: repeated contacts share one episode until three *proven*
  clean checks, and inconclusive/budgeted assessments never reset it.
  This tracker MUST NOT skip any geometry checks, authorize deletion, or
  change brush decisions. CI also covers deterministic contact/quiet/
  uncertain episode sequences and clean async status counters.
  **Acceptance limit:** Refusal is a correct result for an unsupported
  intersecting multi-tower shape; this test does not prove that V15 can
  actually reconstruct and publish every physically contacting tower.
  Persistent live original-surface erosion policies (excluding intentional
  deformation), collision hysteresis affecting geometric *decisions*,
  aggregate work budgets and demonstrated successful multi-tower unions
  remain the next V17 acceptance work. UI Merge geometry remains off.
- [x] **V17e2a — Bounded cumulative original-surface guard and diagnosed physical merger blocker.**
  Add opt-in `SculptAnchoredSurfaceGuard` to the V17d candidate policy:
  explicitly nominated ORIGINAL seven-point-per-face witnesses, stable
  local surviving FaceId search domain, original reference edge length,
  max deviation in those original edge units and hard distance-test cap.
  Every proposed changed Step with a conclusive no-contact classification
  or fully verified private repair is audited BEFORE the V17c publication.
  Exceeded cumulative drift, retired candidate reference faces or over-
  budget work refuse the Step without partial roots, history, mirror or
  render deltas. Deliberately edited original features must be excluded
  explicitly by callers; there is no automatic global erosion policy.
  Regress an accepted first small deformation followed by a second whose
  *absolute* drift exceeds a bound between measured one/two-Step values;
  test direct/private/live refusal and exact budget boundary. Exercise
  the same guard on V17e1's physical symmetric tower outer walls.
  Probe genuine tower union success WITHOUT lowering V15's hidden-area
  threshold. The physical fixture reaches V16 but can reject when a
  reconstructed scar still intersects surviving faces; record this as a
  specific reconstruction blocker, not as a successful merged union.
  **V17e2b remains:** implement a conservative split/clip strategy for
  the actual triangle-surface intersection curves (with matching surviving
  subdivision), or otherwise find certified repairable physical contact
  fixtures that genuinely yield repeated successful symmetric unions.
  Continue aggregate Step budgets, post-CAS exception atomicity, broader
  exposed-witness selection and desktop UI gates separately.
- [x] **V17e2b1 — Bounded transverse intersection sections and private per-face patches.**
  Add a dedicated read-only V13 companion for noncoplanar, genuinely
  transverse triangle-pair contact: calculate a COMMON 3D segment,
  independently verify it on both face planes, retain both local
  barycentric endpoint coordinates, and triangulate each original face
  so the matching segment becomes an explicit, two-sided internal edge.
  Support boundary-to-boundary, boundary-to-interior and interior-to-
  interior endpoints without index-parity assumptions. Strictly preserve
  oriented area; reject vertex grazing, tangency, coplanarity, nearly
  parallel planes, unstable/degenerate triangles and uncertifiable
  triangulations. No partial batch may escape on one uncertain face;
  bound per-batch sections and maintain original persistent BVH roots.
  Regression validates exact common endpoints, per-face winding/area,
  mixed boundary and interior endpoints, budget exhaustion and ambiguity.
  This primitive is **NOT a mesh RegionEdit and never publishes geometry**.
  **V17e2b2 remains:** global per-edge conformance across surviving
  neighboring triangles and symmetry pairs, fragment-by-fragment V14
  occupancy classification and V15 removal, private V16 closure, residual
  collision and witness validation, real repeated successful unions.
  The interactive Merge geometry switch remains off.
- [x] **V17e2b2a — Complete private shared-edge conformance graph for transverse sections.**
  Consume bounded, already-discovered V13 intersection pairs via V17e2b1.
  Assign one **ephemeral** point to every original mesh-edge cut and reuse
  it on ALL originally incident surviving faces; subdivide neighboring
  triangles even when they were not collision participants. Each
  intersecting sheet retains a distinct point at the two shared geometric
  intersection endpoints until V14/V15 can authorize a weld/removal.
  Every affected face has explicitly indexed oriented triangles, checked
  for positive area and original area conservation. Refuse inconsistent
  multi-section faces, ambiguous/nontopological intersections, extra
  cuts on an already sectional source, nonmanifold edges and any
  exhausted per-query contacts/faces/new points/triangles budget WITHOUT
  returning partially usable reconstruction. Optional reflected cut-map pairing uses original reflected edge/face
  provenance rather than coincident coordinates alone; a strict local
  floating-point discrepancy bound permits canonicalization to EXACT
  reflected coordinates, followed by source edge/plane and every-face
  winding/area revalidation. Incomplete/ambiguous reflected partners
  refuse. Regression exercises shared-edge neighbor propagation,
  identical point IDs across adjacent faces, distinct IDs across
  intersecting shells, strict winding and area, hard budget ceilings
  and ordinary/missing reflected pairs. The result is a read-only plan:
  no VertexId allocation, RegionEdit, V14 fragment classification or
  live Merge control yet.
  **Next V17e2b2b:** stage generated VertexIds and all affected faces in
  ONE private RegionEdit; handle multiple contact sections per triangle
  via a bounded polygon-arrangement graph; derive exact intersection
  fragment classification and certified excision then V16 closure,
  mirrored correspondence, residual collision and preserved-witness
  checks. Demonstrate actual repeated successful tower unions before UI.
- [x] **V17e2b2b1 — Unpublished conforming closed-manifold RegionEdit.**
  Given one bounded set of true V13 contacts on a CLOSED orientable mesh,
  regenerate the V17e2b2a conformance plan (never trust arbitrary external
  face/vertex indices), then allocate unique persistent VertexIds for its
  cut points, retain each affected original FaceId for its first child
  triangle and append remaining child triangles as new FaceIds in ONE
  unpublished RegionEdit. Original shared-edge incident faces use ONE
  generated vertex ID; two intersecting solids use DISTINCT generated
  IDs even when their geometric coordinates coincide. Commit only after
  RegionEdit has validated all altered edge orientations, vertex fans,
  links and local triangle geometry. Any unsupported contact, stale plan,
  budget/partition-capacity exhaustion or invalid manifold operation
  discards the private candidate without changing the old immutable root,
  render readers, repository publication or history. A closed pair of
  transverse tetrahedra exercises actual private materialization,
  preserved old IDs and roots, inserted IDs and failure rollback. It is
  explicitly a geometry subdivision, NOT an authorized boolean union:
  residual intersections are EXPECTED until fragment classification.
  Mirrored persistent face insertion is still refused: a separate staged
  reflected face-ID mapping is needed.
  **V17e2b2b2 onward:** multiple intersection curves per source triangle
  through a bounded polygon arrangement; mirrored face transaction;
  per-fragment positive-winding classification and validated excision,
  contour stitching and complete residual/witness checks. Verify
  SUCCESSFUL repeated real symmetric unions before enabling UI.
- [x] **V17e2b2b2 — Bounded multi-section planar face arrangement (unpublished).**
  Extend the V17e2b2a conformance graph when two or more independent
  V13 contacts include the SAME original triangle. Collect section
  endpoints by original face/edge provenance, find and insert every
  pairwise interior crossing or endpoint-on-curve junction with a shared
  ephemeral point index, split all constraints, and assemble planar
  subfaces using an oriented half-edge walk and bounded ear clipping.
  Constrained ends inside a source triangle use separately checked,
  noncrossing triangulation scaffolds; those edges are NOT interpreted
  as Boolean intersection curves. Every reconstructed source face and
  incident edge-neighbor must conserve original signed area and positive
  winding; disjoint overlapping constraints, ambiguous/degenerate
  cycles or exhausted pair-tests/graph-edge/new-node/face/triangle
  budgets (including visibility/ear-clipping predicate tests) refuse
  without exposing a partial plan. The existing
  **single-section path** is retained unchanged for supported cases.
  Real three-tetrahedron regressions exercise two sections CROSSING
  inside one triangle and a second endpoint MEETING the other curve
  there. Both must produce a single unpublished manifold RegionEdit;
  failure budgets, duplicate contacts and historical roots are tested.
  This is NOT an accepted merge: fragments have not been classified or
  excised, and genuine original collisions remain afterward.
  Mirrored MULTI-SECTION persistent face staging remains unsupported
  and explicitly fail-closed.
  **Next V17e2b2b3:** mirrored permanent vertex/face journal for the
  complete subdivided arrangement, then V14 fragment occupancy and
  V15 authorized cuts, V16 verified contours/closure and repeated
  successful physical symmetric unions. Do not turn on Merge geometry.
- [x] **V17e2b2b3a — Read-only mirrored multiple-section orbit preflight.**
  Construct symmetric explicit reflected source-face/edge and new cut-point
  orbits on BOTH intersecting independent closed sheets. Check involutive
  original stable-ID mirrors, EXACT reflected source coordinates, reversed
  original face winding, a unique reflected endpoint/interior junction for
  each section point, corresponding full section curves and bounded
  reciprocal matching. Snap tiny numerical differences to exact reflection
  only with independent source-edge/face and area/winding revalidation.
  Derive each reflected CHILD triangulation from its canonical partner
  (including triangulation scaffolds), not independent diagonal choices.
  Unmatched, duplicated, X=0 new self-orbit, invalid source topology
  or exhausted matching budgets return NO usable plan. Tested on two
  symmetric three-tetrahedra triple-contact arrangements, including real
  interior crossing and T-junction, original immutable roots and refusal.
  **No permanent VertexId/FaceId allocation, published Step or Boolean union.**
- [x] **V17e2b2b3b — One private paired mirrored RegionEdit materialization.**
  Materialize certified reflected single- and multiple-section cut plans
  in ONE unpublished symmetric RegionEdit, rechecking current immutable
  baseline geometry, local original point/face provenance, reciprocal
  mirrored source-face winding, every original/new point and contact
  curve orbit. Call the explicit paired RegionEdit primitives to allocate
  never-reused VertexIds and child FaceIds; retain each rewritten original
  FaceId and return complete net new-vertex and changed-face mirrored
  ID pairs. The canonical first side determines both child triangulations,
  preventing mismatched diagonals; original read roots stay pointer-stable.
  New X=0 cut/seam orbits and invariant source faces continue to refuse;
  no intermediate history, mirror-map or dirty-root state is published.
  Real reflected closed three-tetrahedron crossing and T-junction
  regressions exercise the private revised manifold, exact reflected
  face corners, unmerged sheet identities, original-root stability and
  missing orbit/partition/matching-budget rollback. The ordinary
  nonmirrored behavior is unchanged. **Subdivision is NOT union.**
- [x] **V17e2b2c1 — Real child FaceId two-sided winding assessment (read-only).**
  Preserve exact original FaceId provenance for every retained/inserted child
  in nonmirrored and symmetric private cut commits. Evaluate each ACTUAL
  child triangle with four interior barycentric witnesses at two physical
  local-edge-constrained offsets on either side, using strict accelerated
  winding: inside iff signed w>0, require unit oriented winding jumps and
  equal answers across witnesses/scales. Verify child winding, original
  surface geometry and area conservation first; ambiguous/unreliable
  witnesses discard the whole labeled result with NO deletion permission.
  Bound queries in aggregate and BVH boxes/partitions/triangle visits/rays
  INSIDE each ray query; forbid full-mesh reference fallback. Regress
  exposed, doubly occupied redundant shell, legitimate cavity boundary,
  negative signed winding outside and real mirrored/unmirrored cut candidates,
  work exhaustion and missing/forged original provenance.
  **Finite interior probes are NOT a continuous occupancy certificate.**
- [x] **V17e2b2c2 — Complete local contact-curve coverage certificate.**
  Persist the exact original V13 contact list beside each private
  RegionEdit's original/child FaceId journal. INDEPENDENTLY rediscover
  every original collision involving every changed source face using
  strict bounded V13 collision queries, demanding identical complete
  nonambiguous face-pair sets. Reconstruct each original transverse
  line section, match both distinct persistent sheet endpoints, and
  establish CONTINUOUS segment coverage by real child face EDGES on
  BOTH sheets, including split crossing/T-junction chains. Check real
  child planar containment, positive original orientation and full
  source-area conservation, plus nonoverlapping sibling subtriangles.
  Reject any uncut overlap of a child's OPEN interior with the opposing
  ORIGINAL face's FINITE true section (not its infinite supporting plane).
  Budget original collision search, edge tracing and child predicates.
  Unlisted/stale/ambiguous contacts, failed section traces, area holes,
  overlaps, near-degenerate and unsupported contact geometries refuse
  atomically: NO usable partial certificate, NO deletion or publication.
  Closed two-tetra full-contact positive regression (nine V13 pairs),
  complete binary-exact mirrored two-tetra pair regression (both
  reflected groups), and incomplete or forged lists, endpoints, work
  limits and historical root preservation.
  This establishes COMPLETE local contact-curve coverage only WITHIN the
  supported finite-tolerance transverse V13 domain, not universally
  exact predicates or authorization to delete cavity interfaces.
- [x] **V17e2b2c3 — Atomic cavity-safe real-child removal planning.**
  Recompute the complete c2 V13 contact/child-edge certificate AND the
  strict bounded c1 two-sided positive-winding classifications against
  the SAME immutable conforming cut revision. Every REAL child FaceId
  must have exact matching source provenance and a reliable unit
  signed-winding jump. Preserve Exposed and ReversedBoundary interfaces
  including true cavity walls. Nominate ONLY Buried (positive/positive)
  and Outside (nonpositive/nonpositive) triangles, never by winding
  parity or a whole-original-face fraction. Require complete stable-ID
  mirrored FACE and new vertex orbits, exact opposite face winding,
  identical reflected occupancy classes, symmetric removal sets and
  reflected directed contour-loop/closure choices; missing or mismatched
  twins refuse atomically. Pass those exact sorted FaceIds to V15's
  planRemovalContours; Ready only with a V15-valid complete plan, with
  no partial removal FaceIds or contour metadata on refusal/budget
  exhaustion. The standard shape thresholds remain conservative;
  supported strongly nonplanar tetra regression sets use EXPLICIT finite
  V15 preliminary thresholds, with no relaxation to production defaults.
  Real full-contact nonmirrored and reflected closed-shell fixtures
  exercise Ready, protected surfaces, mirrored paired removal and
  directional contour consistency, missing contacts/mirrors and hard
  work-budget refusal. This only NOMINATES surfaces on ONE unpublished
  candidate: no face retirement, seam weld, V16 surgery, history or CAS.
- [x] **V17e2b2d1 — Strict unpublished V16 surgery acceptance adapter.**
  Always reconstruct c2 complete original-section coverage and c1
  strict real-child positive winding, then derive c3 sorted exact-child
  deletion FaceIds tied to the SAME immutable candidate. V16 must
  independently re-plan those exact faces using MATCHING V15 settings.
  For mirrored cuts require the full original stable-ID involution and
  explicit new reflected cut VertexId pairs; never weld independent
  sheets merely because coordinates coincide. Call the existing
  private V16 surgery, with whole contour/manifold/residual-collision
  and forward scar constraints, then make an ADDITIONAL bounded
  contact query over EVERY surviving changed original-cut child and
  new V16 face. Separately reverse-audit 7 geometric witnesses per
  c3 Exposed/ReversedBoundary child against the completed private
  revision with a tiny original-edge-relative tolerance. Refusals,
  invalid mirror maps, unsupported contours, stale revisions, budgets,
  residual contacts, and protected-surface loss return NO surgery root,
  never a half-approved V15 removal journal. The complete real
  closed-tetrahedron union fixture currently refuses the older V16
  closure with "No edge-length/quality-bounded zipper strip"; do NOT
  relax triangle-quality constraints or claim a successful Boolean.
  No Step CAS/history,
  renderer, picking or UI changes. Actual unjoined transverse seams
  require the next independent proven surgery milestone.
- [x] **V17e2b2d2 — Provenance-bound private cut-seam Boolean join.**
  Recompute c2 V13 complete contact coverage, c1 exact-child positive
  winding and c3 cavity-safe sorted FaceId excision against the SAME
  immutable cut revision. Enumerate every real boundary edge between
  selected removed and surviving child faces; REQUIRE each belongs to
  exactly one original V13 finite transverse contact section. On BOTH
  independent source sheets, pair complete continuous exposed
  child-EDGE chains with IDENTICAL permanent subdivision coordinates
  and OPPOSITE oriented survivor edges. Refuse missing/duplicated
  contacts, unmatched subdivisions, nonexact coordinates or unmatched
  seam vertices; never weld by global position/nearness. Resolve all
  explicit seam vertex equivalence classes to one stable canonical ID,
  preserving mirror equivariance where original/new exact reflected
  pairs are supplied; X=0 self-reflected welds still unsupported.
  Retire ONLY c3-authorized Buried/Outside child faces, rewrite all
  retained incident faces of joined vertices, retire actual orphans
  and commit the entire supported closure in ONE unpublished RegionEdit.
  Verify manifold edge links/fans, bounded WHOLE changed-child
  residual collision absence, original immutable roots, EXACT protected
  Exposed/ReversedBoundary face geometry and reflected surviving face
  winding. Positive acceptance: two closed tetrahedral surface
  components -> ONE connected closed manifold; two reflected pairs
  (four input components) -> TWO connected closed manifold unions.
  Negative checks: truncated contact sections, hard boundary budgets,
  omitted historical mirror involution. No V15 zipper-strip/cap faces
  or UI/worker/history publication; finite-tolerance transverse domain
  only and test-specific finite c3 contour thresholds.
- [ ] **V17e2b2d3 — Compose one original-root private Step and net ID journals.**
  Rebase the completed two-revision conforming cut + exact seam join
  as ONE atomic revision candidate rooted in the ORIGINAL published
  snapshot; reconcile net inserted/rewritten/retired permanent FaceIds
  and VertexIds and complete new/original mirror mappings, dirty
  partitions, cursor/picking dependencies, checkpoint and undo/redo
  semantics without ever publishing the intermediate cut revision.
  Repeat full residual-contact, full protected-surface fidelity and
  every changed region/connected-component acceptance test against
  the combined final candidate. Test CAS conflict, stale old-root
  readers and all-or-nothing refusal; do not enable desktop UI yet.
- [ ] **V17e2b2d4 — Generalize supported transverse cut seam topology.**
  Exercise multiple section curves through one original face, crossing
  and T-junction subdivision chains in COMPLETE closed-shell unions,
  uneven densities, mirrored section intersections and X=0 seam orbits.
  Prove mesh-fidelity bounds and fan validity or fail closed; never
  reinterpret unrelated coincident sheets as contact partners.
- [ ] **V17e2b2d — Private exact-child excision and validated closure.**
  Consume only a V17e2b2c3 Ready nomination tied to one exact candidate
  revision; redo and validate the full private V16 surgery, mirrored
  seam pairing, closure and complete residual-collision/fidelity audit.
  Reject unsupported contours before publishing ANY Step; stage full
  inserted/retired stable-ID and mirror deltas for a later single
  original-root V17c Step CAS. The desktop Merge geometry control
  remains disabled until repeated actual union acceptance.
- [ ] **V17e2b2c — Exact subdivided-fragment positive-winding classification.**
  Classify the exposed/buried status of each REAL child triangle on both
  sides using positive signed winding w>0 and strict local predicates,
  not whole-original-face sample fractions or parity. Preserve legitimate
  cavity interfaces, reject ambiguous near-surface fragments, and
  propagate only explicit selection/removal authorization to V15.
  Then V16 contour closure, complete residual collision and fidelity
  auditing and one original-root V17c Step CAS must be built and proven.
  No desktop Merge geometry UI until repeated successful physical unions.

- [ ] **V17 — Live opt-in 'Merge geometry' pipeline.**
  Wire post-state detection, positive-winding classification, excision
  and zipper into VersionedSculptBrush/AsyncSculptWorker as one complete
  geometry-changing Step and one undo/redo history entry. Re-evaluate
  interactions with split/flip/collapse, checking the *final* candidate,
  not only post-Inflate triangles. Account for newly created mirror pairs,
  dirty renderer/picker partitions, checkpoints and worker statistics.
  Add repeat-contact hysteresis; verify symmetric towers merging, many
  stamps with bounded surface erosion, old-root readers, checkpoint
  round-trip, and that failure retains a valid model and all input Steps.
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
  ambiguous enclosed shells, very uneven/local high-density triangles,
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
