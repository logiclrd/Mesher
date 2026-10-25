# R06-M19 — Residual fold: fixed-connectivity versus local diagonal feasibility

**CPU-accepted diagnostic code:** `9b14e41ea6c1682834f14cdb716eca23470fe129`.
**CI:** [#40175092711](https://github.com/logiclrd/Mesher/actions/runs/40175092711)
**SUCCESS** on Linux GCC, Linux Clang and Windows MSVC `/W4 /WX`,
**122/122 CPU tests** across each platform. The two probes are
READ-ONLY; this result does **not** certify that a locally flipped
candidate would survive whole source+cap assembly, nor does it
represent a physical GPU acceptance.

## Trigger and objective

The user physically inspected R06-M18D/M18E steps 53/54 for
`repair-cases/refusal-1.r06case`. M18D removed the severe returning
zigzag, but an unnatural local ripple remained. M18E's first-order
worst seam angle changed only **53.0138 to 52.9699 degrees** despite
a small average fairness gain. This is not visually adequate.

M19 separates two causes:

1. **Positional feasibility with fixed connectivity:** can legal
   first-inward-ring movements remove the conspicuous fold?
2. **Topological feasibility with fixed coordinates:** would swapping
   the diagonal of an existing strip quad do substantially better?

Neither finite candidate set is a proof of geometric impossibility.

## M19A read-only position probe

Opt in with `--repair-feasibility-probe`. The pure
`probeSpatialCapFirstRingFeasibility()` in
`include/mesher/repair_cap_feasibility.hpp` tries bounded
interpolations toward the plane of each EXACT source surviving face,
optionally with measured high-confidence directional curvature.
The seam boundary/hub remain bit-identical, first-ring vertices
move only as exact mirrored orbits when applicable, and candidates
must pass the old cap q floor, face-normal orientation and the same
cumulative cap self-contact budget. Candidates are ranked by
worst seam and adjacent cap-face normal discontinuities, *separately*
from the mean score. The counterfactual never replaces the patch.

On the original fully M17D+M18D+M18E corrected `refusal-1` cap:

| M19A property | Result |
|---|---:|
| Proposals attempted | 96 |
| Locally admissible improvements | 8 |
| Accepted candidate rounds | 2 |
| Worst cap/survivor seam angle | 52.9699° → **52.9699°** |
| Worst rim-adjacent cap/cap angle | 129.270° → **124.026°** |
| First-ring mean squared continuity score | 0.272759 → **0.264252** |
| Worst cap mean-ratio quality | 0.117503 → **0.117503** |
| Quality rejected / score rejected | 8 / 80 |
| Cap self-contact refused / budget exhausted | 0 / no |

This finite probe finds small legal position improvements but
**no improvement in the worst seam fold**; it does NOT prove
that more coordinated or further-reaching positional edits are
mathematically impossible.

## M19A follow-up: bilateral survivor-average-normal candidates (CPU accepted)

A subsequent user inspection identified an additional flaw in the
original M19A candidate set: its positional proposals interpolated
toward ONE survivor tangent plane or a nearby curvature target and
could not systematically move **beyond that plane**, inward OR outward
along the average original survivor normal. The candidate set was
therefore unnecessarily restricted even though its points were stored
in 3D. An unrelated non-mirrored branch also incorrectly reflected
the candidate vertex against X=0, which suppressed asymmetric legal
trials.

**Tested source HEAD:**
`5bcdc9a9b88a5fd6d9f8e4b7afb6292e3e3a82c2`.
[GitHub Actions #40187905326](https://github.com/logiclrd/Mesher/actions/runs/40187905326)
**SUCCESS** on Linux GCC, Linux Clang and Windows MSVC
`/W4 /WX`; **122/122 CPU tests** on all three.
The default R06 cap, staged faces, vertex identities, worker root,
history and checkpoints are not modified.

The revised M19A samples THREE reference origins: the current first
ring position, the original exact survivor tangent-plane projection,
and a bounded confidence-qualified curvature continuation target.
For each it tests signed displacements in **both directions** along
the normalized mean of the original survivor face normals. Where
reflection applies, the mean normal is restricted to X=0 and every
candidate is reflected as an exact orbit. Nonmirrored vertices
retain their actual 3D coordinates, without a spurious reflection.
The search uses signed levels up to **0.8 times the median original
rim edge**, still subject to the previous **1.5-edge-scale displacement
limit**, original q>=0.10 floor, face orientation, exact rim/hub,
cap embedding and one cumulative shared collision-work budget.
The number of explicit candidate attempts is bounded by **512**.
Only the diagnostic probe searches this richer 3D set; no actual
closure optimizer or fallback is changed.

On original `refusal-1` with all M17C/M17D/M18D/M18E flags enabled:

| Measurement | Prior M19A (plane-limited) | M19A bilateral normal sweep |
|---|---:|---:|
| Positional candidates evaluated | 96 | 512 |
| Locally admissible improvements | 8 | 7 |
| Accepted correction rounds | 2 | 3 |
| Worst cap/survivor dihedral | 52.9699° | **52.9699°** |
| Worst rim-adjacent cap/cap dihedral | 124.026° | **92.2785°** |
| Mean squared boundary+fairness score | 0.264252 | **0.204093** |
| Minimum cap q | 0.117503 | **0.117503** |

The *actual same-case baseline*, unchanged by either probe, has
worst seam **52.9699°**, worst cap-rim bend **129.270°**, mean score
**0.272759**, min q **0.117503**.

The enlarged 512-candidate run attempted **216 negative and
216 positive** normal-offset proposals. It actually sampled
**221 positive-side and 270 negative-side** positions with respect
to their original local survivor tangent planes; the displacement
range projected on the survivor-average normal was **-0.817449 to
+0.811911 median-edge units**. Of the seven admissible improvements,
**three came from negative normal-offset proposals**, none from
positive proposals. Other candidates were rejected by exact
shape/orientation/fairness gates as designed (8 under-quality,
179 orientation, 318 score rejections; no cap contact collision
or contact-budget exhaustion). The normal projection of the
original worst-fold first-ring position is logged as **+0.127175
median-edge units**; this is an orientation-signed measurement,
not a claim that this direction is universally outward/inward
for all surfaces.

A new regression explicitly requires proposals and actual sample
positions on BOTH sides of the survivor tangent plane, signed
average-normal displacement coverage, deterministic repeat,
nonmirrored position handling and exact original R03 boundary
immutability. The original complete PRIVATE closure remains
unchanged and certified.

**Interpretation:** the 2D interpolation restriction was real and
the additional normal dimension finds substantially better
cap-interior fairing without a topology edit. But the worst seam
angle is unchanged in this finite search. This is not evidence
that further coordinated vertex motion is mathematically impossible,
nor does it supersede M19C's independent and larger diagonal
counterfactual seam improvement. None of these changes have
been staged or visually accepted.

## M19C read-only local diagonal probe

Opt in INSTEAD with `--repair-diagonal-probe`. The pure
`probeSpatialCapDiagonalFeasibility()` in
`include/mesher/repair_cap_diagonal_feasibility.hpp`
tries alternate diagonals of the source-indexed four-corner
rim/inward-ring quads. Reflection requires the EXACT reversed
partner quad to flip with it. The original R03 rim, ring coordinates,
hub, 3-faces-per-quad strip shape and complete indexed face count
never move. The candidate must remain cap-local orientation-valid,
q>=0.10 and self-contact free under the same finite budget.
The original cap and ALL existing worker, history and checkpoint
state are unaffected.

On the SAME original M18D+E `refusal-1` starting patch:

| M19C property | Result |
|---|---:|
| Original mirrored-strip alternatives evaluated | 18 |
| Locally admissible improvements | 2 |
| Accepted candidate rounds | 2 |
| Worst cap/survivor seam angle | 52.9699° → **17.5965°** |
| Worst rim-adjacent cap/cap angle | 129.270° → **81.5775°** |
| First-ring mean squared continuity score | 0.272759 → **0.0886244** |
| Worst cap mean-ratio quality | 0.117503 → **0.128450** |
| Quality rejected / score rejected | 2 / 14 |
| Cap self-contact refused / budget exhausted | 0 / no |

**Crucial conclusion:** local triangulation is a major contributor
to the ripple. Two mirrored legal local strip-diagonal corrections
yield a far larger improvement than additional curvature-guided
Poisson placement, without moving any source or cap vertices.
However, the residual **81.58-degree cap-interior fold** is still
large, and the flipped candidates have **not been assembled and
certified with the surviving source mesh**. These are local
counterfactual diagnostics only; not a repaired or published result.

## Test boundaries

The independent `repair_r06_m19a_positional_feasibility` CTest
checks synthetic flat/folded ordinary and mirrored cap test data,
original-rim preservation, reflected moves, deterministic finite
budgets, no-op when validators refuse, and no mutation of the
original patch. The independent
`repair_r06_m19c_diagonal_feasibility` CTest checks reversible
indexed quad flips, bounded mirrored alternative search and no
source mutation. New CLI tests refuse both flags outside
auto/private replay. The immutable archived `refusal-1` CPU
regression compares the original M18D/E cap byte-for-byte with
each probed result: no vertex, face, quality, checkpoint or
history modification is authorized.

## Reproduce and next milestone

Use one independent probe at a time to preserve the cumulative
local contact-work budget:

```bash
build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson --repair-seam-audit \
  --repair-overlay-exact --repair-feasibility-probe
```

Substitute `--repair-diagonal-probe` for the last argument to
run M19C. Console lines
`[repair-r06-m19a-feasibility]` and
`[repair-r06-m19c-diagonals]` describe the separate probes.
Because no cap topology is staged, the GUI picture remains M18E.

**Next independent milestone (not completed):**
add an explicit default-OFF *PRIVATE* staged-local-diagonal proposal
mode, capture the selected exact mirrored quad ordinals, pass ALL
existing stage/source incidence, whole-mesh collision, manifold,
symmetry and shape gates, and require a strict saved-case CPU
regression. Only then request user Vulkan inspection of the
actually flipped candidate. If the remaining 81.58-degree interior
fold is physically unreasonable, test broader transition-band
retriangulation rather than blindly refining a small sliver.
R01–R06 remain private; M18F/M17E physical visual acceptance
remains open.
