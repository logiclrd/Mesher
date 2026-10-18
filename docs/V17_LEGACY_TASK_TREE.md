# V17 original nested task record (verbatim archive)

This document preserves the entire original `todo.md` V17 task block exactly as it appeared on `main` at `28c419f6c3e3ac60ea391f878e96fb996f036f7b`, including completed milestones, superseded next-step comments, repeated open umbrella entries and technical scope/acceptance details.

**Historical record only.** New work is governed by [V17 flat milestones](V17_MILESTONES.md); do not add children to the identifiers below. V18/V19 remain in `todo.md` and were not moved here.

---

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
- [x] **V17e2b2d3 — Compose one original-root private Step and net ID journals.**
  One `prepareExactCutStep` stages BOTH private COW edits (conforming
  source-face subdivision and proven exact-section seam joining)
  against the SAME original immutable revision and returns ONLY the
  original expected root and terminal closed-union root. The two
  intermediate edits remain private; unchanged partition and picking
  nodes are path-shared. There is no expensive full-revision copy or
  traversal to "squash" the Step. Recompute full final changed-child
  residual collisions and all protected exposed/cavity face geometric
  fidelity before authorization. Reconcile NET born/alive added,
  original retired and original rewritten VertexIds/FaceIds; exclude
  born-then-retired intermediates from the published history journal
  while preserving tombstones/never-reused allocator watermarks.
  Validate old and new reciprocal reflected stable-ID orbits,
  surviving mirrored child faces, paired retirement and both added
  and removed MirrorPair history deltas. Track sorted local dirty
  partition owners and conservative cumulative two-edit COW bytes
  for ONE history entry, with original-root single compare-and-swap
  and atomic stale-root CAS refusal. Positive regressions: single
  connected union, mirrored pair of unions, unchanged old-root
  readers and untouched COW partitions, count/ID journals, single
  undo/redo, reflected redo/undo mirror deltas, checkpoint restore
  with exact mirror/tombstone state, and partial-contact/work-budget
  refusal with NO returned root. No UI, worker, automatic publication
  or partial checkpoint/history write.
- [x] **V17e2b2d3b — Worker FIFO, one-CAS union and mirrored history choreography.**
  An EXPLICIT-only background `AsyncSculptWorker::mergeExactContacts`
  request carries its complete original V13 contact list, immutable
  expected root and bounded Step settings together through the input
  queue, refusing active strokes and stale snapshots instead of
  reinterpreting old geometry. Recompute the full private
  original-root union using the d3 composer, then apply the existing
  V17c validator to the final cut/weld candidate. Preflight all net
  added/removed reflected ID pairs against the three synchronized
  sculpt mirror maps. Stage refiner and collapser pairs under
  reversible journal transactions; roll back on any pre-CAS failure.
  Publish ONLY the validated final root by one original-root CAS,
  synchronize Inflate mirrors and append exactly ONE history entry
  using d3 net dirty owner partitions, counts and mirror deltas.
  Existing undo/redo/checkpoint and coalesced renderer snapshots
  receive the SAME immutable union (no intermediate subdivision).
  Separate completed/rejected union telemetry does not affect stamp
  count. Full async regressions cover closed asymmetric and reflected
  tetrahedral unions, captured-root staleness, one undo/redo,
  mirror-safe durable recovery, budget refusal and subsequent input
  draining. UI Merge geometry stays OFF. Exceptional failure after
  CAS remains fatal as in the existing stamped publication path.
  Repeated sculpt/union interaction acceptance is a later V17 gate.
- [ ] **V17e2b2d4 — Generalize supported transverse cut seam topology.**
  Exercise multiple section curves through one original face, crossing
  and T-junction subdivision chains in COMPLETE closed-shell unions,
  uneven densities, mirrored section intersections and X=0 seam orbits.
  Prove mesh-fidelity bounds and fan validity or fail closed; never
  reinterpret unrelated coincident sheets as contact partners.
  **Qualified progress (PR #49):** bounded two-bridge scaffolding for
  detached SIMPLE closed interior section loops, retaining every real
  V13 constraint. A complete eight-contact, three-closed-tetrahedron
  fixture with two separate loops in ONE source face now yields a
  certified 3→1 manifold union using existing c1/c2/c3/d2/d3 gates.
  Private original-root and queued worker tests exercise safe original
  readers, stale root refusal, one history Step, undo/redo and durable
  tombstones. The distinct three-body crossing/T-junction COMPLETE
  graphs are now queried and probed end-to-end; unsupported topology
  continues to refuse with no final Step. The separate 1-above/3-below
  protrusion fixture also identified the still-open case of buried
  original faces never touched by a transverse cut: do NOT retire them
  without complete independent positive-winding authorization.
  **Further qualified progress (V17e2b2d4b):** detached interior
  V13 section graphs with crossing/degree-three/four junctions now
  attempt TWO bounded, noncrossing visible scaffold chords. Original
  area/orientation, c2 child-edge coverage, c1 two-sided positive
  winding and c3 cavity-safe removal are independently proven for
  complete, fully transverse 12-pair generic and 16-pair dyadic
  orthogonal THREE-tetrahedron arrangements, not just selected pairs.
  The d2 join can trace disjoint SURVIVING boundary subintervals rather
  than requiring buried portions of every original section to remain
  exposed, but still requires exact paired section endpoints, matching
  subdivision, opposite directions and one boundary owner. Both triple
  fixtures currently refuse final seam joining because independently
  created junction interval coordinates are not exactly equal;
  refusal exposes no final root or partial weld. Canonical shared
  three-plane junction construction is the next **separate bounded**
  task; no tolerance-based nearest weld or exposed geometry movement.
  Crossings/T-junction completed union, reflected and X=0 seams,
  differing densities and automatic repeated sculpt unions remain OPEN.
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
