# R06-M24 — Shape-guided zipper postprocessing

The existing R06 private closure pipeline remains atomic and subject to the
same final manifold, reflection, collision, quality (zipper q >= 0.10,
survivor q >= 0.012), face/vertex budget, and edge-length constraints.

## Why the old logic was removed

The prior zipper boundary pre-refinement required every old-survivor/zipper
seam edge to be <=0.5 of the zipper's target edge length. The subsequent
generic longest-edge subdivision also processed zipper faces. These are
density-driven operations: repeatedly bisecting a needle can produce a
worse needle and conforming fans in the healthy original survivor mesh.

M24 removes the boundary pre-density pass and excludes zipper faces from
the generic longest-edge/under-quality subdivision loop. The cap
generator, its density pass, and final zipper/cap hard certification are
unchanged. The legacy configuration field remains source-compatible but
does not affect the result.

## Zipper template eligibility

For each current generated zipper triangle, consider all three oriented
choices of base AB and apex C (a base that looks natural under overhang
need not be the mathematically longest edge).

1. No shape subdivision if the triangle's longest edge is strictly below
   0.8 * targetEdge.
2. Candidate base AB must be greater than 2.5 times the perpendicular
   altitude of C to the AB *line*.
3. If both base angles are <45 degrees, split AB at D halfway between
   the perpendicular foot of C and the midpoint of AB; produce ACD, DBC.
4. Otherwise orient AB so that A is the >45-degree base corner; take
   D=(2*A+B)/3 and E=(B+C)/2. The two legal alternatives are
   (ACD, CDE, BDE) and (ACE, ADE, BDE). Select the alternative maximizing
   the minimum normalized mean-ratio quality; ties choose CD, with equal
   area children.
5. All generated children must satisfy the original hard zipper minimum
   quality; no quality-waiver, mirror-welding, or illegal neighbor edit is
   permitted. Candidate bases are compared by their worst-child quality,
   then flatness as a deterministic tie-break.

## Source-backed application

No new mesh mutation API is needed: the same MixedClosureStaging
splitEdge performs each conforming edge edit, always including its
reflected orbit and the actual adjacent survivor topology.

- Apex-to-base: split AB at D.
- CD alternative: split AB at D, then BC at E.
- AE alternative: split BC at E, then AB at D.

The sequencing itself creates the desired diagonal. Each split is a
complete conforming source-backed edit. The entire closure remains
PRIVATE until final certification; any failed next edit refuses the
whole candidate without publishing a partial mesh.

The [repair-r06-zipper-shape] lines report selected vertex IDs,
template/diagonal, parent and predicted child quality, consumed split
budget, and a summary. Normal M16 step-debug still exposes individual
staging-owned splitEdge operations, not a forged success.

## Acceptance

Positive behavior, not the old bad fan pattern, is the regression
contract. The pure template tests assert the centered split,
mild-overhang CD vs strong-overhang AE, mirror invariance, stable
rotation, strict 80%-size cutoff and original quality floor.

For saved cases, check all five certified private closures; especially
compare refusal-2's worst/p10 zipper quality, inserted vertices,
refinement steps, survivor incidence and generated-face count against
the previous logic. Visual inspection must still establish whether
the repetitive fan pattern disappeared. Do not accept a reduction in
numerical edge length alone as a shape improvement.
