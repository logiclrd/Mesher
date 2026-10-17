# Sculpt self-intersection management: positive-winding repair and collision constraints

**Status:** V13–V15 queries/plans plus V15.1 read-only closure choice are implemented; V16–V19 remain proposed.
**Baseline:** V12 `main` at `9e673c4` when this design was captured (2026-10-16).
**Code impact:** V13–V15 supply collision, classification and read-only repair plans; neither the live worker nor renderer is changed.

## Problem and reference geometry

Sculpting can make an otherwise manifold triangle surface self-intersect without violating any local edge/fan consistency checks. Three examples motivated this work:

1. **Inverted fold:** a surface pinches across itself, leaving an inverted loop that should not continue to define the intended solid boundary.
2. **Two intersecting sheets:** two separate parts of the surface penetrate one another, burying portions of both surfaces. Either or both parts may move **simultaneously**. A symmetric pair of towers approaching each other across X=0 is a required test.
3. **Isolated enclosed bubble:** a closed internal component may be left after a pinch-off, with no triangle crossings remaining. Its redundancy must be assessed by containment / solid-side classification, not by collision queries alone.

The user's two-crossing cross-section is crucial: **each original surface is smooth through the intersection points; neither makes a sudden turn at an intersection**.

- Surface A: upper-left surviving blue segment -> **lower** buried/red arc -> upper-right surviving blue segment.
- Surface B: lower-left surviving blue segment -> **upper** buried/red arc -> lower-right surviving blue segment.
- At each of the two crossings, the two buried portions are removed and the exposed surviving surface boundaries are joined. Green marks in the 2D drawing are cross-sections of **3D zipper/ladder seams extending into and out of the drawing plane**, not triangles spanning across the whole buried region.
- The cross-section is illustrative only; actual 3D intersections may form multiple curves, tangencies, coplanar patches, junctions, handles, or several hole boundaries. Never assume all repairs are two ordinary loops.

This is **not** a rule that the visually upper curve owns the upper red arc. That mistaken reconnection destroys the intersection and must not appear in fixtures or diagrams.

## Solid semantics: proposed post-deformation geometry

Use the **entire unpublished proposed new geometry** for all collision, orientation, and solid-occupancy decisions. A former valid revision is useful as a baseline to identify changed elements, but is **not** the surface against which to classify inside/outside. Two independently moved sheets may first intersect each other only in the new revision, while neither intersects the other's old position.

For a consistently oriented, closed, immersed surface define signed winding `w(p)` (exact signed solid-angle sum as a reference; hierarchical accelerated queries are candidates for production). Adopt **positive-winding occupancy**:

```text
inside(p) := w(p) > 0
```

Intersections may create winding 2 in overlapping material; that remains inside. Negative-winding regions count as outside, unlike a nonzero (positive-or-negative) fill rule. After cutting/classifying surface *fragments*, retain only fragments with different occupancy on their two sides, with orientation consistent with the resulting solid. For example 0/1 is an exterior boundary and 1/2 is redundant internal material.

Do **not** assume an entire intersected source triangle has a single classification: its crossing fragments can differ. Sampling slightly to each side requires robust scale/offset selection, ambiguous-case handling, and consistency across adjoining fragments. A classification that cannot be established confidently must not authorize destructive deletion. Define predicate/ambiguity tolerances explicitly, not by arbitrary global world epsilon.

**Qualification:** positive winding by itself does not encode artistic intent. In particular, an enclosed reversed shell can represent either an intended cavity (`w=0` inside that shell and `w=1` just outside it) or unwanted geometry. Such a shell remains a boundary under positive occupancy; we need provenance, explicit cleanup policy, or tool intent to decide whether to discard it. Neither positive winding nor a naive rule "delete every enclosed shell" correctly handles all legitimate hollow or multi-object solids. The first implementation may limit automatic cleanup to provably redundant components (same occupancy on both sides) and report ambiguous cavities for later policy work.

## Two independent tool behaviors

