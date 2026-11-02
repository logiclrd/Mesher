# Crease and Pinch — geometry-directed brush design

Status: design accepted for implementation; CPU geometry and end-to-end physical
behaviour require independent verification. The design is shared between
**Crease** (inward, negative local Z) and **Pinch** (outward, positive local Z).

## Objective and core difference from Inflate

A new brush stroke creates a *dimple* on a flat, initially featureless surface,
then grows that dimple into a crease along the user's actual path. The line is
deduced from current deformed geometry (rather than imposed in an arbitrary
direction or directly copied from pointer travel). An already-defined crease
attracts new stamps automatically. Both brushes **draw existing surface
vertices toward the crease line**, deliberately making the surface's triangles
narrow and dense there. This density is intentional and must not be
aggressively collapsed or flipped away.

Inputs per stamp: contact point C on the surface, brush radius R, physical
crease width W, physical depth D, strength s, camera-facing direction n_view
and mode (Crease or Pinch). W, D, R are positive and scale with mesh units.
The sign convention is local Z *toward the camera*. Brush strength and
mode are latched with the worker's Begin operation so queued 4D sample
segments never change mode in mid-stroke.

## 1. Local, bounded support and surface distance

1. Query the old immutable RegionTopologyIndex's spatial buckets for all
   vertices whose Euclidean distance to C is <= R. This is only the first
   rejection stage; adjacent but disconnected sheets must not be mixed.
2. Anchor a shortest-path edge-graph distance field to the contact triangle
   (actual picked stable FaceId when provided), with the triangle's three
   corners initialized at their straight-line distances to C. Traverse
   adjacent mesh edges by bounded Dijkstra; stop at R. A vertex V enters
   the fit/deformation support only when its estimated R_V <= R.
3. A sampled 4D brush segment may not carry a fresh FaceId for every
   interpolated point. In that case select the nearest **triangle surface**
   from the bounded vertex halo by actual point-to-triangle distance (not
   nearest vertex), then seed its corners. Equidistant disconnected source
   sheets are ambiguous and must fail closed, not select an arbitrary
   component. Record R_V and frozen pre-stamp positions once. Compute all
   fits, weights and displacements from this same snapshot. The frozen
   edge-graph distance conservatively overestimates continuous geodesics;
   on very coarse meshes it may undershoot the visual circular footprint.
4. Bound the number of explored vertices, incident edges, candidate faces
   and total per-stamp copied bytes; exhausted work refuses privately.
   No full-mesh scan per stamp and no unbounded memory allocations.

## 2. Evolving best-fit surface plane

Compute each vertex's representative surface area A_i as one-third of its
incident triangle areas. Area-weight fitting makes the answer insensitive
to the deliberately high density of vertices inside old creases.

Using only the **real** support vertices (no virtual contact depression),
fit the weighted least-squares plane in 3D with weighted covariance. The
eigenvector of the *smallest* eigenvalue supplies the plane normal. Orient
it so +Z points toward the camera, then choose a deterministic XY basis.
A highly degenerate planar fit falls back to a finite area-weighted
surface normal. Recompute the plane on **every** stamp from the current
immutable surface. Its evolution after successive deformations is desired.

Use normalized, finite coordinates while solving eigenproblems; reject NaNs
and preserve physical scale invariance.

## 3. Signed evidence and an imaginary dimple

Let sigma=-1 for Crease and +1 for Pinch. A real vertex contributes
depression/ridge evidence only on the intended side of the plane:

    m_i = A_i * max(0, sigma*z_i - tau),  tau ~ 0.005 D.

This applies an absolute noise floor in proportion to depth rather than
amplifying arbitrarily small geometric noise. The real signal mass is

    M = sum_i m_i.

Add ONE virtual weighted point to the 2D line fit, located at the brush
contact's projected XY position. It represents the depression/ridge that
the current stamp will create, but must not modify the actual vertices or
the fitted surface plane. A reference weight proportional to a meaningful
single dimple, e.g. 0.04 * (support area) * D, is a starting default.

Fit the 2D weighted covariance of real evidence PLUS this virtual point.
The principal eigenvector (eigenvalue lambda1 >= lambda2) is the fitted
crease direction. Two separated dimples therefore give a dominant axis
along their separation, while the first virtual point alone cannot give
reliable line evidence. An established long line can dominate the virtual
contact, so moving over an existing crease follows its geometry.

## 4. Confidence: linearity AND real evidence

A 2D PCA line exists even for random noise, and two arbitrary points have
perfect theoretical linearity. Suppress weak/ambiguous fits continuously:

    L = (lambda1 - lambda2)/(lambda1 + lambda2 + ell0^2)
    E = M/(M + M0)
    k = clamp(L * E, 0, 1).

Here ell0 ~ 0.1 R is a minimum significant *spatial* separation and
M0 ~ 0.02 * (support area) * D is a reference amount of real deformation.
The virtual point contributes to the covariance but NEVER to M.

Expected evolution:
- Flat surface: M=0, k=0, no arbitrary line-oriented deformation.
- One first-stamp dimple: modest real M; the next virtual contact point
  induces a line along the previous and current contact positions.
- Scattered near-zero noise: M tiny, k tiny, no runaway amplification.
- Strong existing feature: lambda1 >> lambda2 and substantial M,
  k approaches 1, strongly line-directed geometry.

Confidence should be a *continuous factor* (not a yes/no PCA cutoff).
The line should be spatially stable under a triangle's ordering and
approximately insensitive to topology-density variations.

## 5. Lateral concentration and inverse-distance depth

