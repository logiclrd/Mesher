# R04 — Always Zipper, frozen private closed surface

**Status:** Development branch; CPU CI and Linux/Windows physical Vulkan acceptance open. **No production sculpt publication.**

Use \`--repair zipper\` (alias `--repair-zipper`) with the existing default sphere, `--torus`, and `--hollow-sphere` fixtures. A successful post-stamp R01 contact freezes the brush and performs R02 vertex-star removal and R03 **actual** private open-boundary excision. R04 then **forces zipper-only** reconstruction. The result is an unpublished private **closed** mesh with newly created seam triangles colored **blue**, from both sides, including an interior camera view. No automatic cap, and no topology work is published to undo, redo, checkpoints, or the ordinary sculpt worker.

## What makes a valid forced zipper

- 2, 4, 6, or 8 oriented excision loops; **1, 3, 5, ...** or 0 loops refuse, without opportunistic capping. Full-component removal with zero loops belongs to later `auto`.
- Complete disjoint pair matching uses symmetric local nearest-point distances and strict finite geometric pairing/budget limits. If there is more than one close-cost perfect matching (including many-to-many arrangements), refuse the entire operation rather than guessing. Pair counts are not assumed from contact-island counts.
- Per pair, orientation is derived from surviving-face contours. One ring is traversed in reverse. A bounded arclength-aware dynamic-programming zipper creates a complete two-manifold strip even with **different numbers of vertices** on the two loops.
- Each long contour edge is subdivided before zipper creation, and the matching adjacent **surviving triangle is subdivided with it**, so there is no T-junction. After zippering, oversized or low-quality new seam triangles trigger bounded **longest-edge bisection** propagated to both incident faces, including existing survivors where needed. Each triangle retains its correct orientation and stable survivor ID; new children use fresh stable IDs. Refuse if triangle-quality/edge-size bounds remain unsatisfied on completion.
- Rebuild a private immutable index from COW storage and independently certify every global edge is exactly 2-sided with opposite winding, every vertex has a single connected closed fan, every retired face stays absent, and every blue face is a genuinely inserted FaceId. On any failure show the previous R03 open cut and a distinctive `R04: ZIPPER REFUSED` HUD/status with the reason.

The diagnostic no longer subjects an otherwise valid curved zipper to the older V15 circular-coaxial frustum *cap-versus-zipper* selection heuristic; that was an over-specific admission decision, not a necessary criterion for a forced zipper. The finite-work quality checks and topology audit are independent.

## Acceptance testing

```sh
git fetch origin pull/139/head
git switch --detach FETCH_HEAD
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher --repair zipper
./build/Mesher --repair zipper --torus
./build/Mesher --repair zipper --hollow-sphere
```

Inflate the ordinary sphere's two protrusions until collision, then pinch the torus center and the hollow-shell mouth as with R01–R03. Look **from both sides** with W on/off: the old R03 gold hole rims should be gone on success, replaced by **real blue connecting faces**. Take screenshots and capture the `[repair-r04] ZIPPED` line (pair count, blue/new face count, boundary splits, refinement steps, minimum seam quality), or the exact `ZIPPER_REFUSED` reason. A successful R04 stamp stops the brush and remains inspectable. Examine whether the seam has long needles, folded/flipped triangles, wrong pairings, or distorted neighboring survivors; mere green CPU CI is not physical geometry acceptance.

The synthetic hollow-mouth neighborhood in R03 generates **one** excision loop, which R04 intentionally refuses rather than capping. A real sculpted hollow-shell pinch which generates two compatible loops can be zippered if independently verified; do not treat the synthetic one-loop refusal as a positive zipper acceptance case. R04 does not require a complete realistic closed-shell cleanup on a hollow sphere; enclosed interior component retirement and cap-vs-zipper decisions belong to R06. R04 may legitimately refuse twisted/near-coincident pathological loop geometry where finite refinement cannot achieve minimum acceptable triangle quality.

**Acceptance gates:** CPU GCC, Clang and MSVC green; at least one successful physical Linux zipper with blue new faces at each geometry type; physical Windows GPU smoke acceptance separately; no worker root publication or checkpoint changes. Keep PR open until the human viewport result is validated.
