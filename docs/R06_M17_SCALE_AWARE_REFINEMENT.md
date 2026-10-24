# R06-M17 — Scale-aware, bounded, shape-preserving closure refinement

**Design decision captured 2026-10-24.** This is a *new staged engineering plan*, not a declaration that existing R06 repair has changed or that the refusal-1 regression is solved. Retain the current M16 step debugger, M15D witness labels, all original `.r06case` bytes, exact reflection requirements, strict quality thresholds and unpublished/private repair semantics.

## Motivation and observed failure

The `refusal-1` Vulkan step debugger showed a thin folded-back triangle created during spatial cap-front staging (around staging operation 53), with provisional-to-permanent ID resolution around step 92. Repeated forced midpoint edge splits of this original thin face and neighboring survivor produce alternating, increasingly narrow cap/survivor children. Apparent geometric T-junctions in the raster may be a very narrow intervening face rather than missing topological incidence. Confirm with exact 3D IDs, effective source overrides and seam topology; do not infer manifoldness solely from pixels. The core defect originates in cap-front construction (ring placement/topology) and refinement currently magnifies it. Both parts need independent corrections.

A midpoint edge split is a sensible **density** operation for healthy triangles expanded during brush strokes; it is not a general **shape-repair** operation for a needle with a pathological angle or a folded surface. A midpoint may sometimes improve minimum child quality, but cannot generally repair the original acute boundary corner or unfold the cap. Flips, edge collapses, and bounded vertex relocation can materially change shapes; such edits are intentional in brand-new approximate sculpt repair, subject to manifold/winding/reflection safeguards.

## Scale-aware decision order

