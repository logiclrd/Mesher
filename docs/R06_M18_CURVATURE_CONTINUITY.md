# R06-M18 — Directional curvature continuation at spatial-cap seams

Status: **design/planning**, October 24, 2026. Branch `r06-integration`; continue
existing draft PR #162, **PRIVATE** repair only.

## Motivation and scope
The archived `repair-cases/refusal-1.r06case` R06 cap initially formed a
returning sliver at staging `EnsureFace(3207,5470,provisional)` around
step 53. M17D (`--repair-front-optimize`) moves the inward-ring provisional
vertex and raises worst initial normalized triangle quality from
~0.061107 to ~0.105608; with `--repair-scale-aware` a CPU-certified private
closure succeeds. The physical Linux Vulkan screenshots still show a tight
unnatural surface fold. **Triangle aspect quality is not curvature continuity.**
This project implements the existing open M06C4e curvature/profile intent,
without misrepresenting M17D CPU acceptance as physical acceptance.

## Geometric model
For each **indexed** boundary edge (A,B), let `t` be its unit seam tangent,
`n_s` the surviving-face unit normal, and `u = normalize(n_s × t)` the
cross-seam tangent (choose the inward sign consistently from source
winding). Compare the immediately attached cap normal `n_c` against
`n_s`. The signed edge-axis dihedral is
`atan2(dot(t, cross(n_s,n_c)), dot(n_s,n_c))`. A signed initial-ring
out-of-plane displacement and normalized displacement relative to local
edge length provide diagnostics, **not** an embedding or crease certificate.

For subsequent milestones, fit a symmetric shape operator on a bounded
SURVIVOR neighborhood in the `(t,u)` basis:

    S = [ k_tt  k_tu ]
        [ k_tu  k_uu ]

Keep separately: (1) first-order normal jump across the rim, (2) along-
seam bending, (3) across-seam bending, (4) mixed twist, (5) second-row
interior fairness. Compare cap to measured survivor curvature; do NOT force
curvature toward zero. Normalize comparisons by local mesh scale and
report confidence for undersampling, creases and noisy neighborhoods.
The curvature score must remain distinct from the strict triangle q floor.

## Source identity and ordering
R03 `RepairContour::survivingFaces[i]` corresponds to its directed edge
`vertexIds[i] -> vertexIds[(i+1)%n]`. `stagePrivateMixedClosure` reverses
the contour for CAP winding. Therefore an outgoing CAP edge
`ordered[k] -> ordered[(k+1)%n]` is associated with the source surviving
face on the matching ORIGINAL reversed edge, not naively
`survivingFaces[n-1-k]`. M18A must assert exact shared original endpoints
and resolve this off-by-one explicitly. No coordinate proximity welding.

The stage currently constructs the spatial cap in
`src/repair_mixed_stage.cpp`, calling
`buildSpatialCapFrontPoisson` in `src/repair_cap_spatial.cpp`.
M17D only adjusts its Poisson-SETTLED first inward ring when worst q < .10,
and its search stops upon reaching that q. Initial 55/45 interpolation
does not persist through full Poisson convergence. Diagnostics must not
move vertices, affect existing tests, or change acceptance/refusal.

## Flat independently testable milestones
See `todo.md` R06-M18A through M18F:
- M18A read-only per-edge rim profile and archived exact-case evidence
- M18B survivor-local directional shape-operator estimate, with confidence
- M18C read-only cap/survivor tensor-continuation score and ranking
- M18D bounded reflected interior-only optimization, opt-in and fail-closed
- M18E curvature guidance added to the fixed-rim Poisson objective
- M18F regression, three-platform CI and physical Vulkan acceptance

Never mark subsequent milestones accepted merely because an earlier one
improves the scalar quality number.

## Invariants and acceptance boundaries
Fixed original R03 coordinates/FaceIds and source-backed staged editor;
same exact X=0 mirrored identity and reverse winding; no implicit welding;
cap/zipper q>=0.10 and survivor q>=0.012 (existing narrow certified planar
rim-tip exceptions only); finite candidate and work budgets, self-contact,
whole-index manifold/orientation, no unwanted internal cavity; R01-R06
private and unpublished, original five `.r06case` byte-identical.
Treat intentional creases specially as low-confidence/special-feature
measurements, not global curvature-refusal bugs. No new strict curvature
threshold before synthetic and physical evidence.

## M18A acceptance targets
Run paired original `refusal-1` spatial patch builds with M17D off/on
and report per original survivor rim edge:
IDs, survivor FaceId, valid/missing source normal, signed degrees, absolute
angle, local edge length and out-of-plane first-ring shift. Confirm
unchanged baseline/optimized geometry from M17D tests, no observer side
effects, and deterministic mirrored per-edge magnitude. Synthetic flat
coplanar, hinged, wrong-winding, incomplete guidance and reflection
tests must pass. Record absolute jump as a diagnostic; it does not alone
measure the shape-operator tensor or declare a crease defective.

## Implemented M18B diagnostic reference (CPU accepted)

M18B provides a bounded, source-index-only normal-difference fit of
`S_tt`, `S_tu`, `S_uu` from at most three adjacent survivor face rings
and 48 original faces per boundary edge. The estimator uses a
seam-tangent/cross-seam local tangent frame, a symmetric three-parameter
least-squares solve with bounded robust reweighting, and **explicit
confidence** for too-few samples, deficient geometry, inconsistent
normals, suspected creases, residual outliers or truncation. It is never
an automatic success/failure decision. Its first-order M18A companion
reports the cap/survivor dihedral independently.

See [M18B CPU acceptance](R06_M18B_ACCEPTANCE.md):
code `dbb708d1`, CI #40162861085 success on GCC/Clang/MSVC, 114/114
CPU tests. Opt in with `--repair-front-optimize` in an R06 replay to
print `[repair-r06-rim-profile-edge]` records containing the measured
tensor and confidence. This is source-only reference curvature, **not
yet cap-vs-survivor curvature comparison** (M18C) and not a geometry
correction or physical GPU acceptance.

## Future acceptance
M18F remains open until the user physically inspects the same localized
fold on Linux and Windows Vulkan. M17E continues to cover the broader
M17 family and is not auto-completed by the M18 diagnostics.
