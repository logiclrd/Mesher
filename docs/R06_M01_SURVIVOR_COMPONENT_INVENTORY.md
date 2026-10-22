# R06-M01 — Bounded read-only R03 survivor inventory

**Scope:** independently testable prerequisite for R06 automatic closure.
The R05 forced caps/zipper code, CLI switches, worker, history and
production publication are NOT changed.

`inventoryPrivateExcision(cut,proposal,limits)` enumerates the ACTUAL
faces of the private R03 open `RegionTopologyIndex`. It identifies maximal
edge-connected surface components by stable IDs, audits two-sided opposite
winding and true directed one-sided boundary edges, verifies each boundary
edge/vertex belongs to precisely one R02 survivor-oriented contour and
surviving face, and attributes contours to their owning component.

Each surviving component reports its face and vertex IDs, edge and boundary
counts, contour indices, Euler characteristic V-E+F and closed/open state.
A surviving closed component with **zero** contours is legal (whole another
closed component was excised), but this does NOT yet license deleting the
remaining component. Geometric enclosure and material classification belong
to R06-M02, closure fitness to M03 and private mixed surgery to M04.

A face collision island, excision patch and loop need not be identical. A
single component may have multiple open rims, disconnected components may
coexist in the same immutable root, and topological identity never follows
matching XYZ positions.

## Fail-closed cases

- No valid R03 root/complete contour plan, or inconsistent global counts.
- Exceeded positive finite face/edge/loop limits.
- Missing/duplicated FaceId, dangling/repeated triangle vertex, edges with
  >2 sides, equal-winding incident face pairs, or a disconnected vertex fan.
- An R02 contour with wrong surviving face or edge direction, duplicate/
  shared boundary vertex, missing actual edge, repeated edge, or mixed
  component ownership.
- A boundary edge without a corresponding contour, or a contour assigned
  to a closed component.

## Automated verification

`repair_r06_m01_survivor_component_inventory` runs full private R02/R03
sphere two-hole, tight torus and hollow mouth fixture cuts, checks assignment
and read-only source integrity, rejects mismatched contour ownership,
repeated loops and finite face-budget exhaustion. Separately, two disjoint
cube shells with one whole component retired verify a surviving closed
orientable surface with 12 faces, 8 vertices, 18 edges, Euler 2, zero loops,
and original immutable tombstones unaffected.

**Acceptance boundary:** these CPU tests/CI prove the topology inventory
only; M01 does not choose cap/zipper, classify enclosed material or perform
R06 automatic closing. Human Vulkan tests begin at M05/M06. The parent R06
remains unchecked pending the entire preplanned flat peer sequence.
