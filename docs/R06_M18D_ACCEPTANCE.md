# R06-M18D — curvature-aware cap-front optimization acceptance

**CPU-accepted code:** `2810f12a3d5a0690c3fae7db1770e86ef3a91bdd`.
**CI:** [#40170070842](https://github.com/logiclrd/Mesher/actions/runs/40170070842),
**SUCCESS** on Linux GCC, Linux Clang, and Windows MSVC with `/W4 /WX`;
**116/116 CPU tests** on all three systems. This is NOT a physical
Linux/Windows Vulkan visual acceptance.

## Motivation and measured improvement

M18A/B/C identified a mirror-paired 156.1462115-degree cap/survivor
dihedral on `refusal-1.r06case` edges `(3207,5470)` and
`(5469,2688)`, despite M17D's satisfactory triangle q=0.105608.
The initial M18D experimental correction performs **two certified
mirrored-ring rounds**, obtaining:

| Metric | M17D only | M17D + M18D |
|---|---:|---:|
| Worst initial cap/survivor seam dihedral | 156.1462115 deg | 53.0138084 deg |
| First-stage minimum cap mean-ratio q | 0.10560836 | 0.16024114 |
| Mean squared M18C boundary + interior normal-fairness objective | 1.25283899 | 0.28684512 |

The entire `refusal-1` **PRIVATE certified R06 closure** also passes
with M17C, M17D and M18D all enabled. The source root, edit history
and checkpoints remain unchanged. A lower score and a 53-degree
remaining seam DO NOT automatically imply visually perfect
curvature: the user must inspect the result.

## Opt-in replay

Build the branch and run:

```sh
build/Mesher \
  --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug \
  --repair-scale-aware \
  --repair-front-optimize \
  --repair-curvature-optimize \
  --repair-seam-audit --repair-overlay-exact
```

`--repair-curvature-optimize` is **independent and off by default**.
It is a PRIVATE R06-only knob available in interactive/live R06,
ordinary saved replay, and step-debug replay. For this original saved
case, enable M17D first to raise starting initial quality above the
unchanged q>=.10 hard floor; M17C remains necessary for complete
certified refinement. With M18D alone and an under-floor initial
cap, it deliberately leaves the cap unchanged rather than bypassing
M17D's repairs.

Search diagnostics are printed as `[repair-r06-m18d]`, including
proposal count, accepted correction rounds, initial/final energy,
maximum boundary-normal fold, minimum initial q, no-op state and
search-budget exhaustion. The separate M18A/B/C rim-profile and
`[repair-r06-continuity(-edge)]` lines describe the candidate actually
staged for GUI stepping.

## Search and invariants

The algorithm operates on the converged Poisson surface, after any
M17D legal quality correction. It explores a bounded set of
first-ring inward-point proposals, rotating opposite vertices about
original cap rim edges toward their observed survivor normals and
correcting limited along-edge overshoot. Exact X=0 reflected partner
vertices move together, without coordinate welding.

The global finite budgets are **4 rounds and 40 nontrivial trials**
(default); all proposed geometry remains subject to the same
cumulative pairwise triangle-contact work budget as M17D. Each
proposal must pass its actual patch: normal-orientation preservation,
nondegenerate triangles, every cap face q>=0.10, local edge-scale
bound, no cap self-intersection, mirror geometry, and no significant
new fold on formerly well-aligned rim edges. Downstream R06
still validates the whole original/survivor/cap mesh; failure there
remains a refusal rather than accidental publication.

The movement objective is M18C's **first-order seam-normal** and
**first-ring interior cap-normal fairness** evidence, using their
separately measured values rather than zero-curvature flattening.
M18B source and cap directional tensor measurements remain available,
but the original refusal-1 has only 2/18 qualified tensor comparisons:
the remaining unqualified tensors are **not assigned artificial
authority as optimization targets**. This is a deliberate bounded
M18D first-order experiment, not an M18E curvature-guided Poisson
solver or complete second-order curvature-matching guarantee.

## CPU regressions and outstanding visual acceptance

- The immutable exact case test compares M17D-only against M17D+M18D
  and now demands **worst fold below 70 degrees**, **>=50% reduction**
  in the combined M18C objective, all patch faces q>=.10, exact
  original rim coordinates and exact mirrored inward-ring partners.
- Tests also require the complete M04C certified private closure,
  and ordinary M17D/default runs are unchanged.
- A CLI regression rejects the opt-in flag without auto/replay.
- The existing 115 tests remain green, plus one new CLI contract
  (total 116).
- **Open:** physical Vulkan inspection at first-ring step ~53 and the
  following step; check actual visual local flow and any residual
  interior folds. The remaining 53-degree peak may still motivate
  further M18D tuning, connectivity alternatives, or M18E
  curvature-guided Poisson construction.
- **Open:** M18E and M18F, including all five saved cases and other
  fixtures under physical Linux/Windows GPU inspection. No R07/R08
  publication or history mutation is authorized.
