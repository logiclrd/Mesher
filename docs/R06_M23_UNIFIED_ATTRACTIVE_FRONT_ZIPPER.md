# R06-M23 — One advancing-front cap/zipper engine with optional attractive placement

**Design specification / preimplementation plan, 2026-10-26.**
Related: [M22 advancing-front cap](R06_M22_ADVANCING_FRONT_REPLACEMENT.md),
[R04 forced zipper history](R04_FORCED_ZIPPER_ACCEPTANCE.md),
[R03 excision](R03_PRIVATE_EXCISION_ACCEPTANCE.md).
The authoritative two-task implementation plan is in [todo.md](../todo.md).
**This document records agreed goals, proposed mechanisms and acceptance
requirements; it does not claim any zipper implementation or acceptance.**

## Motivation and observations

- M22's progressing triangular advancing front produced a visually healthy
  `refusal-1` cap on the Windows Vulkan desktop. Its terminal fan was subsequently
  improved by the existing quality-refinement stage. `refusal-1` is no
  longer a refusal on that observed current AFM route; do not confuse the
  historical saved-case expected refusal with a new target behavior.
- `refusal-2` is a zipper. Its generated blue strip shows fixed fans, long
  narrow triangles, apparent jaggedness and quality refinement that can
  split both new zipper faces and adjacent surviving faces without improving
  acute angles. Record BEFORE/AFTER shape and connectivity metrics.
- R06's mirrored special case currently places one X=0 seam vertex for each
  original contour vertex and makes four triangles per original edge pair;
  the nonsymmetric case invokes `triangulatePrivateZipperStrip`, an
  arc-length-aware DP between two rings. We want to replace BOTH branches
  with one unified construction algorithm rather than maintain a separate
  symmetric generator.
- R03 deletes intersecting faces, generally separating confronting survivor
  rims by an order of two typical mesh-edge lengths. This is a useful
  working expectation, **not** a certified minimum separation. A connection
  must still be quality/contact checked. Since R03 already removes substantial
  deformed surface and later quality stages already split survivor faces,
  slight validated survivor modifications are not categorically forbidden:
  healthy topology/geometry takes priority over absolute preservation of
  every nearby survivor coordinate or vertex. Unrelated source edits remain
  forbidden.

## Fundamental shape: one engine for caps and zippers

1. Use the **same** advancing-front topology/triangle constructor that R06
   uses for ordinary caps. A cap has one cyclic front and no attraction.
   A zipper initially has two (or more region-authorized) directed fronts
   and a pairwise attraction field. A finished, finite-width bridge
   converts two fronts to ONE ordinary *simple* cyclic contour; then the
   shared cap engine fills it. The zipper may keep attraction enabled
   across this handoff.
2. At each generation step consider the smallest admissible interior-angle
   **front occurrence**, deterministically breaking ties. For zippering,
   examine a legal connection to the opposite rim before continuing with
   the ordinary cap templates. Cross-contour candidates must be from the
   already-authorized M03 pair/region; never stitch unrelated islands.
3. For an ordinary angle below or equal to about 75 degrees, consume the
   corner by joining its neighbors with a triangle (subject to quality,
   orientation and contact). For >75 and <=135 degrees insert one projected
   vertex / two triangles; for >135 degrees insert two vertices / three
   triangles, subject to M22's existing actual template/seam variants,
   scale and progress controls. Reflection, when required, is part of
   **the same logical atomic operation**, not a different triangulator.
4. Generate each candidate at the existing local geometrically projected
   point, optionally adjust it using the callback described below, then
   evaluate the **adjusted** proposal through the normal finite, winding,
   quality, contact, embedding and front-topology gates. Do this for BOTH
   generated points of a three-triangle turn. A rejected proposal does not
   change the live private front. Attempt alternative legal proposals with
   bounded work and deterministic tie-breaking.
5. Once the connected front is ordinary/simple, keep the same logic and
   Poisson reconstruction used by R06 caps; the caller-provided positional
   adjustment remains available, so gravity does not abruptly stop at
   bridge time. Preserve seed-side ancestry through the merge. The cap
   constructor itself does not need to know what zippering is.

**Do not accidentally equate symmetry enabled with a symmetric zipper.**
A symmetric *mesh* may have a zipper pair which is not self-reflected; it
may need a second reflected zipper operation elsewhere. Pair all
applicable topology and geometry by exact reflection orbits and accept
them atomically. If the geometry is no longer topologically symmetric,
the engine works on independent asymmetric fronts even when mirrored
brush strokes are requested. Seam fixed points remain exactly X=0
when exact symmetry is required. Never permit an off-plane triangle or
edge to cross the symmetry plane without legal indexed seam topology.

