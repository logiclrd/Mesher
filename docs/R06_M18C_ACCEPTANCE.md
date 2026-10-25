# R06-M18C acceptance — pure cap/survivor curvature-continuity evaluator

**CPU-accepted code HEAD:** `38c080a643a95af0c5bacd3c8990f0294aaf6dce`.
**Three-platform CI:** [#40168106796](https://github.com/logiclrd/Mesher/actions/runs/40168106796) **SUCCESS**,
Linux GCC, Linux Clang, Windows MSVC `/W4 /WX`; **115/115 CPU tests**
on each. The subsequent documentation-only commits are not part of this
precisely verified code snapshot. No physical Vulkan acceptance claimed.

## Motivation and case evidence
The user's M18B physical replay of original `refusal-1.r06case` with
`--repair-front-optimize --repair-scale-aware` shows **two mirrored
cap/survivor discontinuities of 156.146211502 degrees**, at original
indexed rim edges `(3207,5470)` and `(5469,2688)`. Their first-ring
edge-foot parameters are 1.243375 and -0.243375 respectively, while
the corresponding survivor neighborhood maximum normal deviation is
about 23.18 degrees. Most remaining rim edges have much smaller
dihedral angles. Two of the affected M18B tensor estimates report
`reliable` confidence status but only **0.24321 confidence score**;
other neighborhoods report `inconsistent_normals`. These values
are measured evidence, not grounds to invent a trusted smoothness rule.
The M18C archived exact-case CPU test independently reproduces the
mirror-paired **>150-degree** first-order discontinuity after M17D.

## Implemented pure evaluator
`include/mesher/repair_cap_continuity.hpp` builds M18C on the
existing M18A `measureSpatialCapRimProfile` and M18B
`estimateSurvivorDirectionalCurvature`. It uses exact indexed
survivor references in reversed R03 cap-rim ordering, never closest
geometric matches. Cap-side normals and fitting neighborhoods are
sampled solely from actual local CAP face indices/edges with
deterministic bounded adjacency traversal.

Each rim edge reports separately:
- Boundary first-order absolute normal jump (radians; degrees logged).
- Interior CAP/CAP maximum first-ring adjacent dihedral as a local
  fairness indicator.
- CAP-side shape operator `k_tt,k_tu,k_uu` in an edge-aligned
  tangent frame, with the survivor tensor kept independently.
- When both tensors were measured: signed directional deltas
  `h*(k_cap-k_survivor)` for `tt,tu,uu` and their symmetric tensor
  magnitude `sqrt(dt^2+2*dm^2+du^2)`, normalized by exact rim edge
  length `h`.
- Explicit `directional_measured` and `directional_qualified` flags.
  Qualified means BOTH fits have reliable status and confidence score
  >=0.5, with cap/survivor normal jump <=60 degrees. The 156-degree
  folded edges MUST remain visible as first-order defects but their
  directional curvature deltas are **not trusted as comparable
  second-order shape tensors**.
- A read-only `boundary_fairness_score = boundary_jump_rad +
  0.25*interior_fairness_rad`, and a **separate** per-qualified-edge
  additional tensor mismatch. Partial/fully qualified score totals
  should not be compared across candidates with different valid
  directional masks.

No single scalar certifies geometric quality, and no curvature
threshold is a strict admission/refusal gate. Intentional high
curvature or creases are not flattened or rejected by this work.
The legacy `q>=.10` cap/zipper and `q>=.012` survivor floors (with
existing certified planar tip exceptions) are unchanged.

## Wiring and regression
Opt-in R06 automatic/replay spatial-cap diagnostic sessions using
`--repair-front-optimize` append summary
`[repair-r06-continuity]` and per-edge
`[repair-r06-continuity-edge]` records to the existing M18A/B
`[repair-r06-rim-profile-edge]` output. A diagnostic exception
becomes `diagnostic_unavailable`, never a geometry refusal.
No Poisson, provisional position, topology, reflection partner,
collision/embedding, finite-work or publication contract was changed.

New separate CTest
`repair_r06_m18c_cap_curvature_continuity` tests:
- Flat matching normal/tensor zero mismatch.
- One inward-ring point deviating from a flat survivor, ranking
  worse than the smooth patch despite no acceptance logic.
- Independent seam, twist and transverse normalized tensor deltas.
- Exact reflection-invariant magnitudes and consistent mixed parity.
- Low-confidence crease-like survivor reference excluded from
  directional ranking, missing normals, invalid directed cap edge,
  finite cap-face budget and deterministic evaluation.
Existing `repair_r06_m17c_opt_in_real_shape_density_handoff`
additionally checks exact immutable `refusal-1` before/after M17D:
full rim census, full original source identity already tested by M18A/B,
and the surviving two >150-degree mirrored folds. All 115 tests
passed on GCC, Clang and MSVC.

## Remaining boundary and next work
This milestone makes visible the precise failure that M17D's
triangle-quality-only correction did not solve. It does NOT move
cap vertices, change the q threshold, prove convergence of a
curvature-aware solver, or certify the cap's visual appearance.
**M18D** may introduce opt-in bounded, reflection-paired interior-ring
position proposals scored with M18C, while retaining immutable R03 rim,
whole closed indexed manifold, normals/winding/embedding, strict quality
floors, finite budgets and private-only all-or-nothing results.
M18E is future Poisson-guidance integration; M18F and M17E physical
Windows/Linux Vulkan inspection remain open.
