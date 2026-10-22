# Mesher — active development tasks

**Current authoritative roadmap (updated 2026-10-21, source baseline `1d93d313877b892093b2ee9c8b3c3c7ba98365f8`).**
Only actionable milestones are listed here. The exact prior 216-KB
`todo.md` has been preserved **byte-for-byte at its original Git blob**
in [the full historical TODO](docs/archive/TODO_FULL_THROUGH_V18_M01.md);
the large chronological V17 acceptance register remains at
[docs/V17_MILESTONES.md](docs/V17_MILESTONES.md). Nothing was deleted from
the historical architecture, acceptance criteria, decisions or regressions.
See [docs/INDEX.md](docs/INDEX.md) for the current-vs-historical guide.

## Nonnegotiable engineering rules

- Finish each milestone as an independently testable, separately mergeable
  change with explicit positive and refusal cases. When a planned item proves
  too broad, **plan the entire remaining flat peer sequence in this TODO
  before implementing its first part**. Do not partially mark an item done,
  or grow nested task IDs.
- Immutable `MeshRevision` and associated indexed roots: copy only edited
  bounded partitions, share untouched partition pointers, preserve old
  reader/picker references, permanently unique VertexId/FaceId and
  tombstones, one accepted final-root CAS/undoable history transition.
  No intermediate edit or unsafe failed Step may leak to render or history.
- Preserve ordered asynchronous 4D input Begin/Update/End,
  finite worker CPU work, diagnostic failure categories and safe
  continued FIFO processing. Legacy exact-topology reflection and
  original-source certificates remain valid invariants **only for the
  corresponding legacy operations while enabled**. The new general
  geometry pipeline must not require first-original proof for ordinary
  approximate repairs or spatially mirrored brush actions.
  Coincident XYZ never automatically means equal topological IDs.
- **Historical V17 Merge ON is explicit and source-proven, not a universal Boolean.**
  Ordinary unarmed sculpt remains the current default; protected
  legacy Merge starts visually OFF. Its historical accepted domain
  neither proves nor restricts the intended replacement approximate
  repair semantics. Do not weaken protected V17 while developing R01–R08.
- Default interactive curvature correction, collapse and refine are
  **ON** (M73); `--no-curvature-correction`, `--no-collapse` and
  `--no-refine` are opt-outs. Checkpoint settings persistence is
  deferred to M72. V18 must remain separately opt-in through M06.
- Gate PR and independent merged-`main` separately on full
  Vulkan-enabled Linux GCC, Linux Clang and Windows MSVC warnings-as-errors
  CTests, applicable 100K benchmarks and history/checkpoint stress;
  actual device-specific UI claims need physical testing.

## Accepted baselines (do not reopen implicitly)

