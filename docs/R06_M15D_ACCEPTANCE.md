# R06-M15D — Split ancestry and physical Vulkan acceptance

Status: **M15D4 physical GPU acceptance pending**. The CI test suite
cannot substitute for the operator inspecting rendered geometry.

## What is new in D1–D2

When `--repair-seam-audit` is used, every stage-owned edge split carries
a bounded read-only record with the **exact source indexed edge** (two
stable VertexIds), the newly allocated midpoint VertexId, the number
of R03 sides incident on that edge when it existed in the immutable
cut, and the kinds of faces actually split (survivor/cap/zipper).
These are *actual calls to the sole staging split primitive*; no
geometric proximity test manufactures split ancestry.

`[repair-r06-provenance] SUMMARY` prints total recorded split events,
the number of **stored** M13 T-junction witnesses that are verified
split midpoints, and the count of other transaction-created vertices
among the **stored** M13 witnesses. The independent M13 seam audit is
capped at 256 stored issues; its full `geometric_t_junctions` count can
be larger. Do not extrapolate the lineage count to truncated issues.

Each `WITNESS` now reports `vertex_creation`, `split_parent`,
`split_parent_r03_sides` and `split_{survivor,cap,zipper}_sides`.
A geometric witness vertex that is a proven midpoint of a *different*
indexed edge does **not** prove that it was meant to attach to the
geometrically nearby survivor edge. Likewise, an edge classified
`NOT_SOURCE_EDGE` may be a NEW survivor child; it is not evidence
of a pre-existing R03 rim. Distinguish coincident XYZ from identical
topological stable VertexIds.

All of this evidence is advisory, has no topology or publication
side effects, and preserves the strict legacy R06 spatial-front
collision and quality admission policies. Full final manifold
certification remains independent of these proximity warnings.

## Headless reproduction

From a **fresh build of r06-integration**, in the repo root:

```bash
ctest --test-dir build --output-on-failure \
  -R 'repair_r06_m07_physical_case_[1-5]|repair_r06_m13_exact_seam_refusal_[13]|repair_r06_m15a_stage_owned_face_and_live_boundary|repair_r06_m14d_source_witness_provenance'

./build/Mesher --repair-replay-test repair-cases/refusal-1.r06case \
  --repair-quality-warn --repair-seam-audit --repair-overlay-exact

./build/Mesher --repair-replay-test repair-cases/refusal-3.r06case \
  --repair-quality-warn --repair-seam-audit --repair-overlay-exact
```

