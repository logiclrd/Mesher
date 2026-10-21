# V18-M01 — Immutable local before/after OFF-mode contact ledger

## Placement in the original V18 plan

V18 is the **separately opt-in** `Merge geometry: Off` new-crossing
prevention policy. The original `todo.md` objective is unchanged.
The complete flat peer sequence M01–M07 was written to `todo.md`
**before** M01 implementation; later items own full Step admission,
mesh-scale clearance and permitted sliding, constrained normal motion,
mirrored both-sheet input, desktop opt-in, and final user/CI acceptance.

**M01 deliberately does NOT touch the live worker, any default CLI,
geometry publication, curvature/refinement/collapse settings, V17
source-positive Merge surgery, or checkpoint schemas.** It is an
immutable read-only evidence prerequisite rather than a claim that
new crossings are already prevented.

## Exact contract

`assessOffModePostStateContacts(before,candidate,changedVertices,changedFaces,limits)`
reads the real `RegionTopologyIndex` immutable BVH on the *before*
root and on the complete **post-Inflate/refine/collapse candidate**,
never two different aged sheets stitched into one false state.
The passed changed VertexIds and FaceIds are local net-dirty stable
identities, including old and new ids across topology turnover;
the helper does not infer identities from equal world XYZ.

- Both queries are independently finite: max seeded faces 4096,
  box tests 131072, partition tests 32768, candidate triangles
  65536, narrow-phase tests 32768, and reported contacts 4096;
  the caller can lower any. Input stable ID records have a separate
  16384 total default limit. Real stage work is observed separately.
  Budget exhaustion/invalid ID/invalid topology produces
  `Uncertain`, NEVER a truncated `Clean` result.
- `Clean`: no detected nonadjacent candidate contact among the
  complete caller-specified changed neighborhood.
- `UnchangedPreexisting`: post-state contacts already existed by
  the SAME stable FaceId pair and both entire triangles (stable
  corner IDs and all endpoint positions) are exactly unchanged.
  This is a conservative *read-only grandfather classification*,
  not permission to alter the old collision.
- `NewContact`: candidate has at least one **new definite** stable
  nonadjacent contacting pair relative to the locally complete
  before-state query. Such contacts are not silently repaired.
- `Uncertain`: new ambiguous contact, previously intersecting
  faces that moved even with the same IDs, original ambiguous
  contacts that persist, unknown IDs, missing scope for distinct
  roots or bounded-work failure. Refuse rather than inferring
  that a moved pre-existing intersection was not worsened.
  `introduced` is separately available if both new contacts
  and unrelated uncertainties coexist; no partial results are
  returned when either finite query aborts.

A contact becoming disjoint counts as resolved original contact
**only in the read-only report**; the policy does not edit/repair
the initial mesh. Unrelated old collisions outside the changed
local neighborhood aren't enumerated or globally rejected.

The underlying V13 `triangleContact` intentionally reports
independent tangencies in the contact set. M03 must separately
certify legitimate mesh-scale tangency/sliding before any live
accepted admission is broadened. A fast single Step can tunnel
between the two measured endpoints; this is NOT swept CCD.
Likewise, until M02 verifies the complete worker-side dirty
records, caller-supplied incomplete changed IDs cannot constitute
a trustworthy whole-Step no-crossing certificate.

## Focused positive/refusal test matrix

`tests/offmode_collision_tests.cpp` uses genuine COW
`RegionEdit` revisions and their real immutable triangle BVHs,
not hard-coded pair mock results:

1. Two disjoint closed tetrahedra remain clean; moving one into
   the other is a definite new contact, comparing both roots.
2. Moving BOTH opposing closed components into each other at
   once detects new contact from the SAME candidate; old geometry
   of the other sheet is not reused.
3. Two unrelated initially overlapping closed tetrahedra don't
   block independent remote edits; deliberately querying their
   unchanged faces records `UnchangedPreexisting`. Moving an
   already colliding face conservatively becomes `Uncertain`.
   A hypothetical resulting disjoint pair is counted as resolved,
   not quietly presented as a new contact.
4. Ordinary shared-edge/shared-vertex tetrahedral neighbors are
   `Clean`. Same-root empty work is clean; different roots with
   missing dirty evidence and nonexistent stable IDs refuse
   uncertain.
5. Six separately **actually executed** candidate BVH work
   categories are each tested at exactly one below their
   observed successful count, requiring `Uncertain` and zero
   partial accepted contact ledger; input-record budget also
   refuses. Old immutable picker identity is retained.

## Acceptance boundaries and next step

M01 supplies exact finite read-only *before vs candidate* contact
evidence needed for V18. M02 must add a **new opt-in** pre-CAS
terminal refusal gate and complete dirty-record proof, preserving
old immutable root/mirror/history/renderer state and draining
future queued commands. M03–M05 supply mesh-scale clearance,
grazing/sliding and symmetric motion. M06 alone authorizes a
desktop control; M07 independently accepts the unchanged original
V18 objective after full cross-platform and real-user tests.
**Original V18 stays unchecked. V17, M66, M70–M72 and V19
are not modified.**
