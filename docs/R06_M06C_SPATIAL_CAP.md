# R06-M06C — Nonprojective advancing-front / Poisson private caps

**Status: local CPU acceptance complete for C1–C3; C4 full-mesh/private acceptance remains OPEN.** [Actions #39806840808](https://github.com/logiclrd/Mesher/actions/runs/39806840808) passed **75/75 registered CTests on Linux GCC, Linux Clang and Windows MSVC**. In the isolated spatial test, the actual 32-vertex recorded rim produced a non-self-intersecting candidate with initial minimum triangle quality **0.190579**. Without the original incident survivor faces that is NOT a whole-object collision certificate.

**Status:** stacked implementation in [PR #147](https://github.com/logiclrd/Mesher/pull/147), based on the R06-M06B draft. The full C1–C4 acceptance sequence was recorded in `todo.md` before changes. This is a research-backed **bounded initial backend**, NOT an implementation of every optimization in Zhao–Gao–Lin's 2007 method and NOT physical GPU signoff.

## Specific physical problem and fixtures

Captured Linux R06 outputs (2026-10-22):
- Two six-vertex rims pinched off unexpectedly: V15 projected-area planner returned `status=4` before geometric reconstruction. Both exact contours are stored in `repair_auto_plan_tests` using synthetic independently closed neighboring faces (not the original saved mesh).
- Two symmetric 18-vertex warped loops were correctly filled with the old R05 builder (48 green faces; minimum quality 0.208). One exact 18-vertex contour is stored in the new spatial backend tests. Preserve the legacy simpler path.
- A single very long 32-vertex curled rim was refused by all R05 planar projection retries (`contour projection overlaps itself`). The **exact 32 ordered positions** are a spatial-backend regression; stable source FaceIds alone do not reconstruct the original incident faces, so success in a local test cannot prove collision-free full-mesh repair.
- A deliberately crossing ring must not generate a certified cap.

## C1: planning does not certify physical construction

For independent single or provably unlinked rims, V15's projected area is advisory. When it cannot estimate a finite positive area, use a finite perimeter-based approximate comparison (`perimeter^2/(4 pi)`) **only for bookkeeping** and submit each cap to M04. This must never grant authority to zip an unrelated pair or to bypass embedded-quality and manifold audits. Two-loop regions with authoritative contact constraints retain conservative ambiguity refusals.

## C2: fixed local front staging

The original R05 projected ear clipper runs first and is completely unchanged.
Only on its finite projection refusal, construct an alternate spatial cap:

- Reverse the authentic directed R03 rim to preserve orientation. The true boundary coordinates are never moved.
- Build one inward **3D advancing-front ring**, triangulating the oriented strip and closing the remaining central front with a hub. The ring starts as a 0.55/0.45 boundary/centroid interpolation. This is a deliberately minimal front and may fail on deep folds; it does not assume a global simple orthogonal projection.
- New ring and hub vertices carry temporary *private-only* IDs. On a successful staged plan, M04 allocates immutable monotonically increasing IDs in its `MutableMeshView` and substitutes them before conforming edge splitting. No stable ID can be recycled.
- Check local triangle area, extreme edge factors, and all front-front triangle pairs using the production triangle-contact predicate. Reject intersecting/ambiguous cases, without allowing a projected crossing to become a fake accepted cap.

## C3: bounded normal-guided discrete Poisson

Use the oriented adjacent surviving face normal at each original rim vertex; interpolate those normals through the new ring to the hub. With a fixed Dirichlet boundary, solve a sparse uniform graph Poisson system by bounded deterministic Gauss–Seidel relaxation (maximum 256 sweeps, residual at most `10^-9 * median_rim_edge`):

```
degree(i) * x(i) - sum(interior neighbors j) x(j)
  = sum(boundary neighbors j) x(j) + 0.04 * edge_scale * degree(i) * guided_normal(i)
```

This is an inexpensive **normal-biased fairing approximation**, not the complete original paper's gradient estimation/optimization. It improves the spatial patch candidate but does NOT mathematically prevent self-intersections or skinny faces. No arbitrary move of an original boundary vertex is allowed.

## C4: actual mesh certification

After assigning permanent private IDs, M04 uses its shared conforming refinement and unchanged hard limits (`cap q>=0.10`, surviving triangles `q>=0.012`, cap length <=2.5 local edge factor, 512 refinement steps, finite new IDs/faces). Then:
1. Restore the full private COW root with all original R03 tombstones.
2. Run the same globally closed oriented manifold/vertex-fan audit.
3. If the spatial fallback was used, run a second bounded production triangle-pair query seeded by **every resulting green face**, against the entire final mesh (not only the patch). Any new contact or ambiguous/incomplete query aborts the private result.
4. Run M04C's normal reliable inward-component classification/retirement on the accepted closed candidate.

A valid contour can still be unfillable by this first spatial front, or unable to meet q>=0.10 after conforming refinement. The correct outcome is `AUTO_REFUSED` with the actual R03 gold open rims, untouched worker history/checkpoints and no manufactured blue/green faces. Never weaken quality merely to force a green HUD.

## C4a: optional mesh-density balancing after safety refinement

When a spatial fallback is used, M04's original hard cap limit remains
**2.5 times** the local median contour edge, but a new *soft*
target seeks **1.5 times** that edge. Once required M04B refinement
is complete, it can spend at most 128 of the remaining 512 conforming
steps on overlong cap edges. It preflights the triangle quality on **both**
sides of each split (caps and zippers >= 0.10, survivors >= 0.012),
retains the old rim coordinates, and SKIPS density optimization if the
conforming step or face budget would be exhausted. Exceeding the soft
target never causes refusal; the original hard triangle/edge audits and
whole-mesh collision search remain the final certificate.

Physical console reports:
`spatial_density_splits`,
`spatial_density_remaining_edges` (number of cap-edge occurrences
still longer than 1.5 times their per-face target), and
`spatial_max_final_edge_ratio`.
Non-spatial caps retain identical M04 behavior.

## Next quality improvements and verification

- [ ] Confirm full GCC/Clang/MSVC CPU CI and the acceptance of the new six/18/32 recorded contour fixtures.
- [ ] Evaluate multiple local front insertions and robust legal edge-retriangulation for severe folds; a single inward ring may fold and rightfully refuse.
- [ ] Repeat on physical Linux and Windows GPU with the actual curled rim and hollow-sphere mouth, checking green faces, internal views, fully bounded work and no remaining self-contact.

Only when these items pass can M06C and then the broader R06-M06 be marked complete. R07/R08 live publication is not authorized by this private implementation.