Let d be the distance from the original XY point to the nearest point on
the fitted 2D line; let t_hat be the unit direction pointing **toward**
that line, zero at d=0. Let the central crease half-width be W/2:

    q = max(0, d - W/2)
    epsilon = 0.01 R
    g(q) = epsilon/(q + epsilon)
    h(R_V) = 1/(1 + R_V/R)
    a(R_V) = max(0, 1 - R_V/R).

The lateral displacement is toward the fitted axis:

    deltaXY = s_xy * k * q * t_hat * a(R_V).

The inward/outward signed depth displacement has two contributors:

    deltaZ = sigma * s_z * (k * g(q) + h(R_V)) * a(R_V).

The first is a steep reciprocal ramp near the crease edge; the second is
the contact-centred radial dimple/bump that bootstraps a direction on a
previously featureless surface. Only the *line-related* terms get factor k.
The second term is not suppressed by directional uncertainty.

These dimensionless inverse terms preserve the intent of D/|V_XY| and
D/(R_V+1) without making the result depend on the model's unit scale.
The epsilon offset keeps g finite at q=0 while preserving a dramatic
1/x-shaped shoulder. s_xy is a bounded dimensionless pinch fraction
derived from strength and D; s_z is the per-stamp physical depth magnitude.
The requested maximum D is a **depth cap**, not necessarily one stamp's
unattenuated movement. Strength and hardness/falloff remain explicitly
tunable; the zero-strength case is a no-op.

Compute d, q and all profile values from the **pre-update** local position.
After the simultaneous update, apply the directional conditional clamp:
for Crease, if old z is in [-D,0] and new z < -D, set z=-D;
for Pinch, if old z is in [0,D] and new z > D, set z=D.
Do not raise/lower vertices already beyond the depth cap before the stamp:
an evolving plane can validly leave old material below/above that plane.

Transform XYZ offsets back into world space and check finite coordinates.
The point/plane basis and vertex support for the stamp remain immutable
until the complete private geometry proposal is ready.

## 6. Refinement, symmetry, repair and publication

- Only split edges above the established maximum edge length; preserve
  interpolated vertex colour/provenance and reciprocal mirror pairs.
- Do not collapse newly compressed edges, smooth interior creases or
  opportunistically diagonal-flip them merely for regular spacing.
  Existing VersionedLocalRefiner and VersionedDirectionalCollapser are
  controlled per stamp; do not globally disable safety validation.
- For symmetric roots, solve one authoritative half and reflect its
  displacements across X=0, with self-mirrored seam vertices pinned to
  exactly X=0. No approximate mirror identity inference.
- Use existing versioned COW, R07 certified AUTO intersection repair,
  changed-owner diagnostics, finite-work budgets, single-root CAS
  publication, undo/redo, stroke sampling, worker queue, checkpointing,
  and failed-step rollback. A failed or uncertain step must not leak
  partial edits or update history.
- Refuse definite local degeneracy/folds that the repair machinery cannot
  certify. Dense small triangles are allowed; zero-area or inverted
  triangles are not.

## 7. Acceptance and benchmark protocol

CPU:
- Flat mesh: one contact stamp makes a circular dimple/bump with k=0,
  no imposed random line; second separated stamp introduces the axis
  parallel to the two contact points.
- A clear existing line dominates the virtual point; noise alone has
  low k; area-weighting holds under nonuniform tessellation.
- Positive/negative modes are reflected in local Z, while both attract
  XY geometry inward.
- Continuous dependence on W, D, strength, R and mesh scale; no division
  by zero, no invalid finite values, no accidental movement across
  disconnected close-by sheets.
- Each stamp sees frozen old geometry and order-independent fitting.
- Split-only ordinary refinement retains short edges, vertex colours,
  symmetry orbits and valid topology; no inadvertent face collapsing.
- Rejected candidate leaves published root/history unchanged; successful
  stamp can undo, redo and checkpoint.

Physical:
- Build a first crease on a smooth surface with a moving pointer.
- Follow an existing crease with repeated stamps and varying approach;
  inspect the evolution of the plane and the steep edge profile.
- Pinch mirrored sign on the same surface.
- Self-contact triggers R07 AUTO private repair, with certified closed
  final topology or a source-backed refusal.
- Check large/small model scale, locally tiny triangles, mirrored X=0 seam,
  repeat stamps and interactive 4D stroke responsiveness.
- Windows/MSVC, Linux/GCC/Clang and macOS CI are not substitutes for
  actual live Vulkan input acceptance.

## Tunables and intentional unknowns

The first defaults tau=0.005D, ell0=0.1R, M0=0.02*A*D,
virtual weight=0.04*A*D and epsilon=0.01R are initial
engineering values, not independently validated optimums.
Calibrate with CPU metrics and physical experiments, without tests
enshrining an undesirable artifact. Do not introduce a mandatory
stroke-direction state machine: the real+virtual geometry fit is the
authoritative direction.

Implementation should be staged on one draft PR: (1) this design;
(2) isolated CPU fitting/deformation and regressions; (3) versioned
worker/viewport integration, split-only refinement and AUTO;
(4) five-platform CI plus physical Linux/Windows checks before merge.

## Initial desktop controls (experimental)

Keys `1`, `2`, `3` select Inflate, Crease and Pinch between strokes.
`[` / `]` adjust W/R, `;` / `'` adjust D/R by factors of 1.25; initial
W/R=D/R=0.20. Alt temporarily swaps Crease/Pinch for one stroke, while
retaining its existing inverse-Inflate meaning in Inflate mode. The viewport
captures the camera-to-contact direction when the stroke begins. The worker
latches the mode and fractions with the first 4D sample; changes to sliders,
keys or camera do not change already queued samples. This is a preliminary
keyboard UI, pending physical acceptance before dedicated on-screen controls.