## Proximity bridge selection

Let A be the selected small-angle front vertex, and B a closest vertex
of its *opposing* contour. Compare |A-B| with the two adjacent A-to-front
edge lengths, as proposed: when B is closer than the nearby same-front
neighbors, a connection becomes a candidate. If B lies uncomfortably
close to one of A's neighbors, also evaluate the nearer neighbor of B
on the opposing contour; do not force a pathological connection through
the geometrically nearest index. The nearest geometric point is a
*proposal*, not authority to weld IDs or skip quality checks.

Evaluate a small bounded group of nearby candidate vertices, prospective
new patch faces and their connection to surviving faces as a unit. Prefer
high minimum triangle quality, useful local edge lengths, smooth normal
continuation, low local vertex valence and deterministic ties; reject
self-crossing, invalid winding, source intersection, T-junction and
nonmanifold candidates. Close-by source vertices are not automatically
the same stable ID.

**Do not use a zero-width, edge-only or point-only bridge.** In our
discussion, merely adding B-D-B' across a reflected pair creates a
weakly simple front with repeated B, B' and D occurrences. For example
`...A-B-D-B'-A'...C'-B'-D-B-C...` is a meaningful cyclic *walk* with
different angles A-B-D and D-B-C, but D's two adjacent vectors B-D
and B'-D are opposite. There need not be a defined local projection
bisector or trustworthy incident-face normal; processing that bridge
could lead to degenerate triangles, zero-area terminal pockets or an
ill-defined normal. We considered expanding support along both sides
(`L-M-N-O-B-D-B'-O'-N'-M'-L'`), incident-side identity, topological
cancellation of opposite traversals, and, as last resort, bounded
local/survivor contractions. These remain viable *general recovery
ideas* but are **not** the preferred bridge design: seed real finite-width
triangles immediately so that the downstream cap front stays simple
and has real attached faces to follow.

### General nonsymmetric bridge: proposed three-face seed

For candidate opposing vertices B and B', with appropriate contour
neighbors C and C' on the side being consumed, introduce K near

```text
K0 = (B + B') / 2 + (sqrt(3)/2) * |B-B'| * t
```

where t is a unit direction perpendicular to B-B', chosen from the
geometrically admissible side of the prospective patch. Evaluate
orientation(s), deviations of K and several bounded alternatives for
full quality and collision safety. The initial midpoint/offset makes
B-K-B' equilateral *if* t is exactly perpendicular; it does NOT
guarantee C-B-K or C'-B'-K are healthy.

Proposed new triangles, with exact winding determined from source:

```text
B-K-B'    C-B-K    C'-B'-K
```

These seed a positive-area strip attached to the two original contours,
rather than a virtual two-sided bridge. Update both directed fronts and
their source incidence *atomically*, then obtain one simple cyclic front
with complete directed boundary coverage. Independently validate that
the three faces really produce a correct manifold bridge for the chosen
neighbor orientation; the template alone is not sufficient proof.

### Exact self-reflected bridge: proposed four-face seed

