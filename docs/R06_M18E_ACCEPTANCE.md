# R06-M18E — Directional curvature guidance in fixed-rim Poisson construction

**Status: CPU accepted, not visually accepted.** Tested source commit
`03cfb86d211b69786712d9eed4f7f64972da69c0`. GitHub Actions
[#40172858372](https://github.com/logiclrd/Mesher/actions/runs/40172858372)
completed **SUCCESS** on Linux GCC, Linux Clang and Windows MSVC
`/W4 /WX`, **118/118 CPU tests** on all three platforms.
The later documentation-only commits are not the tested code head.

## User-observed problem
After M18D reduced the mirrored `refusal-1.r06case` cap-to-survivor
jump from 156.146 to 53.014 degrees, physical Vulkan inspection of
stepped `EnsureFace` operations around 53/54 still showed a
noticeable non-smooth ripple: a newly added cap face continues in a
different direction from its two neighbors. M18C confirms that
triangle q>=0.10 is not sufficient for a smooth surface.

## Independent opt-in

`--repair-curvature-poisson` is a new default-OFF M18E option
for private R06 live inspection, exact saved replay and stepped replay.
For comparing the same saved geometry as M17D/M18D:

```bash
build/Mesher \
  --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug \
  --repair-scale-aware \
  --repair-front-optimize \
  --repair-curvature-optimize \
  --repair-curvature-poisson \
  --repair-seam-audit \
  --repair-overlay-exact
```

Disable JUST `--repair-curvature-poisson` for the otherwise-identical
M18D control. The stepped cap topology and exact immutable input
remain source-backed, and R01-R06 are entirely PRIVATE/unpublished.

## Mathematical and implementation design

`SpatialCapSettings::curvatureGuidedPoisson` is a separate opt-in.
For each directed CAP boundary edge, the attached original R03 survivor
`FaceId` identifies the same confidence-annotated symmetric curvature
tensor used in M18B/M18C: `k_tt` parallel to the edge, `k_tu`
mixed/twist, `k_uu` cross-edge. Only original surviving faces are
allowed to train the target; no cap surface is taken as ground truth.
Crease-suspected, inconsistent, missing, low-confidence or nonfinite
source tensor data supplies **no false curvature constraint**.

A local edge-frame quadratic height target is computed from the
tensor, with the along-edge contribution `0.5*k_tt*x*(x-L)`
explicitly ZERO at both original edge endpoints, preserving the
original straight mesh edge. Terms `k_tu*(x-L/2)*y` and
`0.5*k_uu*y*y` express mixed and transverse curvature. Its
normal displacement is clamped relative to the median rim edge,
the source curvature magnitude is bounded by local edge scale, and
the source confidence provides a finite constraint weight.

The original M06C Poisson solve first converges with its fixed
Dirichlet R03 boundary. Any opt-in M17D tangential overshoot repair
then establishes a legal q-qualified base patch. **A second,
confidence-weighted Poisson solve** follows, with fixed original rim
vertices, the original graph Laplacian, survivor curvature-guided
normal constraints, and a bounded quadratic positional-fidelity
term retaining M17D's already legal inward-ring topology scale.
This is a change to the Poisson *equations*, not a no-op initial
vertex seed. Mirrored geometry uses Jacobi steps and exact reflected
orbit projection.

The full guided solve is independently bounded (max 128 iterations,
convergence residual within the existing Poisson tolerance).
M18E then evaluates exactly three deterministic strengths: 25%, 50%,
and 100% of the converged correction, with the same **cumulative**
pairwise cap embedding budget; all contact tests count against that
budget, and no extra unaccounted collision retries are permitted.
Any accepted candidate must preserve correct orientation, existing
local edge scale, no cap self-intersection, exact mirror orbits,
strict q>=0.10 on originally healthy caps, nonworsening worst seam
jump and a lower M18C boundary/fairness score. Unreliable/unhelpful
guidance is an unchanged baseline, not a geometry refusal.
The downstream R06 complete original-survivor/cap closed mesh,
classification, contact, manifold and publication gates remain intact.

The opt-in console diagnostic `[repair-r06-m18e]` reports constrained
ring-vertex count, guided Poisson convergence/residual/iterations,
the chosen guidance fraction, before/after first-order score,
orientation/quality/score/contact rejection counts, local initial
q and work-budget outcome. Accepted guided Poisson iterations are
included in R06's total spatial Poisson iteration metric.

## Exact original refusal-1 CPU result

| Metric | M17C+D+M18D (no M18E) | M17C+D+M18D+M18E |
|---|---:|---:|
| Worst initial boundary normal jump | 53.0138084 deg | 52.9699135 deg |
| M18C boundary/fairness score | 0.28684512 | 0.27275902 |
| Minimum initial cap triangle quality | 0.16024114 | 0.11750271 |
| Final original private R06 closed mesh | certified | certified |

For M18E: **14 inward-ring vertices constrained**, second Poisson
solve converged in **23 iterations** (residual about 4.725e-11),
**100% guided correction accepted**, 3 bounded complete local
contact checks with no reported rejection. Subsequent M18D accepted
two bounded correction rounds, preserving certification. The M18E
continuity-score reduction is **about 4.9%** relative to M18D alone,
and the worst dihedral changes only **0.044 degrees**. This is a
real but *small* improvement, far short of a measured or visually
certified smooth transition. The accepted cap q is **lower** than
M18D's q, although the unchanged strict q>=.10 floor still holds.

The archived input and source root are immutable. The regression
requires at least 1% objective improvement over M18D alone, no
increased maximum fold, q>=.10, exact original R03 contour
coordinates, and a fully certified/private final repair.

## Synthetic and guardrail evidence
The `repair_r06_m18e_curvature_guided_poisson` CTest exercises
curved and flat guided surfaces both symmetric and asymmetric,
exact X=0 mirror orbits, unchanged fixed rim and face topology,
curvature confidence/crease no-op, objective monotonicity and
no regression below the old q floor. The curved synthetic tests
explicitly require a **nontrivial accepted geometry improvement**.
The CLI must reject the new option without `--repair auto` or
saved replay. Full CTest: 118/118 across GCC, Clang, MSVC.

## Open work
**M18F remains open.** Inspect step-53/54 normals and local cap
surface in real Linux and Windows Vulkan, compare the same camera
view with M18D only vs M18D+M18E; check whether the residual
52.97-degree fold is actually natural in 3D (it may not be).
Do NOT claim visual smoothness from the 4.9% average score gain.
The original acceptance plan also requires all five archived
cases, healthy sphere/torus/crease/reflected fixtures, and check
that no unintended internal voids or new source collisions survive.
Further local topology changes, boundary-extrapolated tangent
constraints or broader Poisson formulation improvements may be
needed if the ripple remains. M17E physical acceptance remains
open as well; R07/R08 publication is not authorized.
