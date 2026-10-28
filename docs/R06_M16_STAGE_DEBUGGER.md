# R06-M16 — Shared closure construction and staging-operation debugger

**Design state:** M16A architecture survey and milestone contract (2026-10-24). This is a plan, **not** an implemented debugger. The starting branch is `r06-integration` at `cac00ec5cdc2715902a6ca618c858b800a4b9a2f`. PR #162 remains draft.

## Finding: two separate forced implementations

`RepairInspectionSession::inspect` invokes `applyForcedPrivateZipper` (R04) and `applyForcedPrivateCap` (R05) directly. `runAutomaticClosure` (R06) instead calls `applyCertifiedPrivateAutomaticClosure`, which uses `applyPrivateMixedClosure`, `stagePrivateMixedClosure`, and `MixedClosureStaging`. The legacy forced implementations in `src/repair_zipper.cpp` and `src/repair_cap.cpp` therefore bypass the M14/M15 staging editor for their mesh writes. The stage's initial generators already share `triangulatePrivateZipperStrip` and `triangulatePrivateCapDisk` where supported, but the complete refinement/validation pipelines are different.

The desired architecture is *three policy modes, one stage-owned topology editor and one refinement/validation implementation*. Forced cap constructs a plan with every eligible R03 rim capped; forced zipper constructs an explicitly paired plan with no caps; automatic retains the selected M03 classifier plan. An impossible forced choice must refuse, **never fall back** to another algorithm. The forced planners must retain meaningful independent R04/R05 regression coverage. Do not simply call R06's **certified auto wrapper** from forced modes: that wrapper rechecks the trusted classifier decision and handles enclosed component retirement. Factor out the common **execution** path while keeping policy authorization and enclosure decisions explicit.

## Why not rely on thinner wires

The M15D stored witnesses in refusal-1/3 are CAP-only split midpoints on indexed edges different from nearby survivor edges. They can be separated by far less than a physical pixel. Only a chronology of requested stage edits can answer whether geometry first diverges at cap generation, provisional-ID resolution, refinement, flip/fair, or an intended survivor attachment omitted by the generator. A screenshot of an end state alone cannot establish this.

## Snapshot / operation contract

When explicitly armed, each **successful logical, effective state-changing stage operation** emits a monotonically sequenced event, including:
- operation name, exact stable VertexIds/FaceIds (or provisional IDs clearly marked), oriented face corners, owners, arguments, return values, and whether an idempotent request was a no-op;
- stage-effective `faces()`, exact `positions()` (including source-face and position overrides), and `currentBoundaryLoops()` or an explicit invalid/intermediate-boundary diagnostic;
- original R03 source identity and whether the stage belongs to a certified attempt or untrusted quality-warning preview.

Capture after the entire logical edit is complete. `splitEdge` may call internal allocation helpers, but its mirror orbit and all updated incidence sides should appear as *one coherent split operation*, not a half-split snapshot. Avoid callbacks while a stage mutex or mutable view is borrowed by the renderer. Read-only queries are not steps. Failed/no-op calls can be surfaced as metadata where useful but must not masquerade as completed topology edits. Initial staged cap/zipper `ensureFace`, provisional vertex creation and retirement, final ID assignment, split, flip, cap-fan collapse, and fairing all need coverage; a `splitEdge`-only observer is insufficient. Intermediate open boundaries are legitimate.

A snapshot is an **owned, immutable**, bounded render packet, not a pointer to live stage vectors or mutable COW objects. It should store exactly what the UI needs to draw and compare. Source survivor geometry must be included or referenced by immutable root and exact overrides; do not show a stale unsplit gray triangle under a refined cap. Face kind (SURVIVOR / CAP / ZIPPER), original source vs staged new identity, and open boundary provenance are preserved. Compare two consecutive **stable-ID** states to mark introduced, removed and modified faces/edges/vertices, and moved vertices; coordinate equality is not identity.

## Threading and interaction contract

Live sculpt already runs off the Vulkan thread, but exact `--repair-replay` currently computes its full private closure **synchronously before opening the viewport** (`src/main.cpp`). A true replay debugger requires an opt-in worker execution path after Vulkan initialization. The worker copies a snapshot, sends it to a bounded (max two visible frames) thread-safe channel, and blocks at the stage operation boundary on a condition variable. The renderer polls immutable packets without blocking on the worker. Never call the viewport or upload GPU buffers from the worker. Disabling the mode must add no copies, sleeps or synchronization on the ordinary path.

