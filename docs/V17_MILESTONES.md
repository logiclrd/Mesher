# V17 — Flat, independently completable milestones

**Authoritative work plan, rebaselined from Mesher `main` at `28c419f6` (PR #50).** Supersedes the *organization* of the older `V17e2b2...` nesting without changing the feature objectives, numerical acceptance policy, supported-domain limits, or any implementation already accepted. The [original task block](V17_LEGACY_TASK_TREE.md) is preserved verbatim; [`DESIGN.md`](../DESIGN.md) remains the detailed engineering history, and [V16 final acceptance](V16_FINAL_ACCEPTANCE.md) remains the pre-existing private-surgery boundary.

**October 18 M07 scope rebaseline:** M06 exposed three independently completable downstream gates (c3 invariant contour certification, d2 exact Boolean disk/weld, original-root worker/history). The *full* original M07 objective is retained as sequential flat M07–M09, and former M08–M13 objectives are renumbered M10–M15 without loss. Historical append-only DESIGN and archived genealogy retain their historical labels.\n\n## Why a new plan is necessary

The earlier sequence described a speculative linear path through triangle clipping, winding, closure and live publication. Implementation exposed several **orthogonal** obstacles that should not become indefinitely nested descendants of one task:

- The V17e2b2d3b worker path can already publish *explicitly supplied complete-contact* asymmetric and reflected two-tetra unions with one original-root CAS, one undo/history transition and checked mirror/tombstone/checkpoint changes. This is not the automatic sculpt policy.
- PR #49 accepted a real 3→1 **closed manifold** union for two disjoint *simple* interior section loops on one source triangle, including worker replay. Its closed protrusions require all original faces that need removal to have appropriate cut provenance.
- PR #50 admitted detached source-face section **junction graphs**, and admitted surviving subintervals separated by sections buried inside a third body. Complete generic 12-contact and orthogonal 16-contact closed-three-tetra cuts pass private **c2 contact coverage, c1 signed-winding occupancy and c3 cavity-safe removal**. They still fail d2 *exact* cross-sheet junction/subdivision matching, and therefore return **no publishable** final root.
- The older crossing/T-junction 21-pair closed-three-solid examples include genuinely unsupported V13 nontransverse/grazing pairs. Selected two-pair subdivision success must never be mistaken for complete-contact acceptance.
- A separate 1-above/3-below protrusion exposes a **wholly buried original face that was never subdivided**; c3 child-only authorization does not permit its retirement.
- Distinct remaining concerns are unequal seam density, off-plane mirrored section orbits, invariant **X=0** seams, physical repeated sculpt unions, cumulative exposed-feature fidelity/contact policy, aggregate work limits, post-CAS failure behavior and UI activation. They warrant separate completion criteria.

Keeping completed V17e2b2c and V17e2b2d umbrella checkboxes open alongside completed c1–c3/d1–d3b obscured actual progress. Their text remains in the archive; they are **not new uncompleted implementations**.

## Milestone rules — no further task nesting

Each active milestone has exactly one flat ID, **V17-M01 through V17-M18**. M17 and M18 are append-only independent peers scheduled before the original M11 acceptance; M11–M15 identifiers and objectives remain unchanged. No lettered, numbered or recursive child IDs are permitted. New M16 is an append-only peer scheduled **after M10 and before M11**: accepted M01–M15 labels and historical cross-references are not renumbered. This is an execution order, not a recursive ID hierarchy. An unresolved complication becomes either a named *acceptance case inside the same milestone* or a **new peer** milestone through an explicit rebaseline of the list—not M01a, M01b1, etc.

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
| **V17-M06** | Source-proven **X=0** invariant fixed-point cuts and reflected child-face fan topology, **private only** | M05 correspondence conventions |
| **V17-M07** | Read-only positive X=0 c3 self/paired mirror contour, complete child classification and cavity-safe retirement | M06 private cut |
| **V17-M08** | Complete original-contact X=0 exact PRIVATE d2 welded closed union, manifold/fidelity/collision audit | M07, M02/M03 |
| **V17-M09** | ONE original-root Step, net self-orbit mirrors, worker, undo/redo and checkpoint | M08 and M04/M05 history |
| **V17-M10** | Finite supported/refused geometry and adversarial topology acceptance matrix | Existing cut/join; parallelizable |
| **V17-M16** | General off-plane coincidence provenance, identity preservation and certified-only seam welding | M04/M05/M08/M09, source-aware M10 cases; REQUIRED before M11/M15 |
| **V17-M17** | Complete post-sculpt original-source V13 contact graph; bounded read-only classification on actual private remesh candidate | M10/M16 proof domain; existing V17 candidate pipeline |
| **V17-M18** | ONE physically triggered real exact Boolean union inside one sculpt Step, atomic original-root/mirror history | M17 contact graph and M09 original-root composer |
| **V17-M11** | Repeated *actual sculpt-driven* symmetric classified union Steps and recovery | Accepted M04–M09 Boolean domain; M10 and M16 geometric safety gates |
| **V17-M12** | Active cumulative exposed-detail and contact-episode decision policies | Existing anchor/episode diagnostics; can run in parallel |
| **V17-M13** | Aggregate per-Step resource caps and honest timing/diagnostic telemetry | Existing per-query budgets; can run in parallel |
| **V17-M14** | Transactional post-CAS/exception and persistent-state fault acceptance | Existing explicit d3b worker; can run in parallel |
| **V17-M15** | Default-off live **Merge geometry** opt-in and targeted desktop acceptance | M10 and M16 supported-domain gates; relevant earlier positive gates and publication safety |

Dependencies are **test prerequisites**, not permission to make an unbounded combined PR. For example M03, M12, M13 and M14 may be developed and merged while M01/M02 remain unresolved. A blocked milestone retains its ID and a specific refusal/acceptance counterexample; do not keep subdividing it recursively.

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

**Implementation/acceptance:** Complete for the declared
finite original-V13 transverse three-solid fixtures in a PRIVATE
one-original-root net Step. `prepareExactCutStep` now composes M01
canonical source point cuts with M02's additional explicitly verified
source-side subdivision edit (if needed), M03's independent retirement
authorization for whole uncut buried originals, and strict d2 private
exact-weld closure. **The intermediate COW revisions are never published.**
The seam preflight and final d2 removal are checked against the SAME
M03-authorized source boundary, preventing the still-present but
demonstrably buried whole original face from masquerading as an
unpaired survivor seam. Every newly produced child FaceId and retained
original requires independently recomputed c1/c2/c3/M03 proof;
the original/root final comparison yields net stable IDs, tombstones,
rewrites, true dirty partitions and the sum of private COW copied
payloads, excluding identities born-and-retired before publication.

The **complete** generic 12-contact and orthogonal 16-contact
three-tetrahedron graphs both produce ONE final immutable connected,
closed, orientably sewn union with no remaining nonadjacent final
triangle intersections; protected source/cavity surfaces are unchanged.
Their acceptance test audits all final faces, uses exactly one CAS
against the captured baseline, rejects stale replay, appends ONE
undoable history step, undoes/redoes exact counts and original pointer
identity, checks net FaceId/VertexId and dirty journal counts, and
restores a durable checkpoint with identical tombstones. A THIRD
positive fixture—the one-apex-above/three-base-vertices-below
protrusion—removes its completely untouched buried original base
and retains an unrelated inward-oriented cavity shell, resulting
in two intentional final closed components (outer connected union
plus protected interior cavity). Full original V13 graph omission,
tightened net changed-ID and interval sweep budgets and the earlier
full 21-pair unsupported grazing/T examples refuse without any
publishable root, history transition or partial stable-ID journal.

This M04 gate does **not** enable a default or automatic worker
Boolean policy. Its new M02+M03 path is asymmetric only; V17-M05
owns complete reflected new section orbits and mirrored untouched
face retirement; V17-M06 owns X=0 invariant seam orbits. V17-M10
still owns the larger adversarial geometric refusal matrix; V17-M11
owns repeated physical sculpt-triggered union Steps; V17-M12–M14
own active cumulative fidelity/hysteresis, aggregate full-Step work
caps, and post-CAS injected failure robustness. V17-M15 alone enables
the default-off desktop option. Synthetic regression contour
settings retain the previously accepted bounded V15 positive
threshold envelope and do **not** change production defaults.


**Objective.** Compose the three-body V13/c2/c1/c3/d2 results (and M03's extra authorization if necessary) into one private, complete original-root d3 transaction. Unlike M01/M02, this is the first positive **3 intersecting closed shells → 1 connected closed manifold** gate with genuine junctions, not merely two disconnected section loops through one face.

**Acceptance.** At least the complete 12-contact generic and 16-contact orthogonal three-tetrahedron fixtures produce an accepted final immutable root with one connected, closed, correctly oriented, edge- and vertex-manifold union, zero relevant final changed-halo intersections, unchanged protected surface coordinates/witness bounds, valid net FaceId/VertexId retirement/rewrite/insert journals and a single original-root CAS/history/undo/checkpoint transition. Repeat entire contact discovery, no selected-pair shortcut; all unsupported historical 21-pair grazing/T cases still refuse atomically unless independently proven supported. No UI activation or claim of universal Boolean behavior.

## V17-M05 — Explicit mirrored multi-section union, away from X=0

**Implementation/acceptance:** Complete for two reflected
closed-three-tetrahedron groups with FULL generic 24- and orthogonal
32-contact original V13 graphs and distinct off-plane source/child
vertex orbits. The mirrored V13 arrangement treats exact repeated
source-point IDs as one shared endpoint rather than recomputing
numerically unstable double projections; a truly near-but-distinct
endpoint remains an unsupported ambiguity. M01 source triple
junctions, full c2/c1/c3 occupancy, strict d2 exact opposite seam
joins, V15 contour limits and M04 original-root net journals remain
mandatory. The final closed geometry is two disconnected, oriented,
edge/vertex-manifold three-body unions, with a global no-extra-
intersection test over EVERY surviving FaceId.

M03 original unchanged-face classification now supports only
explicitly provided, strictly off-X=0 reciprocal original mirror
maps. It proves each uncut original face partner from its THREE
mirrored original VertexIds, REVERSED winding and exactly reflected
positions, including independently Exposed/ReversedBoundary
protected faces. It refuses a nonreciprocal/missing source pair,
wrong source-face partner or missing opposite occupancy. M04 d2
joins the paired authorized uncut FaceIds and checks that protected
untouched faces remain unchanged even for a NoChanges removal.
A second positive closed geometry has two independently mirrored
1-above/3-below buried original tetra bases and two separately
inverted enclosed cavity shells. Exactly the two buried original
base FaceIds retire as one reciprocal orbit, while BOTH reversed
cavity components stay protected. New paired cut VertexIds/FaceIds,
face winding and mirror journal invariants remain strict.

CI regressions verify terminal mirror closure for EVERY original
and newly inserted live vertex and FaceId, one original-root CAS,
stale CAS refusal, one undo/redo history transition, exact net
mirror/retirement journal and durable checkpoint reloading. A queued
asynchronous worker 24-contact test verifies all three existing
mirror-map controllers remain synchronized through a successful
union, rejected stale command, undo/redo and checkpoint recovery.
Full-pair omissions, incomplete/noninvolutive or reciprocal-but-
geometrically-wrong mirror maps, dropped mirrored child FaceId
orbits, asymmetric original occupancy, zero pairing work, too-tight
terminal net-ID cap and uncut source-connected patch work limits
fail with NO published or partially consumable final root.

**Domain boundary retained:** Nonproven additional mirrored
unequal-density subdivision splits are still refused rather than
forging new partner IDs; M10 will record this in the geometric
supported/refused matrix. M06 owns self-paired invariant X=0
source faces and seam vertices. M11 owns physical automatic
sculpt-triggered union, M12–M14 remaining global policy and
publication fault testing, M15 the default-off UI opt-in. This
milestone does NOT relax any original V13/V15 geometry threshold,
introduce global nearest-coordinate welding or enable live Merge
geometry.


**Objective.** Apply accepted multi-section/crossing/three-body union geometry on **both distinct reflected halves** in one coherent operation. Respect the already implemented reciprocal historical mirror map, new paired point/FaceId orbits, reverse winding, reflected triangulation and explicit removed/surviving mirror journals; never merge independent source sheets because of shared world coordinates.

**Acceptance.** Positive closed-shell reflected multi-section fixture(s), including proper crossing/T contact graphs under complete V13 evidence, yield exact reciprocal reflected final coordinates and a single original-root immutable Step; undo/redo, stale CAS, checkpoint reload, and all three synchronized worker mirror maps preserve both halves. Missing/noninvolutive map, wrong source-face partner, unpaired generated vertex/face, mismatched occupancy, asymmetric removal or budget overrun return no root or partial mirror delta. Existing two-pair mirrored union stays green. Self-paired X=0 cases remain **explicitly unsupported until M06**.

## V17-M06 — Source-proven invariant X=0 cut and reflected child fans

**Implementation/acceptance:** Complete as a **private source
conformance milestone**, not a completed X=0 Boolean union. A new
two-closed-tetra fixture has THREE FULL original V13 strictly
transverse source contacts with X=0 crossing geometry, after
removing one genuine near-tangent side-plane configuration from
the initial diagnostic fixture. A complete exact mirror map
self-pairs existing ORIGINAL vertices on X=0 and reciprocally
pairs the strictly off-plane originals.

The reflected multi-section planner identifies a new self orbit
ONLY for an original proven invariant cut edge or source face.
Independent V13 arithmetic had produced X=-5.55112e-17 instead of
exact zero; the source-constrained normalization sets ONLY that
one X coordinate to zero, within a finite scale-dependent error
envelope. Y/Z, old vertices and independent near-coincident
distinct original source points are NEVER snapped or welded.
The original invariant source triangle's positive-X cut half-fan
is selected once and explicitly reflected into negative-X
children with reversed winding. Each source-owned fixed point
becomes exactly ONE fresh VertexId allocated with
`RegionEdit::appendSeamVertex`; off-plane points are allocated
only as real reciprocal `appendMirroredVertices` orbits.
`RegionEdit::replaceSelfMirroredFace` checks the entire child
fan, staged reflected pairs and possible genuine invariant
singleton child FaceIds before making ONE unpublished COW root.
Every changed source FaceId retains full area, winding, complete
incident local edges/vertex fans and original-source provenance.

The POSITIVE test requires `buildConformingCutPlan`,
`materializeTransverseCutCandidate` and independently recomputed
`certifyConformingCutCoverage` to pass the complete original
contact graph. It audits source-owned fixed/new paired vertex
cardinality, all child face-orbit identities and reversed cycles,
unchanged old corners, distinct immutable source/cut picking
roots and absence of original-root retirement. Missing a V13
pair refuses COMPLETE c2 coverage (whether at planning or
certification); noninvolutive original mirrors and a zero pairing
budget also refuse. No default geometry policy or historical
M05 off-plane behavior has been changed.

**Next and NOT ACCEPTED as part of M06:** Strict c3 child-winding
and mirrored removal authorization currently refuses this same
new source cut at 'Conflicting child-contour vertex mirror mapping'
because the previous V15 child/contour evidence assumed distinct
two-ID vertex/face orbits. V17-M07 explicitly owns support for
proven self-paired X=0 vertices and invariant FaceIds throughout
c1/c2/c3, source-owned cavity-safe retirement, d2 source-paired
seam equivalence/closure, exact symmetric net stable ID history,
one original-root CAS, worker three-controller mirrors and
checkpoint recovery. The initial incomplete terminal-join and
worker experiments were *not* merged into the accepted M06 path:
their earlier branch commits remain available for M07 review,
and M06's runtime join/worker implementations remain fail-closed
on this newly accepted cut. M10–M15 and the V17 overall feature
remain open; automatic Merge geometry is disabled.


**Objective.** Establish one independently completable PRIVATE
conforming cut for full original V13 strictly transverse contacts
crossing the reflection plane. Prove that on-plane intersections
are source-owned fixed points rather than two nearest-coordinate
copies. Invariant source triangles require a shared existing-edge
X=0 midpoint, conforming opposite incident face subdivisions,
and fully reflected child triangles including singleton
self-mirrored invariant FaceIds.

**Acceptance.** Complete closed-shell positive X=0 contact fixtures
pass whole-original V13 section coverage and a locally 2-manifold
immutable cut. One permanent self-mirrored ID per original source
sheet fixed point, exact retained original XYZ, original corner
identity, real reciprocal off-plane mirror pairs and opposite child
triangle winding/area/fan. The synthetic mirror-axis line is
explicitly a conforming source-face subdivision, NOT a new V13
contact or early Boolean weld; all actual V13 source pairs remain
unaltered and complete. Strict refusal for grazing, coincident
planes, unpaired or ill-conditioned mirror points, ambiguous
invariant child triangulation and all work-budget overruns.
No retirement, seam weld or history is part of M06.

## V17-M07 — Invariant-plane c3 mirror/contour certification

**Implementation/acceptance:** Accepted on the exact independently
certified M06 three-original-contact X=0 source fixture. The c3 mirrored
certificate uses one canonical FaceId entry per distinct pair or real
singleton; source-owned fixed VertexIds require a genuine prior original
corner/fan or the exact newly inserted M06 seam journal. New paired and
singleton orbit cardinalities are checked against ALL inserted IDs. Each
child/source face pair is verified against the exact reversed reflected
original face and child triangle cyclic winding. Removed FaceId orbits
can be (a,a); contour rings can likewise have a single self-reflected
index, but their true cyclic source-owned reflection must be unique and
carry exactly two fixed boundary anchors. V15 cap/zipper choices still
need matching reflection orbits without relaxed runtime thresholds.
M06's positive fixture passes independent full c1/c2/c3 Ready, complete
new mirror classification and old-root immutability. Tampered singleton
FaceId mapping, omitted seam ID, contact omission, nontransverse/grazing
original and exhausted classifier/contour work refuse. Linux GCC,
Linux Clang and Windows MSVC 23-suite test/benchmark acceptance was green
on code head 9b4a0a6. **No original-root Step, d2 physical weld, history
or automatic merge is claimed; M08/M09 own those positive gates.**

**Objective.** Consume M06's TRUE complete three-contact X=0
source-conforming private cut; certify c1 occupancy, complete c2
finite two-sheet coverage, verified singleton as well as reciprocal
child FaceId and VertexId involutions, and cavity-safe c3 deletion
and reflected boundary-contour ownership. The published original
root remains unchanged; no Boolean seam join is authorized here.

**Acceptance.** The exact M06 closed symmetric two-tetra fixture
positively reaches c3 Ready: every source child has consistent reliable
positive signed winding, each mirrored child/contour is supported by
original face, edge and whole-fan provenance (including a possible
single invariant reflected orbit), and all legitimate exposed and
reversed cavity surfaces are protected. Any required M03 untouched
original authorization stays independently source-proven. Missing
source contact, erroneous X=0 self-pair, conflicting vertex/FaceId
orbit, incomplete contour, false 4+sheet disk, tangent/grazing,
or exhausted bounded work refuses with no partial nomination.
Existing M05 off-plane behavior remains unchanged. No d2 weld,
terminal history Step, worker or UI activation in this milestone.

## V17-M08 — X=0 exact source-paired PRIVATE Boolean join

**Implementation/acceptance:** Complete on the full M06
three-original-contact symmetric two-closed-tetrahedron fixture as
a PRIVATE certified d2 Boolean union. After independently recomputed
c1/c2/c3 and M07 mirrored contour evidence, each matched seam
boundary is authorized ONLY by the exact original V13 FaceId pair
and identical 3D endpoints with opposite surviving directions.
The historical/new cut mirror map now includes true X=0 self
VertexId orbits; a self-reflected welded equivalence class is
permitted only if EVERY contributing independent source-sheet
vertex is itself exactly self-mirrored and has explicit M06 new
seam-point or true original source-corner provenance. Any reflected
off-plane class remains a distinct reciprocal two-ID class.
One unpublished RegionEdit retires only actual c3-authorized buried
children, exact counterpart seam duplicates and true orphan IDs.

A complete independent audit of ALL terminal faces confirms one
connected, closed, oriented edge manifold, one connected vertex
link at EVERY final VertexId, and zero nonadjacent terminal surface
intersections. Exact old XYZ, original picking root, protected
surface geometry, net private FaceId/VertexId tombstones and
physical reciprocal/singleton mirror orbits pass. The independent
M03 unchanged-original source survey now accepts real invariant
historical vertex and original FaceId orbits, with a singleton
FaceId only if one corner is fixed and the other two are an exact
exchanged off-plane pair under reversed cyclic winding. Its
optional join mode independently produces the SAME valid closed
X=0 terminal private union on this fixture. No invented
unchanged-original deletion is required in that particular
two-tetra positive test.

Refusals for missing source contacts, forged historical fixed
orbit, exhausted exact seam pairing and M07's forged child/point
orbits return no final root, retirement list or welded mapping.
The genuine M06 grazing original remains unsupported. Production
V13/V15 quality and contact thresholds are not weakened;
only the EXISTING synthetic tetra test-local V15 contour settings
are used. The full 23-suite CPU and benchmark CI succeeded under
Linux GCC, Linux Clang and Windows MSVC at code commit fefa926
(Actions run 39138641095). **This is NOT a full original-root
Step: M09 still owns ONE captured-original CAS/history, self-orbit
net mirror journals, worker three-map synchronization, undo/redo
and checkpoint acceptance. M11/M15 retain automatic/live enablement.**

**Objective.** Consume the independently positive M07 c3 proposal
plus any source-complete M03 authorized original-face retirement,
then pair exactly coincident finite original V13 survivor intervals
on BOTH independently represented sheets. A fixed-point Boolean
equivalence class requires each contributing source VertexId to
have independent original X=0 fixed identity; shared world position
or mere mirror orbit never authorizes welding.

**Acceptance.** Complete M06 three-original-contact two-tetra
geometry yields exactly ONE privately staged closed, connected,
properly wound, edge- and vertex-manifold union with exact physical
reflection, valid single on-plane disk/link, no double X=0 loops,
hanging T branch, orphan FaceId or residual nonadjacent collision.
Protected original exterior/cavity coordinates survive unchanged,
only c3/M03 authorized material retires, and unchanged-source
provenance, bounded seam quality and final fan checks remain.
Missing original pair, unrelated coincident sheet, wrong mirror
orientation, nonmanifold 4+sheet junction, tangent/coplanar/near
plane unproven points, unbalanced section intervals and exhausted
join/retirement limits refuse without a consumable final root.
This milestone stages NO published Step or history.

## V17-M09 — X=0 original-root Step/worker/checkpoint acceptance

**Implementation/acceptance:** COMPLETE for the exact M06–M08
source-proven X=0 THREE-original-contact two-closed-tetra geometry.
The terminal original-root net journal includes one (id,id)
`addedMirrors` for every LIVE new source-certified fixed X=0
vertex, and one (id,id) `removedMirrors` when an original fixed
vertex is truly retired. Ordinary off-plane IDs retain one
distinct reciprocal pair per orbit, with exact source and final
XYZ; intermediate born-and-retired private vertices/faces are
excluded from net published mirror/history additions.
The final root retains proper closed oriented vertex-link
manifoldness and no nonadjacent collisions while original
source corners, protected cavity surfaces and persistent
historical readers remain unchanged. Changed root dirty
partitions and ID/count/COW work journals are derived from
only the ORIGINAL versus FINAL immutable states.

The complete Step passes ONE captured-original CAS and stale
replay refusal, one undo/redo history transition, and durable
checkpoint read-back including singleton/pair mirror orbits,
permanent tombstones and stable watermark identities.
The queued async worker first refuses a missing original
source pair with no published cut/history, then successfully
completes one captured-root Step, rejects stale replay, reverses
the mirrored fixed/pair journals on undo, re-applies the same
journals on redo, saves terminal checkpoint, and restores a
new symmetric worker whose three controller maps remain
synchronized. Corrupt original off-plane self-orbits and
work exhaustion refuse the entire Step atomically.
The first failure in the worker regression was its test's
uncanonical mirror-pair ordering, not a runtime restoration
failure; canonicalized pair comparison then passed all
cross-platform suites. GCC, Clang and MSVC warning-as-error
code-head 29fec56 CI passed all 23 CPU tests and benchmarks
in Actions run 39139919037.

Automatic sculpt-triggered merging is STILL NOT enabled
(M11), unexpected post-CAS exceptions are a separate
terminal safety boundary (M14), and the desktop Merge
geometry toggle remains default-off and disabled (M15).

**Objective.** Complete the rest of the *unchanged original M07
objective* by composing M06 cut, M07 c3, M08 closed d2 and any
independently M03-authorized uncut original into exactly ONE
original-root stable-ID transaction, undoable history transition
and durable queued explicit worker union.

**Acceptance.** The positive complete 3-contact X=0 fixture reaches
one ORIGINAL-root CAS/Step, one undo/redo with pointer identity,
complete new/retired/rewritten FaceId and VertexId/tombstone net
journals including genuine self-paired and reciprocal vertices,
exact surviving mirrored child-face orbits and real face-owner
dirty COW partitions. All THREE worker mirror controllers remain
synchronized through queued execution, rejection, undo/redo and
.mshr checkpoint restore. Source-corner fidelity, final full actual
collision, cavity, orientation and manifold checks still apply.
Stale expected root, missing original source pairs, malformed
self or reciprocal orbit, bad disk, exhausted work and worker
stroke conflict refuse without any partially visible revision,
mirror journal or history. Unexpected post-CAS fault handling
remains M14; automatic sculpt-triggered geometry merge remains
M11; the default-off desktop switch remains M15.

## V17-M10 — Supported-domain refusal and fidelity matrix

**Completed M10 bounded acceptance (October 18).** A new
[complete-source supported-domain register](V17_M10_SUPPORTED_DOMAIN.md)
enumerates each bounded positive and refusal case by real ORIGINAL
V13 contact count, source proof, validation stage and final topology
result. The historical crossing and interior T-junction **21-contact**
closed three-tetra original graphs (not their two selected locally
cuttable curves) deterministically refuse, in original and reversed
contact order, with NO intermediate or publishable original-root
Step/journal, no retirement and unchanged three source components.
Independent tetrahedra touching only at one XYZ, wholly coincident
coplanar closed tetrahedral shells and a nested no-contact inner
shell refuse any invented transverse Boolean joining/retirement.
This is strict unsupported-domain behavior, not three claims of
successful Boolean unions.

The V16 near-coplanar, exact tangent, volumetric overlapping
two-cube configurations and rotated/affine-sheared three-cube
multi-shell stress cases are run through complete original V13
discovery and terminal ORIGINAL-root preparation rather than
selective approximate-scar authority. Such an input can only
be classified as positive if the ONE terminal root independently
passes connected closure, opposite oriented edge winding,
connected vertex links and an exhaustive final nonadjacent
collision test; any other case must refuse without residual
journal or original-reader change. Separately, otherwise-positive
generic **12-contact** three-body geometry proves that a too-small
final full collision halo and too-small d2 source-seam trace budget
refuse atomically, as does the existing strict net-ID budget.

Pre-existing proven positives continue as regression controls:
M04 generic/orthogonal 12/16 3→1, M05 off-plane mirrored 24/32,
M08/M09 on-plane 3-contact 2→1, source-certified M02 unequal
density and M03 untouched buried base with preserved reverse
cavity. The M10 matrix enforces exact source geometry and does
not relax production V13/V15 thresholds to force synthetic
positives. Full GCC, Clang and MSVC warnings-as-errors build,
23 CPU suites and applicable benchmarks passed at test code
commit 79793ce (Actions 39142497184). The broader coincident-
vertex equivalence/fan decision remains separate **M16**, already
planned and now NEXT after M10, BEFORE M11 and M15; imported
nonmanifold, tunnels, multi-way arbitrary joins and scalability
remain V19. This M10 acceptance does not enable physical Merge.

**Objective.** Turn actual discovered geometric limits into explicit *bounded, reproducible* supported/refused contracts rather than a promise of arbitrary overlapping solids. Exercise complete 21-pair historical crossing/T cases and V16's near-tangent/folded tests; clarify which near-parallel, coplanar, grazing, shared-vertex, branching, nested-loop, tunnel, multi-component, strongly nonplanar and imported nonmanifold inputs are unsupported.

**Acceptance.** Deterministic matrix of positive cases and negative fail-closed cases using **complete V13 original contact discovery**, with diagnostics identifying the real blocker (input predicate, arrangement, occupancy, missing unchanged face, cavity, seam, quality, manifold, fidelity or work cap). No unsupported case produces a partial edit, new history entry, lost input, orphaned mirror pair, unbounded internal work or accepted residual self-intersection. Preserve finite edge/area/triangle-quality and sampled witness checks; do not silently relax production V15 thresholds to pass synthetic fixtures. This is a supported-domain contract, **not** the broader user-device/100k-vertex V19 program. Accidental exact/near-coincident identities are documented in [the coincidence contract](COINCIDENT_VERTEX_TOPOLOGY.md); M16 owns their independently accepted source-equivalence regression gate rather than silently broadening M10.

## V17-M16 — Generic coincident geometry and seam-equivalence acceptance

**M16 implementation and acceptance evidence.** The
[bounded identity matrix](V17_M16_COINCIDENT_IDENTITY_ACCEPTANCE.md)
links each required *successful* source-certified union and
*refused/preserved* topology to its actual original-source
regression. The positive example translates the real M06
two-tetra/THREE original-contact source geometry completely
OFF X=0, WITHOUT a mirror map: the conforming private cut
must retain two distinct stable VertexIds at EVERY one of
its exact co-located source-owned endpoints. c1/c2/c3 and
the physical d2 operation then produce one REAL 2→1
closed connected oriented full-final collision-free manifold;
the original-root composed Step succeeds, including with
the entire original contact set reversed. This is not an
X=0 special-case weld and cannot be satisfied by a safe
refusal or a convenient position-key deduplication.
The existing M05 fully reflected off-plane source graph
now additionally checks the separate cut IDs and the
postjoin reciprocal mirrored 2-component vertex fans.

Negative and intentional-nonjoin fixtures include separate
closed tetrahedra sharing exactly ONE source corner at
X=0 and off-plane, with both original IDs retained in
the unmodified/local-no-op revision and with NO permitted
vertex-only Boolean seam; four independently closed
tetrahedral tangent cones at a single exact point retain
FOUR distinct VertexIds and must not be fused into a
nonmanifold four-fan; a 1e-7 exterior gap forbids any
near-neighbor pseudo-weld. A third distinct closed shell
touching one corner of the active otherwise-positive
two-body source region cannot be absorbed: either the
proven pair alone joins while the third remains independent,
or a complete refusal results. Omitting a real original
V13 FaceId contact, forging a child owning FaceId, or
exhausting the seam matching budget likewise yields no
publishable root/commit or mirror/dirty delta.

**Accepted code-head verification:** GCC, Clang and Windows MSVC
warnings-as-errors compilation, all 23 CPU test suites and
applicable benchmarks passed at 4979b25 (PR Actions 39144616634).
The runtime pipeline is unchanged; the bounded M16 source-owned
positive/regression contract is now independently accepted.
Genuine off-plane certified 2→1 union and reversed source
order, both reciprocal off-plane reflected terminal unions,
the on/off-plane accidental tangent identities, four-fan
point refusal, near-miss, active-halo unrelated third shell,
source provenance forgery, missing FaceId and exhausted seam
budgets are distinguished as supported-success, preserved
identity or safe refusal, never interchangeable.
M11 is the next independently complete physical-stroke gate.
Full exact-HEAD CI and independent post-merge main verification
remain additional publication checks.

**Historical verification boundary:** The runtime pipeline is unchanged; this
milestone adds a strict integrated positive/refusal test contract
to the already accepted source-owned conformance, c3 occupancy,
d2 interval weld and original-root worker. Review and merge only
after GCC/Clang/MSVC warnings-as-errors, 23 CPU suites, and
applicable benchmarks pass on the exact final head. A refused
tangent or incomplete input is NOT a positive union.

The independent terminal audit tests closed opposite-wound
edge incidence, ONE connected vertex link per live stable
ID, physical full-final collision freedom for certified
joined sheets, permanent identity/protected source fidelity
and immutable original renderer/picker readers. Existing
M02/M03/M04/M05/M08/M09/M10 positive and safe-refusal
regressions remain cross-checks. This bounded acceptance
does NOT enable live physical sculpting: M11 remains the
next peer; M12–M15 and V18/V19 retain their objectives.

**Objective.** Apply the same *provenance-before-identity* rules to arbitrary **off-plane and on-plane** coincident coordinates: distinct source-owned vertices remain independent unless complete original V13 sections, opposite surviving finite intervals, c1/c2/c3 plus M03 authorization and valid full incident links justify a weld. X=0 reflection adds one fixed-point-orbit invariant but does not authorize welding unrelated coincident fans. Never deduplicate by global XYZ or nearest distance. The full design and test classification are normative in [COINCIDENT_VERTEX_TOPOLOGY.md](COINCIDENT_VERTEX_TOPOLOGY.md).

**Acceptance.** An independently source-certified proper transverse **off-X=0** seam whose distinct pre-join vertices have exactly identical XYZ produces a genuine closed, oriented, vertex-/edge-manifold connected union (and mirrored reciprocal output where applicable). The original pre-join IDs remain separate through conformance and merge only at authorized d2 closure. A distinct accidental exact-coordinate duplicate in a valid, unjoined region retains its identity, with both X=0 and off-plane controls; unsupported requested joins safely refuse rather than creating a non-manifold fan. Near-coincident and vertex-only tangent, coincident unrelated component/cavity, coplanar, incomplete FaceId provenance, 3+/4-sheet conflicting fans, forged mirror self-orbits, degenerate link, quality/collision and work-exhaustion cases refuse atomically without coordinate snapping or partial publication. Verify complete V13 contacts, reversible discovery order, entire final edge and connected vertex-link manifold, mirror/tombstone/old-reader/worker-history identity, and unchanged M04/M05/M08/M09 positives. Distinguish **supported positive union** from **safe refusal**. Keep broader imported-mesh stress for V19.

**Sequencing.** Added as an independently completable flat peer after M10, **before M11 and M15**. M01–M15 IDs and their completed objectives are unchanged; M16 is deliberately numbered out of execution order to avoid renumbering history.

## V17-M17 — Complete V13 contact graph from a physical post-sculpt proposal

**Independent acceptance (bounded):** Complete post-sculpt
V13 source discovery is now a pure read-only query returning
`Clean`, `CompleteTransverse`, or `Refused` and the EXACT
immutable candidate-root pointer only for conclusive cases.
Every live candidate VertexId seeds one bounded indexed original
contact graph, independent of selected dirty faces. Every
returned canonical original FaceId pair is independently
a source-supported finite transverse V13 segment; ambiguous,
point-tangent, wholly coplanar, exhausted-work and invalid
mirror-map requests never return a usable partial pair list.
Reflected source validation checks the full candidate involution,
including exact XYZ and genuine on-plane fixed IDs.

The tests require a real normal-driven motion starting from
two disjoint closed tetrahedra and ending with positive finite
contact, alongside a physically displaced complete three-contact
M16 off-plane source and six-contact reflected counterpart.
The positive source graph equals independently rediscovered
candidate FaceIds and survives refusal probes one below
exact section/V13 query budgets. A real local post-Inflate
edge split forces the evaluator to examine the rewritten
terminal candidate source, either returning its entire
verified graph or refusing without leakage. Old immutable
source roots/tombstones, mirror state, renderer and undo
history remain unchanged. The scoped acceptance register is
[V17_M17_PHYSICAL_CONTACT_DISCOVERY.md](V17_M17_PHYSICAL_CONTACT_DISCOVERY.md).

**Not M17:** The returned source pairs are NOT permission to
publish surgery, weld vertices, retire child faces or skip a
terminal collision check. This milestone cannot prove the
first fully committed physical Boolean or any sixteen-Step
repeated union. M18 handles captured original-root stale CAS
and post-remesh net journals; original M11 retains sixteen
physically induced positives and queue/undo/checkpoint
recovery. CI must be green GCC/Clang/MSVC before merging.

**Why independently complete:** V16's existing `evaluateSculptCollisionCandidate`
can classify and approximate-contour-heal a *different* geometry path.
The exact M06–M10 union requires ALL source FaceId V13 pairs on the
actual immutable input revision, not a small selected subset of
contacts. Physical Inflate/refine/collapse may move, split, flip,
or retire source IDs; the candidate's owner graph is therefore
not the brush's previous snapshot. Mixing the two certificates
would violate the source/identity contract of M16.

**Objective:** Provide a read-only, bounded post-sculpt candidate
contact evaluator on the complete candidate root. Preserve exact
candidate FaceIds and full original V13 contacts; distinguish
clean, finite-transverse candidate, ambiguous unsupported,
and exhausted-work classification without a partial pair subset.
Check reciprocal mirrored orbit integrity where specified.
Tests must create REAL physical post-Inflate/remesh motion across a
gap, independently recover full candidate contacts and their
original-source pair identities, then demonstrate the already
accepted exact private planner will see those same pairs. No
Boolean construction, worker CAS, renderer, history or desktop
switch is authorized at M17.

**Negative:** tangent/coplanar, near coincidence, incomplete
mirror source and bounded collision queries may not return a
partially certified contact set. A stale candidate expected root
is refused by M18's original-root CAS, not inferred by M17. Existing accepted
clean stamps continue to commit unchanged. Old immutable
reader/picking roots, mirror identities and source coordinates
remain unmodified.

## V17-M18 — First fully physical exact Boolean original-root Step

**M18 bounded implementation and acceptance (October 18, 2026).**
A separately opt-in, default-disabled **physicalExact**
collision policy in `VersionedSculptBrush::stamp` consumes
M17's complete ORIGINAL-V13 source-contact certificate on
the actual unpublished final post-remesh root. It never
uses V16 approximate scar surgery. For certified finite
contact it stages M09 exact original-source cut/c1/c2/c3/
M03/d2 before the existing stamp's SINGLE captured pre-
stamp original-root CAS. Both the physical moved/remeshed
root and the source-conforming cut and terminal join
remain PRIVATE until the one final closed manifold is
validated. Dirty owners are determined old-to-final,
including every moved, locally inserted/collapsed and
Boolean-added/retired/rewritten ID; a single history
transition receives all copied COW payload costs and
net live counts. The three mirror controllers stage the
post-refinement and post-Boolean state before CAS.
Only the difference between old historical Inflate
orbits and final live reciprocal/fixed orbits is
checkpointed, so born-and-retired intermediate IDs do
not leak into undo; unchanged original IDs retain
their same mirror partners. Refusal rolls back the
candidate local edit and all staged map changes,
history and renderer dirty state. Later physical
input remains admissible. Post-CAS unexpected faults
still belong to M14's strict atomicity program.

**Actual positive tests:** a physically deformed,
supported complete 3-contact off-X=0 two-tetra
source welds as ONE real exact 2→1 union; a different
pair of initially DISJOINT closed tetrahedra is
expanded by a genuine normal-driven stamp into a
source-V13 finite contact and a successful exact
original-root Boolean (not a nominal refusal).
A further independently tested post-Inflate local
edge SPLIT changes FaceIds and yields a supported
source-certified physical terminal union; alternative
ambiguous splits refuse with no history/dirty
leak. The complete reflected two-area stamp produces
real reciprocal mirrored unions, net map rollback
for an injected terminal-validator refusal and a
successful SECOND physical stamp using the SAME
brush, with undo/redo and durable checkpoint.
The async worker exercises one physical 4D queued
stamp end-to-end, one final renderer mailbox update,
one history undo/redo and durable checkpoint restore.
Old immutable picking/BVH roots and stable IDs remain
unchanged; strict full source and quality budget
refusals never publish intermediate revisions.
The bounded source/test acceptance and exclusions
are [V17_M18_PHYSICAL_EXACT_STEP.md](V17_M18_PHYSICAL_EXACT_STEP.md).
GCC, Clang and MSVC code-head compilation warnings-
as-errors, 23 CPU suites and benchmarks passed at
f8e5a20 (Actions 39153631172). Subsequent test
strengthening/docs still require exact-head CI.

**Not M18:** No automatic/desktop activation; the
policy defaults OFF and is mutually exclusive with
the old V16 approximate surgery setting. Arbitrary
tangent/coplanar/N-way, imported pathological
and multi-stamp interaction geometry remains
unsupported. Original M11's 16 repeated successful
alternating-tower physical unions and FIFO queue
recovery remain COMPLETELY OPEN and NEXT. M12–M15
and V18/V19 preserve their objectives.

**Why independently complete:** A real complete V13 candidate
contact graph is not yet a safe sculpt commit. The exact join
recomputes c1/c2/c3 and closure on the candidate, while the
worker must incorporate pre-union Inflate/refine/collapse
edits, changed mirrors and true net original-to-final stable
IDs into ONE captured-original-root CAS/undo/checkpoint.
The private M09 exact Step expected root equals the actual
post-remesh candidate, NOT the earlier painter root.

**Objective:** Use M17's complete candidate-pair decision to
perform ONE genuinely physically induced exact finite-source
transverse merge, including a reflected positive case whenever
the already certified geometry supports it. No approximate
V16 zipper may substitute for source-proven d2, and no private
candidate/cut/join may be separately published. Refused
candidate, budget or post-union validator failure must roll
back all speculative maps, original stamp and dirties; a
later supported physical stamp still succeeds. Verify
current/previous immutable root, net ID and FaceId tombstone,
single renderer dirty set, mirror orbits, one history/CAS,
undo/redo and durable checkpoint. Still default off.

## V17-M11 — Repeated real sculpt-driven union Steps

**Independent bounded acceptance.** Sixteen independent clean
source regions are geometrically distributed across TWO alternate
symmetric tetrahedral towers, eight height-separated regions each.
All source shells start closed and V13-nonintersecting. Unlike the
old V16 sixteen-attempt scar fixture, EVERY ONE of the sixteen
normal-driven stamps must trigger a real supported finite source
join in BOTH reflected halves. The M17 complete source graph
and M18 exact physical c1/c2/c3/M03/d2 pipeline accept one
closed and independently collision-free terminal root per input.
Each Step retires genuine source FaceIds and produces ONE
original-root CAS, one new undo history entry, real net
reciprocal mirrors and old-to-final renderer dirty owners.
Sixteen original-root pointers and their held old picker/BVH
readers are audited; undo then redo traverses all sixteen
roots exactly. The actual async worker repeats all sixteen
real 4D begin/end inputs, including one clean no-op and one
post-terminal-validator refusal rolled back before the same
valid input is accepted. All input counts and pending queue
segments drain; snapshots carry either one fully joined
terminal root or an unchanged clean/refused root. A distinct
multi-region wide physical burst exceeds a bounded V13
section-work cap and refuses entirely, after which a later
valid single-region input on the same worker passes. A
durable queued checkpoint has correct final live only
mirrored IDs/tombstones; all sixteen queued undo/redo
operations restore exact roots.

The previous V16 thirty-two same-patch cap/witness test
and old-root tests stay intact as independent scope
controls, not false claims of 32 physical Boolean
joins. M18's positive physical split-plus-exact-union and
the worker's earlier split/flip/collapse tests remain
valid. The bounded acceptance and its exclusions are
documented in
[V17_M11_REPEATED_PHYSICAL_UNIONS.md](V17_M11_REPEATED_PHYSICAL_UNIONS.md).
It does not establish unlimited welding of one already
joined surface, arbitrary intersection arrangements or
desktop on-mode behavior. M12 next; M13–M15 and V18/V19
unchanged. Final GCC/Clang/MSVC warnings-as-errors,
all 23 CPU suites, benchmarks and independent merged
main CI remain release gates.

**Objective.** Replace the explicit-only V17e2b2d3b test command as the sole demonstrated success path with a narrowly supported, genuinely collision-triggered post-Inflate/refine/collapse union decision. Preserve 4D input sampler order, independently moving sheets, local split/flip/collapse, symmetry and the unchanged default off behavior.

**Acceptance.** The actual symmetric two-tower physical fixture performs repeated **successful** classified union Steps over at least the established sixteen alternating-tower inputs, not merely safe rejection; every accepted Step is complete and globally checked in the relevant changed halo. Mix no-contact and unsupported collisions; reject failing Steps recoverably, consume every input exactly once, and continue to later good stamps. Match CPU private roots, async worker snapshots and original readers; one CAS/undo/redo/dirty owner delta per accepted Step, no in-between cut root. Retain 32-repeat same-patch and old-root controls. No desktop toggle yet.

## V17-M12 — Active original-anchored fidelity and contact hysteresis

**Objective.** Promote the existing optional seven-points-per-original-face anchored witness guard and read-only three-clean-check episode tracker into explicit, conservative **geometric decisions**. Define original reference, stable spatial patch mapping, preserved exposed versus intentionally displaced detail, budget/cumulative bound and a documented reset/hysteresis rule.

**Acceptance.** Absolute deviation is measured from nominated ORIGINAL exposed features across multiple Steps, not from successive drifting revisions; Buried and Outside material is allowed to disappear. Conclusive evidence (not uncertain/budget-exhausted checks) alone permits an episode reset; hysteresis never skips full final collision/classification validation. Demonstrate accepted legitimate deformation, rejected cumulative erosion, exposed/cavity preservation, same-patch repeat, symmetric outer walls, deliberately moved-feature exclusions and budget refusal with exact rollback. No unsupported claim of a continuous two-sided Hausdorff certificate.

## V17-M13 — Aggregate work bounds and decision telemetry

**Objective.** Enforce finite limits across an entire speculative Step in addition to the completed V13 per-query, c1 winding, V15 contour, arrangement and private residual budgets: repeated ray/BVH walks, repeated component tests, normalization, triangulation, join tracing, witness comparisons, and speculative memory/partition-copy accounting. The exceptional full-mesh solid-angle fallback stays disabled for live policy.

**Acceptance.** A declared collection of global Step caps limits counted operations, affected faces/vertices/partitions, candidate contacts, winding queries, trace/arrangement predicates and memory-proxy work; exact-limit success and one-under-limit refusals prove no partial authorization. Status distinguishes clean accepted, successful union, budget refusal, uncertain contact, unsupported geometry, stale original root and unexpected fatal error, with counts, lifetime erosion, contact episode and measured elapsed CPU time. Do not claim strict universal real-time or RSS bounds, even with finite counters; V19 owns broader scalability measurements.

## V17-M14 — Publication-phase exception atomicity

**Objective.** Close the previously identified gap where **exceptions after the final CAS** can remain fatal. Make the final geometry, mirror-map state, stable ID/tombstone changes, render dirty partitions, picking root, history/redo, checkpoint metadata and worker counts one coherent observable Step, or enter a documented safe terminal state without silent divergence.

**Acceptance.** Fault injection and stale-race tests around validation, mirror staging, CAS, history append, snapshot mailbox, and durable checkpoint scheduling prove either (a) a rejected, unobserved Step with the exact old published state, or (b) one fully committed state that readers, renderer, undo/redo and reload observe consistently. No duplicate history transition, partial reflected orbit, leaked candidate root, skipped 4D sample or misleading success telemetry. Distinguish legitimate recoverable validation/budget refusal from unexpected exceptions; no unbounded full-mesh copy on each commit. The already accepted explicit asymmetric/reflected worker unions suffice for *standalone* positive tests.

## V17-M15 — Opt-in Merge geometry activation and targeted acceptance

**Objective.** Expose **Merge geometry = On** only for the explicitly proven V17 supported-domain pipeline, with the prior default behavior remaining **Off**. Treat the feature switch as an independent product/publication gate, not as a by-product of passing a low-level private fixture.

**Acceptance.** Real input strokes choose a complete post-Inflate/refine/collapse Step policy, exact classification and closure, aggregate limits, active feature preservation, hysteresis and full final collision checks. Supported closed joins actually publish; unsupported/uncertain paths safely reject and drain input. Targeted Linux and Windows Vulkan desktop tests observe the on/off default, responsive picker/render roots, symmetric sculpt, dirty partitions, one-step undo/redo, crash-safe checkpoint/recovery and visible rejection diagnostics over repeated contacts. Keep broad imported-mesh, complex near-degenerate topology, 5k/20k/100k+ device benchmarks and universally fast editing **for V19**, and keep the separate Off-mode collision-constrained displacement algorithm **for V18**.

## Cross-cutting safety and unchanged V18/V19 boundaries

- **Coincident XYZ is not identity.** Apply [source-proven equivalence and manifold-link constraints](COINCIDENT_VERTEX_TOPOLOGY.md) to all axes, including accidental exact-coordinate duplicates. Reflection fixed-point orbits add constraints only; no global-coordinate weld or inferred intersection from an isolated touch. M16 is an explicit prerequisite for M11/M15.
- Classify occupancy by **positive signed winding** (inside iff w>0), preserve true ReversedBoundary cavity interfaces, verify a full unit oriented jump across child faces. Finite interior probes alone cannot prove continuous behavior; require complete c2 original-contact coverage and final geometry validation before deletion.
- Re-discover the COMPLETE bounded original V13 contact set; no hand-picked pair subset, global nearest-neighbor seam welding, forged FaceId/VertexId provenance, incomplete original incident-edge conformance, implicit arbitrary cavity deletion or relaxed quality/fidelity limits. Use the same immutable cut root through c1/c2/c3 and d2/d3 authorization.
- Preserve source geometry and original enclosed detail when classified Exposed/ReversedBoundary, and permit proven Buried/Outside deletion. Anchor cumulative witnesses to the ORIGINAL relevant revision, with explicit exemption for intentional sculpt displacement and measured bounded work; do not claim an unconditional two-sided Hausdorff guarantee.
- Preserve symmetric X=0 fixed-point topology, permanent ID and mirror involution, retained/reflected winding, COW partition pointer identity, original-root reader stability and exact undo/redo/checkpoint metadata. No intermediate cut/normalization root can become visible.
- Default brush mode stays unchanged until M15; a rejected input is consumed in FIFO order, does not publish an invalid deformation and does not stop later input. Recoverable bounded-work refusal must be distinct from unexpected fatal failure.
- Existing V16 cap/zipper arc-length ring correspondence, radius-difference/centroid-separation topology heuristics and contour quality policy stay as accepted; the exact-section Boolean join must not manufacture a gratuitous zipper/cap or silently apply a worse contour threshold to avoid a refusal.
- **V18 remains separate:** when Merge geometry is Off, constrain normal displacement to avoid NEW nonadjacent crossing with mesh-scale clearance and permit valid tangential sliding; both sheets moving and symmetry need tests. This is not exact swept CCD; document tunneling and do not clean arbitrary imported intersections.
- **V19 remains separate:** extensive near-coplanar/multicontact/tunnel/folded/imported geometries, arbitrary pre-existing imported exact-coordinate duplicate/nonmanifold pathologies, extreme local density, repeated edits, performance at 5k/20k/100k+, larger meshes when practical and hands-on Linux/Windows Vulkan acceptance, without claims of universal real time, global volumetric remesh or total RSS ceiling.
- **Non-goals unchanged:** mandatory CGAL dependency in the hot path, universal exact-predicate Boolean reconstruction, global volumetric remeshing, global per-Step BVH rebuild, unconditional two-edge-length deviation, forced genus zero, silently discarding arbitrary enclosed cavities, or losing high-density scan detail, UV/vertex-colour fidelity and later painting/import/export/large-model streaming requirements.

## Historical ID map (all original details archived verbatim)

| Historical work | Status at rebaseline | Flat milestone(s) retaining unfinished work |
| --- | --- | --- |
| V17a / V17b | Complete bounded winding/collision foundation | M13 aggregate extensions |
| V17c | Complete prepublication rejection/rollback | M14 post-CAS gap |
| V17d1 / V17d2 | Complete supported classified private/mirrored paths | M05, M06, M11, M14 |
| V17e1 / V17e2a | Complete repeat-contact probes, anchored diagnostics | M11, M12 |
| V17e2b1 / V17e2b2a / V17e2b2b1 / V17e2b2b2 | Complete transverse sections, conformance and private arrangements | M01, M02, M10 |
| V17e2b2b3a / V17e2b2b3b | Complete reflected private plan and ID staging | M05, M06 |
| V17e2b2c1 / c2 / c3 | Complete real-child occupancy, source coverage, protected removal | M01, M03, M04, M10 |
| V17e2b2d1 / d2 / d3 / d3b | Complete surgery adapter, exact join, original-root Step and explicit worker | M02, M04, M11, M14 |
| V17e2b2d4 including PR #49 and PR #50 | Qualified progress, previously open umbrella | M01–M11 |
| V17e2b2c / V17e2b2d | Superseded **open umbrella summaries** over already complete child contracts plus current gaps | M01–M10, M14 |
| V17 | Live product objective, still open | M01–M16, culminating in M15 after M16 |

The original exact identifier text, statuses, comments, test expectations and scope warnings are retained in [the verbatim task archive](V17_LEGACY_TASK_TREE.md). This table maps *ownership of remaining acceptance gaps*, not a claim that previously completed source code needs to be redone. The broad V17 objective remains unfinished until **every required flat milestone, including the new M16 coincidence gate**, has independently passed and M15's explicit activation gate is accepted. V18 and V19 have not been postponed, merged, deleted or changed.


**October 18 post-M16 flat replanning note:** The old single M11
objective remains completely intact. Its real sixteen-stamp physical
positive, alternating symmetric tower, 4D queue drain, clean/no-contact,
refusal-then-success, one original-root history per accepted union and
cross-platform CI are *not* satisfied by M17's discovery or M18's
first physically triggered positive. The sequence M17 → M18 → M11
is required because physical input discovery, its exact candidate
geometry ownership, and committed original-root transaction are
independently testable gates. No recursively nested tasks or
unbounded partially completed M11 checkbox are introduced.

## October 18, 2026 — M12 fixed-reference fidelity rebaseline

M12's original objective and acceptance at this document's existing
section are unchanged and remain OPEN, as do M13–M15, V18 and V19.
The optional seven-witness V16 guard currently uses a static candidate
FaceId list and the three-clean-check tracker is only diagnostic.
Neither establishes lifetime protected source provenance through
split/flip/collapse/d2 joins or intentional movement. The entire work
sequence is recorded in todo.md BEFORE beginning implementation.
New peer IDs are V17-M19–M23, not recursively nested M12 descendants.

| Order | Positive independent gate | Mandatory fail-closed gate |
|---|---|---|
| M19 | Immutable original FaceId bound to seven witnesses, per-original edge length, bounded unchanged-incidence audit during actual movement | Absent/rewritten source FaceId, unrelated coincident patch, invalid origin, distance work over limit |
| M20 | Proven live descendants of original protected FaceId through local remesh and exact join, preserving mirror and distinct coincident fans | Ambiguous or missing ancestry, exhausted graph work |
| M21 | Explicit intended sculpt feature scope and cavity-safe Exposed/ReversedBoundary vs Buried/Outside selection using signed-winding source proof | Geometrically inferred intent or forged/missing original scope |
| M22 | Cumulative reference-to-terminal physical exact Step audit before original CAS; accepted true intended union and first small drift; rejected accumulated second drift | Stale reference, missing lineage, one-under budget, leaked mirror/history/dirty/picker state |
| M23 | Conclusive three-clean-check episode reset, real repeated contact, refusal and FIFO recovery without skipped certification | Budget/uncertain/refused check cannot count as Clean or authorize a cached union |
| M12 integration | All original M12 acceptance including same-patch repeats, protected mirrored exterior/cavity, intentional deformation and worker snapshot/checkpoint | No universal two-sided Hausdorff, arbitrary imported topology or disabled UI bypass |

**Execution:** M19 → M20 → M21 → M22 → M23 → original M12 final
integration, then unchanged M13–M15. Every new milestone must pass
positive and refusal regressions and warnings-as-errors GCC/Clang/MSVC.
No M19-only conservative refusal can count as finished M12. The physical
exact mode and desktop merge geometry UI both retain default-off
behavior until M15. Existing M11 sixteen positive union and M16
coincident-ID safety coverage remain prerequisites.

## October 18 M20 complete flat execution rebaseline (before M24 code)

M20 as originally recorded remains unchanged and unchecked. Runtime
refinement splitting is not source-equivalent to edge flipping, protected
face collapse, or exact c3/d2 survivor classification. One global
XYZ-nearest matcher cannot safely repair these gaps. The whole sequence
is deliberately recorded *before implementation*:
**M24 runtime split proof → M25 flip/collapse proof → M26
source-exact cut/join child proof → M27 across-Step original patch
composition → existing M20 final acceptance**, then M21/M22/M23
and the original M12 integration. New IDs are flat peers, not nested
subtasks. Existing V17-M13–M15, V18/V19 obligations are unchanged.

- **M24 positive:** accepted local RegionEdit split of two adjacent
  original source faces, including reflected and on-plane fixed mirror
  orbits, produces verified before/after commit record and source-to-live
  repeated split descendants, retaining stable ID and local COW roots.
  **Refusal:** stale chain, missing/forged face/vertex proofs, incompatible
  flip/collapse, alias from another coincident source, and one-under cap.
- **M25 positive:** certified 2→2 flip patch area transfer and
  directional collapse with explicit source ownership, mirrored where
  proven. **Refusal:** mixed/incomplete source patch, protected deleted
  fragment without M21 intention, broken or unmatched reflected pair.
- **M26 positive:** existing certified ConformingCutCandidate::childSources
  and full original V13/c1/c2/c3/M03/d2 final survivor decisions produce
  exact living child source ownership including X=0 singleton orbit.
  **Refusal:** absent child, forged original, unsupported seam, missing
  cavity/exposed proof, accidental coincident unrelated fans.
- **M27 positive:** bounded composition from immutable first-root
  protected patch through multiple published Steps, seven first-original
  samples and original local-edge units to final live descendants.
  **Refusal:** stale capture, dropped source, incomparable topological
  patch, budget below exact work and coordinate-only assignment.
- **M20 completion:** all above independently green, with original
  accepted M20 objective still intact; active intended-feature and
  physical terminal decision remain separate M21/M22 gates.

M24 itself is a **read-only provenance consumer** of actual
runtime-emitted split records. It must not switch on Merge geometry,
publish a private candidate, change a V13 predicate, or claim
continuous two-sided Hausdorff fidelity.

## October 18, 2026 — M26 source-exact c1/c2/c3/M03/d2 survivor handoff

The full flat M20 sequence M24 → M25 → M26 → M27 → original M20
integration was recorded before M24. M24 and M25 remain accepted;
this M26 implementation does not change any objective or subdivide
the plan, nor authorize physicalExact in the desktop UI.

After **ALL** existing V13 contact discovery, conforming cuts,
c1/positive-signed-winding c2/c3, M03 uncut component decisions,
d2 reflected/on-plane exact welding, residual full-final collision,
protected surface, mirror and original-root journal validation has
succeeded, the PRIVATE original-root Step alone retains immutable
source evidence. Refused Steps expose none. Evidence includes the
existing sourceFace → child FaceId graph, unchanged private cut
index, complete typed Exposed/ReversedBoundary/Buried/Outside child
classification, explicit M03 protected/retired uncut original FaceIds,
finite original work settings and exact d2 source-proven vertex welds.
Only the terminal Step is publishable; intermediate evidence is not
added to repository, render, picker, history or checkpoint objects.

An independent **read-only** M26 consumer MUST reexecute complete
c1/c2/c3/M03 source classification with original finite limits,
require the same original root pointer, verify all source IDs, exact
child winding decisions, protected output XYZ, net stable ID journals,
mirrored source face orbits (including self-paired fixed X=0), and
explicit exact d2 source weld endpoints. It advances original FaceId
ownership through each *actually living* terminal child; only
source-certified wholly Buried/Outside or M03 authorized buried
uncut originals can have empty live sets. No nearest XYZ matcher,
approximate substituted sheet, partial classification, or protected
exterior/ReversedBoundary deletion is ever authorization. Additional
face evidence/descendant counts are finite, exact-limit passes and
one-under-limit refuses.

M27 later combines these source outputs with fixed original witnesses
across successive accepted Steps; M21 later defines deliberate
protected-feature movement/exception. This stage does not claim
continuous two-sided Hausdorff, universal imported mesh support,
deletion of unclassified cavities, or new desktop controls.

## October 18, 2026 — M27 accepted; M20 integrated acceptance next

M27 has been completed as the already-planned final independently
verifiable source-provenance peer, following M24–M26. The
comprehensive implementation/acceptance is in
`docs/V17_M27_ORIGINAL_PATCH_LIFETIME.md` and PR #71. One FIRST
root binds seven original witnesses and original local edge scale
to each nominated permanent source FaceId throughout successive
actual published CAS revisions. M24 split, M25 flip/protected-safe
collapse and M26 complete c1/c2/c3/M03/d2 transition evidence
advance its certified living FaceIds, with certified wholly
Buried/Outside original retirements explicitly distinguished
from missing protected Exposed/ReversedBoundary features.

The read-only lifetime state has an opaque immutable certificate of
first/terminal root identities, source owners, certified retirements,
cumulative **cross-Step** evidence cost and accepted transition
count. Finite source-local original witness auditing preflights
`7 * total live descendants` complete comparisons, exact budget
accepted and one below refused. Legitimate actual COW remesh,
real safe flip/collapse, true exact union, mirrored and on-plane
fixed identities, original reversed cavities, and independent
coincident closed sheets have positive acceptance. Forged
original witnesses, foreign source IDs, stale reference, spoofed
work history or missing classification and one-under caps refuse.

Substantive run
[39167334610](https://github.com/logiclrd/Mesher/actions/runs/39167334610)
passed warnings-as-errors GCC, Clang and MSVC, 23/23 CPU suites and
applicable benchmarks. Original V17-M20 integration remains
**unchecked**; it should review the combined M24–M27 guarantees,
not replan those passed peers. Original M21/M22/M23/M12, M13–M15,
V18/V19 objectives, publication/worker/history responsibilities,
and the desktop default-off gate all remain unchanged. M27 is a
finite one-sided evidence mechanism and does NOT certify a
continuous Hausdorff bound or authorize a cumulative active CAS
decision.

## October 18, 2026 — V17-M20 original source-provenance integration acceptance

The full flat peer sequence M24–M27 is complete, and the unchanged
**original M20 source-certified remesh patch descendants** milestone
is accepted with PR #72. Detailed original-objective coverage,
positive/refusal matrix and remaining supported-domain boundaries
are preserved in `docs/V17_M20_INTEGRATED_SOURCE_ACCEPTANCE.md`.

The previous separately green stage tests established committed
source-oriented local splits, strictly same-original or unprotected
2→2 flips, full original source collapse retirement/tombstone proofs
and conservative protected-deformation refusal, exact source-owned
c1/c2/c3/M03/d2 surviving children and legitimate wholly buried
originals, reflected/X=0 fixed orbits and unrelated identical-XYZ
sheets. M27 sealed the original-root permanent FaceIds, per-original
seven witnesses/edge scales, living child graph, signed-winding
retirement authority and accumulated source-evidence cost.

M20's new combined test goes beyond these disjoint positives:
one actual connected accepted original-root exact union is followed
by a genuine committed local edge split **on its terminal root**.
ONE lifetime still references the same first-original FaceIds before
either Step; it preserves previously certified buried original
retirements while assigning new split children only via committed
source IDs. Both publish as distinct atomic successful repository
CAS revisions, while a stale CAS, forged source assignment, missing
evidence and one-under cumulative face/distance work cap refuse.
Original picking readers remain COW stable. The substantive M20
CI run #39168754914 is green on GCC/Clang/MSVC, all 23 CPU suites
and applicable benchmarks.

This is the completion of original M20 *source provenance*, not
an approval of deliberate protected surface destruction or an active
quality guard. M21 still owns explicit intended sculpt displacement
and cavity policy, M22 full final pre-CAS cumulative fidelity
acceptance/atomic rollback, M23 episode semantics, and M12 the
remaining integrated fidelity acceptance. M13–M16, V18/V19, the
source-local finite one-sided nature of the seven-witness test,
and desktop default-disabled Merge geometry remain unchanged.

## October 18, 2026 — M21 explicit original-source intentional feature scope

The original M20 source-provenance integration is already accepted,
but *source ownership* alone did not distinguish requested sculpt
deformation from accidental destruction of nearby exposed or reversed
cavity surfaces. M21 now requires the caller to nominate the intended
**FIRST-original permanent FaceIds** BEFORE an edit. No geometric
proximity matcher, candidate-distance threshold, or coincident
triangle can infer intent. The returned private opaque declaration
binds the authentic pre-edit M27 lifetime certificate; M27 accepted
successors now seal the exact predecessor certificate and whether
the stage was actually reflected. The M21 reader accepts only a
DIRECT independently proven M24/M25/M26 successor, with the original
first-root anchors and immutable source ownership intact. Forged
counter resets, sibling branches and reanchoring do not inherit
the preceding declaration.

A reflected edit requires the original reciprocal permanent
VertexId involution and source-proven opposite-winding FaceId
partners (including legitimate X=0 singletons); intended FaceIds
must be closed under this reflection. A reflected successor refuses
an asymmetric intent declaration or incomplete partner proof.
A complete M27 7×living-descendant audit is always performed and
preflight bounded, **including the intentionally moved patch**;
only afterward are the explicitly selected original-source witnesses
exempted from the *geometric drift limit*. Unselected original
Exposed/ReversedBoundary outer and cavity walls must retain
original-edge-normalized first-original fidelity. A hidden entire
source may retire ONLY through the independently completed c3/M03
signed-winding Buried/Outside source proof, not by scoped intent.
The decision reports protected samples, authenticated buried
retirement and maximum protected original deviation without
publishing any mesh.

Cross-platform acceptance in PR #73 checks real asymmetric and
reflected X=0 local sculpt/split, real asymmetric and mirrored c3/M03
burial with preserved independent reversed cavities, two wholly
coincident original closed shells with nonaliased source scope,
and stale/forged/incomplete/one-below-budget refusals. No active
terminal worker gate or new desktop behavior is added. M22 must
consume this authenticated scope before original-root CAS and
prove cumulative first-original threshold refusal; M23 and
original M12 remain separate. The sampled one-sided fidelity
limit and default-disabled Merge geometry option are unchanged.

## October 18, 2026 — M22 flat implementation replan; M28 acceptance

M22's original objective is unchanged: **active cumulative first-original
source fidelity immediately before one complete physicalExact worker
CAS**, with deliberate M21 feature scope, protected outer/reversed
cavity surfaces, exact d2 union, all finite work caps and recoverable
atomic refusal. The actual worker performs several PRIVATE
Inflate/refine/collapse/d2 stages in one original-root Step. M21's
opaque intended feature declaration and M27's lifetime certificate
must name the PREVIOUS PUBLISHED root, never a private intermediate
stage. Accordingly the complete remaining sequence was planned as
flat independently-completable **M28 → M29 → M30 → original M22
integrated acceptance** in `todo.md` BEFORE implementation.

M28 (PR #74; `docs/V17_M28_PHYSICAL_STAMP_SOURCE_BUNDLE.md`)
accepts a complete authentic physical stamp source chain: full bounded
unchanged FaceId/VertexId/ordered triangle incidence across normal-
driven Inflate, exact moved VertexId and affected source face halo,
actual committed M24/M25 splits/flips, conservative protected-safe
collapse and optional M26 certified c1/c2/c3/M03/d2 terminal join.
An actually NO-CHANGE collapse consumes no evidence; a real collapse
requires complete source proof. All finite stage work and original
face ownership are replayed against exact predecessor COW roots,
then sealed into ONE externally valid M21 predecessor-bound successor.
First original seven source witness points/edge lengths never rebase.
Genuine two consecutive physical stamps show increasing protected
first-original drift; an actual Inflate-plus-M24 local split and
an actual physically generated source-exact union prove the full
private chain. Forged movement reports, stale roots, exhausted full
vertex/source evidence and one-under caps refuse. GCC, Clang and
MSVC all passed 23 CPU suites and applicable benchmarks.

This remains a **read-only private-stage proof**. M29 must preserve
real worker published lifetime and recoverable history/checkpoint
or fail safely on missing first-root source credentials. M30
must connect complete terminal M21 fidelity and intent to both
actual physical worker CAS paths, including queued input, atomic
rejection and later valid FIFO draining. Original M22 remains OPEN
until combined M28–M30 acceptance; M23/M12, unrelated milestones,
the current PR-only CI cancellation and disabled desktop Merge
geometry remain unchanged.

## October 18, 2026 — V17-M29 authenticated bounded lifetime history

The preplanned independent M29 gate under unchanged original M22
is accepted in PR #75. `RevisionHistory` now optionally retains
source-authenticated FIRST-original M27 lifetimes alongside each
immutable published COW root, verifying the actual direct parent
certificate, root, original source owner/witness graph and cumulative
work before altering undo/redo history. Rejected missing/forged/stale
or sibling lifetime candidates do not discard a redo branch. Undo
and redo restore the exact matched original proof. Opaque
predecessor certificates are now WEAK links: the active lifetime
holds its first ORIGINAL and current terminal source roots, but
evicted intermediate roots can truly retire without being pinned
by every newer certificate. M28 tracks reflected proof flags
while the private stage roots are still authentic and alive.

The existing checkpoint V12 binary remains supported. A protected
source-backed checkpoint writes V13 with a checksummed requirement
marker. The M27 opaque certificate may travel in a trusted in-memory
checkpoint, but **is not fabricated during binary read-back**;
a terminal-root-only file cannot prove its own prior M24/M25/M26
ancestry or original witness geometry. A protected physicalExact
recovered session without separately authenticated first-original
source evidence therefore **refuses before worker startup**.
This is deliberately fail-closed recovery, not evidence that
protected binary editing can resume from an unauthenticated file.
A future positive disk-resume transcript must be independently
verified and cannot reset the original baseline.

Acceptance tests exercise real sequential COW source Steps,
cumulative original-source work, rejected stale/forged/missing
history, undo/redo, branch replacement, bounded eviction,
legacy V12 and protected V13 checkpoint recovery, read-only
original history, and trusted in-memory handoff. Substantive
GCC/Clang/MSVC Actions #39176592637 passed all 23 CPU suites and
applicable benchmarks. See
`docs/V17_M29_AUTHENTICATED_LIFETIME_HISTORY.md`.

M30 still owns the active pre-CAS worker, mirrored collision,
FIFO, final original witness and atomic refusal integration.
The original M22, M23 and M12 remain open; desktop physicalExact
remains default-off and the PR-only CI concurrency policy remains.


## October 18, 2026 — V17-M30 flat implementation peers; M31 accepted

Source inspection after authenticated-history M29 confirmed two distinct
worker CAS paths with no active M28/M21 terminal-source audit and no
per-command original FaceId scope. Rather than iteratively subdividing
M30 while leaving an unfinished task, its entire remaining execution
sequence was recorded FIRST in `todo.md` as four flat peers:

- **M31 COMPLETE:** caller-supplied permanent first-original FaceId
  scopes on the actual queued physical Begin and explicit union Merge;
  genuine first-root witness/mirror authority; M21 original-source
  ownership/reflection-closure validation; recoverable bad-scope
  physical refusal and explicit union refusal; later valid worker FIFO.
- **M32 OPEN:** authenticate real multi-private-stage physical root and
  enforce cumulative M21 original-descendant terminal thresholds/work
  before CAS, attach M29 lifetime history and trusted checkpoint.
- **M33 OPEN:** independently certify and evaluate the queued explicit
  M26 exact union, original-parent intent and history before CAS.
- **M34 OPEN:** integrated mirrored 4D FIFO, protected cavity, cumulative
  two-step refusal, one-under budgets, snapshots/undo/redo/checkpoint
  and all sixteen M11 positive unions under both active paths.

The **original M30 and original M22 acceptance gates remain OPEN**:
M31 does NOT turn a valid input source nomination into proof that final
post-d2 terminal geometry meets a first-original fidelity threshold,
and production history does not yet attach the complete M28 successor.
M23, original M12, M13–M15, V18/V19 also remain open. No desktop Merge
geometry opt-in was enabled.

M31 was accepted in PR #76 after [Actions #39179461146]
(https://github.com/logiclrd/Mesher/actions/runs/39179461146)
passed the full Linux GCC/Clang and Windows MSVC warnings-as-errors
jobs, 23 CPU tests and applicable benchmarks. See
`docs/V17_M31_ORIGINAL_INTENT_TRANSPORT.md`.


## October 18, 2026 — V17-M32 authenticated physicalExact full-terminal gate accepted

**Accepted PR #77; substantive [Actions #39182191436](https://github.com/logiclrd/Mesher/actions/runs/39182191436):** full GCC, Clang, MSVC warnings-as-errors and 23 CPU suites plus applicable benchmarks passed, including all sixteen genuinely positive repeated physical mirrored M11 unions and a new first-root cumulative pre-CAS policy regression.

At a **fresh opt-in physicalExact import**, the immutable first-original FaceId/seven-witness/edge-scale M27 lifetime now exists *before* M29 `RevisionHistory` is initialized. Every genuine moving physical stamp verifies the full M28 original-root Inflate→M24/M25 refine→protected collapse→optional physical c1/c2/c3/M03/d2 source join privately. Its ONE external direct predecessor remains the old published root and its final target is the actual post-d2 terminal revision. The private bounded full source census, evidence limits, source children/retirements and mirrors must succeed. M21 then binds the explicit caller FIRST-original scope to that old sealed lifetime and checks **all living source-owned descendant witnesses** in original per-source edge units before the one CAS. Any missing/stale/forged source credentials or complete-work/threshold refusal rolls back without changing persistent root/history/mirrors/dirty/checkpoint.

The accepted physical root carries exactly its M29 successor lifetime with one history append; undo/redo recover the true historical source proof, checkpoint requires V13 provenance, and full no-op stamps do not offer null proof to tracked history. An authenticated history refuses credential-free legacy mutation before CAS rather than throwing on history append after CAS.

Real M28-measured first and second small physical movements show a tolerance chosen strictly between their **FIRST-original** deviations: one accepted, next cumulative unintended refusal. Work 56 accepts and 55 refuses, full Inflate vertex count six accepts and five refuses; deliberate source scope can approve a real edit at zero protected tolerance but a later unscoped Step does not inherit that exemption. M18 authentic physical exact union/refinement, reflected M11 sixteen independent spatial bands, queued worker clean/refusal/recovery, dirty render owners, mirror journal/history/checkpoint and original picker permanence remain positive.

For repeated intended unions, the caller deliberately **continues** the original FaceId scope of already edited still-live patches plus newly sculpted patches. After independently M26-certified whole-source Buried/Outside retirement, previously intended but now retired original FaceIds can be excluded from the active M21 declaration. Nothing inferred from XYZ or from a hypothetical brush region; future untouched sources remain protected. A previous still-live sculpted source omitted from a later declaration again becomes protected, and its cumulative original-edge drift can refuse.

**Still open:** M33 certified explicit queued union terminal CAS (currently safely refused for tracked histories), M34 combined worker acceptance, the unchanged original M30 and M22 integration checkboxes, M23, original M12, M13–M15, V18/V19. M14 injected post-CAS exception recovery remains outside M32. Protected V13 disk provenance without an authentic first-original proof remains fail-closed; no UI Merge geometry switch is enabled. See `docs/V17_M32_PHYSICAL_TERMINAL_POLICY.md`.


## October 18, 2026 — V17-M33 protected queued source-exact terminal CAS accepted

**PR #78; substantive [Actions #39184683170](https://github.com/logiclrd/Mesher/actions/runs/39184683170) succeeded:** full Linux GCC/Clang and Windows MSVC warnings-as-errors, 23 CPU suites, and applicable benchmarks. The separately queued original-root c1/c2/c3/M03/d2 exact join now performs its authenticated **M26→M29→M21** complete first-original post-d2 terminal policy on the actual published-history predecessor **before** any speculative mirror controller mutation or original-root CAS. It carries exactly one final sealed successor into M29 undo/redo and trusted checkpoint history.

Complete original c3/M03 Buried/Outside retirement proof is distinct from geometric intent. M21 caller-nominated FaceIds are immutable first-original identities, with no nearest-XYZ substitute; a previously retired original can be omitted from its active declaration only after M26 has certified whole-source retirement. The original immutable reflected ID involution protects M21 original-face closure (including X=0 self-mirror) while the old root's live controller map authenticates M26 exact-cut mirror reconstruction. Finite raw caller input, child evidence, final full seven-witness work, and M29 append proof bounds refuse before CAS. Foreign/duplicate/half-orbit/stale references, incomplete X=0 contact sets, one-under complete M26 and witness work and source-less `physicalExact` recovered V12/V13 inputs produce no new root/history/dirty/mirror checkpoint; a refused alternate request retains the redo branch.

Genuine queued asymmetric complete unions and protected three-contact mirrored X=0 unions publish one terminal snapshot and exact M29 history, retain untouched old picker roots, mirrored singleton/reciprocal IDs, and matching undo/redo source lifetime. Legacy **untracked** explicit unions retain behavior; M32 physicalExact success and M11 sixteen real mirrored unions remain green. See `docs/V17_M33_EXPLICIT_QUEUED_ORIGINAL_POLICY.md`.

**Remaining open:** M34 combined M31–M33 true async 4D FIFO and protected physical/queued interoperation, original M30 and M22 final acceptance, M23, original M12, M13–M15, V18/V19. No M14 post-CAS fault-injection or desktop opt-in claim.


## October 18, 2026 — V17-M34 mixed worker FIFO and publication race accepted

**Accepted PR #79; [substantive Actions #39187712935](https://github.com/logiclrd/Mesher/actions/runs/39187712935) passed** GCC/Clang/MSVC, the entire existing 23 CPU test suites and applicable 100K benchmarks. M31–M33's separate authenticated physical and explicit source-exact CAS policies are now jointly exercised under one REAL FIFO worker and retained first-original certificate.

The positive begins with an explicitly intended tetra source union in a composite of real crossing tetrahedra and independently sculptable closed octahedron. A genuinely one-under queued exact command fails before CAS, then the next command for the unchanged expected root succeeds with ONE final d2 source-certified history root. After that original-source join, a real 4D physical Begin/End intentionally names only the formerly edited tetra source set and leaves octa protected. Complete M28-style private first/second original-witness calculations establish a finite original-edge threshold between two ACTUAL successive physical displacements. The first post-union 4D physical stamp is accepted; the next identical stamp is cumulatively too destructive and refuses with no history/root/dirty delta. Stale queued expected-root refusal and later valid explicitly scoped 4D progress remain FIFO-correct, without first-root rebasing. Full three-root undo/redo, terminal-only renderer/picker and protected V13 provenance-required disk checkpoint succeed. The independently protected three-contact X=0 mirrored queued union also supports the NEXT real 4D reflected physical successor with correct self-mirror and reciprocal identity, history and undo/redo.

The integrated test found an ACTUAL publication-order race: the old worker could drop a new Begin's producer reservation from an unrelated idle prior command before consuming that Begin, while history advanced to a new COW root and renderer mailbox remained stale. The fix only releases a worker-local reservation **after the worker has consumed and completed its own Begin and forced the final root publication**. The test requires the actual latest terminal COW snapshot/dirty face owners. Existing source-owner-independent coincident refusal, reversed-cavity protection, M11 sixteen positive physical mirrored joins and default-disabled Merge UI remain unchanged.

**Not completed or checked:** the ORIGINAL V17-M30 full-terminal final acceptance gate, ORIGINAL M22 joint M28–M30 acceptance, M23, original M12, M13–M15, V18/V19, and M14 injected post-CAS exceptional atomicity. M34 is a flat integration milestone, not the end of V17. See `docs/V17_M34_INTEGRATED_PROTECTED_FIFO_ACCEPTANCE.md`.


## October 18, 2026 — Original V17-M30 accepted after M31–M34 execution peers

**Accepted PR #80**, substantive [Actions #39192742674](https://github.com/logiclrd/Mesher/actions/runs/39192742674) GREEN on GCC, Clang and MSVC with 23 CPU suites and all applicable benchmarks.

Original M30's pre-CAS scope was reviewed independently of merely checking its four flat peers. On physical stamps, M28 certifies actual private Inflate/refine/collapse/optional d2 into one source-owned, final post-d2 M29 successor; on explicit queued exact unions, M26 authenticates actual original-root c1/c2/c3/M03/d2 final child and wholly classified original retirement. M21 binds caller FIRST-original FaceId intent to the precise previous lifetime and audits all seven witnesses per living certified descendant in permanent original edge units; original X=0 mirrors and independent coincident source owners cannot be substituted. One CAS and one matching M29 history entry publish only final dirty renderer/picker snapshots. M34 proves actual serialized FIFO acceptance/refusal/recovery, prior picker root permanence, current vs previous certificate, protected V13 disk checkpoint, exact undo/redo and all sixteen real M11 mirrored unions.

M30 closed the last history-boundary mismatch: **physical M32 now preflights both configured M21 proof limits and M29's immutable default source proof limits before CAS**, just as M33 queued unions already did. The actual accepted physical history credential is independently validated under M29's default even when caller source work cap is more permissive. The Windows M30 rerun additionally identified and corrected a **test-only premature FIFO fence**: physicalExact refusal also contributes to `rejectedUnions`, so an absolute expected count could predate an enqueued stale Merge. The test now waits for one new union outcome relative to its post-physical baseline and requires the exact stale reason and no root/history/renderer change. Full final substantive CI is green.

See `docs/V17_M30_ORIGINAL_TERMINAL_ACCEPTANCE.md`. **The original V17-M22, M23, original M12 and M13–M15 remain unchecked**; M14 injected post-CAS exceptional atomicity and M15 desktop UI opt-in are separate. No V18/V19 goal is auto-accepted.


## October 18, 2026 — Original V17-M22 final joint M28–M30 acceptance

**Original M22 accepted in PR #81** after independent review,
with substantive [Actions #39197216458](https://github.com/logiclrd/Mesher/actions/runs/39197216458)
green on GCC/Clang/MSVC warnings-as-errors, all 23 CPU
suites and applicable benchmarks. The original-source
anchor witnesses and edge scales are permanent; the
complete real physical Inflate/refine/collapse/optional
d2 and separate queued c1/c2/c3/M03/d2 each produce
ONE final old-published-root successor certificate with
full M21 source-descendant seven-witness work BEFORE CAS,
preflighting BOTH configured limits and M29's actual
default proof cap. M32 cumulative original-source
drift, M33 genuine queued source-exact joins, M34
true serialized mixed 4D worker/terminal renderer/
picker/dirty/history/undo/redo/checkpoint, M11 sixteen
positive mirrored unions, and the separately protected
X=0 source/fan/reflected stable IDs remain green.
M22 added an actual post-union X=0 half-original-orbit
physical refusal test that verifies exact old welded
root, original sealed source certificate, live fixed/
paired mirrors, history and dirty state are unchanged.
The explanatory M22 planning item is now prose, not a
duplicate open checkbox; ONLY the original M22 is checked.

**Still OPEN:** M23 fully classified three-Clean
episode decisions, original M12 integrated cumulative
feature/hysteresis, M13 telemetry, M14 post-CAS
injected exception guarantees, M15 default-disabled
desktop Merge mode, V18/V19. See
`docs/V17_M22_FINAL_INTEGRATED_ACCEPTANCE.md`.


## October 18–19, 2026 — V17-M23 conclusive contact-episode diagnostics accepted

**Accepted PR #82, substantive [Actions #39200600909](https://github.com/logiclrd/Mesher/actions/runs/39200600909):**
GCC, Clang, MSVC warnings-as-errors, 23 CPU suites and
applicable benchmarks GREEN. Three CONSECUTIVE fully
classified and downstream-accepted Clean decisions,
not merely three cumulative checks, terminate an
active contact episode. Unknown, source budget
exhaustion, invalid caller intent and rejected Clean
stages interrupt the quiet streak but do not invent
Clean or close an episode; independently proved
Contact starts/extends it even if a later Boolean
weld refuses. `closedEpisodes` records real closure.
V13 and V16 observations happen after the proposed
Step's complete terminal acceptance/refusal decision;
episode state never shortcuts source validation.

A real two-tower symmetrical M11 physical FIFO
makes its first genuine exact source weld, accepts
two actual separately classified exterior Clean
4D stamps, refuses a foreign original FaceId
(Unknown), then requires three more accepted Clean
stamps to close episode one. A second genuine
source-owned tower weld opens episode two. The
older V16 hysteresis test now asserts
`Clean, Clean, Unknown, Clean, Clean, Clean`
rather than retaining interrupted quiet credit;
the real M11 V13 over-budget source query produces
Unknown and next good Contact. All sixteen actual
M11 mirrored unions, original M22 terminal
source protection, history, checkpoint and FIFO
acceptance remain green. See
`docs/V17_M23_CONCLUSIVE_CONTACT_EPISODES.md`.

**Original V17-M12 and M13–M15, V18/V19 remain
OPEN**; original M12 is not implied by the M23
diagnostic gate. Default-disabled desktop
Merge geometry remains disabled; M14 post-CAS
injected fault recovery is separate.


## October 19, 2026 — V17-M35 overlapping local real physical union positives

**M35 independently accepted in PR #83**, substantive
[Actions #39204571996](https://github.com/logiclrd/Mesher/actions/runs/39204571996)
GREEN GCC/Clang/MSVC, full 23 CPU suites and benchmarks. Unlike
the distant paired-source M11 towers, two initially disjoint
closed reflected contact bands now share ORIGINAL live vertex
candidates within the same overlapping sculpt brush neighborhood.
Two separate actual normal-driven physical stamps on one
`physicalExact` source-anchored brush each pass complete
original-source V13/c1/c2/c3/M03/d2 certified Boolean join,
M21 finite post-d2 first-original descendant/edge-scale audit
and M29 immutable history/publication. Two different final
root IDs, increasing source retirements, exact mirrored IDs,
dirty renderer owners, same still-active Contact episode, old
picker/BVH readers and two-step undo/redo all pass.
The fixture does not claim the two joins use one already welded
component, or any arbitrary multi-shell Boolean. See
`docs/V17_M35_REPEATED_LOCAL_PHYSICAL_UNIONS.md`.

**M36 and original V17-M12 remain OPEN** for simultaneous
protected exposed/reversed-cavity/outer walls, intentional
physical movement, cumulative source drift, work budgets,
Unknown/Clean episodes, late accepted FIFO recovery and
independent final acceptance. M13–M15, V18–V19 are separate.

## October 19, 2026 — V17-M37 first flat M36 prerequisite accepted

**PR #84**, substantive [Actions #39208440042](https://github.com/logiclrd/Mesher/actions/runs/39208440042):
full Linux GCC/Clang/Windows MSVC warnings-as-errors, all CPU tests
and applicable benchmarks GREEN. On M35's true two-physical-union
overlapping live original brush support, third protected mirrored
source band plus genuine outward and inward-wound nested cavity
closed shells retain 40 original FaceIds wholly outside explicit
renewed first-original intended scope. Both local normal-driven
joins remain independently positive, each retiring newly certified
sources and publishing independent M29 authenticated COW history.
Signed winding verifies legitimate reversed-cavity void and +1
surrounding material. Full descendant audit, exact original source
identity, protected vertices/face incidences, reciprocal mirror
pairs, original pickable root, dirty owners and double undo/redo
remain valid. Separate exterior and inverted cavity-wall attempted
physical edits refuse atomically with NO change to exact source
certificate, COW root, mirror pair map, history or dirty state.

The proactively recorded flat next items remain **M38**, then
**M39**, then ORIGINAL M36 combined final acceptance, followed by
ORIGINAL M12 separate acceptance. M13–M15 and V18/V19 remain OPEN.
See `docs/V17_M37_SCOPED_LOCAL_CAVITY_ACCEPTANCE.md`.

## October 19, 2026 — V17-M38 protected original work and cumulative drift accepted

**PR #85**, substantive [Actions #39212090176](https://github.com/logiclrd/Mesher/actions/runs/39212090176):
full Linux GCC, Linux Clang and Windows MSVC warnings-as-errors,
23 CPU suites and applicable benchmarks GREEN. The M37 two TRUE
physically triggered same-local-area d2 source unions yield one
trusted M29 original certificate whose full post-d2 audit counts
exactly seven original samples **per living source descendant**
with immutable per-original shortest-edge units. Full exact cap
succeeds and one-under `maxDistanceTests` independently refuses
both read-only and live physical candidate pre-CAS. On the same
original source, two small real protected mirrored outer wall
motions are predicted by certified M28 stages and separately
tested through actual `physicalExact`: first succeeds at the
full witness-work cap; second, with the SAME work fitting within
cap, refuses solely for cumulative first-original protected drift.
No COW root/certificate/mirror/dirty/history/publication changes
leak from refusals. The trusted post-union checkpoint safely
starts the continuing protected source proof; V13 disk-only
restore does NOT recreate it. Exact accepted undo/redo and
independent old picker identity remain intact.

The drift target is part of the same M37 first-original multishell
source, though physically separate from the two joined local
brush footprints: M39 still owes combined real 4D episode/FIFO,
and ORIGINAL M36 and ORIGINAL M12 retain separate integration
acceptance. M13–M15, V18/V19 remain open. See
`docs/V17_M38_SAME_PATCH_WORK_AND_DRIFT_ACCEPTANCE.md`.

## October 19, 2026 — V17-M39 real overlapping-patch 4D episodes accepted

**PR #86**, substantive [Actions #39263238193](https://github.com/logiclrd/Mesher/actions/runs/39263238193)
GREEN GCC/Clang/MSVC warnings-as-errors, 23 CPU suites and applicable
benchmarks. One actual protected first-original 72-FaceId M37
multishell source, 32 explicitly authorized first-root local faces,
40 protected distant/mirrored outer/reversed-cavity owners and two
TRUE source-contact bands inside overlapping live brush support
completes REAL serialized asynchronous 4D
Contact, Clean, Clean, UNKNOWN, Clean, Clean, Clean (first episode
closes precisely now), second genuine source-certified local
physical Contact union (second episode opens), another UNKNOWN,
then valid well-scoped later Clean and final FIFO progress.
The two true Boolean unions independently retire supported source
owners; eight distinct accepted terminal M29 COW/dirty history
roots undo/redo by exact pointer, two refused inputs add none.
Each refusal preserves publisher/history and zero new dirty; durable
before/after probes independently preserve EVERY reflected
mirror pair, original retired FaceId/VertexId and V13 protected
checkpoint marker. The terminal preserves all forty non-intended
original source owners and both exact 0/+1/0 signed-winding
cavities; full V13 disk re-entry refuses forged first-source
provenance. No accidental coincident XYZ source substitution or
relaxed Boolean proof.

ORIGINAL M36 (the simultaneous M19–M23 plus M35–M39 combined
protected policy gate) and ORIGINAL M12 (separate standalone final
review) remain open, as do M13–M15 and V18/V19. The genuine local
brush overlap does not establish a same already-joined-component
three-body Boolean. See
`docs/V17_M39_OVERLAPPING_LOCAL_4D_FIFO_ACCEPTANCE.md`.

## October 19, 2026 — ORIGINAL V17-M36 final same-source combined acceptance

**PR #87**, substantive [Actions #39268160154](https://github.com/logiclrd/Mesher/actions/runs/39268160154)
GCC/Clang/MSVC warnings-as-errors, full 23 CPU suites and applicable
benchmark checks GREEN. Original M36 no longer relies on separate
M38 drift and M39 FIFO tests: ONE true 4D protected original-source
worker processes genuine first overlapping local physical union,
real permitted small protected mirrored wall drift, two Clean,
invalid original FaceId Unknown and cumulative second protected wall
erosion Unknown both without root/dirty/history pollution, THREE
fresh accepted Clean closure, SECOND real local physical Boolean
source union/recontact, another refused Unknown and a valid later
scoped physical FIFO Clean recovery. Exactly full/one-under all
source-owned descendant original seven-witness audit work on the
genuine post-d2 positive root is enforced before CAS; cumulative
second protected drift refusal is shown independent of source work
exhaustion. All protected original walls/cavity face incidences and
signed-winding 0/+1/0 topology, exact reflected IDs, previous and
terminal picker/BVH/dirty, stable V13 checkpoint owner IDs,
root-bound M29 proof and full pointer-exact undo/redo pass.

The same-patch contacts share brush-support live original IDs, but
are independent certified unions; no arbitrary connected three-body
join, continuous Hausdorff or CCD is claimed. ORIGINAL V17-M12
requires its OWN standalone criterion audit and remains UNCHECKED;
M13–M15/V18/V19 remain separately open. See
`docs/V17_M36_ORIGINAL_COMBINED_PROTECTED_ACCEPTANCE.md`.

## October 19, 2026 — ORIGINAL V17-M12 integrated final acceptance

PR #88; substantive [Actions #39272006623](https://github.com/logiclrd/Mesher/actions/runs/39272006623)
GCC/Clang/MSVC 23 CPU suites and applicable benchmarks green.
Original M19 immutable FaceId/seven original witnesses/edge scales;
M20 proven live split/flip/collapse/d2 descendants and source
retirements; M21 explicit original FaceId intent and source-protected
cavity/outer walls; M22 complete first-original cumulative
physicalExact terminal pre-CAS work and real allowed/denied small
protected drift; M23 conclusive three-fresh-Clean and Unknown episode
rules are all independently verified and composed with M35–M39.
Original M36's SINGLE protected async 4D worker history proves their
simultaneous union/drift/refusal/episode/FIFO coexistence.
The NEW standalone M12 regression independently verifies 72 fixed
FIRST original sources, 40 unscoped exterior/cavity original
FaceId/XYZ, exact/one-under full 7×living descendant work, stale
root/forged certificate rejection, mirrored source pairs, signed
0/+1/0 cavity winding and first/intermediate/final COW picking roots.

Full PR-head and separate merged-main CI gate closure of the
SINGLE M12 checkbox. M13 Step telemetry, M14 injected post-CAS
failure guarantees, M15 actual opt-in Vulkan desktop use, V18 Off
collision constraints and V19 broad robustness remain open.
No universal coincident XYZ weld, same-component arbitrary
three-body Boolean, exact continuous CCD or Hausdorff claim.
See `docs/V17_M12_ORIGINAL_FINAL_ACCEPTANCE.md`.

## October 19, 2026 — V17-M40 bounded entire-private-Step COW accounting

PR #89, substantive [Actions #39277749517](https://github.com/logiclrd/Mesher/actions/runs/39277749517):
GCC/Clang/MSVC logs show all 23 CPU suites passing, with applicable
benchmark stages and no compiler errors. Full run conclusion plus
documentation-final and merged-main CI are still explicit merge
gates. Independently scoped first peer of flat M13 plan adds checked
aggregate copied partition bytes for ALL private physical exact
Inflate/refine/collapse/V13 optional union edits or queued exact
cut/refinement/join stages, changed stable-ID references, and
ACTUAL final dirty renderer partitions. Exact boundary accepts;
three distinct ONE-under budgets refuse the genuine otherwise-valid
physically triggered mirrored source union and independent queued
exact source union without COW/source/mirror/dirty/history change.
Last-attempt per-Step diagnostics distinguish measured/early-invalid,
published/refused, direct/queued, copied COW memory-work proxy, changed
IDs, final dirty owners, observed elapsed and exact rejection reason.
One real async worker transports the queued accepted measurements.

Original M13 and flat remaining M41/M42, M14/M15, V18/V19 OPEN.
See `docs/V17_M40_PRIVATE_STEP_RESOURCE_ENVELOPE.md`.

## October 19, 2026 — V17-M41 aggregate winding/collision/arrangement implementation in review

PR #91 implements per-attempt counted actual winding,
collision and multi-section arrangement operations at their
own work sites, including original M26 repeat certification,
with separate finite pre-CAS category limits and honest
attempted vs published async diagnostics. New regressions
include actual symmetric protected M39 source physical
Boolean, authenticated independent queued Boolean, and
the original genuine two-curve three-solid arrangement
plan. Exact-measured category budgets allow; one-under
category refusals must leave COW/source/mirror/history/
renderer unchanged. All 12 operation dimensions and
noexcept overflow accounting are documented in
`docs/V17_M41_AGGREGATE_GEOMETRY_WORK.md`.

**Status is OPEN until full final PR-head GCC/Clang/MSVC and
independent merged-main tests succeed**; M42, original
M13, M14–M15, V18 and V19 remain OPEN.

## October 19, 2026 — V17-M41 accepted on true full original-root query work

**Substantive [Actions #39288853723](https://github.com/logiclrd/Mesher/actions/runs/39288853723),
PR #92: GCC, Clang, Windows MSVC warnings-as-errors, all 23 CPU suites
and applicable benchmarks GREEN.** First CI on PR #91 had only
MSVC C4456 local variable name-shadow error, corrected without
changes to geometry or budget predicates. Tests measure all 10
BVH collision and signed-winding work dimensions for real mirrored
72-source physical and independent authenticated queued complete
c1/c2/c3/M03/d2 source union; exact-category caps allow, each
one-under independently refuses with original COW, first source,
mirrors, history and renderer unaffected. The other 2 dimensions,
true multiple-curve arrangement pair tests and predicates,
are calibrated on the independent real 3-closed-solid two-curve
conforming-plan fixture, with exact and one-under proof.
All actual attempted work remains diagnosable; collision/hysteresis
and M40 COW work are protected.

Original M13 and M42, M14/M15, V18/V19 remain OPEN.
See `docs/V17_M41_AGGREGATE_GEOMETRY_WORK.md`.
Exact documentation-head and merged-main CI still required.

## October 19, 2026 — V17-M43 contour/cap-zipper real work admission

The entire original M42 objective was rebaselined at the start
to M43 actual contours/closure → M44 real join/refinement →
M45 seven-witness feature/mirror/history → M46 worker timings/
cumulative diagnostics → ORIGINAL M42 integrated acceptance →
ORIGINAL M13 independent final. Original M42/M13 untouched OPEN.

Substantive [PR #93 Actions #39296825695](https://github.com/logiclrd/Mesher/actions/runs/39296825695)
GCC, Clang and MSVC all 23 CPU suites and applicable benchmarks GREEN.
M43 instruments true seven finite contour/closure work dimensions
at their processing sites and enforces exact and one-under across
one complete original-source physical/queued candidate before CAS.
A successful genuine unequal 4/8 original-vertex two-prism
zipper tests all seven nonzero category costs (2048 rotated
arc-length correspondences); real 72-source reflected physical
union and separate queued certified exact Boolean calibrate
complete candidate work with individually one-under refusals
preserving mirrored original COW, source authority, dirty and
history. No cap weakening, source proximity alias, or synthetic
partial terminal is accepted. Evidence
`docs/V17_M43_CONTOUR_CLOSURE_WORK.md`.
Final PR-head and merged-main CI still required. M44–M46,
original M42/M13, M14/M15/V18/V19 stay OPEN.

## October 19, 2026 — V17-M44 independently accepted source seam/refinement/join work

[PR #94](https://github.com/logiclrd/Mesher/pull/94)
substantive [CI #39303521777](https://github.com/logiclrd/Mesher/actions/runs/39303521777)
GCC, Clang, Windows MSVC, all 23 CPU suites and applicable
benchmarks GREEN. Seven true private source-certified seam trace,
matched interval, weld comparison, optional refinement interval/
match, mirror-orbit and retained-face rewire categories separately
bounded before CAS. Actual supported real M39 physical reflected
and queued original source unions accept precisely their completed
per-category work and refuse each actually used ONE-under
work category, leaving source/mirror/dirty/root/history unchanged.
Genuine three-solid 12-contact UNEQUAL source fan optional refinement
tests both attempted coverage/split sweeps and independently
matched intervals; read-only exact/one-under and accepted final
single-component join. All earlier M40/M41/M43 tests preserved.

Full post-documentation PR-head and merged-main CI remain gates.
M45/M46, ORIGINAL M42, ORIGINAL M13, M14/M15/V18/V19 OPEN.
`docs/V17_M44_SEAM_TRACE_JOIN_WORK.md`.

## October 19, 2026 — V17-M45 first-original feature/history operation work

M45 is the third independent implemented peer in the precommitted
M43 → M44 → M45 → M46 → ORIGINAL M42 → ORIGINAL M13 flat sequence.
Nine independently finite checked-saturating real operation categories
now observe immutable per-original anchor identity, living owned child
proof, whole-source certified retirement, actual seven-first-original
witness × owned child triangle distance work, caller intention and
unscoped exposed/reversed-cavity protected decisions, original ID-based
mirrored-orbit proof, M29 current root and authenticated transition
rechecks across all real repeated private source verification.
A local preflight refusal cannot claim distance comparisons; generic
witness sampling and cached evidence cannot become false source work.

Genuine protected 72-original reflected normal-driven source union and
independent queued authenticated asymmetric c1/c2/c3/M03/d2 positive
accept their calibrated exact all-category bounds; each genuinely
nonzero category independently one-under refuses before root CAS
without changing source proof, mirrors, immutable COW, dirty or history.
The direct original lifetime audit is also reexecuted twice, proving
twice actual nearest-triangle work and zero phantom comparison cost on
a subsequent preflight refusal.

Substantive PR #95 [run #39318060899](https://github.com/logiclrd/Mesher/actions/runs/39318060899)
is GCC/Clang/MSVC warnings-as-errors GREEN, 23/23 CPU suites and
applicable benchmarks. Exact final PR-head and separate merged-main
CI still gate acceptance. See docs/V17_M45_ORIGINAL_FEATURE_HISTORY_WORK.md.
M46 and ORIGINAL M42/M13, M14, M15, V18/V19 remain unchecked.

## October 19, 2026 — V17-M46 cumulative worker/durable diagnostic semantics

M46 implements the final peer in the preplanned flat prerequisite
series M43 → M44 → M45 → M46; it does NOT execute or check
ORIGINAL M42 or ORIGINAL M13. Observed individual stage
milliseconds now distinguish true initial physical/queued
geometry, source/mirror/intent evaluation, authenticated M29
original witness proof, pre-CAS finite budget admission, and
actual post-CAS history/publication. Attempted vs published
work is separately accumulated for all 35 independent M41–M45
source and geometric categories plus M40 copied COW payload
work proxies, changed ID references and dirty partition
owners, with saturated category totals and explicit refusal
diagnostics. Observed wall times are NOT hard run-time caps
and copy bytes are NOT measured peak RSS. Every rejected
original-root proposal exposes its attempted work without
crediting a successful publication.

The FIFO status now contains runtime-only cumulative Step
totals, undo/redo restorations, checkpoint command counts,
completed/failed/coalesced disk writer stats and actual
published coalesced dirty-owner set sizes, while keeping
original bounded immutable history statistics. Disk-reloaded
V13 roots carry only a first-original provenance obligation,
never an authenticated opaque M29 lifetime nor earlier
session work totals.

The real M39 72-first-original reflected physical union
and independent source-certified queued two-tetra Boolean
union exercise exact and one-under original-witness budgets,
real attempted/published work accounting and stale/refused
source history. Actual asynchronous worker FIFO undo/redo
and durable checkpoint save/reload probe reinforce those
runtime semantics. Substantive
[PR #96 Actions #39328566938](https://github.com/logiclrd/Mesher/actions/runs/39328566938)
passed GCC/Clang/MSVC all 23 CPU suites, warnings-as-errors,
checkpoint stress and applicable benchmarks. Exact final
head and separate merged-main CI remain required. See
docs/V17_M46_WORKER_DIAGNOSTICS.md. Original M42, original
M13, M14/M15 and V18/V19 remain unchecked.

## October 19, 2026 — ORIGINAL V17-M42 simultaneous acceptance after M43–M46

This is the ORIGINAL M42 independent integration review, NOT an
incremental M43–M46 subtask and NOT original M13. Its complete
38-dimensional admission includes all M40 private COW copying
bytes, changed stable-ID references and final renderer owner
partitions, plus every actual M41 collision/winding/arrangement,
M43 source contour/closure, M44 certified seam and join, and
M45 true first-original seven-witness, protected-feature,
mirrored-orbit and repeatedly authenticated pre-CAS M29
proof operation. Its M46 per-stage stopwatch, attempted vs
published aggregate and async FIFO history/dirty/checkpoint
diagnostics are simultaneous, not after-the-fact guesses.

On the complete genuine protected 72-original reflected 4D
source Step, 32/35 source operation dimensions actually run.
On independent queued certified asymmetric two-tetra V17 exact
Boolean, 31/35 execute. For each identical supported full
positive candidate, all 38 ceilings set exactly to actual
work still accept the one and only final-root CAS. EVERY
NONZERO separately lowered limit deterministically rejects
its own named category when every other ceiling is held
at exact work: no source relabeling, mirror substitution,
global XYZ weld, dirty owner, history, picker, or source
original root changes. Never fabricate a missing optional
category's operation count; M41 multi-curve arrangement,
M43 unequal closure, and M44 conditional seam subdivision
remain covered by independently accepted positive fixtures.

Actual worker FIFO 4D source sequencing proves original
Contact → two Clean source-certified protected history
entries → false-intent Unknown that interrupts quiet run,
with finite simultaneous work caps and honest attempted/
published telemetry. Real undo/redo source history, stale
queued expected root and V13 checkpoint fail-closed
nonrecovery of unverified M29 credentials are also tested.
M46 stopwatch observations are NOT deterministic upper
bounds; copied COW bytes are NOT peak process RSS.

Substantive PR #97
[Actions #39332363112](https://github.com/logiclrd/Mesher/actions/runs/39332363112)
was GCC/Clang/MSVC green for all 23 CPU suites and
checkpoint stress plus applicable benchmarks. Complete
final PR-head and separately merged-main CI remain
mandatory. See `docs/V17_M42_ORIGINAL_INTEGRATED_ACCEPTANCE.md`.
The **ORIGINAL M13** independent full Step resource/telemetry
acceptance, M14 fault-injected post-CAS, M15 desktop opt-in
default-off and V18/V19 remain OPEN.

## October 19, 2026 — ORIGINAL V17-M13 full Step resource and diagnostic integration

Original M13 is **not** a duplicate renaming of the original
M42 same-transaction 38-category admission tests. It is the
independent, final whole-session acceptance of **honest
cumulative diagnostic accounting** across M40/M41/M42 in
the presence of mixed genuine published/refused original-root
operations, real worker history and checkpoint transitions.
New `testOriginalM13WholeStepResourceAndTelemetryReconciliation`
builds an independent complete per-Step ledger for every
one of 35 exact source geometry counters and all three M40
private COW/ID/dirty work counters. All independently
recorded attempted/published total fields, refusal types,
and actual finite monotonic time observations must agree
with that ledger—never silently reestimate work from
retained topology or call attempted copied bytes peak RSS.

Real protected M39 72-first-original 4D mirrored source
Contact → Clean → Clean publishes three original-root
COW/history Steps under one 38-dimension finite policy.
An invalid caller FIRST-original FaceId interrupts quiet
hysteresis but does not invent a private COW edit, Clean
classification, source certificate, dirty owner or history
entry. Exact source physical original-witness one-under
budget exercises expensive genuine work with zero published
cost, independent from the early invalid input.
Separate asymmetric source-certified queued exact Boolean
accepts all simultaneously measured ceilings, stale
original-root failure charges zero speculative work, and
one-under original witness accepts actual attempted but
not published costs. The real queued FIFO does accepted/
stale and undo/redo/checkpoint without duplicate COW
work. Durable V13 disk preserves source verification
REQUIRED but never authenticates an opaque M29 source
from disk or resurrects prior in-process resource telemetry.

Substantive [PR #98 Actions #39335754023](https://github.com/logiclrd/Mesher/actions/runs/39335754023)
was GREEN GCC/Clang/MSVC /W4 /WX, 23 CPU suites and
checkpoint stress/benchmarks. Final documentation-head
and separate merged-main three-compiler success remain
independent gates. See
`docs/V17_M13_ORIGINAL_INTEGRATED_ACCEPTANCE.md`.
M14 injected post-CAS exceptional consistency, M15
default-off Vulkan Merge UI, V18 and V19 remain OPEN.

## October 19, 2026 — V17-M14 original-root post-CAS exception acceptance

After M13's separately verified whole-Step COW/source-work
ledger, M14 targets the POST-CAS exceptional boundary:
root publication may succeed while mirror journals,
M29 first-original history, renderer dirty state
and checkpoint authority are not fully installed.
M14 explicitly selects the preplanned **fail-stop,
quarantined, completely reported terminal failure**
alternative rather than pretending to roll back
an already committed original-root CAS.

Opt-in test-only, normally absent fault callbacks
throw at four real post-CAS boundaries in the protected
72-FIRST-original mirrored physical M39 source Step and
in the independent first-original authenticated queued
two-tetra exact Boolean. After any such failure:
no further stamp, Boolean Merge, undo, redo,
trusted durable checkpoint or public indexed snapshot
is permitted; the async worker drops all uncertain
frames and reports terminal reason and attempted
work instead of a phantom recoverable refusal or
accepted publication. An atomic in-flight
publication gate hides the indexed root from
readers between CAS and auxiliary completion,
verified by a held-CAS two-thread read race.

The 35 real M41–M45 work counters and M40 private
COW copy proxy remain charged to attempted work,
not successful complete publication. A separate
invalid-destination asynchronous checkpoint writer
reports an actual FAILED I/O result, leaves one
already verified accepted source root/history intact,
and a subsequent valid persisted V13 checkpoint
succeeds without fabricating opaque M29 authority.
This is not a claim of process-crash-safe rollback
or peak-RSS/hard runtime limits.

Substantive [PR #99 Actions #39340898434](https://github.com/logiclrd/Mesher/actions/runs/39340898434)
passed all GCC, Clang, MSVC warnings-as-errors
23 CPU suites, checkpoint repeat stress and
applicable benchmarks. Documentation-final and
independent merged-main CI remain mandatory.
See `docs/V17_M14_POST_CAS_FAULT_ACCEPTANCE.md`.
M15 desktop default-OFF live Merge opt-in and
V18/V19 remain OPEN.

## October 19, 2026 — V17-M47, first independently accepted M15 prerequisite

Original V17-M15 remains an independent unchecked
default-off live desktop mode milestone; its flat
prerequisite sequence M47→M48→M49 was planned up
front and inserted into `todo.md` BEFORE new code.
M47 adds only bounded, source-authenticated, worker
FIFO exact contact discovery. A UI-selected immutable
root and separately explicit FIRST-original FaceId
nominations are mandatory. A current-root identity
check and initial-import/fully sealed in-memory source
face evidence check precede finite full-root contact
assessment. All `SculptSourceContactLimits` counters
are capped to their default finite ceilings; the
complete graph must be unambiguous and transverse
before existing fully guarded queued M03/d2 source
Boolean is invoked. No stale root rebase, anonymous
coincident XYZ root substitution, whole-mesh
automatic original FaceId nomination, partial
intersection graph, or Vulkan-thread source scan.

A positive asymmetric and distinct authenticated
reciprocal reflected source creates one original-root
COW CAS, history and dirty renderer mailbox. Genuine
stale, legacy OFF, invalid/absent first-original
nomination, query-limit exhaustion, clean source and
untrusted V13 disk source refuse without any
new mutable history or picker frame. Exactly the
same worker can follow budget refusal with later
accepted work, and undo/redo restores the initial
original-root picker without recomputing Boolean.
The explicit in-memory trusted checkpoint source
constructor matches all face IDs to its sealed
original anchors and refuses unverified or advanced
lineages. A mirror map specific to a two-tetra
source is not incorrectly guessed by a symmetric
sphere indexing convention.

Substantive [PR #100 Actions #39345076059](https://github.com/logiclrd/Mesher/actions/runs/39345076059)
passed Linux GCC, Linux Clang and Windows MSVC
full Vulkan compilation /W4 /WX, 23 CPU suites,
repeated checkpoint/worker-stress and applicable
benchmarks. Documentation-final exact PR-head
and independent merged-main CI are required
before the M47 merged-complete report.
**M48, M49, ORIGINAL M15, V18, V19 OPEN.**
See `docs/V17_M47_BOUNDED_LIVE_MERGE_DISCOVERY.md`.

## October 19, 2026 — V17-M48 protected desktop authoring mode (default OFF)

The existing flat M47→M48→M49→original M15 sequence
remains intact. After separately merging M47 bounded
and first-original-authenticated worker discovery,
M48 exposes a USER-CONFIRMED Vulkan input route only.

Normal launch remains legacy Inflate (no source
credential upgrade); `--merge-geometry` explicitly
arms an immutable first-original protected M29
worker only for fresh interactive sessions and
refuses unverified disk/V13 recovery. The visible
UI mode starts **OFF** even when armed. M toggles
it, LMB in ON selects up to 32 distinct **stable**
indexed FaceIds on the exact immutable displayed
revision instead of stamping, Backspace clears,
Enter explicitly submits selected root+FaceIds
to M47's bounded FIFO discovery. New renderer
revision, mode OFF and explicit clear discard
selection; stale worker root checks still refuse.

`RegionTopologyIndex::firstRayFaceHit` obtains
the stable ID and XYZ point from the same
path-shared BVH as legacy position picking,
without global XYZ heuristics or full-mesh
coordinate matching. Typed vs position ray
regressions are in the original 23 CPU suites.
GPU HUD, window title and console report
visible ON/OFF/DISABLED state, selected count,
real outstanding pending count, accepted and
refused outcomes and M14 fatal terminal cause.
Worker still owns any contact traversal,
M29 original proof, COW and history publication;
no Vulkan callbacks from background threads.

The default unarmed worker is unchanged,
while Inflate within an explicitly armed but
UI-OFF protected session uses the opted-in
`physicalExact` source constraints. A real
rendered Linux/Windows GUI verification is
not implied by headless CI. M49 and ORIGINAL
M15 stay unchecked, as do V18/V19.
See `docs/V17_M48_DESKTOP_OPT_IN_ACCEPTANCE.md`.

## October 19, 2026 — V17-M49 targeted desktop input/worker/compiler integration

The third flat prerequisite M49 after M47/M48 tests
the *actual* `DesktopMergeInput` and
`DesktopMergeKeyEdges` that the Vulkan viewport uses.
It proves default OFF both for ordinary legacy
and explicitly authorized protected launch,
M/Enter/Backspace rising-edge handling, bounded
root-bound source FaceId selection, no-hit,
empty, repeated or >32 selections, interrupted,
stale and changed-root behavior. UI Enter sends
the exact indexed root and strictly nominated
first-original IDs through authenticated bounded
M47 `DiscoverMerge`. Genuine asymmetric and
explicitly sealed reciprocally mirrored source
intersections yield ONE authentic COW CAS,
bounded history, dirty-owner renderer frame
and stable picking. Forged first-original source
refuses with no renderer/history mutation;
undo/redo do not rebill geometry. V13 disk
cannot become an opaque M29 source credential.

Four extra no-window `Mesher` command-line tests
verify default OFF help and early rejection of
invalid demo/recovery/noninteractive Merge
authorizations. They execute the REAL desktop
executable, not an invented test stub. The
Windows CI Vulkan SDK is intentionally stripped
of `vulkan-1.dll`: MSVC imports the actual
Vulkan library with delay-load so `--help`
and invalid argument rejection can finish
without running Vulkan. Interactive rendering
still needs the real driver/runtime and is
NOT human-validated by these headless tests.

Substantive final-code [PR #102 Actions #39365774949](https://github.com/logiclrd/Mesher/actions/runs/39365774949)
passed Linux GCC/Clang and Windows MSVC
warnings-as-errors with 27/27 CTest suites
(including four executable CLI gates),
100 repeated checkpoint history worker
tests and applicable Vulkan build/benchmark
acceptance. Exact documented PR head and
independent merged-main CI are still
additional mandatory gates. ORIGINAL M15,
V18 and V19 remain separately OPEN.
See `docs/V17_M49_DESKTOP_INTEGRATION_ACCEPTANCE.md`.

## October 20, 2026 — Original M15 reproducibility gap and flat M50–M52 sequence

After accepted M47/M48/M49 actual controller/CLI/worker gates, original
M15 remains **open**. The executable previously launched its experimental
protected desktop worker only on a freshly generated sphere; positive
bounded source-certified original Merge regression geometry existed only
inside CPU test functions. Thus the actual binary lacked an independently
repeatable positive desktop start case. Before implementation, the complete
remaining flat plan was recorded in `todo.md`: M50 reproducible asymmetric
test source → M51 reflected original and fully explicit occluded-face
nomination → M52 genuinely observed Linux/Windows Vulkan acceptance →
unchanged original M15 integrated review. All are separate from V18/V19.

M50 introduces `--merge-test-tetra` only alongside both explicit
`--merge-geometry` and `--asymmetric`. The same eight-source-vertex,
eight-original-face asymmetric two-tetra model is now shared by
`src/main.cpp` and the actual M47/M49 authenticated live worker tests.
Fixture construction neither opts the GUI into ON, selects first-original
FaceIds, nor requests any surgery; it simply makes already tested complete
transverse source geometry reproducible in an interactive executable.
Two new executable CLI argument refusal cases preserve pre-GPU validation;
an independent CPU source-positive test checks the protected fresh-root,
one admitted worker CAS/history and final immutable dirty root. With viewport
enabled, the complete CTest set is 29 (23 CPU plus 6 headless CLI).

**Limit:** no Linux/Windows live Vulkan drawing was observed by these
code/CPU/CLI checks, no automatic blanket original-source nomination was
introduced, and hidden tetra faces remain an explicit M51 UI nomination
problem. M50 source/acceptance details are in
`docs/V17_M50_DESKTOP_ASYMMETRIC_FIXTURE.md`. M50 must not be checked
before final-head and merged-main GCC/Clang/MSVC CI are verified;
original M15 and V17 remain unchecked regardless.


## October 20, 2026 — V17-M51 reflected first-original fixture and deliberate occluded FaceId nomination

After M50 established the real GUI startup from the same source-positive
asymmetric two-tetra geometry as M47/M49, M51 supplies a separately
reciprocal reflected fixture (16 independently owned original vertices,
16 wound faces, eight actual source-proven mirrored vertex pairs) and
deliberate ON-mode **A** keyboard nomination of all original fixture
FaceIds, including occluded triangles. The worker receives the sealed
fresh in-memory M29 first original with the exact partition-imported
reciprocal vertex map; it **never** treats a sphere mirror order, XYZ
proximity, untrusted V13 disk snapshot, or post-sculpt descendant as
a new first-original certificate.

A is one **rising-edge** GUI input, ON and idle only, authenticated to the
exact initially displayed fixture indexed root. It nominates only the
fixed eight asymmetric / sixteen reflected source FaceIds supplied
by the named startup, no inferred all-mesh root scan or phantom
automatic surgery. The shared `DesktopMergeInput` validates EVERY
nonzero living nominated face, uniqueness and <=32 final selections,
and rejects stale, busy, foreign, malformed or over-budget requests
*before mutating* its existing selection. Ordinary generated spheres
cannot invoke this fixture-only list. One explicit Enter still requests
the M47 bounded complete original contact source proof and fully
pre-CAS M21/M29/M40–M46 exact union. Default legacy OFF, ordinary
left-click picking, one-step history and source fail-stop are unchanged.

Actual CPU tests cover asymmetric AND reflected A/Enter positive
live worker cases with exact immutable dirty frame/history, old root,
one-Step undo/redo and explicit refusal/key hold. Three more real
headless executable CLI refusal gates bring viewport-enabled CTest
to 32. All compiler checks and remaining human-rendered desktop
limitations are recorded in
`docs/V17_M51_REFLECTED_DESKTOP_AND_NOMINATION.md`.
Original M15, M52 and V18/V19 remain separately unaccepted until
their own observable acceptance.

**M51 fixture camera:** Reflected source groups centered near X=±5
would be off-screen under the ordinary unit-sphere camera. Named
developer fixtures alone set `ViewportInteraction::cameraScale`
to 2 (asymmetric) or 4 (reflected), and the actual Vulkan
`makeConstants` camera and both CPU immutable indexed ray-picking
functions scale their eye origins together. The default camera
remains scale 1; this is display framing only, with no geometry,
FaceId, proof, worker, operation-limit or M15 observed-GPU permission
change.
