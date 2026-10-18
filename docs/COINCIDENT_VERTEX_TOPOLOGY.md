# Coincident geometry, provenance, and vertex identity

**Status:** Normative design; the bounded V17-M16 regression domain
is accepted at GCC/Clang/MSVC code head 4979b25 (Actions 39144616634).
This is NOT an assertion that every coincident-vertex configuration
below is a supported Boolean union. Established 2026-10-18.
**Owners:** [V17-M16](V17_MILESTONES.md#v17-m16--generic-coincident-geometry-and-seam-equivalence-acceptance)
(the bounded off-plane/general acceptance gate, scheduled after M10 and before
M11); M10 owns the broader adversarial refusal matrix; V19 owns stress,
imported meshes and device-scale generality. The already accepted M04/M05/M08/
M09 proofs remain valid within their stated fixture domains.

## Fundamental contract

Two VertexIds can occupy the identical 3D location without being the SAME
topological vertex. Conversely, after an explicitly certified surface union,
some independently allocated seam VertexIds must become ONE vertex. Decide
which situation applies from certified topology and geometry provenance,
**never from location, a distance tolerance, or a reflection orbit alone**.

- **Distinct coincident identities:** Preserve distinct IDs when surfaces
  remain independent (including tangential contact, overlapping but unjoined
  shells, independently generated junction endpoints, or protected cavity
  geometry). Identical XYZ does not make a global mesh invalid by itself;
  whether the requested Boolean operation is supported is a separate matter.
- **Certified equivalent identities:** A final seam class may be welded only
  when the *complete* original V13 contact evidence authorizes both surfaces'
  corresponding finite directed survivor intervals; c1/c2/c3 and any M03
  untouched-original retirement independently agree; the local cut is
  conforming on ALL incident source faces; all endpoints match EXACTLY in 3D;
  surviving seam directions are opposite; and the proposed final vertex links,
  edge incidences, winding, area/quality and collision checks pass.
- **Unproven or conflicting identities:** Refuse the entire proposed Step.
  Do not auto-snap, use a nearest-neighbor search, silently fuse a shared
  corner or create an intermediate public cut/normalization root. A safe
  refusal is NOT a successful supported Boolean union.

A mesh may have two independently valid closed components touching at one
coordinate; merging only the coincident vertices would create a disconnected
one-ring at that vertex, which is non-manifold. A proper Boolean union must
establish which faces are removed and which entire loops/links may be joined
before any stable ID is substituted. Separate source point indices with the
same XYZ are intentional in the *private cut*. They become one final ID only
after the join proof, rather than during arrangement or materialization.

## Reflection is an extra invariant, not welding authority

The currently implemented reflection is R(x,y,z)=(-x,y,z).

- **X != 0:** A mirrored vertex orbit has two reciprocal IDs. Two distinct
  source vertices at the same off-plane XYZ are *not* automatically one ID.
  A supported reflected weld must be independently source-certified on both
  sides; its final classes must remain valid reciprocal reflection orbits.
- **X = 0:** A genuinely fixed vertex orbit has exactly one self-mirrored ID.
  It can be introduced or retained as a single seam representative ONLY
  with source-proven fixed-point identity, exact on-plane coordinates and a
  complete valid reflected incident-face fan. Two unrelated vertices that
  merely happen to lie at the same point on X=0 are not equivalent.
  Reflection does not authorize welding separate touching components or
  joining unproven one-rings.
- **Independent of orientation:** The coincidence/provenance/manifold rules
  are coordinate-free. The implementation's current `X=0` checks are tied
  to the chosen reflection plane; this document does not claim arbitrary
  reflection-plane support. Generalizing the reflection transform is a
  separate architectural enhancement, not a requirement of M16.

Exact matching concerns equality of *source-certified reconstructed seam
coordinates*, not the globally valid assertion that every equal coordinate
should become one ID. Near-equal but distinct points must not be moved or
merged as a convenience. Numerical uncertainty is a supported reason to
refuse, not a reason to relax provenance.

## Required decision and validation sequence

1. Discover the complete bounded original V13 contact set, retaining original
   FaceId pairs, finite intersection sections and source corner/edge/face
   owners. No selected-pair shortcut. Original and candidate roots must be
   immutable until one final publication.
2. Construct locally conforming cut points and source-owned child faces,
   retaining separate identities across independent source sheets even where
   positions coincide, and retaining exact original surface coordinates.
3. Independently classify c1/c2/c3 and M03 retirement eligibility. Distinguish
   actual transverse contact curves from tangencies, zero-dimensional touch,
   coplanar overlaps and ambiguously overlapping inputs. No retirement or
   join can be inferred from coincident positions.
4. Match complete opposite surviving original-contact intervals with
   source-paired endpoints. Reconcile differing subdivisions where already
   supported; derive vertex equivalence from these certified intervals,
   never from global XYZ deduplication.
5. Validate each proposed equivalence class and the ENTIRE final topology:
   no surviving degenerate triangle, correct local winding and edge pairing,
   one connected incident link per surviving vertex, correct components and
   cavity boundaries, no nonadjacent residual intersections, valid exact
   reciprocal/self mirror orbits, stable ID/tombstone journals and quality
   bounds. Reject conflicts such as four incompatible incident sheets.
6. Publish only the terminal original-root revision, once, with one net
   history/undo/checkpoint and mirror/dirty journal (where applicable).
   A refusal leaves the old root, IDs, old readers, queue and history intact.

These are *validation obligations*, not a mandate to build a global duplicate
vertex normalizer or to reject all meshes with coincident-but-distinct IDs.

## V17-M16 independently completable acceptance fixtures

**Required supported outcomes:**

1. An explicitly source-certified **off-X=0** transverse intersection
   with independently generated geometrically coincident seam endpoints
   yields one correctly connected, closed, oriented manifold Boolean
   union, with verified source interval pairing and welded terminal IDs.
   Exercise asymmetric and reflected off-plane variants where the existing
   accepted domain permits them, including a mismatch in local subdivision.
2. A valid, intentionally unjoined configuration with different VertexIds
   sharing exact XYZ preserves both IDs and their separate incident fans
   through a read-only classification/no-op or safely refuses an unsupported
   requested union. Demonstrate both off-plane and on-plane examples.
3. Existing M04 (asymmetric three-body), M05 (reflected off-plane) and
   M08/M09 (source-certified fixed-plane) positive control fixtures still
   pass without changed historical original coordinates or mirror journals.

**Required refusal/identity-preservation outcomes:**

- Coincident but unrelated isolated corners; vertex/edge touch without a
  complete transverse seam; coincident vertices from different protected
  shells/cavities; and two fans whose naive union would be disconnected.
- Overlapping/coplanar patches, tangency, incomplete original V13 FaceId
  pairs or missing reflected source partners, and incompatible 3+/4-sheet
  junctions without a proven single manifold link.
- Near-but-not-exact source points, almost matching sections, inconsistent
  edge subdivisions, forged source ownership and forged self-mirror maps.
- Degenerate child triangles, inverted edges, lost cavity/exterior faces,
  asymmetric retirement, collisions after an otherwise plausible weld and
  exhausted independent work/quality budgets.

Every fixture must check **both** source-owned ID relationships *before*
the candidate weld and the terminal edge/vertex-link manifold structure
*after* it. A negative requires no partially usable cut/join result or
public revision; on worker paths, no lost 4D input, history, tombstones,
checkpoint or dirty/mirror changes. Repeat with reversed contact discovery
order and unrelated duplicate vertices near the active repair halo.
Explicitly report accepted / safely refused / unsupported, rather than
counting a safe refusal as a positive union.

## Bounded implementation and independent evidence

The concrete fixture-by-fixture contract is now recorded in
[V17_M16_COINCIDENT_IDENTITY_ACCEPTANCE.md](V17_M16_COINCIDENT_IDENTITY_ACCEPTANCE.md).
Its off-plane *positive* two-sheet cut and original-root Boolean union
must retain different stable VertexIds at EXACT equal positions in
the conformance stage, but leave only source-authorized final seam
representatives after a complete d2 join. The separate reflected
off-plane positive control requires two distinct reciprocal joined
components and a valid connected incident fan at every vertex.

The deliberately *negative/preservation* tests retain independent
on-plane/off-plane tangent vertex identities, a four-way exact point
of tangency, a 1e-7 near-miss, an unrelated tangent shell near the
active source repair region, and missing/forged/over-budget source
inputs. These are not claims that arbitrary 3D coincident data
is automatically weldable. A fully closed manifold union must
actually be proved; a refused source operation is not a positive
Boolean result.

## Scope and independently accepted delivery boundary

M16 is a distinct flat **peer** milestone, not a recursive subdivision of
M10 or reopened M06–M09. Schedule M10 then M16 before M11's repeated
physical unions and M15's opt-in activation. Historical IDs M01–M15
remain unchanged; the numeric suffix M16 is append-only and does not
denote execution order. If implementation reveals an independently large
new obligation, replan with named peer milestones *before* beginning
partial implementation; do not add recursive M16a/M16b tasks.

M16 proves a bounded, source-certified coincidence domain. It does NOT
promise arbitrary tangential/coplanar Boolean reconstruction, sanitizing
invalid imported meshes, global duplicate elimination, generic N-way
junctions, arbitrary reflection transforms, exact swept CCD, or universal
performance. Those cases must remain fail-closed in V17; extensive
robustness and imported-geometry stress belong to V19.