**Merge geometry = On:** Permit proposed surface interpenetration, detect hidden surface portions in the whole post-deformation candidate, remove them, and stitch corresponding exposed boundaries. A repaired seam becomes normal editable mesh geometry on subsequent stamps. Do not continuously realign it to yesterday's exact intersection. Avoid accumulated shrinkage caused by repeatedly cutting an already repaired area.

**Merge geometry = Off:** Constrain proposed motion to maintain a small clearance between non-adjacent surface patches. Prefer rejecting/scaling the offending normal component while allowing tangential motion rather than freezing every moved vertex. Clearance is tied to local target geometry resolution / numerical robustness, **never brush radius**. This tool mode does not automatically "fix" unrelated pre-existing import errors.

The default when these features are eventually enabled is a product decision to test on real desktop interaction. Until the live integration milestone, maintain V12 behavior rather than silently changing the current brush.

## Detection and local computation

- Extend the persistent revision-specific triangle BVHs from ray picking to triangle-AABB candidate queries and accurate triangle-triangle overlap tests. Include **changed vs changed** and **changed vs unchanged** candidates, all from the **same post-move candidate revision**; process each unordered face-ID pair once.
- Shared vertices/edges are normal only at their legitimate incident contact; do not blanket-ignore adjacent face pairs if their interiors overlap. Cover coplanar overlaps, near-tangencies, near-degenerate configurations, and broad- vs narrow-phase agreement.
- Use old/new changed-face halos and spatial owners to update BVHs locally without global geometry scans per ordinary stamp. A collision between topologically distant but spatially close partitions must be detected.
- Hierarchical winding / inside-outside queries should be limited to relevant intersection neighborhoods and component candidates. Seed known outside/inside cells when safe, propagate consistent classifications locally, and use conservative checks around ambiguous boundaries. Prove by tests that no *old* state leaked into the post-state classifier.
- There is no generally hard O(local) bound for pathological tangles: work is also proportional to candidate collisions, affected connected regions, and constructed topology. Bound/reject complex transactions rather than publish incomplete repairs.
- No mandatory exact swept-volume CCD in the initial plan. Ordinary Steps are small; endpoint checks plus cheap swept AABB broad phase and displacement bounds are the initial compromise. Explicitly document that thin-wall tunneling is not categorically excluded; defer optional CCD for large-moving-feature tools.

## Approximate, density-aware excision and zipper stitching

Prefer following existing mesh edges near the intersection, retaining original vertices and local fine detail, to exact intersection-curve reconstruction and arbitrary-precision Boolean surgery. Removing full triangles and joining existing boundaries is an approximation with a **measured local geometric error budget**, not mathematically exact Boolean union.

- Local target edge length `h` is independent of brush radius and is an *editing resolution*, not a mandate to downgrade existing higher-density scan detail.
- The user's "~2 mesh pixels" intuition motivates an error budget where cuts follow edges near a crossing: if each contour stays within about `h` of the actual intersection, their separation may be about `2h`. **This is conditional, not a universal upper bound** (acute crossings, uneven triangulation, imperfect contour pairing, or accumulating repairs break it). Choose and validate actual numeric limits empirically.
- Extract boundary contours from locally classified faces, including cases with multiple loops/open chains and intersection junctions; determine compatible pairings and winding. Do not accidentally connect unrelated loops or bridge across the whole buried region.
- Zipper stitch by advancing around paired boundary contours using surviving vertices, adding minimal vertices only when necessary to respect length/shape/error constraints. Enforce nondegenerate, oriented, two-face manifold edges, valid closed one-ring fans, and no newly intersecting triangles. Support mismatched sample counts and cross-partition edges.
- Validate geometric fidelity (distance from original visible geometry / intended intersection neighborhood), edge-length distribution, triangle quality, surface orientation and vertex attribute provenance. Preserve existing vertex positions/colours outside the repair halo; interpolate newly synthesized attributes explicitly when colour support arrives.
- Preserve positive-winding boundary semantics and avoid smoothing or coarse simplification as an unrequested side effect. Repeated repair must have hysteresis / identity-aware stability and be tested for cumulative erosion.
- A difficult/ambiguous case may require local subdivision or geometric fallback. Exact intersection reconstruction is a **possible fallback/reference**, not the default implementation strategy.

