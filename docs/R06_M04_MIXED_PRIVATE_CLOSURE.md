# R06-M04 — Mixed private geometry and certified enclosed-component retirement

**Status:** implementation and CPU regression on the stacked R06-M04 draft PR, awaiting compiler and test matrix verification. The parent R06 remains open, with developer `--repair auto` and physical GPU acceptance still assigned to M05/M06.

## Contract and all-or-nothing boundary

Unlike R04 forced zipper and R05 forced cap, which each demand ownership of every hole, automatic repair may require **both** in a single stamp. Sequential invocation of either forced builder would audit a partially open mesh and prematurely refuse a valid mixed operation. The new M04 pipeline instead uses one common staging ledger, no public edits and one private closed-state validation.

M04's three preplanned flat peers are recorded in `todo.md` before code, not nested ad hoc:

- **M04A:** expose unchanged R04's bounded oriented strip DP and R05's projected ear clipping via small wrappers; simultaneously stage M03-selected caps/zippers; assert each directed R03 rim has exactly one new oppositely oriented side and reject missing, repeated, unapproved cross-region loops; preserve all old source FaceIds.
- **M04B:** share every tracked retained triangle and all new green/blue triangles in **one** adaptive edge-splitting ledger. When an edge is split, split *both* tracked incident triangles to avoid T-junctions. Enforce per-kind R04/R05 0.10 triangle quality, 2.5-times local edge bounds and original-survivor 0.012 lower bound, with finite inserted vertex, generated face, tracked face and subdivision budgets. After one COW transaction, restore IDs/tombstones and require `validatePrivateClosedManifold` including globally connected vertex fans. No worker CAS/history/checkpoint change.
- **M04C:** independently reconstruct M03's choices from exact pre-R03 frozen source and trusted contact FaceIds; check surviving face identity/positions against R03, prohibit a forged/stale closure. Require explicit M02 material certification for **any zero-loop whole-component** R03 deletion, rather than treating all disconnected solids as redundant. Run M04B first to obtain actual closed geometry, then M02's bounded signed-volume/containment/collision witnesses. Retire *only* wholly proved inward enclosed closed components (Mesher never intentionally preserves sealed cavities). Remove all their faces/vertices together, restore all tombstones, globally re-audit closed topology, retain outward exterior components. Filter temporary green/blue faces later removed with the inward shell; final mesh has no interior cap/zipper.

All exceptions or ambiguous evidence abort the entire private proposal; stable ID allocation may consume monotonic identity slots but no geometry root/history/checkpoint is published. New faces/vertices always have distinct IDs, old retained faces keep stable IDs, and retired IDs never recycle.

## Bounded acceptance

Dedicated CTests `repair_r06_m04a_mixed_face_staging` and `repair_r06_m04c_enclosed_retirement` target real tight-torus two-cap R02/R03 cuts; a synthetic three-prism mixed cap/zipper candidate; forged global contour/mismatched-region refusal; exhausted staged/new-face/cavity budgets; a test-only pair of independent concentric outward/inward shells with fabricated *test* contact provenance, then one surviving outward Euler-2 shell with the entire inward component retired; and two disconnected outward cubes proving that complete zero-rim retirement cannot silently erase an exterior solid.

**Limitations and separate stages:** M03's area score is only a topology-selection heuristic. M04 does NOT promise every valid contour can yield quality-bounded triangles. A supported refusal is safer than degrading mesh quality. GPU visual inspection, repeated live strokes, the worker CAS, persistent history, checkpoint publication and true physical torus/hollow post-stamp behavior are respectively M05/M06 and R07/R08, not claimed by these synthetic CPU fixtures. Zero-rim retirement of a wholly isolated inward shell is allowed only after M02 certifies it against the original closed candidate; otherwise refuse. Imported inverted/ambiguous exterior shells are not deleted by normal direction alone.

R04 and R05 forced-mode behavior and their prior 68-test acceptance remain unchanged.
