# R06-M19A5 — double the original normal-search extent

**Trigger:** On the user's Vulkan screenshot from the prior `normalSweepEdgeScales=1.0` trial, the cap hook remains visible at step 54. The user clarified that the intended experiment was to **approximately double** the original search distance rather than increase the previous `0.8` coefficient to `1.0` (a 25% expansion).

## Controlled change

Within `SpatialCapFeasibilityLimits` in `include/mesher/repair_cap_feasibility.hpp`:

| Parameter | Original M19A2/M19A3 | M19A4 | M19A5 |
|---|---:|---:|---:|
| `normalSweepEdgeScales` | 0.8 | 1.0 | **1.6** |
| `maximumDisplacementInEdgeScales` | 1.5 | 1.5 | **3.0** |
| `maxProposals` | 512 | 512 | **512** |
| `maxRounds` | 4 | 4 | **4** |

Both extents use **median original rim-edge length**, not the nearest connected-neighbor distance. The second limit was doubled deliberately: the original 1.5-edge displacement gate could silently clip the newly requested ±1.6-edge current-point normal samples or longer moves measured from the tangent-plane/curvature reference origins. This is a finite wider **sample grid**, not the separately discussed positive/negative bracketed binary search, nor the proposed distance bound of `0.5 * min(connected-neighbor distance)`.

The normalized mean survivor normal (with exact X=0 reflection projection when required), three reference origins, candidate fractions, immutable R03 rim and hub, unchanged cap q>=0.10, winding, mirror orbit, finite contact-work accounting, and downstream strict source-backed PRIVATE whole-mesh validation remain unchanged. The default cap construction without M19 diagnostic/preview flags remains unchanged. This experiment does **not** grant permission to publish to root/history/checkpoints.

The pure feasibility regression now checks that a ±1.6-edge current-point sample really survives the old 1.5-edge cap in **both directions**, that the new separate bound is 3.0 edges, and that the finite/probe/baseline immutability and no-worsening constraints still hold.

## Visual test

Use the unchanged M19A3 opt-in step-debug command:

```bash
build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson --repair-feasibility-preview \
  --repair-seam-audit --repair-overlay-exact
```

Inspect the `ENSUREFACE` around steps 53/54, mirrored partner, and complete private verdict. Console field `normal_sweep_max_over_edge=1.6` distinguishes this new binary from the prior `1.0` experiment; also record `candidate_applied`, `min_normal_delta_over_edge`, `max_normal_delta_over_edge`, cap/seam discontinuity, and quality. Compare the same view/flags against a build from M19A4 or its screenshot, and optionally against no preview.

**Status:** code and unit coverage pushed for CI. Physical Vulkan comparison and strict whole-mesh visual acceptance remain open. Bigger excursions are permitted to be evaluated, not assumed to improve the patch; candidates still must meet all gates. The separately implemented M19D private diagonal preview remains independent and unchanged.