## Transactional and history invariants

Produce a private candidate indexed root after simultaneous displacement, then classification/repair and post-remesh collision checks. The published result must include the *whole* validated Step or none of it. No intermediate invalid geometry is visible to renderer, picking, history or checkpoint writer. If a repair exceeds its budget or fails validation, do not commit a partially repaired mesh; choose a safe rollback/constrained deformation or explicit diagnostic, and preserve the ordered 4D sample stream (never silently drop an input Step).

Continue to use `RegionEdit`, immutable copy-on-write bounded partitions, permanent stable vertex/face IDs and tombstones, incremental indices and per-owner render dirty sets. All symmetry topology edits are coupled and atomic, preserving exactly shared X=0 seam vertices and reflected winding; no overlapping duplicate rings on the symmetry plane. Undo reverts one completed geometry-changing Step, including the entire repair; redo and checkpoints preserve the resulting stable-ID topology. No global triangle soup/flatten/rebuild is acceptable in the ordinary stamp path.

A geometric operation from split/flip/collapse can itself create intersections. Therefore recheck the final candidate after all topology changes, not just the raw Inflate movement. The processing order is a design variable, but collision-free validation of the final root is required before publication when the feature is enabled.

## Test and performance contract

- Fixtures: inverted fold, one-sided intrusion, **both sheets moving in one Step**, exact-symmetry towers meeting at X=0, overlapping winding 1/2, negative-winding inverted loop, disconnected internal redundant shell, enclosed intentional cavity, tangency, coplanar overlap, shallow crossing, triple/junction configurations, holes/handles, near-contact and pathological ambiguous cases.
- Test expected red fragments and original-surface continuation in the user's two-crossing diagram: A upper-left -> lower red -> upper-right; B lower-left -> upper red -> lower-right. Test both seam positions, no artificial corners.
- Compare incremental intersection candidates with a brute-force reference on small fixtures; compare accelerated classification to a high-accuracy reference away from ambiguous boundaries; test shadow reader old-root immutability, stable-ID nonreuse, mirror invariants and saved/recovered stitched topology.
- Repeated stamps: no uncontrolled erosion, detail loss, degeneracy, hidden remaining crossings or pathological tiny/long seam edges after hundreds of small motions. Report edge lengths vs local target `h`, error bounds and throughput in addition to raw face counts.
- Respect V12's existing GCC/Clang/MSVC warning-clean CI, 18 CPU CTest suites and reference benchmarks. Headless tests cannot establish native Vulkan/focus/recovery timing; schedule real Linux/Windows desktop checks with stamps, undo/redo and save/recover before claiming interactive quality.

## V13 implementation: post-state collision queries

`RegionTopologyIndex::triangleIntersections(changedVertices,changedFaces,stats)`
visits each face in the supplied local dirty set (including incident faces of
moved vertices), traverses that **revision's** immutable partition BVHs by
triangle AABB, deduplicates canonical stable face-ID pairs, and applies
`triangleContact()` to candidates. The query returns definite geometric
contacts/intersections and explicitly ambiguous pairs; ordinary edge/vertex
incidences are excluded when the faces only touch at their legal shared feature.
Statistics expose seed-face count, boxes and partitions visited, candidate
triangles and deduplicated narrow-phase checks. Retired changed FaceIds are
ignored; unknown live IDs reject rather than silently return a clean answer.

The triangle narrow phase is an explicitly **conservative, floating-point**
static test: separated-axis culling, planar polygon overlap for shared-vertex
coplanar cases, and interval-clipped edge/plane contacts where needed.
Near-degenerate geometry is marked ambiguous. This is not an exact-predicate
Boolean kernel; small numeric tolerances can admit false positive contacts,
and arbitrary degenerate/nonmanifold input remains unsupported. V14 must not
treat ambiguous cases as certified safe for deletion. V13 also does not detect
motion that tunnels through a surface between sampling endpoints.

