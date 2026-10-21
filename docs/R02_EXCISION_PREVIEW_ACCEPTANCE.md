# R02 — frozen read-only excision classification & contour preview

**Status:** R02 implementation in stacked PR (base: R01). Physical Linux/Windows GPU acceptance remains open. This is not a general-purpose Boolean and never changes connectivity.

## Operator contract

Use `--repair-preview` (instead of R01's `--repair-inspect`) with an interactive brush fixture. The first new post-Inflate/Refine/Collapse triangle contact freezes **an uncommitted candidate**. Neither the full candidate nor the hypothetical removal is published into history, checkpoints, or ordinary mesh data. The camera remains freely movable and ordinary stamps/undo/save are blocked. A completed collision query and any budget/refusal categories remain independently visible.

| Color | Meaning |
| --- | --- |
| R01 red dots and dark-red rods | vertices/edges incident to contacting triangles; not necessarily penetrating |
| **Orange inset triangle** | V14 barycentric samples indicate at least 62.5% buried/outside; proposed whole-face excision only |
| **Purple inset triangle** | V14 classification has an ambiguous sample or bounded query error; do **not** treat this face as deleted |
| **Golden boundary rod** | directed surviving-face loop edge after *hypothetical* deletion, as extracted by existing V15 contour planner |

No highlighted face or boundary implies that geometry was actually removed. Mixed faces, classification failures, contour-graph errors, and later closure-quality uncertainty are reported explicitly in `[repair-r02] PREVIEW` lines. `complete=0` means classification or contour extraction was incomplete, **not** approval to delete orange faces. `closure_advisory=...` separately names V15 cap/zipper fitness or matching refusals; a valid R02 removal-and-loop preview may show `complete=1` with a closure advisory, because R02 is not responsible for choosing a repair. The R02 planner uses a finite maximum of 128 classified faces, 96 removable faces, 24 contours, and V14 depth-1 barycentric sampling with *no unbounded reference winding fallback*. It may refuse a complex region, which is a visible diagnostic result and **not** automatic repair.

Collision-island count groups related contact FaceIds; excision-component count groups removed faces along shared mesh edges; contour count is number of actual directed survivor loops. There is no one-to-one cardinality assumption among these. R02 deliberately does not select cap or zipper, so an ambiguous V15 closure can still leave drawable oriented loops.

The hollow sphere inner wall is still topologically connected through the mouth at the R02 time of inspection, so there is no legitimate detached redundant inner component to delete *yet*. After eventual surgical closure, R06 must classify/delete a resulting isolated interior without capping/zippering. Neither R02 nor the mere original red contact area purports to solve that future material-topology problem.

## Physical exercise

Build the PR's exact SHA, then use:
```bash
./build/Mesher --repair-preview --torus --vertices 5000
./build/Mesher --repair-preview --hollow-sphere --vertices 5000
./build/Mesher --repair-preview --asymmetric
```
On each fixture, stamp an unrelated surface first (no freeze), then deliberately pinch across a surface. Confirm candidate is frozen, red contact incident geometry is visible, and any *certain* orange selected faces and golden directed boundary loops can be viewed from both sides without a mesh hole appearing. Where winding is ambiguous, purple classification and the matching `complete=0 reason=...` line are expected; never claim orange selection or loop success if none was supported.

Record exact `git rev-parse HEAD`, a screenshot showing colors, and `[repair-r02] PREVIEW` plus selected FaceId / loop lines. Confirm that repeated LMB, Ctrl+S, Ctrl+Z and checkpoint state do not mutate the committed root, but camera/W/G remain usable. Re-launch and repeat using another site or magnitude. Cross-platform physical Vulkan acceptance is distinct from CPU tests.

CPU suite `repair_r02_private_excision_preview` positively confirms two independent cube intersection islands produce **8 selected faces and 4 directed loops**, despite deliberately unsupported V15 closure fitness. It also checks contact-free work avoidance, bounded invalid settings, a live full candidate, old-root/undo safety and R01 markers preserved. V15's separate test suite covers oriented loops from explicit hypothetical face removal. Positive torus/hollow classification *quality* must be judged with the first physical viewport test and iteratively improved; the inability to classify a difficult scene is an honest R02 gap, not authorization to guess.

## Non-goals

No face deletion, no holes, no automatic capping/zippering, no component extermination, no per-stamp ongoing repair, no Merge-OFF collision clipping. R03 introduces a separate **private open-boundary snapshot** and R06 handles completed enclosed redundant shells.
