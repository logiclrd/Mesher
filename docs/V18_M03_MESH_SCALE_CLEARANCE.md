# V18-M03 — Local mesh-scale clearance and conservative sliding proof

## Scope and architectural boundary

**M03 adds an independently metered static candidate clearance predicate**
to the separately enabled **V18 OFF-mode** pre-CAS worker admission implemented
in [M02](V18_M02_PRIVATE_STEP_ADMISSION.md). It is not a different
Boolean, does not silently clean imported geometry, and does not enable
constraints in the ordinary desktop. `--merge-geometry` retains the
authenticated V17 source-certified policy and is explicitly incompatible
with the OFF-mode guard. The original V18 objective, V19 and deferred
V17 UI work remain open.

The new `RegionTopologyIndex::nearbyTrianglePairs` uses the *same*
revision-owned, path-shared directory/partition triangle BVH as M01.
For each stable changed face (including all incident faces of changed
VertexIds), its AABB is expanded by a configurable fraction of its
**own shortest nonzero triangle edge**, not by a brush radius, model
bbox or coarse neighbor's edge. The actual pair clearance requirement
uses the **minimum of both faces' shortest edges**:

```text
r(pair) = 0.05 × min(shortestEdge(triangle A), shortestEdge(triangle B))
```

The 0.05 fraction is a conservative initial policy default, NOT a
universal accuracy theorem. Since the pair's required clearance never
exceeds the seed's own query expansion, this broad phase cannot prune
a pair solely because the other triangle has finer density. Shared
**stable VertexIds** are legitimate local adjacency; M01 still checks
folding/overlap on such pairs. Exact XYZ coincidence never creates
stable identity.

The query meters *each* actual seed, directory box, partition,
candidate triangle, unique broad-phase pair and returned near pair.
All six counters have separately bounded nonzero defaults
(4096/131072/32768/65536/32768/4096 respectively) and one-under
regressions. `maxDistanceTests=8192` independently charges every
current and historical distance evaluation. Any exhaustion throws
or reports **Uncertain**, never a truncated safety certificate.

## Geometric decision

`assessOffModeMeshClearance(before,candidate,changedVertices,
changedFaces,limits)` first runs the existing **full M01 before/after
intersection ledger**. A newly introduced nonadjacent contact is
`Contact` (refusal), and an ambiguous or moved old contact is
`Uncertain` (refusal) before any positive distance interpretation.

For each candidate near pair, M03 computes the minimum Euclidean
triangle distance over both sets of vertex-to-triangle witnesses
and all nine edge-to-edge witnesses, with double-precision Voronoi
closest-point tests. End-state triangle intersection/penetration is
covered separately by M01. Degenerate triangles, nonfinite or
unreliably tiny local scales, and distances within the numeric
boundary uncertainty are conservative refusals. The positional
tolerance is bounded by
`max(1e-12 × localShortestEdge, 64 × DBL_EPSILON × maxAbsoluteCoordinate)`.
A normal displacement that newly reduces separation below the
mesh-local requirement returns `TooClose` **even when the two final
surfaces remain disjoint**.

A previously sub-clearance pair is **not automatically repaired**.
It can remain `UnchangedOld` when both entire triangles and stable
corner IDs are bitwise unchanged. A previously too-close pair that
really moves is admitted as `TangentialSlide` only when ALL are
independently proven: both original face IDs and ordered corner IDs
remain stable; each face undergoes an approximately rigid translation
within the numeric tolerance; their normals are parallel or
antiparallel; the relative translation lies in BOTH face tangent
planes; positive endpoint separation does not decrease; and M01
finds no unsafe new/moving historical contact. Changed topology,
rotation/shear, a normal approach, and zero-gap/grazing that lacks
reliable M01 evidence are **not silently called legal slides**.

This is a conservative **finite endpoint** certificate, not exact
swept-volume CCD. A sufficiently large stamp could tunnel between
endpoints. M04 will implement bounded normal-motion limiting and
backtracking; M05 will exercise reflected source orbits and motion
of both opposing sheets in real transactions. Independently supported
grazing motions may be broadened only with separately tested
positive evidence, not by weakening the current refusal.

## Worker integration and negative cases

When `SculptCollisionSettings::offModeConstraints.enabled=true`,
M02's real `VersionedSculptBrush::stamp` runs the classifier on
the COMPLETE post-Inflate/refine/collapse private candidate after
the actual attempted COW/dirty work accounting, BEFORE the single
published-root CAS, mirror journal, undo/history transition, renderer
dirty mailbox or any accepted episode credit. The contact limits
are inherited from M02; separate neighborhood and distance caps
are configurable through `offModeConstraints.clearanceLimits`.
A `TooClose`, `Contact` or `Uncertain` refusal follows the
existing pre-CAS rollback and worker FIFO recovery path.
**The default remains disabled**, V17 protected Merge unchanged.

`offmode_mesh_scale_clearance` tests actual immutable COW edits
from `RegionEdit` and exercises: safely separated parallel sheets,
approach to sub-mesh-scale clearance BEFORE contact, real intersection,
legal rigid tangential slide at unchanged pre-existing small gap,
normal component rejection, both sheets moving toward one another,
mixed coarse/fine densities using the smaller local scale, old
unrelated imported crossing preservation, stable shared adjacency,
all six one-under BVH categories, zero distance-test work budget,
invalid numeric policy and preserved old indexed-root/picker identity.

A further **real `VersionedSculptBrush` stamp** drives one vertex
toward a separate triangle without any improper triangle crossing;
M03 must reject the complete private root and retain source,
history and renderer identity. A subsequent valid outward stamp
must publish one undoable root, and pointer-exact undo/redo must
continue working.

## Completion / next work

M03 can close only after exact PR-head and independently merged-main
GCC, Clang and Windows MSVC Vulkan-enabled suites, history stress
and benchmark CI. **M04 is next:** finite conservative normal
displacement clamping/backtracking, with rerun of this final
post-refine/collapse contact/clearance gate for each attempted
private candidate. Original V18, M05–M07 and V19 remain open.