Tests include independent/coplanar/tangent triangles, shared-feature normal
adjacency, coplanar folded faces, reflected tetrahedral solids moving together
in one candidate revision, brute-force candidate comparison, local pruning
on a sphere, independent retained older revisions and a no-op shared BVH.
Live sculpt collision prevention/merging begins only at V17/V18.

## V14 implementation and conservative boundaries

The immutable revision now exposes `positiveWindingAt()` (accelerated
integer signed ray-crossing count; multiple ray agreement required),
`referenceWindingAt()` (slow independent signed-solid-angle sum),
`classifyFaceFragments()` (bounded 4^depth barycentric *sample patches*,
not exact clipped polygons), and `classifyClosedComponent()`
(bounded connected shell inspection).

The classifications use the whole **new** proposed revision, so they
account for two surfaces simultaneously moving toward each other.
Signed winding 2 is filled; negative winding is outside. An
`Exposed` sample changes positive occupancy across an original face,
a `Buried` sample lies in positive material on both sides, and an
`Outside` sample is nonpositive on both sides. Near-coincident,
on-surface, and numerically inconsistent probes return ambiguous.
Returned patch labels are advisory until V15 has planned validated
trim boundaries: a sample cannot certify that every point of an
original triangle or subtriangle has the same class.

Disconnected, completely closed, intersection-free shell components
can be inspected within an explicit face budget. An isolated reversed
bubble or redundant positive internal shell is reported as redundant;
a legitimate enclosed void boundary is reported as
`PotentialCavity` because artist intent cannot be inferred from
winding alone. Component checks are not allowed to delete geometry.

The twentieth CPU CTest adds direct validation of 0/1/2/-1 winding
against a long-double solid-angle reference, mixed exposed/buried
samples on a single intersected face, symmetric simultaneous motion
and old/new root separation, redundant bubbles, cavities, on-surface
uncertainty, and typical BVH pruning. Full-graph fallback is rare but
possible when all candidate ray directions graze topology; V14 does
not claim a hard worst-case local bound or exact-predicate guarantees.

## V15 read-only excision-plan implementation

`planApproximateExcision()` works against one complete immutable
post-deformation revision. V13 collision pairs nominate relevant
regions; V14's barycentric positive-winding samples drive bounded
candidate face selection, while explicit new closed-component
seeds permit redundant internal bubble elimination without seams.
Ambiguous samples, over-budget regions and topology that cannot be
safely paired are diagnostic failures, not silent guesses.

`planRemovalContours()` handles hypothetical face deletions: the
*surviving-face* orientation determines each directed boundary edge,
and existing stable vertices form closed non-branching contour loops.
Nearby rings are paired via perimeter-resampled correspondence
within a local mesh-edge-scaled gap allowance and a bounded
minimum-cost global matching. Ambiguous equally plausible
matchings are rejected; multiple separate collision sites can
produce multiple local zipper pairs.

The plan contains no stitches and changes no mesh. Face-selection
thresholds, the maximum removed edge length, and seam-distance
statistics are approximations, not strict proof of a 2h geometric
bound. Before removal becomes possible, V16 must ensure that the
proposed excision preserves important visible geometry and that
all new seam triangles are oriented, manifold and collision-free.
The new twenty-first CPU suite exercises these plan structures
and their rejection paths.

## V15.1: Fitness-driven cap-vs-zipper selection

The same two surviving rings may need independent caps (collapsing
torus tunnel) or a zipper (two penetrating walls). Both can be valid
manifold closures, so topology alone is insufficient to choose.
Estimate candidate **additional surface area** for both choices,
prefer the smaller one only when the relative advantage is meaningful:

- Effective ring radii: r_i=sqrt(A_i/pi), from area of each sufficiently
  planar compact directed contour; use its area-weighted centroid C_i.
- Two independent caps: A_caps=A1+A2.
- Coaxial circular-frustum zipper: A_zip=pi*(r1+r2)*
  hypot(length(C1-C2),r1-r2).

