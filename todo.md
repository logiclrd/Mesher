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
- [ ] **V3 — Region edit transactions and boundary integrity.**
  Implement read/write-set discovery for a local region and its topological
  halo; allocate writable copies of every affected partition before mutation.
  Define atomic mirrored/seam edits spanning multiple partitions. Validate
  cross-partition adjacency, references and rollback on failed edits. No
  published reader may observe a half-completed topology operation.
- [ ] **V4 — Versioned local spatial/connectivity indices.**
  Replace global-index-dependent mutable adjacency and location caches with
  revision-consistent partition-local indices and bounded cross-partition
  links. Update them without full-mesh reconstruction for local operations.
  Keep picking and brush candidate queries exact against their chosen revision.
- [ ] **V5 — Local Inflate and per-stamp pre-edit baseline.**
  Move deformation and curvature calculations to region transactions while
  preserving current pre-stamp simultaneous normals, influence maximum,
  symmetric updates, and directional edge-length comparisons. Avoid copying
  the entire vertex array for `before` state. Compare geometry against the
  established deterministic synchronous fixtures.
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
* V1 establishes storage semantics; V2 adds stable ID allocation and a
  one-time flat/partitioned adapter. Validated topology transactions,
  indexed element lookup, production remeshing, viewport integration, and
  durable undo remain explicit future tasks.

## Other remaining design goals

Keep brush radius independent of editing density, first-class vertex-colour
fidelity and UV-to-colour error budgets, local geometric validity/error limits,
import/export, painting tools, and large-model streaming as future requirements.
Do not silently simplify high-density scan detail as an optimization.
