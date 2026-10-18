# V17 — Flat, independently completable milestones

**Authoritative work plan, rebaselined from Mesher `main` at `28c419f6` (PR #50).** Supersedes the *organization* of the older `V17e2b2...` nesting without changing the feature objectives, numerical acceptance policy, supported-domain limits, or any implementation already accepted. The [original task block](V17_LEGACY_TASK_TREE.md) is preserved verbatim; [`DESIGN.md`](../DESIGN.md) remains the detailed engineering history, and [V16 final acceptance](V16_FINAL_ACCEPTANCE.md) remains the pre-existing private-surgery boundary.

## Why a new plan is necessary

The earlier sequence described a speculative linear path through triangle clipping, winding, closure and live publication. Implementation exposed several **orthogonal** obstacles that should not become indefinitely nested descendants of one task:

- The V17e2b2d3b worker path can already publish *explicitly supplied complete-contact* asymmetric and reflected two-tetra unions with one original-root CAS, one undo/history transition and checked mirror/tombstone/checkpoint changes. This is not the automatic sculpt policy.
- PR #49 accepted a real 3→1 **closed manifold** union for two disjoint *simple* interior section loops on one source triangle, including worker replay. Its closed protrusions require all original faces that need removal to have appropriate cut provenance.
- PR #50 admitted detached source-face section **junction graphs**, and admitted surviving subintervals separated by sections buried inside a third body. Complete generic 12-contact and orthogonal 16-contact closed-three-tetra cuts pass private **c2 contact coverage, c1 signed-winding occupancy and c3 cavity-safe removal**. They still fail d2 *exact* cross-sheet junction/subdivision matching, and therefore return **no publishable** final root.
- The older crossing/T-junction 21-pair closed-three-solid examples include genuinely unsupported V13 nontransverse/grazing pairs. Selected two-pair subdivision success must never be mistaken for complete-contact acceptance.
- A separate 1-above/3-below protrusion exposes a **wholly buried original face that was never subdivided**; c3 child-only authorization does not permit its retirement.
- Distinct remaining concerns are unequal seam density, off-plane mirrored section orbits, invariant **X=0** seams, physical repeated sculpt unions, cumulative exposed-feature fidelity/contact policy, aggregate work limits, post-CAS failure behavior and UI activation. They warrant separate completion criteria.

Keeping completed V17e2b2c and V17e2b2d umbrella checkboxes open alongside completed c1–c3/d1–d3b obscured actual progress. Their text remains in the archive; they are **not new uncompleted implementations**.

## Milestone rules — no further task nesting

Each active milestone has exactly one flat ID, **V17-M01 through V17-M12**. No lettered, numbered or recursive child IDs are permitted. An unresolved complication becomes either a named *acceptance case inside the same milestone* or a **new peer** milestone through an explicit rebaseline of the list—not M01a, M01b1, etc.

Each milestone must deliver a bounded, independently reviewable change to the **existing immutable mesh**, private read-only diagnostic or explicit worker/desktop policy; its own positive/negative regression tests and measurable acceptance conditions; documentation of supported/refused geometry; and warning-clean GCC, Clang and MSVC CI. It must be possible to merge the milestone with the feature still disabled; where later milestones consume earlier ones, prerequisite interfaces must be explicit, rather than treating the entire V17 as one merge-sized unit. **Tests that accept safe refusal do not prove successful union.** Close a milestone only on its stated positive acceptance; a persistent exact-junction mismatch must not be rebranded as a finished three-body union.

Unless a milestone explicitly says otherwise, every private operation keeps the current synchronous and async Inflate paths operational, respects the original 4D sampling/ordering, preserves stable VertexId/FaceId, tombstones, never-reused allocator watermarks, old-root/BVH picking identity, shared unaffected COW partition pointers and mirror symmetry, and publishes nothing before the whole final Step is authorized. In a live path, there is at most **one CAS against the captured original root and one undo/history entry**; worker rejection consumes the input without inventing a replacement stamp or losing subsequent input. Failure returns no partial usable FaceId removals, join mappings, mirror journal, final root or renderer dirty update.

### Status baseline (already implemented, not re-opened)

The accepted foundation is V17a, V17b, V17c, V17d1/d2, V17e1/e2a, V17e2b1, V17e2b2a/b1/b2/b3a/b3b, V17e2b2c1/c2/c3, V17e2b2d1/d2/d3/d3b and the qualified V17e2b2d4 PR #49/#50 subsets. This includes bounded query/winding work and fail-closed exceptions, private conforming RegionEdit and exact mirrored IDs, original V13 graph rediscovery, finite c1 child occupancy sampling, c3 protected cavity classification, the d2 strict original-contact seam weld, the d3 immutable original-root composer, explicit worker queueing, history/checkpoint handling, and original-anchored witness/hysteresis **diagnostics**. Any claimed completion is restricted to its existing test fixtures and stated refusal domain; it does not imply arbitrary closed-shell Booleans or desktop activation.

## Flat completion matrix

| Milestone | Standalone deliverable / acceptance gate | Dependencies for its positive case |
| --- | --- | --- |
| **V17-M01** | Canonical source-proven junction coordinates on three intersecting planes, with conforming fans and exact section-point identities | Existing cut and coverage framework |
| **V17-M02** | Complete exact survivor-interval ownership/subdivision reconciliation, including mismatched source densities | M01 for true triple-junction positive cases |
| **V17-M03** | Independent classified retirement of untouched fully buried original faces, cavity-safe and bounded | Existing c1/c2/c3 |
| **V17-M04** | Publishable private **three-body** union with complete original contacts and manifold/fidelity certification | M01–M03 where fixture needs them |
| **V17-M05** | Explicitly mirrored off-plane multi-section/triple-surface union | M02/M04 geometry as needed |
| **V17-M06** | Reflected **X=0** invariant seam/fixed-point contact topology | M05 correspondence conventions |
| **V17-M07** | Finite supported/refused geometry and adversarial topology acceptance matrix | Existing cut/join; parallelizable |
| **V17-M08** | Repeated *actual sculpt-driven* symmetric classified union Steps and recovery | Sufficient M04–M06 accepted domain |
| **V17-M09** | Active cumulative exposed-detail and contact-episode decision policies | Existing anchor/episode diagnostics; can run in parallel |
| **V17-M10** | Aggregate per-Step resource caps and honest timing/diagnostic telemetry | Existing per-query budgets; can run in parallel |
| **V17-M11** | Transactional post-CAS/exception and persistent-state fault acceptance | Existing explicit d3b worker; can run in parallel |
| **V17-M12** | Default-off live **Merge geometry** opt-in and targeted desktop acceptance | Relevant earlier positive gates, no unresolved publication safety failures |

Dependencies are **test prerequisites**, not permission to make an unbounded combined PR. For example M03, M09, M10 and M11 may be developed and merged while M01/M02 remain unresolved. A blocked milestone retains its ID and a specific refusal/acceptance counterexample; do not keep subdividing it recursively.

## V17-M01 — Canonical three-sheet junction provenance

**Implementation/acceptance:** Complete on the private, strictly transverse
source-proven domain. `buildMultiSectionCutPlan` now enumerates face triples
only if ALL THREE original V13 source-pair sections exist and meet at a
finite common point. It derives one coordinate directly from those source
planes, retains a different point index for each independently owned
source face, and rechecks the three original-face fans and exact physical
coordinates. A hard `maxJunctionCandidates` bound and existing arrangement
predicate/quality checks preserve atomic refusal. Source contacts are
materialized in a deterministic FaceId-pair order, then the caller's
original section-journal order is restored. The complete generic
12-contact and orthogonal 16-contact fixtures pass canonical provenance,
reversed discovery order, independent c1/c2 and c3 classification, and
work-budget regressions without introducing preclassification welds.
The generic fixture also happens to pass current d2/d3 3→1 join checks;
this **does not** satisfy M04's separate requirement that BOTH full
12/16-contact fixtures publish valid three-body unions, nor M02's
unequal-density seam-matching acceptance. Finite section endpoint
grazes, ambiguous/near-parallel original pairs and unsupported old
21-contact full graphs remain fail-closed. M01 adds no worker/UI action.


**Objective.** For a fully transverse original three-face/three-plane contact network, calculate a *single reproducible physical junction coordinate* and propagate it through the affected source-face edge/interior point provenance while retaining **distinct VertexIds for independently intersecting solids until authorized welding**. Resolve the two existing triple fixtures (12 and 16 complete original V13 pairs) in which c1/c2/c3 already pass, but d2 refuses independently computed endpoint coordinates.

**Acceptance.** Every declared triple junction is explicitly associated with all participating original FaceIds and finite V13 sections; each source patch preserves original face plane/edge location, area, orientation and conforming incident-fan topology within existing strict numerical predicates; all sheets derive exactly identical physical junction coordinates without searching for unrelated coincident vertices. The resulting private cut passes independent original-contact rediscovery and bounded c1/c2, including missing/ambiguous/near-parallel/grazing and work-exhaustion negatives. Deliver reproducible positive original point-orbit tests on both the generic and orthogonal cases. **Not** required to weld or publish a three-body union in M01.

## V17-M02 — Exact seam-interval and unequal-density reconciliation

**Implementation/acceptance:** Complete as an **independent private
refinement-and-matching adapter** on the certified finite transverse
original-section domain. The existing d2 join now compares exact
source-paired 3D vertex endpoints rather than requiring bit-identical
rounded scalar line projections; direction/face provenance remains
mandatory. The new `reconcileCertifiedSeamSubdivisions` consumes a
complete c1/c2/c3 authorized unpublished cut and sweeps the directed
surviving intervals of each original V13 contact, treating third-body
buried gaps as valid only if BOTH sheets agree. It inserts the other
source sheet's EXACT known boundary vertex coordinate into the
unmatched sheet's local edge, in a single unpublished RegionEdit that
conformingly splits BOTH incident source children (and handles multiple
points/edges). It preserves old vertices, validates positive triangle
areas and the source-child area sum, re-runs full independent c1/c2/c3
against all new child FaceIds and requires unique exact opposite
endpoint match of every remaining authorized boundary edge. Hard
predicate/vertex/face/tracing limits and mirror orbit preflight refuse
without any usable revised root.

Positive regression: introduce a REAL asymmetric child-edge split
into the certified generic 12-contact three-body cut; the old d2 join
strictly refuses, the adapter inserts at least one opposite-side
point and both incident faces, c1/c2/c3 pass, and existing unchanged
d2 can finally produce a connected 3→1 terminal private union.
The unchanged cut takes a zero-edit path; incomplete V13 originals,
zero insertion budget, unsupported mirrored new-split orbit and stale
historic readers are negative controls. This milestone neither
publishes nor changes the d3 original-root Step composer. Folding
the adapter's EXTRA private COW edit into a **single original-root
publication and net undo/tombstone journal** remains M04. New
mirrored orbit staging remains M05 and X=0 is M06.


**Objective.** Reconcile the complete original-contact **surviving** boundary graph across independent sheets when third-body burial removes parts of source sections, when proper crossings or T-junctions change segment subdivision, or when the two source surfaces have different local edge densities. The already supported simple-loop/full-length case remains valid. Preserve explicit original contact provenance; **never** infer weld partners by nearest coordinate.

**Acceptance.** Pair each surviving oriented edge interval exactly once to a source-proven counterpart, with shared canonical section-point coordinates, compatible child subdivisions and strictly opposite survivor directions. Split/refine *only* the source-proven local faces and all adjacent shared-edge fans where required, in bounded private edits; no hanging vertices or changed exposed/cavity corner positions. Missing one-sided sections, duplicate/overlapping interval ownership, mismatched winding, forged contact identities, under-budget limits and intentionally nonexact coordinates refuse **before** exposing a weld. Include asymmetric-density, partially buried interval, crossing/T and mirrored-negative regressions. Deliver a passing **private seam matcher**, not the whole automatic worker feature.

## V17-M03 — Fully buried unchanged-source-face authorization

**Implementation/acceptance:** Complete as a private READ-ONLY
retirement proposal. `planUncutFaceRetirement` recomputes complete
original-source V13 c2 coverage, real-child signed-winding c1 evidence and
the only valid child removal classes, then expands the full bounded
ORIGINAL edge-connected patch of untouched source faces adjacent to
the actual cut sources. Every traversed triangle must retain exactly
the same old FaceId, owner, three original VertexIds and physical
coordinates on the privately cut revision. An independent local V13
collision search over each whole original face certifies there is NO
intervening source-surface contact through its interior; uncertain,
grazing, nonmanifold or partially explored components refuse. Four
interior sample locations, each with two exact signed offsets on both
normal sides, must agree on a reliable unit positive-winding jump
without unbounded global reference fallback. Buried/Outside original
faces are authorized; all Exposed/ReversedBoundary faces (including
genuine cavity boundaries) remain protected. The authorized and
protected original IDs are combined with the rederived real-child
journals and freshly validated through V15 contour readiness; a
caller-fabricated Ready contour does not confer deletion authority.

The positive fixture is a one-apex-above/three-base-vertices-below
closed tetrahedron penetrating a second closed tetrahedron. Its buried
base has NO V13 section and NO c1 cut-child provenance, but becomes
a separately proven unchanged original FaceId in the combined
retirement proposal. All untouched outer faces remain protected.
An additional disconnected inverted closed shell inside the large
tetrahedron represents a true cavity and is not even scoped as
eligible for removal. Incomplete source pairs, zero original-face
component budget, exhausted ray budget, and exhausted original V13
candidate discovery each fail atomically with no authorization; old
root/picking identity and tombstones remain unchanged.

M03 does NOT actually retire geometry or return a joinable COW root.
M04 owns consuming this fresh independent proposal, exact d2 closure
and original-root single-Step net FaceId/VertexId/undo publication.
Mirrored new untouched-source retirement needs M05/M06's complete
original and new involution orbits. Full V19 large-model performance
and universal exact Boolean support remain explicitly out of scope.


**Objective.** Extend exact-child c1/c2/c3 provenance and retirement to an ORIGINAL triangle that was *never cut* yet is proven wholly Buried/Outside in a complete local union. The prior 1-above/3-below protrusion case is the concrete missing geometry; changed-child-only deletion must remain unauthorized.

**Acceptance.** A separate complete-original-face occupancy/connected-component decision proves homogeneous strict positive-winding behavior, correct original source identity and no intervening intersecting section before any retirement. Preserve Exposed and ReversedBoundary cavity/outer detail and forbid an arbitrary enclosed cavity from being discarded. Bound discovery, winding and fan/collision checks; atomically refuse mixed, uncertain, unsupported or partially surveyed unchanged faces. Include a positive formerly buried-uncut-face fixture, boundary/cavity negatives and original root/tombstone checks. The authorized FaceId set must compose with c3's journal without allowing a manually nominated V15 Ready plan to authorize removal.

## V17-M04 — Complete closed three-body Boolean union

**Objective.** Compose the three-body V13/c2/c1/c3/d2 results (and M03's extra authorization if necessary) into one private, complete original-root d3 transaction. Unlike M01/M02, this is the first positive **3 intersecting closed shells → 1 connected closed manifold** gate with genuine junctions, not merely two disconnected section loops through one face.

**Acceptance.** At least the complete 12-contact generic and 16-contact orthogonal three-tetrahedron fixtures produce an accepted final immutable root with one connected, closed, correctly oriented, edge- and vertex-manifold union, zero relevant final changed-halo intersections, unchanged protected surface coordinates/witness bounds, valid net FaceId/VertexId retirement/rewrite/insert journals and a single original-root CAS/history/undo/checkpoint transition. Repeat entire contact discovery, no selected-pair shortcut; all unsupported historical 21-pair grazing/T cases still refuse atomically unless independently proven supported. No UI activation or claim of universal Boolean behavior.

## V17-M05 — Explicit mirrored multi-section union, away from X=0

**Objective.** Apply accepted multi-section/crossing/three-body union geometry on **both distinct reflected halves** in one coherent operation. Respect the already implemented reciprocal historical mirror map, new paired point/FaceId orbits, reverse winding, reflected triangulation and explicit removed/surviving mirror journals; never merge independent source sheets because of shared world coordinates.

**Acceptance.** Positive closed-shell reflected multi-section fixture(s), including proper crossing/T contact graphs under complete V13 evidence, yield exact reciprocal reflected final coordinates and a single original-root immutable Step; undo/redo, stale CAS, checkpoint reload, and all three synchronized worker mirror maps preserve both halves. Missing/noninvolutive map, wrong source-face partner, unpaired generated vertex/face, mismatched occupancy, asymmetric removal or budget overrun return no root or partial mirror delta. Existing two-pair mirrored union stays green. Self-paired X=0 cases remain **explicitly unsupported until M06**.

## V17-M06 — Invariant X=0 seam and shared-plane topology

**Objective.** Support the currently refused self-reflected fixed-point seam orbits and crossing source faces at the reflection plane in exact-cut union, not simply V16's earlier cap/zipper normalization. Reuse the established mirror design: off-plane pairs at exact reflected coordinates, **one permanent self-paired vertex on X=0**, opposing face winding and one shared topological seam rather than doubled coincident border loops.

**Acceptance.** Positive complete closed contact fixtures with on-plane junctions/curves and symmetric cut, classified removal and final repair; all invariant vertex/edge/face orbits are explicitly proved and retained or retired atomically. No duplicate self-paired seam IDs, wrong-sided fans, independent colliding-sheet coincidence welds or partial mirror-map publication. Refuse genuinely nonconforming four-or-more contacts on one invariant disk ring, tangent/coincident planes and ambiguous self-plane topology unless separately certified. Validate old readers, mirror journals, history and durable restore.

## V17-M07 — Supported-domain refusal and fidelity matrix

**Objective.** Turn actual discovered geometric limits into explicit *bounded, reproducible* supported/refused contracts rather than a promise of arbitrary overlapping solids. Exercise complete 21-pair historical crossing/T cases and V16's near-tangent/folded tests; clarify which near-parallel, coplanar, grazing, shared-vertex, branching, nested-loop, tunnel, multi-component, strongly nonplanar and imported nonmanifold inputs are unsupported.

**Acceptance.** Deterministic matrix of positive cases and negative fail-closed cases using **complete V13 original contact discovery**, with diagnostics identifying the real blocker (input predicate, arrangement, occupancy, missing unchanged face, cavity, seam, quality, manifold, fidelity or work cap). No unsupported case produces a partial edit, new history entry, lost input, orphaned mirror pair, unbounded internal work or accepted residual self-intersection. Preserve finite edge/area/triangle-quality and sampled witness checks; do not silently relax production V15 thresholds to pass synthetic fixtures. This is a supported-domain contract, **not** the broader user-device/100k-vertex V19 program.

## V17-M08 — Repeated real sculpt-driven union Steps

**Objective.** Replace the explicit-only V17e2b2d3b test command as the sole demonstrated success path with a narrowly supported, genuinely collision-triggered post-Inflate/refine/collapse union decision. Preserve 4D input sampler order, independently moving sheets, local split/flip/collapse, symmetry and the unchanged default off behavior.

**Acceptance.** The actual symmetric two-tower physical fixture performs repeated **successful** classified union Steps over at least the established sixteen alternating-tower inputs, not merely safe rejection; every accepted Step is complete and globally checked in the relevant changed halo. Mix no-contact and unsupported collisions; reject failing Steps recoverably, consume every input exactly once, and continue to later good stamps. Match CPU private roots, async worker snapshots and original readers; one CAS/undo/redo/dirty owner delta per accepted Step, no in-between cut root. Retain 32-repeat same-patch and old-root controls. No desktop toggle yet.

## V17-M09 — Active original-anchored fidelity and contact hysteresis

**Objective.** Promote the existing optional seven-points-per-original-face anchored witness guard and read-only three-clean-check episode tracker into explicit, conservative **geometric decisions**. Define original reference, stable spatial patch mapping, preserved exposed versus intentionally displaced detail, budget/cumulative bound and a documented reset/hysteresis rule.

**Acceptance.** Absolute deviation is measured from nominated ORIGINAL exposed features across multiple Steps, not from successive drifting revisions; Buried and Outside material is allowed to disappear. Conclusive evidence (not uncertain/budget-exhausted checks) alone permits an episode reset; hysteresis never skips full final collision/classification validation. Demonstrate accepted legitimate deformation, rejected cumulative erosion, exposed/cavity preservation, same-patch repeat, symmetric outer walls, deliberately moved-feature exclusions and budget refusal with exact rollback. No unsupported claim of a continuous two-sided Hausdorff certificate.

## V17-M10 — Aggregate work bounds and decision telemetry

**Objective.** Enforce finite limits across an entire speculative Step in addition to the completed V13 per-query, c1 winding, V15 contour, arrangement and private residual budgets: repeated ray/BVH walks, repeated component tests, normalization, triangulation, join tracing, witness comparisons, and speculative memory/partition-copy accounting. The exceptional full-mesh solid-angle fallback stays disabled for live policy.

**Acceptance.** A declared collection of global Step caps limits counted operations, affected faces/vertices/partitions, candidate contacts, winding queries, trace/arrangement predicates and memory-proxy work; exact-limit success and one-under-limit refusals prove no partial authorization. Status distinguishes clean accepted, successful union, budget refusal, uncertain contact, unsupported geometry, stale original root and unexpected fatal error, with counts, lifetime erosion, contact episode and measured elapsed CPU time. Do not claim strict universal real-time or RSS bounds, even with finite counters; V19 owns broader scalability measurements.

## V17-M11 — Publication-phase exception atomicity

**Objective.** Close the previously identified gap where **exceptions after the final CAS** can remain fatal. Make the final geometry, mirror-map state, stable ID/tombstone changes, render dirty partitions, picking root, history/redo, checkpoint metadata and worker counts one coherent observable Step, or enter a documented safe terminal state without silent divergence.

**Acceptance.** Fault injection and stale-race tests around validation, mirror staging, CAS, history append, snapshot mailbox, and durable checkpoint scheduling prove either (a) a rejected, unobserved Step with the exact old published state, or (b) one fully committed state that readers, renderer, undo/redo and reload observe consistently. No duplicate history transition, partial reflected orbit, leaked candidate root, skipped 4D sample or misleading success telemetry. Distinguish legitimate recoverable validation/budget refusal from unexpected exceptions; no unbounded full-mesh copy on each commit. The already accepted explicit asymmetric/reflected worker unions suffice for *standalone* positive tests.

## V17-M12 — Opt-in Merge geometry activation and targeted acceptance

**Objective.** Expose **Merge geometry = On** only for the explicitly proven V17 supported-domain pipeline, with the prior default behavior remaining **Off**. Treat the feature switch as an independent product/publication gate, not as a by-product of passing a low-level private fixture.

**Acceptance.** Real input strokes choose a complete post-Inflate/refine/collapse Step policy, exact classification and closure, aggregate limits, active feature preservation, hysteresis and full final collision checks. Supported closed joins actually publish; unsupported/uncertain paths safely reject and drain input. Targeted Linux and Windows Vulkan desktop tests observe the on/off default, responsive picker/render roots, symmetric sculpt, dirty partitions, one-step undo/redo, crash-safe checkpoint/recovery and visible rejection diagnostics over repeated contacts. Keep broad imported-mesh, complex near-degenerate topology, 5k/20k/100k+ device benchmarks and universally fast editing **for V19**, and keep the separate Off-mode collision-constrained displacement algorithm **for V18**.

## Cross-cutting safety and unchanged V18/V19 boundaries

- Classify occupancy by **positive signed winding** (inside iff w>0), preserve true ReversedBoundary cavity interfaces, verify a full unit oriented jump across child faces. Finite interior probes alone cannot prove continuous behavior; require complete c2 original-contact coverage and final geometry validation before deletion.
- Re-discover the COMPLETE bounded original V13 contact set; no hand-picked pair subset, global nearest-neighbor seam welding, forged FaceId/VertexId provenance, incomplete original incident-edge conformance, implicit arbitrary cavity deletion or relaxed quality/fidelity limits. Use the same immutable cut root through c1/c2/c3 and d2/d3 authorization.
- Preserve source geometry and original enclosed detail when classified Exposed/ReversedBoundary, and permit proven Buried/Outside deletion. Anchor cumulative witnesses to the ORIGINAL relevant revision, with explicit exemption for intentional sculpt displacement and measured bounded work; do not claim an unconditional two-sided Hausdorff guarantee.
- Preserve symmetric X=0 fixed-point topology, permanent ID and mirror involution, retained/reflected winding, COW partition pointer identity, original-root reader stability and exact undo/redo/checkpoint metadata. No intermediate cut/normalization root can become visible.
- Default brush mode stays unchanged until M12; a rejected input is consumed in FIFO order, does not publish an invalid deformation and does not stop later input. Recoverable bounded-work refusal must be distinct from unexpected fatal failure.
- Existing V16 cap/zipper arc-length ring correspondence, radius-difference/centroid-separation topology heuristics and contour quality policy stay as accepted; the exact-section Boolean join must not manufacture a gratuitous zipper/cap or silently apply a worse contour threshold to avoid a refusal.
- **V18 remains separate:** when Merge geometry is Off, constrain normal displacement to avoid NEW nonadjacent crossing with mesh-scale clearance and permit valid tangential sliding; both sheets moving and symmetry need tests. This is not exact swept CCD; document tunneling and do not clean arbitrary imported intersections.
- **V19 remains separate:** extensive near-coplanar/multicontact/tunnel/folded/imported geometries, extreme local density, repeated edits, performance at 5k/20k/100k+, larger meshes when practical and hands-on Linux/Windows Vulkan acceptance, without claims of universal real time, global volumetric remesh or total RSS ceiling.
- **Non-goals unchanged:** mandatory CGAL dependency in the hot path, universal exact-predicate Boolean reconstruction, global volumetric remeshing, global per-Step BVH rebuild, unconditional two-edge-length deviation, forced genus zero, silently discarding arbitrary enclosed cavities, or losing high-density scan detail, UV/vertex-colour fidelity and later painting/import/export/large-model streaming requirements.

## Historical ID map (all original details archived verbatim)

| Historical work | Status at rebaseline | Flat milestone(s) retaining unfinished work |
| --- | --- | --- |
| V17a / V17b | Complete bounded winding/collision foundation | M10 aggregate extensions |
| V17c | Complete prepublication rejection/rollback | M11 post-CAS gap |
| V17d1 / V17d2 | Complete supported classified private/mirrored paths | M05, M06, M08, M11 |
| V17e1 / V17e2a | Complete repeat-contact probes, anchored diagnostics | M08, M09 |
| V17e2b1 / V17e2b2a / V17e2b2b1 / V17e2b2b2 | Complete transverse sections, conformance and private arrangements | M01, M02, M07 |
| V17e2b2b3a / V17e2b2b3b | Complete reflected private plan and ID staging | M05, M06 |
| V17e2b2c1 / c2 / c3 | Complete real-child occupancy, source coverage, protected removal | M01, M03, M04, M07 |
| V17e2b2d1 / d2 / d3 / d3b | Complete surgery adapter, exact join, original-root Step and explicit worker | M02, M04, M08, M11 |
| V17e2b2d4 including PR #49 and PR #50 | Qualified progress, previously open umbrella | M01–M08 |
| V17e2b2c / V17e2b2d | Superseded **open umbrella summaries** over already complete child contracts plus current gaps | M01–M07, M11 |
| V17 | Live product objective, still open | M01–M12, culminating in M12 |

The original exact identifier text, statuses, comments, test expectations and scope warnings are retained in [the verbatim task archive](V17_LEGACY_TASK_TREE.md). This table maps *ownership of remaining acceptance gaps*, not a claim that previously completed source code needs to be redone. The broad V17 objective remains unfinished until **every required flat milestone** has independently passed and M12's explicit activation gate is accepted. V18 and V19 have not been postponed, merged, deleted or changed.