This shares the same r_i between alternatives, so an arbitrary minimum
radius clamp is unnecessary. Dimensional units agree. It is an
approximation to the actual surface triangulations, *not* an
exact/general minimum-area solution. Nonplanar or insufficiently
coaxial loops and close numerical decisions remain ambiguous.
The global bounded selector considers a cap for each individual
loop or a pairwise zipper, rather than forcing even-ring pairings.

For geometric zipper feasibility, use normalized cumulative *arc
length* correspondence, searching rotations and both orientations;
unevenly spaced source vertices must not bias the distance metric
or force extreme zipper triangles. V16 must advance fronts by physical
distance, subdivide any exceptionally long surviving boundary edges,
also retriangulate their adjacent surviving surface faces to prevent
T-junctions, and reject skinny triangles or distorted scar geometry.

A torus whose inner tunnel collapses requires cap closure to remove
the handle (genus 1 to 0); a torus with a still-open tunnel does not
trigger repair simply because its hole is small. Test paired cases:
pinched torus, wide torus, merging walls, unequal ring sizes and
coarsely/fine tessellated ring pairs. A V15.1 Ready result remains
read-only and is never permission to commit mesh surgery.

## V16a: Private transaction for capped and zipper repairs

`applyRepairSurgery(snapshot,readyPlan,surgerySettings)` constructs
replacement faces on a private RegionEdit against the exact immutable
V15.1 planning revision. It independently re-runs the plan to reject
stale contour or closure choices. Existing removed face IDs and any
orphaned vertices are retired; unaffected vertices retain positions
and stable IDs. Every candidate new face must satisfy bounded triangle
quality and count limits. The resulting RevisionCommit and paired
immutable index remain **unpublished**: the caller must explicitly
publish the new index with `IndexedMeshRepository::publishIfCurrent()`.

For independent caps, a projection-based polygon ear clip creates
oriented non-fan triangles. For a zipper, a two-front dynamic-programming
strip walks each boundary in opposite winding directions and advances
them independently. Candidates are scored by triangle shape and size,
subject to a maximum *normalized physical arc-length phase lag*;
without that lag constraint, two individually decent triangles could
form a crossed scar. No vertex-index parity is assumed.

Edges longer than the contour's local target length are divided.
Subdivision inserts vertices into both the repair boundary and its
adjacent retained source triangle **within the same RegionEdit**,
preventing T-junctions. Changed edge incidences and vertex one-ring
fans are validated on private topology; the V13 BVH then rejects
intersections involving new or modified scar triangles on the complete
new revision. The original index/root remains unchanged if any stage
fails. The V13 triangle narrow-phase was adjusted so tolerance-expanded
coplanar segments cannot manufacture false penetration at a single
shared scar vertex; the exact pair is covered by a regression.

This initial V16a path supports a bounded subset of simple, non-
symmetric contours. The actual torus regression applies surgery to
the pinched ring and checks Euler characteristic 0 -> 2 (handle removal),
rather than testing only a theoretical closure decision. V16b
**remains necessary**: symmetric face/vertex insertion and deletion
(including shared X=0 seam), full residual intersection checks and
quantitative geometric drift/feature preservation, more general
nonplanar/concave surfaces, and repeated robust repair tests.
None of this runs in the sculpt worker yet; that is V17.

## V16b: Symmetric topology and bounded local post-repair checks

All symmetry-related topology changes happen in one RegionEdit
transaction. Reflected vertices and faces are paired by stable IDs;
new shared vertices on X=0 have exactly one self-mirrored ID. A
symmetric zipper joining two rings which reflect into one another
uses a central seam ring in the plane X=0; its two halves share the
same vertices and have reverse-reflected triangle winding, never
two nearly coincident sheets. Paired planar caps, paired independent
zippers and full symmetric shell removals use the same transaction
rules. Reflected boundary edge subdivision also updates BOTH
neighboring retained triangles before validating the new topology.
Incomplete mirror maps or unsupported crossing/interleaved contour
cases fail conservatively.