Let B' = reflect(B), C' = reflect(C). If the connection intersects
the X=0 plane, select an exact seam vertex D there (for B joined directly
to B', D may be the midpoint), and **another distinct seam vertex K**
offset in the plane and positioned to produce real area. Proposed faces:

```text
B-D-K    B'-D-K    B-K-C    B'-K-C'
```

The two pairs are reflected with reversed winding and share exactly
the *indexed*, not merely coincident, seam edge D-K where appropriate.
The resulting merged front contains distinct seam nodes rather than a
double-traversed zero-area string. D and K must not accidentally coincide
or produce disconnected vertex fans. If connecting general A to B
crosses with its reflected A' to B', determine their legal exact plane
intersection C and stage all required bridge/mirror faces together:
do not assume the midpoint rule for the A=B' case handles all orbits.

These three-/four-triangle forms are **initial candidate templates**
for their specific local incidence arrangement, not a fixed forced
replacement for every symmetric/asymmetric situation. Reflected mesh
symmetry must also support non-self-reflected zipper pairs and their
separate mirrored operations. Refuse or try a different candidate if
the orbit or manifold/winding requirements cannot be satisfied.

### The merged front

An admissible finite-width bridge should yield a simple cyclic
*vertex-ID* boundary with no duplicate occurrence of its seed connection
vertices. The simple-front condition must be checked from actual
edge-incidence and vertex-link topology; coincident XYZ alone does not
identify vertices. Resume the normal cap advancing front on that
boundary. It may be necessary to evaluate orientation and front
component accounting before reducing two open contours to one disk;
never assume a bridge template is topologically correct merely because
its triangles look plausible.

## Opposing-contour attraction ('gravity')

Initial AFM projection remains the geometric baseline. For each *new*
vertex proposal P, calculate attraction toward vertices Q_i on the
other originating side's active front, using equal vertex mass initially:

```text
F(P) = sum_i (Q_i - P) / (|Q_i - P|^2 + softening^2)^((p+1)/2)
p = 1.4 initially
```

The individual unsoftened force magnitude falls as 1/r^p.
Softening prevents singularities; all quantities and work are bounded.
A scalar gain converts the field into a displacement, optionally
clipped to a local trust radius (initial experiment around 0.3 times
the shorter incident front edge). Preserve some distance-dependent
strength as the gap narrows: normalizing F to unit length everywhere
would remove that variation. Evaluate a bounded set of gains, including
no adjustment, and select only admissible geometry. Tune empirically,
not from an untested physical-force interpretation.

Equal-mass vertices may over-attract toward densely sampled/jagged
patches of a contour. Measure this explicitly; weighting by surrounding
arc length is a possible later alternative if clustering proves harmful.
Keep a deterministic, bounded opposing set and finite O(front size)
or indexed candidate work; do not pull vertices toward their own
ancestral contour. An attractive displacement does not confer legal
topology, quality, collision or mirror status.

Track immutable **side-of-origin / opposite-front ancestry** separately
from vertex ID and from whether the boundaries have become one loop.
New vertices inherit ancestry from the front operation; the bridge
can have neutral/blended/cross-side provenance explicitly specified.
After the bridge, use the ancestry already carried by the zipper
controller to identify which opposing vertices attract a new proposal;
do not reclassify by closest XYZ or symmetry plane. If no meaningful
opposing side remains for a candidate, its callback can return the
original proposal. On reflected mesh operations compute once per orbit,
reflect the result exactly, and validate the complete proposed orbit.

## Mandatory extension point in existing cap generator

Add an optional, caller-provided vertex-placement adjustment callback
to the advancing-front cap construction entry point, with default
`nullptr` (or equivalent empty non-owning callback). Existing ordinary
cap call sites explicitly pass `nullptr` for the first milestone,
and geometry/diagnostics/Poisson should be unchanged bit-for-bit when
it is null. Avoid `std::function` allocations or a per-vertex
classification pass on the ordinary no-op path where practical.

The callback receives the proposed local position, selected front
occurrence or ID, local scale and enough operation context to use the
zipper controller's precomputed ancestry; it returns an adjusted
position (or no change). **The callback is a placement proposal only.**
The cap constructor remains the owner of mirror constraints, provisional
IDs, immutable rim positions, front updates, full face/quality/embedding
checks and finite work/refusal. Invocation belongs between initial
projection and geometric checks/commit, including the one- and
two-vertex turns, NOT after committing a candidate. It should never
alter existing survivor or original rim vertices. If a proposal is
mirrored, callback execution and returned coordinates must produce
an exact reflected pair atomically; seam proposals remain X=0.

The zipper should pass a *thin capturing wrapper* to the cap generator:
the core gravity function takes an already known opposing target set and
does NOT need to rediscover which side of the zipper its P belongs to.
The wrapper consults the controller's operation/ancestry context,
chooses the correct opposing side, then delegates to the core force
computation. There is no zipper algorithm branch inside the cap engine.

**Poisson caveat:** the existing generic fixed-rim graph Poisson solve can
move newly generated vertices after the attractive placement. M23B must
measure whether attractive bending survives. If not, add an optional
compatible positional guidance/anchor to that solve (null path unchanged)
and revalidate full quality/contact. Do not add a last-minute unchecked
post-Poisson displacement, and do not let this future behavior enter the
independent no-op M23A change.

## Survivor geometry and degenerate recovery policy

A zero-width bridge should be prevented upfront, not fed to an
extended weakly simple cap routine. Nonetheless, if a later front has a
tiny nearly-zero-area pocket, preferentially test a legal local
contraction over fabricating arbitrarily skinny triangles. For the
illustrative B-D-B'-K pocket, K might safely contract onto D and
cancel opposite boundary traversals while leaving B and B' intact.
Some genuine tiny multi-vertex terminal holes may need a broader
source-backed contraction. **Do not assert unconditional ability to
collapse any remaining loop to a single vertex**: a collapse can invert
survivor faces, destroy their unique IDs, or pinch the vertex link.
Propose the full local change, including removal/retriangulation of
affected survivor faces where necessary, check small diameter as well
as small area (a long thin strip must not be wholesale crushed), and
accept only a legal private closed manifold. A rejected contraction
leaves the immutable R03 source untouched. This is optional recovery
for the later implementation, not a substitute for the finite bridge.

Source-backed neighbor splits/flips and carefully bounded motions/
contractions are permitted in principle when objectively healthier,
including affected survivors; they must be journaled, mirrored where
required, quality/contact/vertex-fan certified, and never silently
leak to history. The current original-face source-view limitations are
implementation constraints to adapt consciously, not reasons to
introduce geometric cracks or nonmanifold welding.

## Deterministic validation and observability

- M23A: a truly no-op callback interface change. With null at every
  current caller, preserve existing R06 ordinary-cap and forced-R05
  numerical paths, source geometry, mirror orbits, refusal reasons,
  step-debug frames and Poisson results; CI on GCC, Clang and MSVC /W4
  /WX. Independently test invocation exactly when a new position is
  proposed, returning the same coordinate, bounded modified coordinate,
  two-vertex cases, rejection and mirror handling.
- M23B: synthetic asymmetric zipper, self-reflected zipper, symmetric
  **non-self-reflected** zipper orbit pair, dissimilar contour counts,
  stepped/aliased jagged R03 rims, unequal edge lengths, zero-width
  bridge avoidance, near-plane placements, crossed/close contacts,
  badly clustered opposing samples, degenerate/ambiguous candidate
  and finite-budget refusal, multiple unrelated islands, plus archived
  `refusal-2` physical replay. Existing `refusal-1` ordinary cap must
  remain numerically and visually unchanged.
- Log initial zipper and finished result independently: new vertex/edge/
  face counts, actual sewn rim edges, Euler/boundary and vertex-link
  checks, triangle quality min/p10/median/p90/max, worst 10% mean,
  max/median edge ratio to survivor scale, new vertex valence,
  cross-face normal jumps, number of survivor edits/splits, attraction
  acceptance/rejection and Poisson-induced displacement, paired orbit
  counts, real finiteness/proposal budgets. Do not conflate a locally
  acceptable patch with completed source-certified repair.
- All changes are PRIVATE until exact accepted final root exists:
  directed rim identity, correct orientation, min generated q>=0.10,
  accepted survivor quality floor >=0.012 with only existing explicit
  exceptions, bounded work, no new T-junction/contacts, connected
  oriented closed two-manifold with valid vertex links, no forbidden
  sealed interior cavity, deterministic reflection and unaffected
  async worker/undo/history/renderer. Failed candidates refuse cleanly
  or preserve an independently verified existing legal closure.
- M23B may switch the R06 zipper call site from both existing zipper
  generator branches to the unified one **in the same completed task**
  only after the new constructor and matching full tests are genuinely
  complete; no premature change of R06 defaults. Preserve historical
  R04 forced zipper behavior unless separately authorized. Keep an
  explicit old-vs-new comparison path for debug/physical testing until
  accepted. Windows Vulkan inspection remains a distinct acceptance
  gate; passing headless CI alone does not certify visible smoothness.

## Two flat independent tasks

**M23A — Optional cap placement adjustment (no-op integration).**
Expose the callback at the appropriate public/internal cap construction
boundary, pass nullptr from every current caller and preserve default
behavior. Test the callback in isolation and original null regressions.
This is one independently finishable interface-only change.

**M23B — Unified attractive-front zipper, bridge and cap reuse.**
Implement the two-front advancing logic, proximity seed evaluation,
finite-width 3/4-face templates with general symmetry orbits,
side ancestry, equal-mass softened p=1.4 attraction, cap callback wrapper,
merge to ordinary simple front, full source-backed validation and
diagnostics. Include the call-site switch only if complete and tested
in this same task. Otherwise keep the existing R06 path and leave the
task open. Do not fragment into ad-hoc partly checked-off subitems:
if full M23B later proves too broad, preplan its entire independent
flat peer sequence in `todo.md` before implementing the first part.
