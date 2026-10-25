# R06-M18B acceptance — directional survivor curvature (CPU only)

**Implemented code HEAD:** `dbb708d1596150734a992f1a1323afa9efd6d9b5`
([commit](https://github.com/logiclrd/Mesher/commit/dbb708d1596150734a992f1a1323afa9efd6d9b5)).
**Exact-head CI:** [GitHub Actions #40162861085](https://github.com/logiclrd/Mesher/actions/runs/40162861085),
**SUCCESS** on Linux GCC, Linux Clang, and Windows MSVC `/W4 /WX`;
**114/114 CPU tests** on each platform. No physical Vulkan acceptance asserted.

## Scope

This is **M18B only**, following read-only M18A rim normal/dihedral
diagnostics and preceding read-only M18C continuity scoring. No cap,
zipper or survivor vertex is moved. No candidate quality, collision,
orientation, reflection, finite-work, source-identity, manifold or
publication rule is changed. Exact archived `.r06case` input files remain
unchanged, and worker/history/checkpoint are untouched.

## Implementation

- `include/mesher/repair_survivor_curvature.hpp` introduces a **pure
  local 2x2 symmetric shape-operator fit** from the *original surviving*
  face normals and centroids. The tangent-frame components are
  `k_tt` (along outgoing cap seam edge), `k_tu` (mixed twist), and
  `k_uu` (across-edge tangent, on survivor side).
- Each reference rim edge resolves its *exact* original surviving
  `FaceId`, preserving M18A's reversed CAP-vs-R03 edge ordering.
  `collectSurvivorCurvatureNeighborhood()` traverses only indexed shared
  edges on the **R03 privately excised source index**. It visits at most
  48 faces and at most 3 face-adjacency rings; boundary holes cannot
  contribute removed faces. Traversal is deterministic and stable-ID
  sourced, never proximity-based.
- `estimateSurvivorDirectionalCurvature()` fits the local normal
  derivative with three bounded symmetric 3x3 least-squares solves
  (three-pass Huber IRLS). It reports residual, pivot-ratio, sample
  count, missing/degenerate faces and diagnostic confidence. Confidence
  may be low for missing seed, insufficient neighborhood, rank-deficient
  support, inconsistent normals, suspected crease, large residual or a
  truncated traversal. The confidence score is **heuristic and
  uncalibrated**, not a new acceptance gate.
- Opt-in R06 spatial-cap console profiles enabled by
  `--repair-front-optimize` now append per-edge
  `curvature_status`, `k_tt`, `k_tu`, `k_uu`, and sampling diagnostics
  to existing `[repair-r06-rim-profile-edge]` output. Exceptional
  diagnostic errors are logged as `diagnostic_unavailable`, not repaired
  by unbounded fallback or translated into geometry refusal.

## Validation

- New isolated CPU test
  `repair_r06_m18b_directional_survivor_curvature` covers flat,
  cylindrical, sphere-like, saddle and twisted analytic normal fields,
  signed reflection parity (mixed component sign), missing neighbor
  samples, collinear rank deficiency, suspected creases, reversed
  normals, bounded truncation and deterministic repeats.
- Existing `repair_r06_m17c_opt_in_real_shape_density_handoff`
  test additionally checks the exact original `refusal-1.r06case`
  M18B neighborhood reference is the surviving `FaceId` for each cap
  edge. It enforces bounded finite results and prints per-edge
  `(3207,5470)` tensor/confidence diagnostics alongside old M18A
  baseline-vs-M17D comparisons; no invented success threshold based
  on those measurements.
- GCC/Clang/MSVC all compiled and executed **114/114 tests**; full CI
  workflow green on all three platforms at the implemented code HEAD.

## Limits and the next milestone

A triangle's discrete normal field is only a sampled approximation to
smooth curvature. The current estimator can be unreliable at irregular
high-curvature areas, sharp sculpt creases or insufficient survivor
neighborhoods; these are explicit *uncertainty signals*. This milestone
does NOT say whether a particular cap matches the survivor curvature,
does NOT certify the visual fold in `refusal-1` as fixed, and does NOT
change M17D's quality-driven vertex relocation or R07/R08 publication.
**M18C** must use these survivor references plus M18A's cap-edge profiles
to score first-order normal jump and directional cap-vs-survivor
continuity without altering geometry. M18D/E are future opt-in
construction improvements; M18F and broad M17E real Vulkan
acceptance remain open.