Both the non-symmetric and symmetric surgery paths now verify all
surviving faces in the local edit neighborhood (rather than only new
scar faces) against the COMPLETE post-repair indexed revision.
Every inserted vertex and triangle centroid is also checked for
distance to the excised triangles, normalized by local median edge
length; an explicit maximum bounds this one-sided drift, and a
local collision face-count cap bounds the work. Neither guarantee
is a mathematical bidirectional geometric fidelity certificate.

The old revision remains immutable on all failures. The successful
private RevisionCommit and index still require explicit atomic
publication. V16c is needed for general self-reflecting contour
rings, arbitrary seam-crossing subdivisions, robust nonplanar caps
and bidirectional feature-fidelity guarantees. There is no live
`Merge geometry` worker activation until V17.

### Coplanar narrow-phase numerical stability

The V13 triangle-collision predicate has a regression for a very thin
triangulated boundary segment beside a zipper scar. Two coplanar
triangles touching only at a common vertex previously produced a
false positive when (a) polygon clipping expanded its half-planes by
a geometric tolerance, and (b) the projected clipped polygon's area
was summed with a global-coordinate shoelace formula, losing
significant digits through cancellation. Coplanar clipping now uses
the exact half-plane, while area uses a first-vertex-relative fan of
cross products. A final area threshold still rejects near-degenerate
contacts; true positive-area overlaps remain collisions.

## V16c1: One self-mirrored cap and constrained feature-preserving fidelity

A ring fixed as a set by reflection does not have a second independent
ring to cap. With exactly two existing X=0 seam endpoints and a
contiguous positive-X half-arc, the cap can use that arc and the one
seam chord as a temporary half-polygon. The reflected triangle pair
shares the real seam vertex IDs/chord and closes the complete disk;
no duplicate half-shell or new seam vertices are constructed.
The existing vertex-link and V13 collision checks still decide whether
the final private geometry is valid. A gently nonplanar but bounded
self-reflected rim is supported; more general plane crossings remain
explicitly unsupported and therefore cannot publish a revision.

Boundary subdivision no longer assumes one long edge per retained
triangle. Original faces with multiple split contour edges are handled
by a single local oriented subtriangulation followed by one atomic
face replacement and any necessary new faces. Its pure geometry
routine is directly tested for dividing two or all three sides of a
triangle. Symmetric pairs use one subtriangulation reflected onto the
other retained face; seam vertices remain single IDs.

Optional bidirectional fidelity must NOT be applied blindly to
collision excision: correctly deleting buried triangles creates large
removed-to-new distances. For operations known to retain exposed
detail, opt into a sampled reverse distance (original removed vertices
and triangle centroids to new + surviving local geometry), bounded
in units of the original local median edge length. The existing
new-to-old distance remains enforced by default. The explicit choice
of a reverse check, its limit and the measured maximum are retained
in the surgery settings/result. The samples are not a continuous
surface Hausdorff proof.

## V16c2a: Separate isolated-cap geometry from multi-ring decisions

For ONE isolated surviving ring, there is no zipper candidate.
Allow a separately bounded projected-area cap estimate despite
moderate nonplanarity (up to 0.75 effective radius) or concavity
(isoperimetric ratio up to 3.0), rather than applying the stricter
two-contour frustum approximation. Ear clipping now explicitly
refuses projected self-crossings among non-adjacent ring edges.
Normal triangle-quality, local post-state BVH intersection and
deviation checks remain mandatory before even returning a private
repair result. Heavily distorted rings still fail closed.
Crucially, multiple rings **never** inherit this relaxed option;
the topology decision remains ambiguous if the frustum score
cannot be trusted.

The mirrored X=0 seam is an identity condition, not a tolerance:
a true seam vertex has x==0 and is one self-mirrored vertex ID.
The preflight counts exact plane contacts, crossing edges, edges
entirely on the plane and near-plane nonzero x values relative to
the local edge scale. An edge straddling the plane without a seam
vertex, an on-plane edge, a near-plane numerical ambiguity, or a
ring with a nonzero seam count other than two is refused.
A conforming split of cross-plane edges and more complex multi-
contact contour decomposition remains **future V16c2b** work,
not something silently approximated by mirroring a duplicate sheet.

