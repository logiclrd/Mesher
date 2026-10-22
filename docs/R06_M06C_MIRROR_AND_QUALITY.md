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

## 2026-10-22 second physical acceptance round (three runs)

**A successful single reflected cap:** one mirrored 26-vertex R03
rim was capped with `cap_loops=1`, `green_faces=98`, `inserted_vertices=37`,
`refinement_steps=10`, `min_cap_quality=0.166`,
`spatial_cap_loops=1`, `spatial_poisson_iterations=49`,
`spatial_density_splits=10`, `spatial_density_remaining_edges=0`,
`spatial_max_final_edge_ratio=1.489`. This is positive physical
evidence for spatial closure and optional mesh density, **not** proof
of sufficient curvature continuity or R07 live mirror-map updates.

**A refused single reflected cap:** a new exact 16-vertex R03 rim,
with two real X=0 seam endpoints, chose CAP in M03 and refused M04
`R06-M06C mirrored conforming edge lacks two incident tracked faces`.
Root cause in M04A: projected R05 ear clipping was still attempted
FIRST on a symmetric cross-plane rim, yielding an unpaired diagonal
configuration. The mirror-mapped split edge was absent on the
other side of the staging halo. C4c1 now routes ALL such spanning
single-loop caps directly to the reflection-safe AFM front. Both
the exact physical 16-vertex local front and a separate synthetic
projected-simple symmetric loop are regression fixtures. The
physical rim's front passed local topology checks at initial
minimum q=0.423486 (source surviving face geometry unavailable).

**A refused mirrored two-loop zipper:** separate mirrored, directed
15+15 boundary loops chose ZIPPER in M03 with saving 0.505506 above
the specific directly-linked facing-contact threshold 0.45.
The preexisting arbitrary DP zipper crossed X=0 without a seam,
and the *correct* final geometry mirror audit refused it.
C4c2 now identifies an exact reflected/reverse-wound zipper pair,
constructs one genuine x=0 vertex for each paired boundary vertex,
and places mirrored half-strips with identical reflected diagonals
on either side. Existing rim IDs are untouched and no coincidence
welding occurs. The exact 15+15 physical coordinates are used in
a private M04A staging fixture; malformed winding and ambiguity
refuse, cyclic origin shifts are allowed. The test does not
recreate the original 11K-face sculpt; success is not guaranteed
until a full physical AUTO_CLOSED case passes M04B.

C4c3 expands the complete private post-refinement collision
check to **BOTH** green cap and blue zipper faces whenever
spatial AFM or a mirrored seam zipper was used. It retains
bounded test count, quality and manifold constraints, and reports
`mirrored_seam_zipper_pairs` and `mirrored_seam_vertices`
in successful `[repair-r06] AUTO_CLOSED` diagnostics. M04B
mirrored conforming splits still consume the same total 512
step / 1024 vertex budget; asymmetric zippers remain on the
original R04 area-DP route.

**Remaining certification:** GCC, Clang and MSVC on the latest
full implementation; physical Linux rerun of all three
configurations; direct matched reflected face-ID journals
for eventual R07 live CAS/history/checkpoint; bounded symmetric
quality-improving diagonal flips in C4d. Keep PR #148 draft
and all C4c parent, C4d, C4e, M06C parent and R06 checkboxes OPEN.