Let `L_t` denote the local existing target edge length (the closure executor currently uses each face's `targetEdge * maxEdgeFactor` for its hard length test). Let `L` denote maximum relevant local edge length, and `q` normalized triangle quality. Keep existing `minSurvivorQuality`, `minCapQuality`, `minZipperQuality`, hard length admission, finite orbit/vertex/refinement budgets and reflection policy.

1. Diagnose BOTH deficits at once: oversized edges and under-quality faces, including the actual two incident effective faces (original survivor overrides and generated triangles), and complete reflected orbit when applicable.
2. **Coarse geometry**: if the local scale is substantially above target, permit a limited number of conservative density splits to shrink shape-changing operations' absolute displacement. A provisional policy is `L / L_t > 2`, **not** yet a production constant. The cap is per local patch/event; do not interpret it as a license for hundreds of subdivisions.
3. **Shape correction**: once inside the coarse-scale band, or if another subdivision would worsen the worst local quality / repeat a sliver, prefer permitted diagonal flips, carefully guarded collapses and constrained new-vertex repositioning. A hard displacement/travel constraint should be expressed relative to target size; no speculative collapse across an arbitrary large survivor patch.
4. **Final density**: subdivide remaining oversized *healthy* geometry, checking complete child quality and original survivor incident faces BEFORE accepting each edge mutation. Reassess shape/density locally after each successful topology change.
5. When a proposed operation cannot improve shape/density safely within finite work, emit an informative deterministic refusal or an explicitly untrusted warning-preview; never silently relax q, publish a partial result, or exhaust the budget on a non-improving split cascade.

The current R06 ordering makes `improveFairing()` and `improveQuality()` conditional on `largestRatio <= 1`. This can prioritize a density split for a triangle that is BOTH oversized and exceptionally skinny. Replace with a scale-aware policy only after a separate read-only planning milestone and positive regressions establish a safe selection and bounded fallback.

## Split location is a decision, not an invariant

For canonical edge endpoints `A,B` and an incident third vertex `C`, use
`M(t)=A+t*(B-A)`, `0<t<1`, and projection parameter
`t_perp=dot(C-A,B-A)/dot(B-A,B-A)`.
Midpoint is `t=0.5`; a foot-perpendicular split is only a candidate when the projected foot lies *strictly inside* the edge and sufficiently far from its ends.

**Joint neighborhood:** a manifold edge belongs to up to two incident faces, whose third vertices generally project to DIFFERENT points. Score minimum child quality over BOTH incident sides, include neighbor geometry and reflection orbit, ensure original-survivor child quality never falls below its required floor; don't optimize one cap face independently from the survivor attached on the other side. A flipped endpoint ordering maps `t` to `1-t`; reflected edges require corresponding mirrored XYZ and a consistent canonical parameter, never different ad-hoc per-half choices.

## The user's hybrid diagram and bounded trajectory

A pure foot-perpendicular rule can keep slicing close to the opposite vertex, producing a geometric progression of tiny triangles and a zigzag chain. The **hybrid template** first splits the long base at its midpoint, then *on the relevant child interval* considers one quality-aware off-center or perpendicular split. This yields three triangles for one original face with **two serial split operations**, not three unbounded recursive splits. Importantly, a shared edge and reflected orbit may require more total faces/vertices; count complete conforming operations, not merely drawing strokes.

A bounded planner evaluates, at minimum, these alternatives:
- `Midpoint`: one balanced density split.
- `OffCenter`: one clamped/projection-derived quality-aware split, permitted only with a minimum segment fraction and positive neighborhood merit.
- `Hybrid`: midpoint followed by at most one corrective split of one child subedge, re-evaluating both adjacent effective sides and the entire reflection orbit BEFORE each operation. If the first mutation invalidates the second candidate, replan/refuse; never assume atomicity across unpreflighted edits.

Do **not** blindly maximize immediate child quality. Evaluate the *resulting worst quality*, remaining excessive scale, minimum child edge length, proximity of repeated projections to interval endpoints, and risk of another same-pattern split. The future policy must bound number of descendants, split depth, inserted vertices, and total work. A slightly better immediate `q` that induces another sliver chain is inferior to bounded improvements. If neither pattern improves the worst local pathology, choose shape repair or refusal, not subdivision.

In particular a split on AB does not change the corner angle at A between AB and AC. A hybrid can make the intermediate mesh easier to process without being able to eliminate the original needle. It is NOT an automatic substitute for upstream cap-front correction.

## Spatial front root cause (separate, earlier operation)

`buildSpatialCapFrontPoisson()` currently seeds an inward ring by interpolation toward a global centroid and chooses mirrored quad diagonals using a reflection-side rule; these can form fold-backs on concave/nonplanar rims. A front with q barely above `1e-8` may be admitted into a downstream phase whose cap floor is `0.1`. Before staging, evaluate alternative mirrored quad diagonals, ring placements and fold/dihedral constraints over the entire patch. Do not assume a non-self-intersecting spatial cap is well-conditioned.

## Implementation boundaries (independently observable)

- **M17A**: pure, finite read-only split-template planner/evaluator with canonical `t`, perpendicular-foot bounds, midpoint/off-center/hybrid proposals, two-sided quality scoring, explicit local scale, meaningful progress and hard work/segment-size constraints; deterministic CPU tests (skinny diagram, near-endpoint zigzag, two unequal opposite-vertex projections, healthy large triangle, mirrored orientation, impossible case). No geometry mutation.
- **M17B**: stage-editor parameterized conforming edge split with complete mirrored orbit preflight (all incident survivor/cap/zip children, remote XYZ duplicate, lineage, observer operation/argument/XYZ), preserving old midpoint overload and normal defaults. Positive COW mesh tests.
- **M17C**: bounded hybrid executor + scale-aware refinement priority. Jointly evaluate the first and *replanned* second split; finite per-patch operation/descendant budgets, crossover to flips/collapse/fairing; no zero-progress loop. Exact saved-case replay equivalence when disabled; refusal provenance and stable symmetry when enabled.
- **M17D**: upstream spatial-cap candidate evaluation preventing premature skinny fold-backs, including mirror-consistent diagonal candidates and geometric validation. Diagnose step-53 3D and preserve all successful archived cases. Can be evaluated before M17C if direct root-cause experiments show greater benefit.
- **M17E**: three-platform warnings-as-errors CI and physical Linux/Windows Vulkan before/after inspection, including `refusal-1` (primary), `refusal-3`, torus, symmetric closure, pre-existing successes and no-regression publication. Never mark actual GPU success from headless CPU tests.

The existing M16D physical acceptance and M16B forced R04/R05 shared-executor migration remain distinct open work. Do not create a deep TODO hierarchy; split broad tasks into independently completable flat peers.