## V16c2b1: Atomic self-reflected crossing-edge canonicalization

Conforming X=0 boundaries sometimes require splitting *self-reflected*
triangles, not only reflected pairs of distinct old face IDs. A
self-reflected triangle has a crossing edge between opposite-X
mirror partners and a third vertex fixed in X=0. Dividing the
crossing edge at its exact plane intersection yields one shared
self-mirrored midpoint ID. The old face ID becomes the first half;
the second half receives a new ID, and the two halves are now
ordinary reflection partners with reversed winding.

The standalone `normalizeSelfMirroredPlaneEdge` operation normalizes
both incident self-reflected faces atomically, so the midpoint is
shared by four triangles and no T-junction remains. It validates
the modified immutable revision against the full collision BVH and
returns it WITHOUT publishing it. Invalid mapping, mismatched edge
or incomplete geometry refuses normalization. A symmetric tetrahedron
regression checks oriented manifoldness, historical old-root
stability and explicit repository CAS publication.

The V16c2a fail-closed boundary guard stays active in mirrored
cap/zipper surgery. A future V16c2b2 step must normalize all
eligible crossing edges and RE-PLAN the removal set after the
new face IDs and contour topology change. More than two seam
contacts additionally requires noncrossing decomposition; it
cannot be inferred from evenness of the number of contacts alone.

## V16c2b2: Conforming crossing contours without intermediate publication

A single original cut ring can cross the X=0 symmetry plane through
edges joining exactly mirrored vertices. The V16c2b2 orchestration
pre-normalizes those edges with the V16c2b1 self-reflected
two-face split. Every crossing creates one exact X=0 vertex shared
by both incident faces; the original face ID is retained for one
half and its new reflected partner gets a new ID.

Important: if the original removed face crosses such an edge, BOTH
of its normalized halves need to be removed. The new system carries
forward all original removed FaceIds and adds those new IDs. It
then re-runs cap/zipper closure selection on the normalized
immutable revision and refuses changes to original loop order,
pairing or intended topology. Final triangulation and post-state
collision tests remain the same as the prior mirrored private
surgeon. Each intermediate root is unpublished; the final index
is published, if requested, by ONE optimistic CAS against the
ORIGINAL source index.

Safety bounds: each original loop must have zero or two total
exact seam-contact/crossing locations; the crossing edge must
join exact reflection partners and separate one retained from
one removed triangle; its two incident triangles must themselves
be reflected onto themselves. Nonconforming cases, budgets or
ambiguously re-planned contours refuse the whole private operation.
The old low-level mirrored-surgery API still fails closed without
such pre-normalization. Generic crossing edge pairs and
self-reflected contours with more than two contacts remain open
rather than being automatically snapped to the seam.

## V16c2b3: Distinct mirrored edge crossings are pre-existing intersections

An edge crossing X=0 and its reflection share the exact world-space
crossing point: the plane is fixed pointwise by reflection.
If they are DISTINCT nonadjacent mesh edges, they are already
geometrically crossing, not two valid seams awaiting independent
subdivision. The pre-normalization classifier now reports this
explicitly. It refuses to create duplicated, coincident X=0
vertices or to weld two unrelated manifold fans at one point.
Only the self-reflected edge-orbit normalization from V16c2b1/b2
can be applied automatically. Existing crossing sheets need
excision and explicit topology selection, which is a different
problem from vertex/edge canonicalization.

The boundary of a reflection-symmetric embedded disk has two
transverse X=0 contacts, delimiting its single fixed seam arc.
A vertex that merely touches X=0 and returns to the same side
is tangential, not another transverse crossing. Four contacts
on one contour cannot be blindly paired as two seams without
risking crossing chords, pinches or changed topology. The
plane-contact preflight now distinguishes those cases and
refuses nontransverse or excessive contacts. Four crossings
across TWO distinct independently capped contours remain
valid and have a positive end-to-end regression.

No intermediate geometry is published on refusal. The remaining
V16 safety work concerns high-complexity collision/excision,
feature-bearing surface classification and fidelity validation,
not creating duplicate reflection-plane seams by normalization.

