# R06-M11A3: finite private closure remeshing and boundary-constrained q

This series is for R06 PRIVATE inspection only. It does not publish an
immutable mesh revision, alter worker/history/checkpoints, or change
the brush-time versioned collapser. The original five 5K .r06case
archives are immutable historical observations.

## Implemented independently reviewable changes

* [#157](https://github.com/logiclrd/Mesher/pull/157): Before
  subdividing a low-quality cap or zipper needle, move only NEW
  interior vertices, not original survivor or R03 rim vertices.
  Every motion must preserve local winding, improve the aggregate
  q/edge-length deficit, preserve the previous local minimum, avoid
  turning an acceptable face into a bad face, keep exact reflected
  counterparts in an atomic orbit, and pass final full manifold and
  collision audits. Finite maximum 32 moved vertices. No general
  permission to move a fixed R03 cap boundary.
* [#159](https://github.com/logiclrd/Mesher/pull/159): 3-to-1 reverse
  subdivision of redundant NEW interior cap valence-three fans only.
  Preserve all existing vertices/faces/loop IDs, exact orientation,
  preexisting survivor geometry and mirrored pairs; only allow when
  the replacement satisfies the ORIGINAL q=.10 and hard edge scale.
  Never run the brush's pre-stamp edge collapser on unpublished caps.
* [#160](https://github.com/logiclrd/Mesher/pull/160): A narrow
  exception for faces spanning BOTH immutable original R03 segments
  at a sharp *locally planar* tip. Both new endpoints must lie on the
  original segments to within 1e-9 of their length. Derivation: for a
  triangle with two sides a,b at included angle theta, normalized
  mean-ratio q = sqrt(3)*a*b*sin(theta) /
  (a*a+b*b-a*b*cos(theta)). Its maximum over positive lengths
  occurs at a=b, giving

      q_max(theta) = sqrt(3)*sin(theta)/(2-cos(theta)).

  Thus when the enforced planar fixed-corner wedge theta is acute,
  q_max(theta) < the usual minCapQuality=.10, and no triangle lying
  within that planar sector can pass the ordinary q floor. For an
  exception the ACTUAL triangle must additionally achieve >=65% of
  this best possible q, have q>=1e-4, nonzero finite area, a genuine
  unmodified R03 tip, the two remaining endpoints on separate original
  boundary segments, and meet the unchanged hard edge-length bound.
  This is NOT a general claim that no bulging 3D surface could improve
  its angles: bent/nonplanar or off-segment triangles get NO exception.
  The ordinary closed manifold, reflection and geometric contact
  audits stay mandatory. Each exception is counted in replay diagnostics,
  not silently treated as a generic low-q success.
* [#158](https://github.com/logiclrd/Mesher/pull/158): independent
  approximately-one-pixel antialiased cap/zipper and red diagnostic
  edges directly on the real colored triangles. Removes zoom-dependent
  thick world-space rods; W remains independent. Truly zero-area
  faces keep only tiny exceptional red edge rods.

## Measured exact 5K V1 replay behavior

* refusal-2 remains a genuine certified ZIPPER closure, with minimum
  q>=.10 (remesher primitive counts may change; acceptance does not
  depend on any particular algorithm).
* refusal-4 remains a genuine certified CAP closure, minimum q>=.10.
* refusal-5 now produces a NEW certified CAP closure: **72 green
  faces, min q=0.100624, 3 refinement splits, 1 interior fairing move**;
  old V1 originally refused after 512 subdivisions. CTest deliberately
  recognizes the replay's exit-code-2 OUTCOME_MISMATCH as a *reviewed
  recovery*, not a failure. The old .r06case file is unchanged.
* refusal-1 and refusal-3 STILL refuse with nearly zero q at generated
  pointy cap fans (~2.86e-13, ~2.72e-13). Both have used the finite
  32-vertex local fairing budget and need a larger topology/ribbon
  strategy (TODO M11A3b2/b3). Their quality is nowhere near the certified
  planar corner optimum, so merely removing q=.10 would be wrong.

## Testing

Use `--repair-replay repair-cases/refusal-N.r06case`
(`N=1..5`) with the same area classifier across builds.
For a refused case also test `--repair-quality-warn` to inspect
the untrusted staged geometry. Never confuse quality-warning preview
with AUTO_CLOSED. The real 5K case archive tests are in CTest and
distinguish reviewed recoveries (2,4,5) from still-refused (1,3).
Review changes in explicit new worktree, keeping main untouched.

For combined testing of the sibling thin-overlay branch and the
constrained-remeshing branch, fetch both PR heads locally, create
a worktree from PR160, and merge PR158 into that LOCAL worktree.
Neither GitHub draft branch nor main must be merged for inspection:

    git fetch origin pull/160/head:refs/heads/m11a3-geometry
    git fetch origin pull/158/head:refs/heads/m11a3-thin-inspection
    git worktree add ../Mesher-M11A3-acceptance m11a3-geometry
    cd ../Mesher-M11A3-acceptance
    git merge --no-ff --no-edit m11a3-thin-inspection
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

Manually inspect the three original refused physical cases from inside
and outside at several zoom levels, test W independently, and confirm
the red face/edge diagnostic when warning mode is chosen. Recovered
cases 2/4/5 should render green caps or blue zippers with thin dark
triangle edges. No PR is merged until the user confirms physical Vulkan
acceptance on the intended GPU and the full CI matrix passes.

Further publication of boundary-quality exceptions would require
recomputing the planar fixed-rim certificate after sculpt changes;
do NOT persist a permanent per-FaceId quality exemption.
