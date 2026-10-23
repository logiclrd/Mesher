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
- [x] **R06-M01 — Bounded, read-only R03 survivor component and contour ownership inventory.**
  Enumerate topological face components by shared stable vertex/edge incidence,
  identify directed boundary-loop owner components from actual surviving
  FaceIds, count faces/vertices/edges/boundary loops and Euler characteristic,
  and distinguish whole closed components from still-open surfaces.
  Fail on missing/duplicated contour assignments, malformed adjacency and
  finite work exhaustion. Test sphere/torus/hollow fixtures, disjoint
  components, entire-component R03 removal (zero loops), mutated or ambiguous
  loop ownership, and source/retirement nonmutation. No closure policy/CLI.
  Implemented in [PR #141](https://github.com/logiclrd/Mesher/pull/141):
  stable-ID real R03 connected components, exact oriented contour ownership,
  Euler/boundary counts, open/closed and zero-rim cases, malformed/budget
  refusals. **2026-10-22 PR-head full Linux GCC/Clang and Windows MSVC CI
  SUCCESS** ([Actions 39725703354](https://github.com/logiclrd/Mesher/actions/runs/39725703354)).
  Separate R06 integration/GPU acceptance remains under M05–M06.
- [x] **R06-M02 — Bounded isolated/enclosed redundant-component proof.**
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
  Implemented in [PR #142](https://github.com/logiclrd/Mesher/pull/142):
  read-only candidate-closed-root component evidence, translated signed
  volume, bounded per-component winding witnesses, indexed crossing checks,
  and explicit ambiguity for inverted uncontained/exterior or nested oddities.
  Never deletes geometry. **Full Linux GCC, Linux Clang and Windows MSVC
  70/70 CTest SUCCESS**, [Actions run 39726664591](https://github.com/logiclrd/Mesher/actions/runs/39726664591).
  R06-M03 closure selection completed separately in PR #143. M04 atomic
  surgery, M05 CLI and M06 GPU acceptance remain open; do not mark the
  parent R06 complete.
- [x] **R06-M03 — Per-independent-region automatic cap/zipper decision plan.**
  Use R06-M01 actual survivor and excision-patch ownership plus verified
  R01 contact-face provenance, compatible oriented contours and tested V15.1
  added-area fitness as a heuristic only. R06-M02 material evidence applies
  to the eventual **closed** candidate at R06-M04, not to R03 open cuts. Prefer short safe independent caps for the tight-torus
  configuration, permit zipper when truly preferable, cap unmatched isolated
  suitable loops, detect genuine near-ties and ambiguous many-to-many
  associations (including 3-vs-5); one refusal refuses whole proposal.
  Plan-only, no candidate mutation. Bounded deterministic CPU regressions.
  Implemented in [PR #143](https://github.com/logiclrd/Mesher/pull/143):
  linked patches only on actual nominated contact FaceIds, conservative
  V15 ring-score/arc-length DP, same-shell topology penalty, independent
  caps for tight torus, mixed zipper+cap, no untrusted proximity pairing,
  finite work and genuine near-tie refusals, whole-plan many-to-many
  (>4 linked loops) refusal, zero-loop retirement without new faces.
  **Full 71/71 CTest SUCCESS on Linux GCC, Linux Clang and Windows MSVC**
  ([Actions 39730105439](https://github.com/logiclrd/Mesher/actions/runs/39730105439)).
  M04 mixed actual geometry, M05 CLI and M06 physical GPU acceptance remain
  open; parent R06 stays unchecked.
- [x] **R06-M04 — All-or-nothing mixed private closure and complete redundancy removal.**
  Apply R06-M03 planned caps and zippers together without cross-region
  stitching or duplicate edge splitting; retain R04/R05 quality limits,
  shared-incidence conforming splits, stable IDs/tombstones, and the global
  closed-orientable vertex-fan audit. Retire only M02-proven wholly redundant
  components, including zero-rim cases, without capping/zippering them.
  Test mixed islands, torus Euler 0-to-2 where appropriate, hollow exterior
  one component/Euler 2, and deliberate refusal leaving original untouched.
  **M04 flat peer sequence planned before implementation (2026-10-22):**
  existing R04/R05 each require all open rims to be theirs; they cannot
  run sequentially with partial holes. Preserve the original M04
  all-or-nothing final acceptance contract across these peers.
- [x] **R06-M04A — Mixed private face staging and shared R04/R05 geometry primitives.**
  Factor reusable R05 oriented projected ear-disks and R04 oriented DP
  zipper strips without changing forced-mode behavior. Stage caps and
  M03-selected explicit zipper pairs simultaneously from the SAME R03
  open root, with stable IDs and one combined face-incidence ledger.
  No unsafe intermediate closed-root assertion, no worker/history/CAS.
  Test exact loop usage, mixed winding, cross-region no-stitching,
  malformed plan refusal and source immutability.
- [x] **R06-M04B — Shared conforming adaptive refinement and closed-root audit.**
  Unify old-survivor and newly inserted blue/green face splitting so
  each edge is split on both incident sides, bounded by per-kind R04/R05
  quality and scale limits. Commit ONE private root after full contour
  coverage and global closed orientable vertex-fan audit; preserve stable
  IDs/tombstones and refuse all bad-quality/over-budget mixed repairs.
  Test torus Euler 2 where supported, independent multi-island cap/zipper,
  and never permit a half-closed published/private result.
- [x] **R06-M04C — Proven enclosed-component retirement and integrated acceptance.**
  Analyze closed M04B candidate with R06-M02's bounded, reliable
  containment/orientation witness; remove ONLY fully proved redundant
  inward closed components and their isolated vertices, with preserved
  retirement tombstones, then re-audit full closed root, Euler/component
  count and source immutability. A zero-loop component deletion requires
  independent proof/authorization, not mere lack of a contour.
  Test hollow sphere outer-only Euler 2, intentionally disconnected
  exterior solids preserved, uncertain inverted/nested overlap refused,
  finite budgets and complete rollback. Record GCC/Clang/MSVC gate.
  **Implemented and CPU-accepted on [PR #144](https://github.com/logiclrd/Mesher/pull/144).**
  Shared R04/R05 primitives, one mixed cap+zipper staging ledger, explicit
  conforming two-sided edge splits and finite quality/refinement budgets,
  closed vertex-fan manifold audit before returning a private root.
  Revalidated trusted M03 plan, M02-proven cavity-only retirement, stable
  tombstones, zero-rim enclosed cavity acceptance / exterior-solid refusal,
  genuine annular torus Euler 0 -> 2 and independent local torus Euler 0.
  **Full 73/73 CTest SUCCESS on Linux GCC, Linux Clang and Windows MSVC**
  at [Actions #39734314503](https://github.com/logiclrd/Mesher/actions/runs/39734314503).
  Supplemental narrow seam regression on the finalized head confirms
  whole-operation refusal without partial publication. **Final 73/73 CTest
  SUCCESS on GCC/Clang/MSVC**, [Actions #39737112861](https://github.com/logiclrd/Mesher/actions/runs/39737112861).
  R06-M05 CLI, M06 physical GPU and eventual production publication remain
  separately open; the R06 parent is NOT complete.

- [x] **R06-M05 — Frozen developer `--repair auto` and diagnostics.**
  Wire M04 into R01/R02/R03 freeze, distinguish actual green caps/blue
  zippers/deleted components and refusal reasons, expose plan/quality/work
  metrics and actual private root to viewport. Never mutate async worker,
  undo/history or checkpoint; do not replace R04/R05 forced modes.
  Add CLI/overlay/real pre-CAS inspector tests, finite budget and stale refusal.
  **CPU accepted on [PR #145](https://github.com/logiclrd/Mesher/pull/145):**
  one-shot `--repair auto`/`--repair-auto` frozen developer mode, M03/M04
  final root or real R03 open-rim refusal, double-sided surviving GREEN cap
  and BLUE zipper overlays, separate AUTO COMPLETE/REFUSED HUD/title, genuine
  per-region/material-retirement and quality/work diagnostics. Real
  candidate zero-loop exterior deletion refused; post-stamp worker PRE-CAS
  freeze, unchanged root/history/dirty partitions tested.
  **74/74 CTests on Linux GCC, Linux Clang and Windows MSVC**,
  [Actions #39739406371](https://github.com/logiclrd/Mesher/actions/runs/39739406371).
  No actual physical Vulkan signoff or live publication is claimed;
  R06-M06 and the parent R06 remain unchecked.
- [x] **R06-M06A — Explain and exercise automatic closure decisions.**
  Physical Linux noodle collisions yielded 3/5 undesired independent caps
  and one legitimate quality refusal. Log per-region actual M03 choice and
  cause (fast V15 score vs same-survivor area penalty vs ambiguous two-cap
  fallback), trusted contact/excision association and geometric statistics.
  Log which closure kind/triangle violated M04 quality and the finite
  refinement count. Add regression proof that no speculative plan is
  published and no quality bound is relaxed.
  **Completed diagnostics in [PR #146](https://github.com/logiclrd/Mesher/pull/146):**
  exact cap-vs-zipper and V15 fallback reasons, normals/area/proximity,
  failed CAP/ZIPPER triangle IDs and quality with consumed subdivisions.
  74/74 GCC/Clang/MSVC CTests in
  [Actions #39792765074](https://github.com/logiclrd/Mesher/actions/runs/39792765074).
  Confirmed physical misclassification: savings=0.503887 rejected by
  same-surface minimum 0.55 despite opposite normals -0.998 and a
  verified single contact-linked two-patch region.
- [ ] **R06-M06B — Geometry-grounded cap/zipper decision hardening.**
  Replace silent ambiguous *contact-linked* two-rim -> independent caps with
  a bounded alternative validated against real oriented rings and direct
  R01 source contact provenance, preserving tight torus cap preference.
  Regression for equal-area ties, long torus bridge refusal, close facing
  noodle contacts and separate distant repairs; do NOT unconditionally
  zipper all directly contacting patches.
- [x] **R06-M06C1 — Planner area-independent unpaired cap admission.**
  When V15 cannot provide projected area, permit independent caps for
  a single directed loop or clearly unlinked independent loops; record
  an advisory fallback estimate only, never mistake it for proof that
  the cap can be embedded. Use supplied two six-vertex physical rim
  contours for a deterministic planner regression. Retain R01 patch
  provenance, explicit refusal on ambiguous linked many-to-many and
  zero-loop material certification.
  **CPU accepted:** both real physical six-point contour coordinate
  sequences now reach M03's unlinked cap decision; no geometry certified.
- [x] **R06-M06C2 — Local-indexed advancing front with interior-vertex contract.**
  Introduce a bounded spatial-cap result with preserved oriented rim,
  staged new vertices/triangles and a deterministic front. Try only
  after projected-ear failure; no global projection and no forced-R05
  regression. Require patch-face self/candidate collision screening,
  closed fan and strict budget refusal before private publication.
- [x] **R06-M06C3 — Dirichlet normal-guided Poisson fairing.**
  On a certified front triangulation, freeze every boundary vertex,
  derive a bounded normal/gradient field from surrounding oriented
  faces, solve sparse interior Poisson systems with residual and finite
  iteration budgets, and recheck all triangle/embedding/quality gates.
  Include planar and curved 6/18/32-vertex physical coordinates, and
  deliberately self-overlapping unsafe test fixtures. No global weld.
  **C2/C3 local CPU accepted** in draft [PR #147](https://github.com/logiclrd/Mesher/pull/147)
  ([Actions #39806840808](https://github.com/logiclrd/Mesher/actions/runs/39806840808)):
  **75/75 CTests Linux GCC, Linux Clang and Windows MSVC**. The
  recorded elongated 32-point rim produced a local self-checked front
  (initial q=0.190579), but source incident faces were NOT recorded.
  Normal-guided fairing uses a 256-sweep bounded discrete Poisson
  approximation, not the full paper's gradient-fitting solver.
  C4 remains OPEN pending real surviving-surface collision and physical
  M04 private-root acceptance; the parent M06C/R06 remains unchecked.
- [ ] **R06-M06C4a — Optional spatial-cap mesh-density refinement.**
  Following required M04B quality/edge-length refinement, spend only
  remaining finite conforming-refinement budget on spatial-mode cap
  edges longer than a tighter local target (~1.5x median rim edge).
  Split both incident faces, but preflight every prospective child
  quality (new cap >=0.10; surviving old triangle >=0.012;
  zipper >=0.10). Do NOT change frozen rim positions, relax hard
  limits, or turn a previously valid repair into a budget refusal.
  Report optional split counts and worst final cap edge/target ratios.
- [ ] **R06-M06C4c1 — Always mirror-stage spanning single-loop caps.**
  Physical 16-point R03 self-mirrored contour had one cap plan but M04
  refused `reflected conforming edge lacks two incident tracked faces`.
  A successful projected R05 triangulation may not reflect its
  diagonals, despite the source being paired. Route every symmetric
  single contour spanning X=0 through the mirror-front backend
  *before* trying projected ear clipping. Reject off-plane triangles
  crossing the reflection plane without true seam vertices. Regress
  the exact 16-point contour as local isolated geometry; no full-mesh
  replay claim. Preserve asymmetric/projected R05 behavior.
- [ ] **R06-M06C4c2 — Mirror-paired opposing-loop zipper with real seam.**
  Physical 15+15 mirrored opposing rims selected a supported zipper
  (saving=0.505506, threshold=0.45), but arbitrary DP triangles
  intersected X=0 and a final mirror audit refused. Build exact
  reversed-contour pair matching; create one *self-mirrored, x=0*
  seam vertex for each pair, join each side with inverse-orientation
  mirrored half-strips, and forbid a shortcut face crossing the plane.
  Stage new seam IDs privately, preserve the original rims, test true
  30-point coordinates, and keep source/zipper collision, quality,
  closure, ID and finite-work gates. No coordinate welding.
- [ ] **R06-M06C4c3 — Mirror-split completeness and diagnostic gates.**
  Preflight a tracked reflected edge pair before any split and
  distinguish missing topological mirrored edge from an absent
  survivor in the local M04 editable halo. Keep old-face identities,
  enforce all-or-nothing paired splits, 512-step/1024-vertex/4096-face
  limits, and query BOTH blue zipper and green cap surfaces against
  the complete post-refinement private mesh. Surface actual
  spatial-zipper usage separately from cap Poisson metrics.
  Verify real Linux test cases, GCC/Clang/MSVC CI, and leave C4c
  pending live mirror-map authorization.
- [ ] **R06-M06C4c — Preserve intrinsic mirror topology in private automatic caps.**
  New physical single elongated R03 rim spans X=0 with symmetric source
  stable vertices, but projected/spatial closure triangles crossed the
  mirror plane arbitrarily; known mirror relationships were invalidated.
  Prohibit a false mirrored repair success. Demand two true seam endpoints,
  explicit mirrored front/Poisson orbits (one self-paired on-plane hub),
  paired oppositely wound reflected faces, no plane-crossing unseamed
  triangles, and bounded post-conforming mirror audits. Prefer mirror-
  orbit-preserving seam splits; otherwise refuse privately with GOLD R03
  boundaries and leave source symmetry state unchanged. No silent switch
  to asymmetric or coordinate welding. Test the user's 26-point rim and
  a tampered asymmetry, while leaving genuinely asymmetric mode unchanged.
- [ ] **R06-M06C4d — Fix symmetric thin-scar quality stagnation.**
  On the new 26-point physical single-rim case, M04B chose CAP but
  exhausted 512 subdivisions and refused triangle q=0.090671 <0.10,
  length_ratio=0.240044, 539 inserted vertices. Implement bounded,
  *mirror-orbit-paired* local diagonal flips or patch retriangulation
  that improves minimum q instead of bisecting stagnating skinny
  triangles; certify adjacency, winding, q>=0.10, embeddedness,
  source survivor quality and all-or-nothing private rollback.
- [ ] **R06-M06C4e — Curvature and surface-profile quality diagnostics.**
  The one-loop physically manifold/quality-valid cap has a sharp
  pinched-waist/star-like profile. Triangle shape q>=0.10 and
  watertightness alone do NOT certify curvature continuity or a
  natural boundary transition. Add bounded cap-to-survivor dihedral
  statistics (including maximum normal discontinuity), signed
  out-of-plane fairing displacement from measured adjacent normals,
  and optional curvature-aware re-optimization of INTERIOR vertices
  only. Preserve fixed original rim, reflection paired moves,
  q>=0.10, source collisions and all-or-nothing private refusal.
  Do not force a visual smoothness rule that incorrectly erases
  intentional sculpted creases; expose an explicit diagnostic first.
- [ ] **R06-M10A — Captured refusal census and bounded quality-flip groundwork.**
  Keep five immutable physical V1 refusal cases, distinguishing cap q=.082329,
  zipper q=.098881, cap q=.096550, symmetric split-orbit budget, and
  cap q=.014067. Record deterministic current M03 plan, M04 refusal type,
  subthreshold q, inserted vertices/refinement count (and classifier);
  diagnostics must not be silently mistaken for a passing quality test.
  Implement only *interior* new/new cap-cap or zipper-zipper diagonal
  improvements, with four distinct vertices, proven two-face manifold
  adjacency, safe orientations, bounded local improvement and full
  post-change contact/manifold audits, without moving the old R03 rim.
  Prefer improvement before consuming further mid-edge subdivisions;
  cap quality >=.10 and zipper quality >=.10 remain compulsory.
- [ ] **R06-M10B — Entire mirror-orbit flip and finite deterministic passes.**
  For intrinsic X=0 symmetry, plan a complete reflected edge-flip
  orbit before modifying either side, including self-mirrored orbit
  safety; never produce one-sided geometry. Limit flip count independently
  from split budget and refuse or skip uncertain orbit. Unit tests for
  local improvement, nonconvex/inverted rejection, mirror equality,
  no duplicate edge or survivor/closure swap, and no partial orbit.
- [ ] **R06-M10C — Recorded-case regression and honest result review.**
  Replay the five real 5K archives under their original area classifier,
  compare with pre-change diagnostics, verify improvements don't
  introduce orientation/mirror/collision regressions and include
  M04 flip count in private logs. If an archived expected REFUSED
  legitimately changes to CLOSED, review that exact archive and
  document/explicitly update its expectation instead of silently
  declaring all old refusals to pass. GCC/Clang/MSVC CTests plus physical
  replay on Linux and Windows; keep draft/unpublished pending acceptance.
- [ ] **R06-M12A — Inspection-only quality-warning private staging.**
  Add explicit opt-in CLI for live R06 auto or saved R06 replay. Attempt
  ordinary certified AUTO first; after a quality-related refusal, perform
  separate bounded PRIVATE M04 staging with *quality and density gates*
  downgraded to diagnostic warnings, retaining the original candidate,
  exact excision and contact provenance. Never change production default,
  publish root/history/checkpoint, or mislabel a warned geometry as
  AUTO_CLOSED; non-quality topology, manifold, collision, mirror integrity
  and finite allocation limits remain fatal to preview. Retain per-face
  deficient q and edge-ratio with exact IDs and refusal reason.
- [ ] **R06-M12B — Real colored triangles plus explicit edge overlay.**
  Render actual cap and zipper triangulation edges, whether AUTO, R04,
  R05, or warned-private staging. Mark every quality-deficient face
  light red and all three associated triangle edges vivid red; normal
  cap green and zipper blue remain visible, with dark contrast edges.
  Honor front/back inspection and prevent phantom geometry after refusal.
  Label HUD/CLI warned previews as UNTRUSTED rather than COMPLETE.
- [ ] **R06-M12C — Strict/no-leak CLI and archive regressions.**
  Verify default R06 and R04/R05 unchanged, mode argument validation,
  deterministic archived 5K cases, per-face warning inventory with
  both low-q and long-edge cases, source and history immutability,
  refusal where topology/collision certification fails and correct
  overlay material palette. GCC/Clang/MSVC + physical Vulkan acceptance.
- [ ] **R06-M11A1 — Reject unsafe original-survivor midpoint children before mutation.**
  Implement pure, deterministic read-only quality assessment of both
  children of each original-survivor incident face at the proposed
  conforming edge split; preflight the complete reflected orbit and
  choose eligible safer alternative edges where possible. Refuse rather
  than create new q<0.012 survivors or partial mirror orbits. Confirm
  cases 2/4 remain recovered. Implemented for PR #155, but the original
  case 1 remains REFUSED, so this narrow safety improvement is NOT
  equivalent to solving M11A.
- [ ] **R06-M11A2 — Diagnose and prevent degenerate, nonreflectable
  refinement paths in physical case 1.**
  Exact Clang/GCC post-A1 replay of original 5K V1 refusal-1 reports
  skinny new CAP triangle q=0 with IDs 6360,6370,6369; all three edges
  have two incident tracked faces and no uniquely resolvable exact mirror
  partner, and none touches a survivor. Record all three precise XYZ
  positions and number of exact mirror candidates per endpoint; audit
  prior accepted operations for duplicate/collocated midpoint vertices
  and incomplete seam topology. Prevent invalid operations EARLY before
  affecting either mirrored half. Do not infer partners from approximate
  positions or let degenerate geometry reach M04 final acceptance.
### R06-M14 — Source-backed transactional closure refactor (2026-10-23)

Design decision and full independent acceptance conditions:
[docs/R06_M14_SOURCE_BACKED_CLOSURE.md](docs/R06_M14_SOURCE_BACKED_CLOSURE.md).
The original R03 post-excision mesh remains the immutable authority. A
single private source-backed transaction supplies survivor lookup and
transaction-owned edits for both original and new geometry; do not
separately mutate disconnected copies of survivor face/vertex records.
Current M13 warnings do **not** prove a known shared rim was split twice.

- [ ] **R06-M14A — Source-backed reads and transaction-local overrides.**
  Bind to the exact immutable R03 source, reference original survivor IDs,
  establish effective source/override reads across the complete survivor
  mesh (not just contour neighbors), regression-test identity,
  override precedence and source nonmutation. No new repair acceptance.
- [ ] **R06-M14B — Unify staging/refinement working topology.**
  Replace mutable survivor snapshots with ID-backed effective face/vertex
  reads, shared pending edits and source-backed traversal. Preserve
  original replay outcomes and finite quality budgets.
- [ ] **R06-M14C — Transaction-level conforming edge splits.**
  Discover effective two-face incidence (including previously untracked
  original geometry), share exactly one new midpoint ID across both sides,
  preflight full mirrored split orbits, quality/winding and finite limits.
- [ ] **R06-M14D — Link M13 geometric witnesses to split provenance.**
  Distinguish new/interior vs inherited original rim IDs; classify actual
  shared interfaces versus incidental 3D contacts and deduplicate issues.
- [ ] **R06-M14E — Bounded geometric nonconformity recovery.**
  Conform legitimate attachments via unified topology; change
  excision/front or refuse on true crossings. Never coordinate-weld
  unrelated surfaces.
- [ ] **R06-M14F — Archive, CI and physical Vulkan verification.**
  Preserve all historical case bytes, current valid CLOSED recoveries
  and legitimate refusals, compare geometric witnesses, confirm
  immutable source/history/checkpoint and exact reflected orbits.
  Require full cross-platform CI and manual GPU acceptance.

- [ ] **R06-M13A — Independent private cap/survivor shared-edge audit.**
  On explicitly requested exact-case/live R06 preview, use the final
  displayed RegionTopologyIndex and actual green/blue new FaceIds to
  inventory each cap/zipper vs surviving interface edge; log both FaceIds,
  exact ordered VertexIds, XYZ at full precision, directional signs,
  edge incidence and whether the boundary is topologically conforming.
  Never infer connectivity from positions alone. Keep full audit separate
  from the existing strict R04 manifold certificate.
- [ ] **R06-M13B — Bounded geometric T-junction and crossing diagnostics.**
  Independently inspect new cap edges against nearby surviving vertices,
  surviving edges against cap vertices, and disjoint new/surviving edge pairs
  using an explicit spatial tolerance plus complete/incomplete counters.
  Detect coincident but differently indexed vertices. Do not report 'no
  intersections' when finite budgets or pairwise-only coverage prevent a
  comprehensive triangle-triangle test. Add synthetic positive/negative
  regression fixtures and exact 5K captured refusal-1/-3 CLI census.
- [ ] **R06-M13C — Exact inspection mode and separately marked seam edges.**
  Add explicit CLI switch removing all per-triangle normal displacement
  from colored cap/zipper overlays; render actual interface edge positions
  in a distinct diagnostic color, never by creating a second mesh offset.
  Retain ordinary inspection default and warning color distinction. Document
  depth-test caveats and verify on Vulkan at extreme zoom with W on/off.
- [ ] **R06-M13D — Verify archive and nonpublication invariants.**
  Ensure neither opt-in mode modifies q threshold, R06 CLOSED/REFUSED,
  exact source root, checkpoint/history state, or original immutable
  .r06case files. Run CTest on Linux GCC/Clang and Windows MSVC; physical
  GPU visual acceptance before merging.
- [ ] **R06-M11A3a — Finite interior-vertex fairing with geometric merit.**
  In private staged closure only, try bounded deterministic moves of new
  interior vertices toward the centroid of their immediate neighbors;
  refuse moves that touch a surviving/R03 contour vertex, break oriented
  face normals, create coincident vertices, cross the intrinsic symmetry
  plane, lower a previously healthy face below its current quality floor,
  or fail to improve the local aggregate quality-deficit. Move mirror orbits
  atomically with exact reflected coordinates; keep end-of-stage collision
  audits, finite work budgets and original q=0.10 floor unchanged. Record
  accepted fairing counts. Independently test both ordinary and paired moves.
- [x] **R06-M11A3a1 — Bounded fixed-rim interior fairing implementation and proof.**
  Draft #157 supplies deterministic new-interior-only local movement,
  exact matched mirror orbits, strict q and collision validation, independent
  unit regression. The real 5K V1 `refusal-5` now AUTO_CLOSED:
  CAP 72 faces, q=0.100624, 3 refinement splits, ONE fairing move, versus
  the original 512-step refusal. Historic V1 archive unchanged; reviewed
  recovery CTest requires current CLOSED and q>=0.10. Linux/Windows CI
  green on layered #159 head; physical GPU acceptance still needed.
- [x] **R06-M11A3b1 — Only provably redundant 3-to-1 private cap fans.**
  Draft #159 removes one new valence-three interior center only when
  strict replacement q/edge quality, survivor ID freeze, fan orientation,
  duplicate-face, and complete exact reflected pair all pass. 91/91
  GCC/Clang/MSVC tests passed. This is NOT an unrestricted edge collapse,
  and did not by itself remove the refusal-1/-3 geometric obstacles.
- [ ] **R06-M11A3b2 — General interior edge contraction after rim proof.**
  Extend the cap-aware collapse pass beyond valence-three fans to short
  interior edges with complete link condition, proven zero old/rim deletion,
  finite whole-neighborhood q deficit improvement, and atomic reflected
  two-edge operations. Exact 5K V1 refusal-1/3 currently STILL refuse at
  q~2.86e-13 and q~2.72e-13 after fairing budget=32; they need a
  **different triangulation**, not a quality floor waiver.
- [ ] **R06-M11A3b3 — Sharp-tip spatial front/ribbon fallback.**
  For cases whose intrinsic R03 boundary corners are very elongated
  and whose existing Poisson fan repeatedly subdivides unchanged
  narrow wedges, stage a bounded patch-wide strip/medial-axis
  triangulation with arc-length-based spacing (no parity pairing),
  immutable directed rim, seam reflection, oriented manifold, and
  real 3D collision verification. Accept only if q>=0.10 apart from
  individually mathematically certified fixed planar tip exceptions.
- [ ] **R06-M11A3b — Constrained collapse and local topology reduction.**
  After evaluating M11A3a on all five saved 5K cases, design finite
  interior edge contraction/removal of expendable NEW vertices only,
  with link-condition checks, exact reflected face orbits, signed-area
  and winding checks, stable survivor/rim IDs frozen, no duplicate faces
  and strict reduction of the whole neighborhood quality deficit. Do not
  graft the brush collapser into closure: it requires a pre-stamp baseline.
- [ ] **R06-M11A3c — Geometrically proven boundary-corner quality exemption.**
  Restrict any exemption to a fixed original R03 rim corner whose
  certified local planar wedge imposes a *provable* triangle quality
  upper bound below minCapQuality; do not equate exhaustion of a
  subdivision budget with a proof. Define precisely how this exceptional
  boundary condition is recomputed after a future sculpt stamp,
  with no relaxation of area>0, symmetry, manifold, collision or
  edge-length requirements. Test acute vs merely elongated polygon,
  3D folded wedge, and opposite mirrored tip cases.
- [ ] **R06-M11A3d — Thin accurate inspection edges and five-case census.**
  Replace thick world-space cap/zipper inspection rods with existing
  depth-aware near-one-pixel screen-space edge rendering, retain color
  distinction red vs dark and the W-independent visibility, and
  inspect Linux/Windows Vulkan. Verify exact refusal-1/3/5 and recovered
  2/4, document any outcome changes as reviewed regressions; do not
  change immutable V1 case files.
- [ ] **R06-M11A3 — Finite quality-safe alternate repair for case 1.**
  After A2 isolates cause, implement either bounded, geometrically
  certified non-midpoint conforming edge subdivisions with exact
  reflected positions and child q floors, or a separately validated
  interior retriangulation that preserves frozen R03 old rim and full
  reflected face orbit. Reject nonconvex/folded/duplicate vertices,
  preserve the no-void/no-intersection and M04 q=.10/0.012 audits.
  Update captured-case regression to require actual AUTO_CLOSED ONLY
  after demonstrating it on case 1 (original V1 archive unchanged).
  Full GCC/Clang/MSVC and physical GPU acceptance required.
- [ ] **R06-M11A — Survivor-safe quality choice for real refusal-1.**
  Physical 5K V1 case 1 changes from q=.082329 cap refusal after 512
  splits to `M04B conforming split left skinny original survivor`
  under M10 flips. Trace which mandatory split degraded a surviving
  stable FaceId (edge endpoints, original and child q, partner kind),
  and reject or route around that split before it happens while
  preserving the immutable R03 cut, seam conformality, and 0.012
  minimum survivor quality. No changing old survivor geometry beyond
  existing bounded conforming operations; targeted replay regression.
- [ ] **R06-M11B — Global quality-deficit merit and nonplanar correction.**
  Physical 5K V1 case 3 remains CAP q=.096550 after 512 splits
  AND 128 interior flips; case 5 also used 128 flips. Measure
  number of under-quality faces, summed q deficits and worst q
  after each accepted flip/split; replace purely local minimum-q gain
  with a bounded deterministic global decrease rule (avoid cycles
  and unproductive exchange), then consider a separately bounded
  interior reposition/retriangulation of genuinely nonplanar caps
  only if required. Never move frozen R03 boundary vertices, lower
  q=.10, or invent a planar projection.
- [ ] **R06-M11C — Adaptive mirrored split-orbit scheduling.**
  Physical 5K V1 case 5 now refuses at 511 refinements and 128
  interior flips: `complete mirrored edge-split orbit exceeds
  finite budget`. Keep reflection atomic and mandatory adjacency
  checks; optimize pre-split choice and only use available complete
  orbit budget for quality/length defects that genuinely decrease
  remaining deficit. No global relaxation of finite budget.
- [ ] **R06-M11D — Full five-case closed acceptance and multi-platform GPU.**
  Preserve all original V1 files as historic REFUSED capture, and
  require current CLOSED with real good q and no material/mirror
  collisions before treating each as recovered. Case2 now closes
  with zipper q=.138548 after 58 splits and 4 flips, case4 caps
  q=.102239 after 1 split and 2 mirrored flips (M10 first CI).
  Extend explicit reviewed recovery CTest expectations only when
  independently demonstrated; run all five in GCC/Clang/MSVC and
  inspect Linux/Windows Vulkan screen, remain unpublished R06.
- [ ] **R06-M10D — If still refusing: separately planned bounded remedies.**
  Use actual replay results to identify which cases require nonplanar
  interior node relocation/retriangulation or adaptive frontier repair,
  versus changing symmetric edge-split sequencing for mirror budget.
  Record independently complete follow-up milestones BEFORE implementing
  additional algorithmic classes; never respond by lowering q=.10 or
  unboundedly increasing subdivision limits.
- [ ] **R06-M09A — Revalidation matches selected replay classifier.**
  A captured v2 AUTO_CLOSED case saved under area can be replayed with
  proximity; M03 chooses zipper but M04C independently replans with
  default area, falsely flagging the legitimate alternative as
  stale/forged. Thread the selected `AutoClosurePlanSettings` into
  the existing private M04C trust recheck and compare its independently
  generated cap and zipper sets. Keep real source/cut validation and
  all M04 quality/manifold/symmetry/collision refusal unchanged;
  intentionally forged selections must still be refused.
- [ ] **R06-M09B — Read-only classifier comparison in every frozen R06 HUD.**
  Evaluate area and proximity M03 from the same immutable post-stamp
  candidate, R03 cut, R02 proposal and R01 contacts (without running
  two M04 geometry builds); on both live and exact replay, display two
  top-left lines `AREA: ...`, `PROXIMITY: ...`, appending `<---`
  to the enabled classifier. Green when both valid planned selections
  are identical, red when valid selections differ; label incomplete
  plans explicitly as unavailable, avoiding false claims about correctness.
  HUD must not measure geometry success or auto-adjust classification,
  and must remain available even when active M04 refuses. Shader/HUD
  tests must prove red and green glyph materials and no changes to
  established top-right FPS/stamps display.
- [ ] **R06-M09C — Pairwise archived A/B regressions and physical gate.**
  Regression covers private certifier accepting independently
  recomputed selected proximity plans, rejecting modified forged
  plan irrespective of classifier, comparing both planned decisions
  on identical frozen candidates, preserving original area default,
  V1/V2 archive contents and mismatch CLI behavior. Verify all five
  physical 5K fixtures and the new user-held
  `classification-zipper-1.r06case` once shared. GCC/Clang/MSVC
  before user physical Linux/Windows Vulkan HUD check. Keep draft
  and do not publish worker history.
- [ ] **R06-M08A — Selectable experimental two-ring proximity fitness.**
  Keep `--repair-classifier area` as byte-for-byte default V15 area-DP
  behavior and add `--repair-classifier proximity` for eligible, proven
  two-rim regions only (never invent contacts/loops). Compute each ring's
  translation-conditioned Newell/vector area, NOT PCA best-fit-plane area;
  calculate bidirectional nearest OTHER-VERTEX distances with adjacent
  edge-arc-length weights, normalize by total weight, and compute
  `score=1*average_area -1*abs(area1-area2)-2*mean_distance^2`.
  Strictly positive => zipper, otherwise caps; finite/count/geometry
  guards and exact zero => caps. Preserve direct trusted contact/identical
  patch prerequisites and all R06 M04 geometry/quality/manifold/symmetry,
  sealed roots and refusal budgets. Log area1/area2/average/difference,
  weighted bridge d/d^2, three weighted terms and score/decision plus
  eligibility. No V15 area-savings override in proximity mode. On
  >2-rim contact islands, explicitly diagnose use of original V15
  multi-rim matching, rather than pretend proximity DP was implemented.
- [ ] **R06-M08B — Exact replay comparability and classifier regressions.**
  Thread the classifier setting through live R06 inspection and both
  `--repair-replay`/headless-test against the SAME archived post-stamp
  candidate; archived outcome/intent stay immutable and mismatches are
  reported without publication. Regression for planar/nonplanar vector
  area distinct from PCA, arc-length sampling invariance under edge
  subdivisions (bounded error), unequal radii and 3:1 cutoff,
  high-distance caps, exact tie, zero-length refusal, valid linked
  two-rim topology, absence of invented separate-island pairings and
  unchanged default area results. Use all five physical V1 cases as
  deterministic baseline in area mode; compare proximity with explicit
  CLI while treating changed outcomes as expected experimental evidence
  (not silently rewriting fixture oracles). CI GCC/Clang/MSVC and
  actual Linux GPU cap/zipper/replay needed before acceptance.
- [ ] **R06-M08C — Future >2-contour alternative fitness pairing.**
  Independently design bounded bitmask-DP pairing where positive local
  proximity fitness can compete with caps over 3–4 loops, while respecting
  R01 contact connectivity and M04 acceptance. Not part of M08A/B:
  first gather two-rim physical results; preserve V15 fallback and
  clearly log it in proximity mode rather than imply full support.
- [ ] **R06-M07D — Capture all frozen R06 outcomes and explicit classifier intent.**
  The first physical acceptance stored five real 5K-vertex refusals on the
  r06-m07-captured-refusal-replay branch: cases 1/3/5 CAP quality
  q=0.082329/0.096550/0.014067 after 512 steps; case 2 ZIPPER
  q=0.098881 after 512; case 4 mirrored complete split-orbit budget.
  Current Ctrl+S excludes valid AUTO_CLOSED, losing wrong-but-successful
  V15 cap/zipper decisions. Allow **Ctrl+S for any frozen R06 outcome**
  (retaining normal Ctrl+S checkpoint behavior outside frozen inspection).
  Mark desired classifier separately at capture time with **Ctrl+Shift+S**
  (prefer zipper) or **Ctrl+Alt+S** (prefer caps), not by inventing
  inferred topology from a screenshot. Preserve a portable, checksummed
  **case V2** semantic intent field, backward-load existing V1 exactly;
  retain recorded actual success/refusal as its separate regression oracle.
  Save complete POST-STAMP candidate with original R01 evidence for both
  green/blue success and gold refusal, distinct non-overwriting case names,
  and private-only/worker-history invariants. Headless replay must report
  actual plan choice and independently check optional intent, with clear
  outcome-vs-choice mismatch exit codes. Do not adjust the V15 area-DP
  heuristic based solely on one screenshot.
- [ ] **R06-M07E — Corpus and physical classifier acceptance.**
  Treat committed refusal-1..5.r06case as first-class deterministic R06
  replay fixtures, preserving original archive bytes. Add V1/V2 roundtrip
  tests, completed closure capture, both tagged classifier intents, mixed
  plan decision checking, invalid tag rejection, unique naming/overwrite,
  and headless parser paths under GCC/Clang/MSVC. Physically capture the
  pictured *completed but undesired caps* with Ctrl+Shift+S, then replay
  the newly archived case to inspect the complete M03 region evidence.
  Only then plan independent classifier-geometry tuning and validate that
  desired zipper passes R06's unchanged quality, collision, manifold and
  reflected symmetry gates. Keep PR draft until GUI key combinations and
  replay on actual Linux/Windows Vulkan are accepted.
- [ ] **R06-M07A — One-file frozen automatic repair-case capture.**
  Add a developer-only checked/versioned finite-size R06 case archive embedding
  the complete exact immutable POST-STAMP candidate MeshRevision (including
  stable vertex/face IDs, partition ownership, allocation watermarks and
  tombstones) via the existing durable checkpoint codec, PLUS original
  R01 trusted face-contact pairs, intrinsic-symmetry source flag and expected
  final success/refusal outcome/diagnostic. The candidate and contacts are
  precisely the input immediately before R02/R03/M03/M04 automatic repair,
  not merely the printed contour, screenshot or original input brush gesture.
  A failed R06 frozen viewport shall let Ctrl+S save one unique numbered
  `repair-cases/refusal-XXXX.r06case` by default or specified `--repair-case-out`
  path, with atomic replacement, CRC/hash/length/version bounds and no
  actual worker/undo/history/checkpoint publication. No save for unfrozen
  or successful AUTO; preserve ordinary Ctrl+S checkpoint semantics outside
  frozen inspection and never destructively overwrite another recorded case.
- [ ] **R06-M07B — Deterministic loaded-case replay and frozen Vulkan inspection.**
  `--repair-replay FILE` restores the complete candidate and original contacts
  and reexecutes R02 preview, R03 excision, R06 M03 planning and the current
  R06 M04 private closure, then opens the ordinary frozen Vulkan viewport
  with actual green/blue closed faces OR the true gold R03 refused rims.
  Display same AUTO_CLOSED / AUTO_REFUSED diagnostics including stored-vs-
  current outcome. No manual stamps, no previous-source regeneration,
  no mutation of live mesh or authoritative symmetric journal, no
  `--recover` ambiguity. Reject malformed files, incompatible CLI and
  internally inconsistent contacts before any geometry display.
- [ ] **R06-M07C — Headless deterministic replay regression test and docs.**
  `--repair-replay-test FILE` runs the same actual private repair pipeline
  without Vulkan/GLFW and checks the archived expected success/refusal status;
  distinct mismatch exit status so fixture artifacts can be committed to
  CI. Positive closure and quality-limited refusal roundtrip tests, corrupt/
  truncated/oversized case rejection, exact stable ID/watermark/tombstone
  and source symmetry persistence, single history/root nonpublication,
  repeated replay determinism, default save-path collision preservation;
  Linux GCC/Clang and MSVC warnings-as-errors gates. Require physical
  Linux GUI Ctrl+S -> future-version --repair-replay confirmation and
  Windows Vulkan test separately before checking integration parent.
- [ ] **R06-M06C4f — Finite alternative for self-intersecting mirrored spatial fronts.**
  Latest physical Linux evidence: independent 16- and 20-vertex exact
  reflected single loops refused at the spatial-front self-contact gate;
  a different 18-vertex loop closed with q=0.140 and no density backlog.
  First try the existing reflected Poisson front unchanged. When its
  generated triangles self-intersect, construct an alternative reflected
  half-ribbon around a genuine X=0 polyline whose endpoints are the
  existing seam IDs; never cross X=0 with off-plane faces and never weld
  different rim IDs. Reject overlapping/skewed segments, collapsed
  triangles, unmatched mirror triangles and finite collision-work limits.
  Keep source rim fixed, only use new private COW seam IDs, and require
  all original M04B quality>=0.10, survivor>=0.012, full-mesh collision,
  manifold, symmetry and 512-step finite-budget gates. Regress both
  full-precision physical loops as LOCAL fixtures, malformed mirror
  refusal, and the prior accepted 18/26/32 and asymmetric paths; distinguish
  front vs ribbon telemetry. Full original sculpted root and physical GPU
  acceptance still need replay. This is an independent fallback peer;
  existing C4d and M06D own quality stagnation, NOT quota increases.
- [ ] **R06-M06C4f-acceptance — Physical ribbon and mirror-map acceptance.**
  Record physical AUTO_CLOSED/AUTO_REFUSED with the new fallback for
  the two recorded single-loop cases and routine mirrored successes;
  do not check C4f or parents based on local contour-only tests. Keep
  explicit authoritative mirror-ID journal for R07 independently open.
- [ ] **R06-M06C4b — Full R06 spatial closure acceptance.**
  Repeat real nonplanar sphere/hollow/torus cases with profile views
  and `spatial_cap_loops` logs; check optional detail refinement,
  COW rollback, whole-mesh intersection, no ghost self-collision,
  Linux GCC/Clang and Windows MSVC physical Vulkan signoff.
  Distinguish independent two-disk gaps (M03 topology) from spatial
  disk shrinking (C3 geometry), and preserve actual gold-rim refusal.
- [ ] **R06-M06C4 — Integrated mixed private cap backend and CI acceptance.**
  Map local front IDs to fresh immutable MeshRevision stable IDs inside
  one COW transaction; conforming quality correction must split *both*
  incident sides. Run complete root/boundary/contained-shell audits and
  preserve R04/R05 forced path, GOLD refusal and worker/history/checkpoint
  invariants. Repeat 74+ CTests GCC/Clang/MSVC before checking parent C.
- [ ] **R06-M06C — Nonprojectable, folded single-loop cap reconstruction.**
  New physical noodle self-contact merges two would-be cuts into a
  long skinny curved boundary loop; R05 exhausts all projection
  orientations (`contour projection overlaps itself`). Design a finite
  nonprojective 3D disk triangulator or an equally explicit cut-recovery
  strategy. Require boundary-preserving oriented embedded triangles,
  verified against each other and the surviving surface, no foldovers,
  unchanged q>=0.10, manifold/fan audit, finite budgets and whole-plan
  refusal on ambiguous genuinely unfillable rims. Preserve R05 forced
  behavior, and regress projected-simple caps plus folded 3D contours.
  A synthetic test is NOT evidence of reconstructing the exact physical
  boundary without an R03 replay fixture.
- [ ] **R06-M06D — Mixed scar triangle-quality improvement.**
  Actual close noodle zipper refused after all 512 conforming midpoint
  subdivisions: ZIPPER q=0.078209 <0.10, length_ratio=0.229595,
  inserted_vertices=512. Prefer finite quality-improving legal diagonal
  flips or local retriangulation over midpoint-only splitting, and
  reject crossing/invalid candidates; preserve same quality and COW
  rollback guarantees. Exercise badly shaped but recoverable seams
  and irrecoverable skinny safe refusals.
- [ ] **R06-M06E — Repeat physical and integrated CPU/GPU acceptance.**
  Repeat sphere noodles (multiple real stamps, off-axis self-contact,
  both accepted zippers and legitimate refusals), torus tunnel,
  hollow inward-shell removal, mixed islands, quality/manifold
  invariants, Linux and Windows hardware provenance and stack/main CI.
  Keep the parent R06 unchecked until M06E is accepted; R07/R08 remain
  future independent publication milestones.
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
