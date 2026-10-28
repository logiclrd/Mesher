# R06-M08 — Experimental proximity classification

**Default unchanged:** `--repair-classifier area`, selecting the current
V15 area-DP heuristic with its ordinary same-owner and contact thresholds.

**Optional test:** `--repair-classifier proximity`, selecting the original
proposed two-rim metric (with the agreed arc-length sampling refinement).
This is an experimental M03 *topology decision* only. R01 trusted contact
pairs, R02/R03 source and cut, region/loop ownership, M04 finite face
quality >=0.10, manifold, nonintersection, exact X=0 symmetry, and private
worker/history/checkpoint nonpublication are all unchanged.

## Calculations

For each ordered (possibly non-coplanar) rim, translate every position by
the arithmetic centroid for numerical stability. Compute
`area=0.5*norm(sum(cross(p_i-centroid,p_{i+1}-centroid)))`.
This is Newell's vector area: maximum magnitude of *signed* projection
area, not the least-squares/PCA best-fit-plane polygon area. Highly folded
loops can exhibit cancellation; the result is deliberately a cheap
classifier estimate, not a legal cap area proof.

Each source rim vertex is matched to its nearest OTHER RIM VERTEX in 3D,
in both directions. Its weight is half the sum of the lengths of its two
incident ring edges. Pool `sum(weight*nearest_distance)/sum(weight)`
across BOTH rims. The weights are proportional to the fraction of rim
arc-length represented by a vertex, mitigating uneven adaptive sampling;
they do NOT make the estimate exactly invariant if new vertices themselves
change the nearest-vertex distances. The operation is bounded at <=512
vertices per contour and O(n*m) nearest-vertex comparisons.

One signed score decides:
```
mean_area = 0.5*(area1+area2)
delta_area = abs(area1-area2)
d = arc_length_weighted_bidirectional_mean_nearest_vertex_distance
average_term = 1.0*mean_area
difference_term = -1.0*delta_area
distance_term = -2.0*d*d
S = average_term + difference_term + distance_term
```
Strict S>0 => zipper when both rims belong to the SAME removed excision
patch or DIFFERENT excision patches with direct R01 trusted contact. A
missing trusted pairing => independent caps regardless of score.
S<=0 => caps; equality does not arbitrarily introduce a new handle.
This entails a 3:1 projected area-ratio cutoff for zipper regardless of
distance. Source directed contour order/owner is never inferred by
proximity. A classifier result is not a validated green/blue closure.

Every two-rim decision logs all individual raw terms, their signed
contributions, score, provenance eligibility, and actual selection
in `[repair-r06] region=... selection=M08 proximity: ...`.
Area mode keeps the existing V15 selection logs.
One-rim regions have only the cap option; contact islands with >2 rims
still use the current V15 DP and explicitly log
`M08 multi-rim V15 area-DP fallback`. Full alternative multi-rim
matching is a separately planned M08C step, not silently implemented.

## Physical and replay comparison

```bash
./build/Mesher --repair auto --repair-classifier area
./build/Mesher --repair auto --repair-classifier proximity

./build/Mesher --repair-replay repair-cases/refusal-2.r06case --repair-classifier area
./build/Mesher --repair-replay repair-cases/refusal-2.r06case --repair-classifier proximity
```

The first two GUI commands require a new mouse stroke and hence are NOT
controlled A/B evidence. Use the same immutable `.r06case` for an exact
side-by-side comparison in the later two commands. The archive's actual
outcome and optional human `prefer zipper` / `prefer caps` annotation
are unchanged; `--repair-replay-test` still reports outcome mismatch
(exit 2) or semantic choice mismatch (exit 3) when an alternative
classifier produces a different result. An expected experimental
mismatch is informative, not a reason to rewrite the physical original
archive automatically.

Future coefficient tuning should be driven by the recorded score terms.
The initial coefficients 1,-1,-2 are constants, not an additional
run-time parameter. No threshold or quality limits are relaxed.

Acceptance: (1) area defaults match the recorded five V1 physical
cases; (2) both modes make valid, finite two-rim decisions on exact
fixtures and the current M04 closure honestly succeeds or refuses;
(3) higher-resolution physical capture of a "caps where zipper desired"
case with Ctrl+Shift+S; (4) Linux/Windows viewport comparisons on the
SAME case; (5) GCC/Clang/MSVC all tests pass. PR remains draft until
physical acceptance.
