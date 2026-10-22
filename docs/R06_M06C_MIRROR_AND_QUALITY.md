# R06-M06C4c/C4d — Mirrored nonplanar caps and skinny-scar work

**Status: draft [PR #148](https://github.com/logiclrd/Mesher/pull/148), stacked on draft [PR #147](https://github.com/logiclrd/Mesher/pull/147). Neither C4c nor C4d is complete or approved for live R07/R08 publication.**

## Physical Linux evidence (2026-10-22)

A sculpted *intrinsically mirrored* single directed contour crosses the
X=0 reflection plane at exactly two existing seam vertices. The
spatial cap produced triangles spanning the plane with no explicit seam
edges; a result that is manifold under R06-M04B need NOT be a valid
mirror-pair topology for Mesher's existing symmetry mode. The user
prefers preserving intrinsic symmetry; a silent one-sided topological
mutation is unacceptable. R06 diagnostic results are PRIVATE and do not
update the live worker's mirror map.

The later physical same-type contour has 26 exact mirrored vertices,
including two X=0 seam vertices. M03 correctly chose ONE cap but M04B
refused a skinny triangle after exhausting 512 subdivisions:

```text
kind=CAP vertices=5598,5596,5597 q=0.090671
required=0.100000 length_ratio=0.240044
refinements=512 inserted_vertices=539
```

The edge length ratio is well below its bound; uncontrolled midpoint
splitting is unable to eliminate the underlying bad diagonal. A
separate attempt also refused while another unusual-shaped single cap
succeeded. Neither the screenshots nor a topology-only manifold audit
alone establishes an attractive surface profile or a certified mirror
map for that unusual shape.

## C4c implementation contract (diagnostic, NOT live)

The spatial-front backend now has an explicit X=0 mirror mode, used
when a known symmetric source has a true contour spanning both halves.

- Require exactly two genuine existing seam vertices on X=0.
  Build exact reciprocal contour vertex partners, never approximate
  them by coordinate proximity; refuse malformed, duplicate or
  incomplete pairs. Edges connecting negative X to positive X
  directly are unsupported without seam subdivision.
- Build the ordinary inward front and existing on-plane central hub;
  demand a reversed-winding reflection mate for EVERY staged triangle
  and no unseamed triangle spanning the reflection plane.
- Symmetrize staged off-plane ring positions and face-normal guidance.
  The normal-guided Dirichlet Poisson solve uses simultaneous Jacobi
  updates in mirror mode and projects new interior positions into
  their exact reflected orbits; a self-paired on-plane vertex has X=0.
  Existing rim coordinates never move.
- After M04's existing conforming subdivision and redundant-cavity
  retirement, a new strict private geometric mirror auditor requires
  unique exact reflected XYZ partners and matching reversed-oriented
  faces for the final geometry. Ambiguous duplicate positions,
  missing reciprocal faces or triangles crossing the plane cause an
  explicit private R06 refusal with the true gold R03 rim.
- Keep the familiar asymmetric solver unchanged. **Never silently
  switch a symmetric session to asymmetric** in this patch.

The whole-root geometric audit is more conservative than an
authoritative identity-proven mirror map; coincident independent
vertices can cause a refusal rather than a fabricated weld. Geometric
mirror partners are NOT permission to update live mirror relationships.
R07 must transport new exact mirror-pair IDs and mirror paired
subdivisions through one worker CAS/history/checkpoint transition;
until that is explicitly built, no live publication.

## C4d future: bounded quality-improving paired retriangulation

Before multiplying midpoint splits, examine local two-triangle
diagonal flips or front connectivity changes, using finite quotas and
strictly increasing worst-case triangle quality. On symmetric topology,
perform both reflected edge orbits as an all-or-nothing operation.
Screen inverted/folded triangles and collisions against the surviving
root, and re-run the full q>=0.10, conforming adjacency, manifold/fan,
and actual contact audits. Refuse honestly if no admissible improvement
exists, with no budget inflation or quality relaxation.

## Test and physical gates

CPU regressions:
- exact mirrored on-plane-front disk, reversed-winding twin faces and
  paired Poisson positions
- corrupted mirror boundary and unseamed crossing-edge refusals
- exact 26-vertex physical contour coordinates as a local solver fixture
- strictly mirrored octahedral closed mesh, then tampered coordinates,
  winding and crossing-face refusals
- existing R05/R06 torus, asymmetric, cavity and worker-CAS tests

Physical: repeat the source-symmetric single-cap failure with
`./build/Mesher --repair auto --repair-dump-contours`, checking for
an actual symmetric cap or an explicit conservative refusal, including
camera inspection on both sides of the X=0 plane. Supply the full
`[repair-r06] AUTO_CLOSED` / `AUTO_REFUSED` and triangulation metrics;
the previous 26-point list is a local loop fixture, not a restorable
complete pre-sculpt source mesh. Windows physical signoff and the
C4d quality algorithm remain outstanding.

**No checkboxes for C4c, C4d, C4b, or parent R06 may be marked complete
until their respective remaining work and cross-platform evidence exist.**
