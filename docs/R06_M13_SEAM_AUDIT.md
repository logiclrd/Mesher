# R06-M13 — Exact seam and geometric conformity diagnostics

Purpose: Investigate a suspected green-cap / gray-survivor T-junction in
physical warning previews of `refusal-1` and `refusal-3`.
**Neither the R06 repair algorithm nor its strict acceptance rule changes.**
These are explicitly opt-in, read-only diagnostics on the **actual displayed
private** `AUTO_CLOSED` index or `QUALITY_WARNING_PREVIEW` index, never on
the uncut candidate or separately regenerated geometry.

## Replay

```bash
./build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-quality-warn --repair-seam-audit --repair-overlay-exact

./build/Mesher --repair-replay repair-cases/refusal-3.r06case \
  --repair-quality-warn --repair-seam-audit --repair-overlay-exact

# Headless: exit codes and historical case expected-REFUSED labels are unchanged.
./build/Mesher --repair-replay-test repair-cases/refusal-1.r06case \
  --repair-quality-warn --repair-seam-audit
```

For live sculpting, use `--repair auto --repair-quality-warn
--repair-seam-audit --repair-overlay-exact`. Both flags require R06
AUTO or replay; neither changes worker CAS, checkpoint/history or
immutable `.r06case` contents.

## Log interpretation

Each `[repair-r06-seam] EDGE` line is an **indexed** interface edge
with exactly the actual stable endpoint VertexIds and their full-precision
XYZ, both generated FaceId and surviving FaceId, and the directed
orientations used by each face. Opposite directions with exactly two
incidents form a topologically sewn interface; the green and gray
internal triangulation diagonals do **not** need to be collinear.

`[repair-r06-seam] ISSUE` reports:
- `MISMATCHED_INTERFACE_INCIDENCE` or `SAME_DIRECTION_INTERFACE`:
  topology/winding problem on an indexed interface edge;
- `OPEN_OR_NONMANIFOLD_NEW_EDGE`: generated edge lacks exactly two
  faces (independent of the existing full R04 manifold audit);
- `SURVIVOR_VERTEX_INSIDE_NEW_EDGE` or
  `NEW_VERTEX_INSIDE_SURVIVOR_EDGE`: a distinct stable ID lies strictly
  in the interior of a surface edge to within a recorded, mesh-size
  relative tolerance. These are geometric T-junction candidates;
- `COINCIDENT_DIFFERENT_IDS`: cap/survivor IDs disagree despite
  bitwise-identical positions (a potential disconnected join);
- `UNSHARED_NEW_SURVIVOR_EDGE_CROSSING`: near-zero 3D closest
  separation with both closest parameters strictly in segment interiors,
  excluding shared-index endpoints.

The SUMMARY shows counts, `complete`, explicit finite work budget,
`issues_truncated`, and
`full_triangle_collision_certificate=0`. An edge/vertex diagnostic,
even when all counts are zero and `complete=1`, **cannot certify absence
of triangle interior intersections**. In warning mode the separate
full 3D collision query may still refuse or run out of its contact
reporting budget. Output reports any such limitations rather than
asserting a manifold embedding.

## Rendering

`--repair-overlay-exact` makes the colored cap/zipper triangles use
exact authoritative vertex XYZ, with **zero normal displacement**, and
adds small yellow/gold (`material=13`) interface edge markers at the
actual shared edge endpoints. The red warning faces/edges remain
semantically red. No user-visible projection or manufactured seam
endpoints are used for the indexed diagnostic.

Caveat: drawing colored triangles exactly over the underlying gray
triangles creates equal-depth fragments; with the usual Vulkan
LESS_OR_EQUAL depth test, numerical Z-fighting or occlusion is possible.
The independent CPU seam log is authoritative even when the depth
rasterization is visually ambiguous. The existing PR #158 thin
pixel-space cap edge shader is a sibling of the current geometry
branch and can be tested in a *local* combined worktree.

Inspect two angles on each case, rotate behind the cap and toggle W.
Compare the gray diagonals to the gold exact interface (not to
silhouette displacement). Do not infer a hole from a projected
diagonal crossing without an indexed/geometry ISSUE line.

## Acceptance

- Synthetic manifold tetra face must report exactly three indexed
  opposite-directed interface edges and zero geometric anomalies.
- A gray vertex independently inserted *in* a cap edge must report
  `SURVIVOR_VERTEX_INSIDE_NEW_EDGE`; a gray edge intersecting a cap edge
  must report an unshared crossing.
- An artificially exhausted pair budget must report `complete=0`,
  not a clean global certificate.
- The two 5K physical cases must produce reproducible summary and
  exact interface rows without changing their original outcome/quality
  acceptance (normal `AUTO_REFUSED` with optional warning preview).
- Linux GCC, Clang, Windows MSVC CTest and physical Vulkan review
  are required before merge.

No repair or triangle topology changes should be attributed to this
diagnostic-only milestone.
