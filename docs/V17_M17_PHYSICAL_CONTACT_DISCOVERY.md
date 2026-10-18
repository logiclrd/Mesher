# V17-M17 — complete candidate-root physical source-contact discovery

## Contract

This is the independent **read-only** gate between the proven exact
source-paired Boolean work (V17-M01–M10 and M16) and physically
triggered sculpt publication (M18, then original M11 repeat acceptance).
The input is the ACTUAL unpublished immutable
`ProposedSculptStep.candidate` from Inflate, local split/flip and
collapse, not the prior published `before` root and not a selected
pair of V16 approximate contour scars. FaceId and VertexId are
stable candidate-source identities; no global XYZ deduplication is
allowed.

`assessPostSculptOriginalContacts(proposed,symmetric,limits,mirrors)`
returns one of:

- **Clean**: the bounded complete query on this candidate found *zero
  reported V13 surface contacts*. It does not prove arbitrary
  coplanar/nested/touching Boolean geometry or authorize retirement.
  An unchanged old root may itself contain contacts; the source root
  in the returned result is still the supplied candidate.
- **CompleteTransverse**: all queried original candidate FaceId pairs
  were returned unambiguously, deduplicated/canonicalized and each
  independently has a finite supported V13 transverse segment
  through `splitTransverseTriangleContact`. The returned source
  root and full original contacts can be supplied unchanged to the
  downstream exact private cutter. This certificate alone does **not**
  authorize welding or a sculpt Step.
- **Refused**: an absent original/candidate root; live candidate
  vertex/face budget exhaustion; incomplete or physically invalid
  reciprocal/self symmetry map; candidate collision-query exhaustion;
  too many original sections; or any ambiguous, coplanar,
  grazing, tangential or otherwise unsupported source pair.
  An explicit diagnostic is returned; contacts and candidate
  authorization are empty. Diagnostics may retain a discovered pair
  count and bounded collision query statistics, but NEVER an
  independently consumable partial source graph.

The candidate query seeds **every live candidate VertexId** rather
than relying on `changedFaces` to enumerate the source graph. The
indexed per-query BVH remains bounded (not an unbounded O(F²)
face-pair iteration). All queried contacts belong to the same
immutable candidate root. Before classifying a symmetric result,
the whole live vertex involution must have one mapping per live
ID, reciprocal links, exact reflected XYZ, and true self-paired
IDs iff the coordinate is precisely on X=0. Neither closeness nor
coincident unrelated vertices provide mirror or weld authority.

The stage allocates no new stable IDs, performs no c1/c2/c3
retirement or d2 join, changes no mirror controller, appends no
history, calls no original-root CAS and publishes no dirty
partition. A call's source and candidate shared pointers are
read-only; it cannot contaminate an old picking/BVH reader.

## Positive and refusal tests

The `repair_surgery_tests` acceptance test explicitly creates
candidate revisions with **actual VersionedInflateBrush stamps**:

1. An already-supported three-contact, off-X=0 two-tetra
   geometry is physically moved without erasing its finite
   transverse source sections. The certificate must equal an
   independent complete live-vertex query on the *new* root,
   including all original FaceId pairs.
2. A reflected two-half version of the same shape has a physical
   paired stamp and exact reciprocal mirror map; **both** sets
   of contacts must appear. A corrupted map must refuse before
   providing any source identity.
3. Two initially disjoint closed tetrahedra undergo actual
   normal-driven Inflate movement. A finite transverse candidate
   discovered **after** the motion must be positively identified
   from its own root. The pre-stamp source has no intersections.
   No preselected pair list, hand-written source contour or
   post-stamp region edit is accepted as a substitute.
4. Exact insufficient section-count and V13 reported-contact
   budgets, and a too-small global source-root face limit, yield
   complete diagnostic refusals with no partial source graph.
   Old immutable roots retain original FaceId/VertexId/tombstones
   and stable picking identity.

Other M10/M16 adversarial fixtures continue to constrain
unsupported tangent, coplanar and coincident geometry. A physical
candidate that has some finite sections but also ONE unsupported
source pair must not be treated as a legal positive union.

## Boundary and next delivery

**M18** integrates this read-only source graph with a true closed
source-certified 2→1 Boolean result, and composes an original-root
atomic sculpt Step spanning earlier Inflate/refine/collapse and
final c1/c2/c3/d2. The fixed parent root and its complete mirror
history need one net journal, not separate preliminary CAS
transactions. The original **M11** then demonstrates real
repeated physically triggered symmetric contact unions over
sixteen alternating tower inputs, FIFO queue drain, clean/rejected
continuation and one history entry per accepted Step.

No M17 success implies a positive M18 or M11 Boolean union;
refused geometry does **not** count as successful merging.
The desktop Merge geometry switch remains disabled (M15),
V18 Off-mode constraints are separate, and broad imported
pathological geometries remain V19.
