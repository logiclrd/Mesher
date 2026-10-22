# R06-M03 — Read-only automatic closure topology planner

**Scope:** stacked after R06-M01 component ownership and R06-M02 enclosed-cavity evidence. M03 chooses *which oriented R03 rims should cap or zipper*; it does not allocate triangles, delete cavities, edit the worker revision, publish history/checkpoints, or commit a mesh. R06-M04 remains the independent all-or-nothing private surgery milestone.

## Provenance over coincidence

The input consists of the same immutable pre-excision candidate used by R02, its actual R03 open cut, the complete R02 proposal, and verified R01 contact FaceId pairs. A fake contact that is ambiguous or refers to a non-excised face is refused. M01 first verifies every genuine directed surviving contour and its surviving surface component.

Next, M03 determines connected components of **deleted faces** by stable-ID triangle-edge adjacency in the pre-cut candidate. Each contour is mapped to its unique *excised patch* using the actual deleted incident triangle across every surviving rim edge. This is distinct from the surviving surface component (which may extend between distant cuts) and from the R01 contact graph. If one rim borders two deletion patches, the entire transaction refuses.

Two excision patches enter the **same candidate repair region only with an explicitly trusted R01 triangle-contact link** between those patches. Each linked contact island is planned independently; unlinked near-by loops may not silently zipper by XYZ proximity. Many-to-many contact regions over four loops are unsupported in this first R06 planner; a deliberately linked 3-vs-5 scenario therefore refuses the whole plan. This is conservative, not a general many-to-many solver.

## Fitness and topology

Within each supported region (one to four loops), reuse the historical, tested V15.1 `selectRepairClosures` fitness:

- Estimated projected ring area and annular zipper area, with arc-length-based 32-sample correspondence and an exact bounded subset DP; real selected-face topology is not inferred from matching vertex indices.
- Planarity/isoperimetric limits are relaxed toward R05's more capable quality-aware ear clipping, without asserting that a future M04 triangulation will succeed. Final cap/zipper geometry must still pass the R04/R05 safety checks and R04/R05 global manifold audit.
- Near-tied area estimates, unsupported many-to-many, unsupported pairing, missing direct cross-patch contact provenance and finite work-limit exhaustion refuse. There is no guessing a zipper because two loops are close.
- One sufficiently non-degenerate isolated loop caps. Where V15 cannot justify a coaxial pairing, two loops from a **single surviving component** may provisionally select independent disks: this allows torus-tunnel removal rather than inventing a long slender bridge. M04 must independently validate the disks.
- Require a substantial zipper area saving: at least 20% for loops from distinct surviving shell components, and 55% for two loops on the **same** surviving shell (whose zipper might retain/create an undesired handle). If a V15-supported zipper lacks that advantage, downgrade to two caps in the topology-only plan; this preference is not a geometric proof or unrestricted auto-merge guarantee.
- Disconnected surfaces that are independently exposed may coexist in the same document. A zero-loop patch represents entire-component retirement by R03 and needs neither cap nor zipper; **material-based deletion approval** is explicitly deferred to M04, using M02 evidence on an actual closed post-repair candidate.

The output names global R02 contour indices, actual V15 arc-length pairing metadata, independent repair regions, cap/zipper lists, and non-authoritative estimated area. Any refusal clears the *entire* proposed action set, not just the offending region. R04 forced zipper and R05 forced cap remain unchanged.

## Automated acceptance

Dedicated `repair_r06_m03_automatic_closure_plan` exercises:

1. Real tight-torus R02/R03 pinch: two caps, not the historically undesirable long zipper.
2. Two widely separated R02/R03 sphere cuts on one connected surviving sphere: independent caps, never a cross-island zipper.
3. Two adjacent, independently bounded prism rims with synthetic *unit-test* contact-provenance links plus a distant third cap: one planned zipper and one cap in the same private candidate. Without the synthetic links, all three cap independently. The synthetic links test routing; M05 will exercise real post-stamp trusted contacts.
4. Eight linked rim patches (including an illustrative 3-vs-5 ambiguous grouping) explicitly refused with no partial closure publication.
5. Face budget and false/ambiguous provenance refusal.
6. Complete removal of one of two disjoint closed cube shells (zero open rims): no invented cap/zipper or automatic deletion authority; original/source stable-ID state unaffected.

**Non-goals:** executable mixed surgery (M04), material classification of R03 open surfaces (M02 currently certifies only closed surfaces), live selection/viewport (M05), hardware acceptance (M06), arbitrary >4 rim associations, exact minimal seam length or guaranteed zero residual crossings. The parent R06 remains unchecked.