## V16d1: Feature-sensitive reverse fidelity from original V14 samples

When material is removed to resolve an intersection, some old
triangles may have sampled regions that lie on the exterior
positive-winding boundary and OTHER regions that are completely
buried inside the solid. Applying a reverse-distance requirement
to every removed vertex incorrectly forces buried geometry to
remain present. Ignoring every exposed fragment, however, risks
destroying fine surface features.

V16d1 offers a read-only original-revision sampled provenance
report. Optional `requireExposedFragmentFidelity` checks the
distance from the barycentric centroids of Exposed and
ReversedBoundary patches to the local final surface. Buried and
Outside patches are permitted to disappear. Ambiguous samples
cause a private refusal. This mode is mutually exclusive with
the older `requireReverseSurfaceFidelity` mode that checks all
removed faces irrespective of original winding evidence.
Budgets constrain sampling and the number of evaluated
sample/triangle distances, and the result reports the counts.

The low-level read-only removal plan must NOT be treated as
authoritative classification of what should be deleted; callers
must explicitly choose preservation policy. The default remains
unchanged. Neither one-sided proximity metric certifies a
continuous surface Hausdorff distance or feature equivalence.

An outer cube cap and an inner, positively wound redundant shell
are regression fixtures for these opposite policies. Eight
widely separated cube caps exercise a combined private
near-coplanar multi-component edit, collision validation and
bounded-work refusal. No live worker is wired in V16d1.

## V16d2a: Policy decision belongs to the classified caller

The V15 low-level removal-contour planner is intentionally usable
with an arbitrary nominated face set, for tests and preflight.
It is NOT authority to delete occupied exterior features.
`applyClassifiedExcisionRepair` instead starts with current-revision
dirty vertex/face IDs and optional isolated component seeds,
runs V13 intersection + V14 winding + V15 approximate removal
planning, then requires an explicit choice between TopologyOnly
and PreserveSampledExposedFeatures. Ambiguous or no-op plans
return without any private revision; a Ready proposal is still
subject to all private V16 repair validation.

The exposed-detail policy samples ONLY original Exposed and
ReversedBoundary fragments, not buried patches. For mixed
exposed/buried triangles, the report now has a direct regression
using overlapping cubes from the V14 test suite. It remains a
sampled constraint; whole-triangle approximate excision does
not magically become exact cut-polygon reconstruction.

Additional robustness: five successive independent cap
repairs preserve all held historical roots, stable retired FaceIds,
and genus; attempts to reuse an old removal plan fail. Near
coplanar/tangent and ordinary overlapping shell candidates
cannot mutate their original snapshots whether classification
rejects or a private repair is validated. All work remains
off the live sculpt worker until V17.

## V16d2b: Audit cumulative local surface loss through held revisions

To detect progressive erosion over many otherwise bounded surgery
steps, the caller can preserve witness points in the FIRST snapshot
and compare them to a chosen live patch after any number of edits.
`sampleSurfaceWitnesses` acquires seven geometry samples for each
original face; `auditSurfaceWitnesses` returns original-edge-length
normalized maximum, mean and RMS distances to the nearest triangle
in an explicitly nominated final face set. It counts actual
point-triangle evaluations and rejects out-of-budget work or
retired candidate IDs. The current mesh may have entirely new
FaceIds; the witness coordinates are independent of topology
identity and time.

The regression replaces one square surface patch in a closed cube
32 consecutive times, keeping every old revision readable. Every
step requires manifoldness, valid orientation, zero cumulative
sampled displacement against the INITIAL patch and one CAS
publication. A deliberately incorrect target patch is also
measured, to ensure the audit would observe real material loss.
An additional offset three-solid overlap matrix exercises the
collision planner, safe private rejection and immutable reader
roots on difficult folded inputs.

These measurements are discrete and must not be described as
continuous surface fidelity certificates. The API is a
read-only diagnostic, not automatically invoked by the worker.
V17 must decide which exposed features to anchor and how
excessive accumulated erosion should affect a live brush Step.
