# V18-M05 — Reflected ownership and both-opposing-sheet transaction proof

## Scope, and why V17 credentials must not be fabricated

This milestone integrates and tests the V18-M01–M04 finite private
OFF-mode collision/clearance/backtracking pipeline when *both*
independent opposing surfaces move in the SAME proposed Step and
the mesh has real reflected, reciprocal permanent source IDs.
It does not enable a desktop mode (M06), general-purpose arbitrary
Boolean (V19), or swept-volume CCD. The original V18 objective and
M06/M07 remain open.

**Distinction:** V17's M29 sealed FIRST-original authorization and
V18's OFF-mode constraint policy are mutually exclusive. V18 cannot
and must not relabel a disk-restored mirror map as an authenticated
M29 first-original certificate. Instead M05 explicitly proves the
entire *current* original mirror identity and topology at session
construction, from supplied stable-ID ownership, before trusting its
local mirror journals for subsequent edits. This is a source-ID
consistency check, **not protected Boolean Merge authorization**.

## New bounded source-derived mirror audit

`auditOffModeMirrors(root,mirrors,changedVertices,changedFaces,fullSource)`
uses only explicit VertexId/FaceId and the immutable root. It checks
a complete upfront source at construction (maximum 32768 vertices,
65536 faces) and then just the finite changed VertexId/FaceId halo
plus both mirror mates' incident face stars for EACH private candidate
and the final would-be terminal root.

Checks include:
- One full living-vertex mirror involution, reciprocal live IDs,
  source-stable ownership, and exact coordinates
  `x_mirror = -x_original; y/z identical`.
- `(id,id)` corresponds **if and only if** original X=0.
  Genuine fixed-plane points must remain on it throughout the
  transaction; off-plane points require two separate live IDs.
- Every inspected oriented triangle has a separate or self-symmetric
  reflected stable-corner triangle with reversed orientation.
  Reversing orientation is essential for reflection of surface
  winding. Equal XYZ is never substituted for matching stable IDs.
- Every edited/retired original VertexId must have its old original
  partner retired as well; pair-local checks run on both speculative
  candidates and the final pre-CAS candidate.
- All local work is bounded; a missing partner, bad orientation,
  exhausted source halo or invalid original mirror map refuses
  rather than publishing an unproven half-symmetric mesh.

The upfront complete original scan is intentionally separately
bounded and done ONCE, not once per stamp. Per-trial checks are
local and feed existing M04 journal rollback. The final audit runs
AFTER work accounting and BEFORE the one root CAS, so it is a
recoverable full-Step refusal with no renderer, history or mirror
leak. V17 protected source authority is unchanged.

## M05 genuine geometry tests

`offmode_reflected_opposing_sheet_transactions` uses independent
triangles with mirrored winding on X-positive and X-negative
islands, two opposing Z sheets on EACH side, and a separate
triangulated X=0 seam with true self-mirrored stable vertices.
The complete proposed Inflate stamp contains all four inward
moving sheet corners (not only one moving sheet against stale
other-sheet positions). M01 contact, M03 local clearance and M04
bounded normal fractions must evaluate the same fully staged
post-refine/collapse root, then publish exactly ONE final
root/history/mirror transition. Old original picker and every
unrelated stable ID remain unchanged.

An independent genuine local-refinement scenario forces a midpoint
split of the shared X=0 seam edge. The validator deliberately refuses
the first complete privately staged split: the new permanent ID and
new self-mirror pair must disappear from *published* state and the
speculative refiner/collapser mirror journals must roll back. The
same stamp then succeeds, publishes the additional X=0 self-pair
exactly once, and pointer-exact undo/redo restores and reapplies
the incremental mirror journal.

A separate intentional one-trial unsafe close approach refuses,
leaves pointer-identical indexed root, empty dirty partitions,
unchanged mirror map and zero history, then permits a valid
X=0 fixed-point stroke on the same brush with undo/redo. The
asynchronous 4D symmetric worker exercises the same real
refusal, queued recovery, dirty frame ownership and undo.
A source fixture contains unrelated coincident XYZ triangles
with independent permanent IDs and distinct mirror pair
ownership. Swapping two mirrored partners that have equal
coordinates *still* fails the upfront stable-oriented-topology
audit; their apparent geometry cannot silently weld identities.
An externally nominated stale immutable root validator refuses
before CAS/history without retroactively mutating the last
accepted root. This is the worker's existing explicit
expected-root callback boundary, not an invented multiwriter
CAS guarantee.

All M02–M04 original strict refusal/backtracking assertions
remain in the test matrix. This milestone never enables V18
desktop controls, changes original Merge credentials or relaxes
the mesh-local static endpoint limitation. A sufficiently thin
unresolved sheet can still tunnel between sampled endpoints.
M06 adds explicit CLI/UI policy selection and M07 independently
accepts original V18 on CPU and physical GPU.

## Acceptance

Exact PR-head Linux GCC, Linux Clang and Windows MSVC full
Vulkan warnings-as-errors CTests/stress/benchmarks; separate
merged-main success is required before M05 is closed.
