# R06-M19A6 — fivefold signed-normal range, preserving near candidates

## Trigger and interpretation

The user observed another visual improvement on an exact `refusal-1.r06case` step-54 Vulkan screenshot with M19A5 (signed offset range ±1.6 × median original rim-edge length), but a residual fold remained. The request is to extend the reach by another **factor of five**.

**Critical correction:** the M19A search is not a binary search. The current implementation evaluates a bounded **finite sample grid** of proposed vertex positions, ranks cap/survivor seam and cap/rim discontinuity, and admits only locally legal candidates. An arbitrarily wide bracket does *not* automatically contract back to the best region. Normal discontinuity is not necessarily signed, monotone, or unimodal along the sampled direction; distant locally smooth surfaces could be physically undesirable.

## Isolated CPU experiment

| Setting | M19A5 | M19A6 |
|---|---:|---:|
| Normal sweep maximum per sign (median rim-edge units) | ±1.6 | **±8.0** |
| Sampled distances along each normal sign | 0.8, 1.6 | **0.8, 1.6, 4.0, 8.0** |
| Maximum overall moved-vertex distance (edge units) | 3.0 | **15.0** |
| Finite search proposal budget | 512 | **1,024** |
| Maximum accepted rounds | 4 | **4** |

The expanded guard ensures the ±8 outer positions can be genuinely tested rather than rejected by the old 3-edge displacement check. Preserving the original ±0.8/±1.6 bands ensures that widening the range does not *inherently* eliminate the previous small-displacement candidates. To pay for additional discrete proposals, the finite limit is increased proportionally (but the existing cumulative cap self-contact workload limit is **not** relaxed). No claim of logarithmic binary-search cost is made.

The algorithm still tests original current-point, tangent-plane and confidence-qualified curvature origins with finite fractions, both signs and exactly mirrored orbits where required. It does not move original R03 boundary or center-hub vertices, does not change face connectivity, and retains unchanged q>=0.10, orientation, embedding checks and source-complete downstream PRIVATE certification. Default cap building without explicit feasibility flags remains unchanged; R01–R06 repairs still cannot publish to worker/history/checkpoints through this feature.

## Observable coverage and regression

The pure M19A feasibility CPU regression now requires actual admitted-to-trial samples from *both signs of all four bands* and signed extrema beyond ±7.95 edge lengths. It also checks deterministic repeat, bounded proposal count, original patch identity, and no worsening of the measured local seam/cap discontinuity. The console `[repair-r06-m19a-feasibility]` line reports `positive_sweep_bands=` and `negative_sweep_bands=` with four comma-separated counts in ascending 0.8,1.6,4,8 order.

This coverage checks generated **candidate positions** before cap quality/contact admission. It does NOT prove any ±8 proposal is healthy, selected, whole-mesh certified or visibly better.

## Reproduce

```bash
build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson --repair-feasibility-preview \
  --repair-seam-audit --repair-overlay-exact
```

Inspect unchanged camera orientation around `EnsureFace` steps 53–54 and the reflected side. Compare the normal-sweep extent and four band counts against M19A5, `candidate_applied`, the peak seam/cap dihedrals, minimum cap q, and the downstream private repair result. User physical Vulkan acceptance is separate from three-platform CI.

## Pending next design decision

If the expanded finite grid plateaus or produces unnatural large motions, do **not** merely expand without limit. A separate, explicitly planned bilateral one-dimensional adaptive search could optimize signed directional curvature residuals where bracketed, or bounded nonlinear minimization with coarse-to-fine sampling and local full-patch validation where residuals are not monotone. It should retain healthy-fast-path, exact mirrored orbits and finite budgets, and must not equate a locally reduced dihedral with true curvature continuity or valid surface embedding.
