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
- Preserve exactly ordered asynchronous 4D input Begin/Update/End,
  original symmetric fixed-plane and reciprocal source identity,
  finite worker CPU work, diagnostic refusal categories and safe
  continued FIFO processing. Keep source authority anchored to the
  immutable FIRST original; coincident XYZ never implies equal IDs.
- **V17 Merge ON is explicit and source-proven, not a universal Boolean.**
  Ordinary unarmed sculpt remains default; protected Merge starts its
  visible mode OFF until enabled. No global source repair, unproven
  cavity deletion, broad imported-mesh correctness or arbitrary
  contact topology is authorized by V17 acceptance.
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

## Current work: V18, then V19

**Order:** V18-M04 is NEXT. M04–M07 are flat peers
preplanned before M01 implementation. Do not move original V18
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
- [ ] **V18-M04 — Constrained normal displacement with conservative large-step policy.**
  Use local pre-/post-state geometric evidence to reduce/limit
  proposed normal motion where clearance is endangered while
  preserving separately valid tangential movement. Define a
  finite deterministic bound/backtracking policy on the PRIVATE
  candidate and rerun post-refine/collapse contact/clearance checks.
  Unsupported large stamps may refuse; do not claim continuous
  swept-volume CCD or no tunneling between samples.
- [ ] **V18-M05 — Both-opposing-sheet and reflected-symmetry transaction proof.**
  Integrate simultaneous motion on two opposing sheets against
  the SAME complete post-state, and source-certified reciprocal
  mirror pairs including fixed X=0. Positive symmetric clearance/
  slide and deliberate close-approach refusals preserve one
  terminal CAS, permanent stable IDs, original-root picker,
  mirror journals, undo/redo, stale refusal and independent
  coincident-coordinate ownership. Bounded work only.
- [ ] **V18-M06 — Explicit desktop opt-in, default unchanged, and diagnostics.**
  Add an unambiguous CLI/UI selection for V18 OFF-mode collision
  constraints (NOT the V17 protected Merge ON authorization).
  Verify ordinary startup/default flags/old checkpoints,
  invalid combinations, visible refused Steps and later resumed
  editing in real interaction; keep numeric settings/feature
  checkpoint persistence M72 as separately deferred. No
  automatic retrofit on imported bad meshes and no silent
  feature enablement on recovery.
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