For baseline comparison on the preceding M15C head (CI
[#40073922184](https://github.com/logiclrd/Mesher/actions/runs/40073922184)),
the first case recorded 382 geometric T-junction candidates and
4 unshared edge crossings; the third recorded 356 and 14. Both
reported zero indexed interface-incidence or orientation failures.
The M13 audit stored only the first 256 witnesses from each case.
M15D changes provenance **only**; it should not change these counts
or the expected archived dispositions.

The five source captures remain unchanged. Current regression
expectations: refusal-1 and refusal-3 remain REFUSED in strict R06
with optional `UNTRUSTED_WARNING_PREVIEW`; refusal-2/4/5 are
reviewed CLOSED recoveries. Do not update .r06case files to
make a regression green. Any changed choice or outcome requires
an explicit review and a new independently testable task.

## Physical Vulkan inspection on the user's GPU

Run both cases separately with the **GUI** `--repair-replay` (not
`--repair-replay-test`):

```bash
./build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-quality-warn --repair-seam-audit --repair-overlay-exact

./build/Mesher --repair-replay repair-cases/refusal-3.r06case \
  --repair-quality-warn --repair-seam-audit --repair-overlay-exact
```

1. Note branch commit and whether the overlay indicates an
   **untrusted warning preview** or a certified private closure.
   A warning preview is not published and may fail quality.
2. Toggle **W** for wireframe. Inspect from the outside and move
   the camera behind/inside the suspected cap. Compare the
   gold interface segment to the green cap boundary and gray
   survivor edge; do not require unrelated internal diagonals
   to line up.
3. Orbit to at least two substantially different angles. Exact
   overlay removes the normal offset, but depth-equal fragments
   may Z-fight. Screenshots alone cannot prove indexed welding;
   correlate the suspicious physical location with its
   `[repair-r06-seam] ISSUE` stable VertexIds and the new
   `[repair-r06-provenance] WITNESS` split-parent identity.
4. Specifically test whether a cap-created vertex near a gray
   survivor edge has `vertex_creation=VERIFIED_EDGE_SPLIT`;
   if so, check its **original indexed parent edge**. Is the
   gray edge the exact same original/descendant edge, or merely
   geometrically coincident? If the cap intended the gray
   edge as an attachment but never requested its split, that
   is a **generator intent bug** requiring a new code milestone.
   The staging API cannot guess the intended weld.
5. Capture one full outside view, one inside/backface view,
   one close-up of the gap, and the corresponding console
   SUMMARY and WITNESS rows with complete IDs and XYZ.
   Include any sign of a nonmanifold boundary, z-fighting or
   quality warning. Report each case independently.

## Criteria for declaring M15D accepted

- All five exact original captures retain their reviewed outcomes,
  with the R06 strict admission policy unchanged.
- Cross-platform CI passes GCC, Clang, and Windows MSVC; split
  ancestry regressions confirm actual ID matching and reject
  fabricated ancestry.
- The two pathological cases have reproducible and bounded
  opt-in lineage census, not only unqualified PASS/REFUSED.
- Physical GPU inspection establishes whether the apparent
  independently split interface persists, and correlates at
  least one representative witness to source-vs-new ID lineage.
  A persisting visual issue cannot be marked resolved simply
  because the topology-edit API is now encapsulated.

M15D4 remains open until this evidence is supplied. No
publication enablement or merge of draft PR #162 is implied.

## M15D1/D2 exact replay findings — 2026-10-24

Clang CI [run #40077555945](https://github.com/logiclrd/Mesher/actions/runs/40077555945)
recorded **441** actual staging split events in `refusal-1` and
**421** in `refusal-3`. The M13 census remains 382 / 356 geometric
point-on-edge candidates, with 4 / 14 unshared crossing pairs and
**zero** indexed interface-incidence or orientation errors.

For **each** case, the first 256 stored M13 T witnesses are:
- 256/256 verified stage-created edge midpoints (NOT newly introduced
  unaccounted-for vertices);
- 256/256 generated by splitting an edge with **two cap** incident
  faces and **zero survivor** faces;
- 256/256 lie near a gray edge marked `NOT_SOURCE_EDGE` (a final
  survivor edge not present verbatim in the original R03 source);
- 0/256 have a split-parent edge with exactly the same two indexed
  endpoints as the nearby survivor edge. In refusal-1, 164/256 share
  one endpoint and 92/256 share none. In refusal-3, 204/256 share
  one endpoint and 52/256 share none.

Representative refusal-1 witness:
`vertex=6715`, parent cap edge `(3207,6713)`, near survivor
edge `(3207,6687)`, separation approximately `2.45e-9`,
split parent R03 sides 0, cap sides 2, survivor sides 0.
Refusal-3 begins with vertex `6746`, cap parent
`(6719,6745)`, near survivor edge `(6719,6724)`.
These results show **independent, nearly co-linear geometric
refinement** of distinct indexed cap and survivor edges, NOT a
failure to propagate an explicitly requested split of one
indexed shared edge. They do not establish that those cap
vertices were *intended* to attach to the gray edge; that
requires generator intent and physical topology review.

**Sampling limitation:** every stored witness was within the 256-issue
M13 budget. Neither the other 126/100 point-on-edge warnings nor the
full triangle/triangle embedding were reclassified. Do not present
a stored-witness ratio as proof for all candidates. The warning
preview remains untrusted, and strict refusal policy is unchanged.
