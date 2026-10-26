# R06-M22 — Adaptive advancing-front cap replacement (2026-10-26)

## Decision and scope

Replace the fixed original-rim -> one inset vertex per rim vertex -> hub
**construction** within `buildSpatialCapFrontPoisson` with a local adaptive
advancing front, based on Zhao, Gao and Lin (2007), "A robust hole-filling
algorithm for triangular mesh", §4. The paper's normal-guided rotated-gradient
Poisson refinement (§5) is a separate, subsequent candidate. Do not conflate
these two hypotheses when comparing the recorded `refusal-1.r06case` ripple.

The current M17–M21 variants assume `n..2*n-1` are a corresponding inward
ring and `2*n` is a fixed hub. They MUST NOT be applied to a variable-size
advancing-front patch without structural adaptation. Preserve the old generator
for exact A/B comparison and as a *verified* complete-closure fallback; do not
rename a fixed ring operation and call it advancing front.

Use a staged, finite-work PRIVATE implementation on the same
`r06-integration` branch and draft PR #162. Do not publish a worker root.
Before any default change, test actual source-backed closure, not only local
patch quality. The existing rim (first boundaryCount entries) remains fixed
with exact original IDs/winding and no coordinate-near welding.

## Front rules

For every finite, oriented front contour maintain an ordered cyclic list
of local vertex IDs. Select the minimum **interior** corner angle, measured
consistently with the oriented local surface normal (distinguish concave
reflex corners from acute ones); stable-ID tie breaking. The Zhao et al.
construction uses <=75 / <=135 / >135 degrees for its ordinary corners.
Only construct local triangles from real front edges; test orientation,
q, collisions and budget transactionally before modifying the front.
The front is not itself the certified survivor mesh; the patch is not
accepted until the complete indexed source-backed validator succeeds.

For a true mirror-plane source, the original R03 contour has two *real*
X=0 endpoints, exact mirrored point/edge orbits, and NO original boundary
edge whose endpoints straddle X=0. Symmetric operations are selected by
orbit and staged atomically. An off-plane operation either constructs paired
oppositely wound faces or triggers this special **temporary front bridge**
case, when its selected vertex A is adjacent along an active front edge
to exact mirror A' (that edge can only be a generated FRONT edge, not an
original R03 survivor edge).

Define P,A,A',P' consecutive in the temporary front and require
P'=reflect(P), A'=reflect(A). Let S be the EXACT midpoint of AA' on X=0.
Let Q be the forward intersection of the chosen local corner bisector
with X=0. The user proposes three cases:

- angle <45: split temporary bridge AA' at S; add the two mirror
  triangles P-A-S and S-A'-P'; front P-A-A'-P' becomes P-S-P'.
- 45<=angle<90: insert S and Q; add P-A-Q, A-S-Q and their two mirror
  faces (four faces, NOT the diagram's three because the self-mirrored
  central face A-A'-Q is split along seam edge S-Q); front becomes P-Q-P'.
- angle >=90: insert S and Q; add A-S-Q and its mirror S-A'-Q;
  front becomes P-A-Q-A'-P'.

These are **front topology edits**, not permission to collapse original
survivor edges. The original proposed central self-mirrored triangle
A-A'-Q is mathematically reflection-symmetric, but would violate the
existing spatial cap rule forbidding off-plane triangles crossing X=0.
The S-Q seam construction preserves that rule. Q may coincide with S,
the bisector may not meet the forward seam, and the candidate may make a
skinny triangle: in each case reject/try another legal finite action and
eventually preserve the old valid cap rather than weaken q/contact tests.

If AA' already has a generated cap face on its other side, splitting AA'
must also split that incident face *using the same S ID* in the staged
patch. If that cannot be done legally, refuse the transaction. There must
be no duplication of a seam vertex under approximate coordinate welding.
Seam vertices are single self-mirrored orbit IDs and paired off-plane
vertices have exact reflected coordinates.

The paper's minimum-angle selection cannot be applied naively to paired
front vertices: choose the orbit, simulate BOTH halves, then commit both
or neither. Reflected doubles may tie at machine precision; choose a
canonical orbit by local index/immutable provenance. Never update one
half before deciding the other.

## Constraints and acceptance

- Original R03 rim positional and permanent ID identity; no source-rim
  edge splits outside the existing M15 source-backed transaction.
- All new patch triangles q>=0.10 at final acceptance; original survivor
  children q>=0.012 except already certified tip exceptions; no inverted
  faces, T-junctions, folded fronts or non-adjacent contacts.
- Exact reflection, edge incidence two, single manifold vertex fan,
  bounded face/interior/contact/attempt budget; maintain same cavity and
  source-owner policies as R06-M04.
- Existing valid original cap must survive optional new generator or solver
  failure, including **downstream full closure** failure: compare complete
  isolated staged candidates and commit only the chosen legal one.
- Comparative replay: (A) legacy ring/hub+legacy solver, (B) AFM
  topology+legacy generalized solver, (C) legacy topology+normal-guided
  rotated-gradient Poisson, (D) AFM+new Poisson. Start with (A)/(B).
  Record topology, vertex/face counts, q, initial/settled geometry,
  seam normal profiles, actual ripple location, collision work and
  PRIVATE full-source verdict. Physical GPU A/B is independent of CPU CI.
- The first limited unit tests cover the three seam-angle cases, symmetric
  face pairing, source edge immutability, bridge generated-face splitting,
  exact self-mirrored seam IDs, zero-area bisector, bad front, finite work,
  and atomic rejection. Additional tests: planar and warped disk, highly
  concave/long narrow contour, exact refusal-1 and refusal-3 replays,
  torus, hollow sphere, intentional crease and asymmetry.

## Flat milestones

M22A — Pure symmetry-aware advancing-front *operations* and independent
CPU cases including the three bridge rules. No default closure behavior.

M22B — Variable-connectivity patch generation and generalized fixed-rim
Poisson. In-place candidate through existing spatial cap entrypoint,
initially PRIVATE controlled A/B; disable ring-dependent M17–M21
optimizers explicitly for this candidate.

M22C — Whole source-backed closure validation and atomic original-cap
fallback. Preserve legal closure even when speculative front fails.

M22D — Independent normal-guided rotated-triangle gradient Poisson
candidate; first test against SAME triangulations and normal references.

M22E — GPU matched-view refusal-1/3, three-platform CI, geometric
acceptance/performance and explicit decision on making AFM default.
