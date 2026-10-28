# R06-M19D1/D2 — selected diagonal provenance and provisional step-preview acceptance

**Exact CPU-tested implementation:** `171a3f8e0116bd50d2a2dd2a122478a26c61d4ef`

**CI:** [#40211466871](https://github.com/logiclrd/Mesher/actions/runs/40211466871) **SUCCESS**. Linux GCC, Linux Clang, and Windows MSVC `/W4 /WX` all built and passed **126/126 CTest cases** per platform. M19A4 wider-normal search in the same implementation is described separately in [R06_M19A4_WIDE_SWEEP_TRIAL.md](R06_M19A4_WIDE_SWEEP_TRIAL.md).

## M19D1: pure candidate/provenance result

Existing M19C `probeSpatialCapDiagonalFeasibility()` keeps its read-only default; it may now optionally output the selected local `SpatialCapPatch`, and reports each accepted representative + reversed reflected partner strip-quad ordinal as `acceptedQuadOrbits`. Missing symmetry partners, failed q/orientation, self-contact and finite budgets are unchanged. Synthetic ordinary/mirrored CPU regressions assert exact partner identities, the original patch and positions unchanged, and deterministic selected faces/rounds. M19C's `--repair-diagonal-probe` log now includes `selected_quad_orbits=...` for the chosen counterfactual.

## M19D2: visible, separate provisional topology

New **default-OFF** `--repair-diagonal-preview` is legal only in interactive `--repair-step-debug --repair-replay FILE`, not live automatic repair or headless replay tests. It is mutually exclusive with the independent `--repair-feasibility-preview` position experiment.

When the spatial-cap baseline is locally legal, M19D2 invokes that same finite M19C mirrored diagonal search, collects the optional selected patch and substitutes **only its face connectivity** into the existing PRIVATE cap builder before the ordinary staging `EnsureFace` mutations. New `[repair-r06-m19d-provisional-stage]` logs `candidate_applied`, `changed_faces_only`, local minimum cap quality and explicit `whole_mesh_certified=0 publication=disabled`. HUD says `M19D PROVISIONAL DIAGONAL PREVIEW - NOT CERTIFIED; NO PUBLICATION`. If no locally improved candidate, baseline is unchanged.

The original exact `refusal-1.r06case` CPU regression confirms that the opt-in patch actually has changed face indices, retains the same face/position counts, leaves **every original source rim and interior position unchanged**, and respects the unchanged q>=0.10 cap floor. Invalid CLI invocations are rejected by two new CTest cases. The `--repair-diagonal-probe` mode remains read-only.

## Manual comparison

```bash
build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson --repair-diagonal-preview \
  --repair-seam-audit --repair-overlay-exact
```

Control: remove `--repair-diagonal-preview` and preserve the original camera view. To compare M19A4 positions instead, use `--repair-feasibility-preview`, **not both simultaneously**. Inspect first changed `EnsureFace`, reflected partner, complete downstream private verdict and source seam.

## Crucial limits and next milestone

This acceptance demonstrates only **local cap candidate admission + provisional private staging**, NOT full original-survivor+cap collision/manifold/reflection/crease certification of the modified patch, NOT a production-safe automatic topology optimizer, NOT physical Vulkan smoothness, and NOT worker-root/history/checkpoint publication. The unchanged complete private validator downstream can refuse.

**M19D3 / M19B2 remain OPEN:** establish explicit source-complete counterfactual assembly and collision, closed-index one-ring/edge-incidence, winding, original survivor q and reflected-orbit checks on immutable saved and healthy/intentional-crease fixtures. Record a legitimate refusal rather than silently accepting locally plausible geometry. M19D4/M18F/M17E remain physical acceptance.
