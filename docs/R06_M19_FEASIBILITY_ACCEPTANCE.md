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
