# Sculpt self-intersection management: positive-winding repair and collision constraints

**Status:** V13 detector implemented as an opt-in CPU API; V14–V19 remain proposed/unimplemented.
**Baseline:** V12 `main` at `9e673c4` when this design was captured (2026-10-16).
**Code impact:** V13 adds query-only CPU collision detection with no change to the live worker or renderer.

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
