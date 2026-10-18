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

Each active milestone has exactly one flat ID, **V17-M01 through V17-M16**. No lettered, numbered or recursive child IDs are permitted. New M16 is an append-only peer scheduled **after M10 and before M11**: accepted M01–M15 labels and historical cross-references are not renumbered. This is an execution order, not a recursive ID hierarchy. An unresolved complication becomes either a named *acceptance case inside the same milestone* or a **new peer** milestone through an explicit rebaseline of the list—not M01a, M01b1, etc.

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

**Objective.** Apply the same *provenance-before-identity* rules to arbitrary **off-plane and on-plane** coincident coordinates: distinct source-owned vertices remain independent unless complete original V13 sections, opposite surviving finite intervals, c1/c2/c3 plus M03 authorization and valid full incident links justify a weld. X=0 reflection adds one fixed-point-orbit invariant but does not authorize welding unrelated coincident fans. Never deduplicate by global XYZ or nearest distance. The full design and test classification are normative in [COINCIDENT_VERTEX_TOPOLOGY.md](COINCIDENT_VERTEX_TOPOLOGY.md).

**Acceptance.** An independently source-certified proper transverse **off-X=0** seam whose distinct pre-join vertices have exactly identical XYZ produces a genuine closed, oriented, vertex-/edge-manifold connected union (and mirrored reciprocal output where applicable). The original pre-join IDs remain separate through conformance and merge only at authorized d2 closure. A distinct accidental exact-coordinate duplicate in a valid, unjoined region retains its identity, with both X=0 and off-plane controls; unsupported requested joins safely refuse rather than creating a non-manifold fan. Near-coincident and vertex-only tangent, coincident unrelated component/cavity, coplanar, incomplete FaceId provenance, 3+/4-sheet conflicting fans, forged mirror self-orbits, degenerate link, quality/collision and work-exhaustion cases refuse atomically without coordinate snapping or partial publication. Verify complete V13 contacts, reversible discovery order, entire final edge and connected vertex-link manifold, mirror/tombstone/old-reader/worker-history identity, and unchanged M04/M05/M08/M09 positives. Distinguish **supported positive union** from **safe refusal**. Keep broader imported-mesh stress for V19.

**Sequencing.** Added as an independently completable flat peer after M10, **before M11 and M15**. M01–M15 IDs and their completed objectives are unchanged; M16 is deliberately numbered out of execution order to avoid renumbering history.

## V17-M11 — Repeated real sculpt-driven union Steps

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