UI must support: Next (release exactly one operation), Continue (disable further pausing), Previous / Current (view the last two *already captured* frames without undoing the algorithm), and Cancel/Exit (wake worker and safely join). Navigation between previous/current must not implicitly advance or mutate the calculation. Avoid accumulating Next permits if no worker step is pending. A completed/refused attempt is terminal and remains safely inspectable. A failed operation reports the attempted arguments and exception without certifying incomplete geometry. No private stage may publish to worker root/history/checkpoints.

Display operation details in the **upper-left, starting on row 3 (zero-based row 2)** beneath the two existing area/proximity classifier rows. Show step number, operation, exact arguments, result/error, whether PREVIOUS or CURRENT is displayed, and a legend for changed topology. HUD clipping/wrapping for long argument sets is required. The right-side FPS and stamp-rate HUD must continue working.

Use a dedicated `--repair-step-debug` opt-in, validated for interactive `--repair-replay` and eventually live frozen inspection. It must not alias the existing animation Space/Right controls until the mode-dependent key routing is explicit. Document actual keys after implementing them. Normal R01–R06 CLI behavior, M15D exact-case outcomes and strict quality/repair acceptance are invariants.

## Independently observable milestones (flat)

- **M16A — Architecture and acceptance plan.** Verify call sites, source identity, worker/replay lifecycle, renderer HUD and snapshot boundary. Record design and TODO before implementation.
- **M16B — One shared executor, three planners.** Migrate R04/R05 forced policies onto the same M15 stage/refinement and final private validator used by R06, without invoking R06's classifier authorization from forced modes. Test legal refusal, forced choice fidelity and original replay results. Keep legacy adapters only if they genuinely preserve API compatibility; remove duplicate geometry writers once parity is demonstrated.
- **M16C — Instrumented stage events and two-state gate.** Opt-in callbacks for all logical stage edits, exact arguments, immutable copy, bounded two-frame transfer, controller Next/Continue/Cancel, and independent headless concurrency tests. Disabled-mode event count/cost zero.
- **M16D — Vulkan integration for exact replay.** Launch computation on a joined worker, keep Vulkan responsive; refresh effective geometry/differences; Previous/Current/Next/Continue/Cancel; top-left HUD row 3 onward. Handle early refusal and source-only initial state.
- **M16E — Live-frozen worker integration.** Reuse same gate from the live single-worker pre-CAS R01–R06 inspection path with frozen source and no publication, no worker queue deadlocks, and cancellation on exit.
- **M16F — Regression and physical acceptance.** Five unmodified `.r06case` blobs; refusal-1/3 replay step correlation; preservation of M15D 382/356 geometric census and warning-preview trust labels; deterministic event IDs; GCC, Clang and Windows MSVC warning-as-error; user-observed Vulkan screenshots/controls. M15D4/M14F remain separate unfinished physical gates.

## Open implementation hazards

1. `stagePrivateMixedClosure` currently returns a stage **by value**; attach an observer before initial generation without giving the callback a dangling reference after move/return.
2. Stage operations are not all in `repair_mixed_stage_edit.cpp`; `src/repair_mixed_closure.cpp` also coordinates operations and may perform final private-vertex position synchronization. A complete operation stream requires reviewing all mutation sites.
3. Snapshots may contain nonpermanent provisional IDs; never convert their visual presence into a published mesh revision.
4. Capturing all effective survivors at each step may be expensive. Keep the renderer's immutable R03 base and stage overrides/deltas, bounded packets, and explicit max-memory enforcement. A max-two-frame queue alone does not limit a single frame's bytes.
5. Failed archived repairs produce `AUTO_REFUSED` but may offer an opt-in untrusted `QUALITY_WARNING_PREVIEW`; step through the chosen attempt, labeling warnings without silently changing certification.
6. The existing UI draws classifier rows at upper-left and FPS/stamps at upper-right. At row 3, use a separate left-side panel rather than changing the classifier semantics.

**No safety admission, symmetry, attachment or quality policy is changed by M16A.**