| Baseline | Status / source of detailed proof |
| --- | --- |
| V1–V12 | Complete immutable local COW partitions, indices/picking, async worker and durable V12/V13 checkpoints; historical [full TODO](docs/archive/TODO_FULL_THROUGH_V18_M01.md) |
| V13–V16 | Bounded triangle contacts/winding and qualified PRIVATE topology repair; [repair design](docs/SCULPT_COLLISION_REPAIR.md) and [V16 acceptance](docs/V16_FINAL_ACCEPTANCE.md) |
| V17 finite source-certified core | Complete, original M15 and physical M52 accepted; [original M15](docs/V17_M15_ORIGINAL_INTEGRATED_ACCEPTANCE.md), [GPU evidence](docs/V17_M52_DESKTOP_OBSERVATIONS.md), [full milestone history](docs/V17_MILESTONES.md) |
| V18-M01 | Complete, read-only bounded before/after ledger, [PR #128](https://github.com/logiclrd/Mesher/pull/128), [M01 specification](docs/V18_M01_POSTSTATE_CONTACT_LEDGER.md); neither sculpt gating nor opt-in behavior active |

## Architectural rebaseline: iterative, inspectable geometry repair

**Effective 2026-10-21:** the original V17 and V18 supported-domain acceptance records remain historical evidence, **not** fulfillment of the clarified product-level Merge and Non-Merge requirements. The intended design, proof boundaries, non-goals, precise R01–R08 acceptance cases, symmetry migration and deferred Smooth brush are specified in [Geometry repair rebaseline](docs/GEOMETRY_REPAIR_REBASELINE.md). R01 begins as an opt-in observation-only diagnostic; no production-path defaults change. Preserve all V1–V16 reusable infrastructure, exact V17 as reference, and previous open V18-M07 physical acceptance truth. Do not mark V17/V18 original feature goals complete for new purposes based on old restricted tests.

- [ ] **G01 — Developer startup fixtures:** `--torus` and `--hollow-sphere` with deterministic surface sampling/relaxation, directed manifold stitching, exact geometry and CLI validation, and CPU+physical viewer acceptance; no automated topology repair implied.
- [ ] **R01 — Detect and freeze on the first relevant post-stamp triangle intersection; red incident vertex/edge overlay, private candidate inspection, finite CPU and physical Linux/Windows Vulkan acceptance.**
- [ ] **R02 — Read-only vertex-star excision preview: select vertices incident to contact triangles, include every incident face, absorb branching boundary vertices under finite bounds, and display simple directed cut loops for independent islands without editing geometry.** Implementation under [PR #137](https://github.com/logiclrd/Mesher/pull/137), stacked on R01; CPU/CI and physical Linux/Windows viewport acceptance remain separate. Do not mark accepted on R01 contact evidence alone.
- [ ] **R03 — Frozen PRIVATE excision-only snapshot with actual holes and stable boundary loops, not a normally published closed mesh.** Implemented on stacked R03 branch; require private open-boundary CPU audit and physical Linux/Windows acceptance before checking off.
- [ ] **R04 — Forced `--repair zipper`, blue new faces, multiple independently matched loop pairs, explicit refusal of unsupported cases.** Implemented on stacked R04 branch with bounded boundary/seam subdivision and a private closed-manifold audit. CPU and physical GPU acceptance remain separate gates; do not mark complete from CI alone.
- [ ] **R05 — Forced `--repair cap`, green new faces, one/multiple independent loops and explicit unsuitable-cap refusals.** Implemented on stacked R05 branch: projected quality-aware ear clips, bounded two-sided subdivision, private manifold audit. 68/68 GCC/Clang/MSVC CI passed; Linux physical sphere/torus/hollow PASS with actual green caps and `CAPPED` closed-manifold reports (2 per fixture; quality 0.243 / 0.357346 / 0.158545). Windows physical GPU / final PR integration remain separate, so formal checkbox stays open.
- [ ] **R06 — Automatic closure per independent region (cap, zipper, or redundant component elimination), ambiguous many-to-many refusals; test torus tunnel removal and hollow-shell mouth sealing with correct closed-component excision of a now-isolated redundant interior, without a cap or zipper.**
  **R06 flat peer plan (2026-10-21, committed BEFORE implementation):** R06 is
  too broad for one independently verified milestone. The following six peers
  are ordered work, not nested independently expandable subtasks. Maintain
  one all-or-nothing private closure transaction; actual Windows/Linux
  acceptance and published sculpt remain separate. **The Mesher material
  model permits NO intentionally sealed interior cavities.** A completely
  enclosed, independent internal surface is redundant when structurally
  closed and reliably proven inside material. Isolation or reversed normals
  ALONE do not establish safe deletion: distinguish retained independent
  exterior components, malformed/inverted inputs, and uncertain enclosure.
  Never turn this into unbounded automatic cleanup of unrelated imports.
- [ ] **R06-M01 — Bounded, read-only R03 survivor component and contour ownership inventory.**
  Enumerate topological face components by shared stable vertex/edge incidence,
  identify directed boundary-loop owner components from actual surviving
  FaceIds, count faces/vertices/edges/boundary loops and Euler characteristic,
  and distinguish whole closed components from still-open surfaces.
  Fail on missing/duplicated contour assignments, malformed adjacency and
  finite work exhaustion. Test sphere/torus/hollow fixtures, disjoint
  components, entire-component R03 removal (zero loops), mutated or ambiguous
  loop ownership, and source/retirement nonmutation. No closure policy/CLI.
- [ ] **R06-M02 — Bounded isolated/enclosed redundant-component proof.**
  From M01 stable-ID face components, require genuine disconnection,
  orientable closed surface and finite orientation evidence (e.g. signed
  volume), then verify spatial containment/side-of-material against other
  surviving shells under explicit work limits. A reliably enclosed,
  inward-wound cavity wall is redundant under the **global no-sealed-voids
  rule**, regardless of whether isolation is newly caused by the stamp.
  An exterior disconnected component must remain; a reversed but uncontained
  shell is NOT automatically disposable, and a wholly buried outward shell
  requires its own material evidence. Explicitly refuse uncertain occupancy,
  wrong-side deletion, ambiguous nesting, self-intersections and exhausted
  witnesses. Neither isolated IDs, negative volume, nor nested AABBs alone
  authorizes deletion. Test hollow mouth before/after, pre-existing enclosed
  shells, disjoint exterior solids and malformed inverted exterior meshes.
- [ ] **R06-M03 — Per-independent-region automatic cap/zipper decision plan.**
  Use R06-M01 owners and M02 occupancy, compatible oriented contours,
  evidence-based zipper pairing and V15-style added-area fitness as a
  heuristic only. Prefer short safe independent caps for the tight-torus
  configuration, permit zipper when truly preferable, cap unmatched isolated
  suitable loops, detect genuine near-ties and ambiguous many-to-many
  associations (including 3-vs-5); one refusal refuses whole proposal.
  Plan-only, no candidate mutation. Bounded deterministic CPU regressions.
- [ ] **R06-M04 — All-or-nothing mixed private closure and complete redundancy removal.**
  Apply R06-M03 planned caps and zippers together without cross-region
  stitching or duplicate edge splitting; retain R04/R05 quality limits,
  shared-incidence conforming splits, stable IDs/tombstones, and the global
  closed-orientable vertex-fan audit. Retire only M02-proven wholly redundant
  components, including zero-rim cases, without capping/zippering them.
  Test mixed islands, torus Euler 0-to-2 where appropriate, hollow exterior
  one component/Euler 2, and deliberate refusal leaving original untouched.
- [ ] **R06-M05 — Frozen developer `--repair auto` and diagnostics.**
  Wire M04 into R01/R02/R03 freeze, distinguish actual green caps/blue
  zippers/deleted components and refusal reasons, expose plan/quality/work
  metrics and actual private root to viewport. Never mutate async worker,
  undo/history or checkpoint; do not replace R04/R05 forced modes.
  Add CLI/overlay/real pre-CAS inspector tests, finite budget and stale refusal.
- [ ] **R06-M06 — Cross-platform and physical R06 acceptance.**
  GCC/Clang/MSVC warnings-as-errors full matrix, exact PR-head and separately
  merged-main CI, Linux and Windows Vulkan inspection for sphere/torus/hollow,
  mixed multi-region, ambiguous refusals, output Euler/components/quality,
  stable picks, renderer and no ghost geometry. Keep R06 parent unchecked
  until the end-to-end gate closes; R07 continuous publication is separate.
- [ ] **R07 — Continuous iterative approximate repair, structurally valid intermediate published results, local residual contacts and bounded refinement.**
- [ ] **R08 — Production approximate Merge integration and physical acceptance, independent brush proposal, symmetry handoff, no developer freeze modes.**

The general many-to-many loop reconstruction, severe imported defects and Smooth brush remain deferred and **are not R01–R08 prerequisites**. Spatially varying Merge-OFF contact resolution is an independently planned follow-on workstream, not conflated with the Merge repair proof milestones.

## Current work: V18, then V19

**Order:** V18-M07 is IN INTEGRATED REVIEW; M07 is the remaining
flat peer preplanned before M01 implementation. CPU/CI and actual
Linux/Windows GPU reports are separate acceptance gates. M07 and
original V18 remain unchecked pending full evidence. Do not move original V18
acceptance or introduce default-on behavior ahead of M06.
The original V18/V19 objective text below is retained verbatim
apart from dated M01 verification annotations.

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

**V18 flat peer implementation plan — 2026-10-21, before M01 work.**
Keep the **original V18 checkbox and text above unchanged**. V18 is
a new, separately opt-in *OFF-mode collision avoidance policy*, NOT a
Boolean weld. M01–M07 below are independently finishable, mergeable
peers. If one proves too large, first preplan its entire remaining flat
peer sequence rather than creating nested children or partially marking
it done. Do not alter ordinary default behavior until M06's separate
opt-in integration is accepted; do not weaken V17 source-authorized
Merge ON, original-surface proof, accepted history or worker FIFO.

- [x] **V18-M01 — Finite before/after changed-neighborhood contact ledger (read-only).**
  On exact immutable `before` and fully staged `candidate`
  post-Inflate/refine/collapse roots, compare bounded post-state
  nonadjacent face contacts against the *corresponding original
  changed neighborhood*, with six real BVH work caps on EACH query,
  stable IDs rather than XYZ correspondence, explicit new/old/uncertain
  classification, and no full-root renderer scan. Unrelated pre-existing
  contacts must not be globally cleaned/refused. Ambiguous or changing
  pre-existing contact and topology turnover conservatively yield
  uncertainty rather than a forged clearance proof. Test zero change,
  disjoint two sheets, one moving sheet, simultaneous opposing motion,
  pre-existing unrelated bad sheets, adjacency/tangency, budget one-under,
  same-root immutable pointer retention and actual work counters.
  **No deformation or publication behavior change in M01.**
  Implemented read-only `include/mesher/offmode_collision.hpp`,
  `src/offmode_collision.cpp`, and
  `tests/offmode_collision_tests.cpp` with the true pre-/post-root
  `triangleIntersections` BVH, finite six-budget accounting,
  complete supported status/refusal classes, stable-identity
  grandfather check and candidate moved-sheet tests. Actual
  worker admission/clearance/edit limiting is deliberately deferred
  to M02–M05; ordinary sculpting remains unchanged. See
  `docs/V18_M01_POSTSTATE_CONTACT_LEDGER.md`. PR #128 (`1d93d313`)
  passed PR-head and independently merged-main GCC/Clang/Windows Vulkan
  CI (#39551048562).
- [x] **V18-M02 — Opt-in private final-candidate admission/refusal before one original-root CAS.**
  Route M01's complete read-only ledger into a separately enabled
  worker-side final Step gate, AFTER all normally enabled Inflate,
  refine, collapse and staging, BEFORE one immutable publication.
  New/uncertain unsafe contact refuses the complete candidate,
  with correct attempted work/rejected Step, no root/mirrors/history/
  dirty-owner change; later valid queued 4D input still drains.
  Initial conservative refusal gate need not yet restore full permitted
  motion; no V17 approximate repair and no exact ON-mode weld.
  Implemented `SculptCollisionSettings::offModeConstraints` as an
  explicitly enabled, non-UI policy incompatible with V17
  `physicalExact` or `collision.enabled`. The real final
  candidate from Inflate/refine/collapse and combined dirty IDs
  is checked using M01 immutable before/candidate BVHs after
  attempted COW/dirty resource measurement, BEFORE CAS.
  New/uncertain or exhausted contact cases reject with full
  pre-CAS rollback and honest attempted-vs-published telemetry;
  unrelated old unchanged contacts do not cause global cleanup.
  One direct and actual async 4D worker regression prove an induced
  source-independent crossing is refused without root/history/
  mirror/renderer publication, followed by genuine valid worker
  progress and pointer exact undo/redo. No desktop mode yet and
  no swept CCD/clearance guarantee. See
  `docs/V18_M02_PRIVATE_STEP_ADMISSION.md`; M03 is next.
  Final PR-head and independent merged-main CI are separate gates.
- [x] **V18-M03 — Local mesh-scale clearance and legal sliding classification.**
  Specify and implement finitely metered nearby independent
  triangle/edge/vertex distance or signed-separation checks tied to
  *local mesh spacing*, never brush radius; distinguish a legal
  tangent or tangential displacement from interpenetration/unsafe
  ambiguous contact, with double-precision tolerance and
  mixed-density neighborhood tests. Work exhaustion conservatively
  refuses; do not erase/repair unrelated initial contacts.
  Implemented finite `RegionTopologyIndex::nearbyTrianglePairs`
  over the persistent candidate BVH: each changed seed AABB expands
  by a fraction of its OWN shortest edge, and actual pair clearance
  requires `0.05 × min(shortestEdgeA, shortestEdgeB)` (never brush
  radius or coarse adjacent density). Double-precision vertex-to-
  triangle and edge-to-edge witnesses refuse newly too-close
  disjoint surfaces as well as M01 contact; unchanged old close
  pairs and strict stable-face rigid tangential no-approach motion
  retain conservative permitted progress. Six separate finite BVH
  caps and an independent distance-test budget refuse without
  truncation. Hooked into the explicitly enabled M02 final
  post-Inflate/refine/collapse pre-CAS gate, with real worker
  close-approach, safe recovery, root/undo and mixed-density tests.
  Default desktop and V17 Merge ON remain untouched. Numerical
  near-zero grazing without proof still refuses; no swept CCD or
  M04 normal backtracking yet. See
  `docs/V18_M03_MESH_SCALE_CLEARANCE.md`. M04 next, original
  V18 still open until M04–M07 acceptance.
- [x] **V18-M04 — Constrained normal displacement with conservative large-step policy.**
  Use local pre-/post-state geometric evidence to reduce/limit
  proposed normal motion where clearance is endangered while
  preserving separately valid tangential movement. Define a
  finite deterministic bound/backtracking policy on the PRIVATE
  candidate and rerun post-refine/collapse contact/clearance checks.
  Unsupported large stamps may refuse; do not claim continuous
  swept-volume CCD or no tunneling between samples.
  Finite worker-only policy `SculptOffModeConstraintSettings::backtrack`
  (enabled only under explicit OFF-mode constraint authorization)
  retries normal-only fractions 1, 1/2, 1/4, 1/8, 1/16,
  up to eight configurable finite trials. Every trial recomputes
  actual private Inflate/refine/collapse including mirrors and
  complete stable-ID M01/M03 clearance, then rolls back all
  speculative mirror journals before another try. Large-stamp
  motion is independently limited to half the immutable original
  shortest incident edge per moved vertex (not brush radius),
  reducing but NOT eliminating possible swept tunnelling.
  Uncertain/cost-exhausted geometry refuses; no false no-op
  success. Cumulative actual speculative copied bytes, changed
  IDs and distance work are metered under separate caps and
  charged to the ONE terminal original-root CAS/work history.
  A passing trial is rerun through the complete original
  stamp/admission path; it cannot publish intermediate geometry.
  Dedicated direct/async CPU regressions cover smaller safe
  motion, no retry/budget refusal, large steps, mirror-neutral
  rollback, later FIFO progress and exact undo/redo. M05's
  independent symmetrical/dual-sheet integration remains
  open. See `docs/V18_M04_BOUNDED_NORMAL_BACKTRACKING.md`.
  Full exact-head and merged-main GCC/Clang/MSVC CI required.
- [x] **V18-M05 — Both-opposing-sheet and reflected-symmetry transaction proof.**
  Integrate simultaneous motion on two opposing sheets against
  the SAME complete post-state, and source-certified reciprocal
  mirror pairs including fixed X=0. Positive symmetric clearance/
  slide and deliberate close-approach refusals preserve one
  terminal CAS, permanent stable IDs, original-root picker,
  mirror journals, undo/redo, stale refusal and independent
  coincident-coordinate ownership. Bounded work only.
  Implemented a finite full-source OFF-mode mirror admission (one
  bounded session-start scan), with local per-trial and pre-CAS
  changed stable-ID halo verification of reciprocal live vertex
  involution, exact reflected XYZ, true fixed X=0 vertices, mirrored
  face-corner topology and reflected orientation, and source-pair
  retirement. No source-matching by XYZ, no original M29 protected
  Merge authority forgery, and no full mesh traversal on each edit.
  Added four simultaneous inward-moving sheets across two
  reflected islands with M04 normal backtracking and M01/M03
  whole-post-state admission, unrelated coincident independent
  sources, full-Step refusal, later fixed-seam recovery, source
  mirror checkpoint/undo/redo, explicit stale callback refusal
  and actual 4D worker FIFO tests. See
  `docs/V18_M05_MIRRORED_OPPOSING_SHEETS.md`. M06 next,
  original V18 still open, full PR/main CI required.
- [x] **V18-M06 — Explicit desktop opt-in, default unchanged, and diagnostics.**
  Add an unambiguous CLI/UI selection for V18 OFF-mode collision
  constraints (NOT the V17 protected Merge ON authorization).
  Verify ordinary startup/default flags/old checkpoints,
  invalid combinations, visible refused Steps and later resumed
  editing in real interaction; keep numeric settings/feature
  checkpoint persistence M72 as separately deferred. No
  automatic retrofit on imported bad meshes and no silent
  feature enablement on recovery.
  Desktop `--avoid-collisions` explicitly arms only the
  V18 OFF-mode `SculptCollisionSettings::offModeConstraints`;
  it is forbidden with V17 `--merge-geometry` or noninteractive
  demo/compare modes and never creates protected Boolean authority.
  Ordinary startup/default and unarmed old V12/V13 checkpoint
  recovery remain unguarded and unchanged. Recovered meshes
  run the finite guard ONLY with another explicit opt-in; no
  numeric/feature flag checkpoint persistence (M72).
  Window title persistently identifies OFF-mode avoidance.
  Separate `[collision]` lines report even refused unpublished
  Steps and later recovery, while `[sculpt]` records actual
  private retry/normal-fraction/copied-byte, refusal and
  geometry work counters. Headless tests run real desktop
  argument parsing with no Vulkan loader/GPU, and CPU tests
  prove accepted/default/recovered worker isolation, a
  refused close approach, resumed valid edits and exact
  undo/redo. `docs/V18_M06_DESKTOP_OFFMODE_OPT_IN.md`.
  M07 independently verifies the full device-visible
  Linux/Windows Vulkan integration; V18 remains open until
  M07 physical/CPU acceptance and merged-main CI.
- [ ] **V18-M07 — Original V18 integrated acceptance and reproducible GPU/CPU boundary.**
  Independently reconcile M01–M06 for initial unchanged
  geometry, legal grazing/sliding, both sheets moving, mirrored
  strokes, safe large stamps and work exhaustion, stale/undo/redo/
  checkpoint/history, pre-existing unrelated contacts and
  conservative fail-stop. Human Linux/Windows GPU acceptance
  as necessary for genuinely new visible controls. Record exact
  supported-domain limits and tunneling non-CCD caveat; close
  ORIGINAL V18 only after exact final PR-head and separately
  successful merged-main GCC/Clang/Windows full Vulkan CI.
  Leave V19 broad imported/complex geometry/performance open.
  **Evidence under review (not yet accepted):** Named desktop
  `--avoid-test-sheets` fixture requires
  `--avoid-collisions --asymmetric`, supplies two independent
  opposite-facing close triangles and a remote recovery target.
  Dedicated `v18_integrated_near_sheet_checkpoint_acceptance`
  runs actual M01–M06 source-independent backtracking/refusal,
  repeated approach to a mesh-scale limit, later remote edits,
  pointer-exact undo/redo and an edited checkpoint reloaded with
  and without V18 reauthorization. GPU-free CLI preflight checks
  prove invalid fixture and protected-mode combinations refuse.
  The full [M07 protocol](docs/V18_M07_INTEGRATED_ACCEPTANCE.md)
  requires separate physical Linux and Windows Vulkan observation
  with exact executable hashes and actual console evidence.
  **Neither M07 nor original V18 is complete** until these
  observations, exact PR-head and independently merged-main
  three-platform CI pass. Keep exact-zero-gap grazing and
  intersample tunneling caveats explicit.

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


## Deferred, nonblocking independent V17 UX work

- [ ] **V17-M66 — Optional backface culling.** Retain two-sided
  interior inspection by default. Device-visible outside/inside,
  near-pole, occlusion, picking and W/G tests and measured performance
  before enabling optional display-only culling. Original acceptance
  contract: [historical TODO](docs/archive/TODO_FULL_THROUGH_V18_M01.md).
- [ ] **V17-M70 — Switch from symmetric to asymmetric editing at runtime.**
  Do not silently invalidate mirror correspondence, history,
  sculpt worker or M29 source certificate. See historical TODO.
- [ ] **V17-M71 — Symmetric strokes on intrinsically asymmetric geometry.**
  Distinguish spatially mirrored brush activity from certified
  stable-ID reciprocal topology; never invent geometric identity.
  See historical TODO.
- [ ] **V17-M72 — Persist/restore interactive feature settings.**
  Define CLI overrides vs checkpoint policy and legacy compatibility
  without using settings as source authority. See historical TODO.

## Remaining longer-term goals

V19 robustness/performance remains separate from V18's opt-in
collision constraints. Further work includes high-density mesh
streaming, vertex-colour and UV fidelity, import/export and
painting tools, preserving brush-radius independence from editing
density. The original detailed acceptance/non-goals are retained
in [the full historical TODO](docs/archive/TODO_FULL_THROUGH_V18_M01.md).
