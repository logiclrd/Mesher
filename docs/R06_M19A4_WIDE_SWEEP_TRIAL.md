# R06-M19A4 — widened bilateral first-ring normal sweep (experimental)

**Purpose:** user Vulkan step-debug screenshots comparing ordinary M18E staged cap (no preview) and the opt-in M19A3 preview show a **large visible reduction** of the step-53/54 returning hook, but an appreciable residual kink remains. Try a modestly wider M19A2 signed normal sample before committing to a different solver or topology.

## Exact changed coefficient

`SpatialCapFeasibilityLimits::normalSweepEdgeScales` changes from **0.8** to **1.0**. The search samples signed displacements **±1.0 × median original rim-edge length** around current-point, tangent-plane and qualified curvature origins, subject to the previously existing **1.5 × median-rim-edge maximum total moved-vertex displacement** and cumulative 512 proposal / 4 round ceilings. Both mirrored/original rim and q>=0.10 / orientation / cap-local embedding gates are unchanged. Only explicit M19A diagnostic/step-preview paths invoke this search. The ordinary initial staged cap is unchanged when the feature is not enabled.

**Important:** this is *not* a change from `0.5 × closest connected-neighbor distance` to `1.0 × closest connected-neighbor distance`. That nearest-neighbor bound was a proposed future adaptive-search design but is not used in current M19A2/M19A3 code. The separate proposal for independent positive/negative bracketing/bisection also remains unimplemented. Neither the sampled range nor the 512-proposal ceiling guarantees an optimal location; increasing reach may reduce sampling density near an optimum when the grid is finite.

## Manual A/B commands

```bash
# Wider M19A4 candidate actually displayed provisionally:
build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson --repair-feasibility-preview \
  --repair-seam-audit --repair-overlay-exact

# Control: same command and frozen camera removing
# --repair-feasibility-preview. Optional read-only probe replaces it
# with --repair-feasibility-probe.
```

At the `ENSUREFACE` steps around 53 and 54, compare the remaining cyan/green cap kink with and without preview, from matching camera positions. Console `normal_sweep_max_over_edge=1`, `candidate_applied`, quality and discontinuity readings distinguish the search from the rendering. Test both mirrored sides and the full downstream private repair verdict. The user will conduct GPU visual acceptance separately.

## Acceptance boundaries

Pure M19A test covers both signed normal extremes and finite proposal bounds. Existing original archived `refusal-1` M19A3 regression checks candidate staging, original rim/topology, q, and mirror behavior. Three-platform warning-as-error GitHub CI is the code acceptance gate, and the actual new physical Vulkan test is **not yet done**. This trial cannot imply certified whole-mesh closure or authorization for published worker/history/checkpoint state.

## Next independent milestone

M19D1 exposes the deterministic M19C selected **quad-orbit identities** and optional locally admissible alternate-face patch without staging it; M19D2/3 must add explicit default-off PRIVATE staging and source-complete certification before GPU acceptance of topology changes.
