# V18-M02 — Explicit private OFF-mode collision refusal before terminal publication

## Status and scope

M02 is the SECOND flat V18 peer after the accepted, separately
read-only [M01 immutable before/after contact ledger](V18_M01_POSTSTATE_CONTACT_LEDGER.md). This milestone adds an
**opt-in C++ worker policy, still NOT a desktop CLI switch**.
The ordinary unarmed/default sculpt and the V17 first-original
`--merge-geometry` source-certified Boolean retain their behavior;
M06 owns any later visible opt-in. Original V18 remains open.

## Live direct/worker seam and truthful attempted costs

`SculptCollisionSettings::offModeConstraints.enabled` defaults
`false` and is explicitly incompatible with either
`collision.enabled` (V16 approximate surgery) or
`collision.physicalExact` (V17 source-certified Merge).
Both fresh and checkpoint-restored brush constructors validate this.

In `VersionedSculptBrush::stamp`, normal/private Inflate, local
refine, and collapse all execute FIRST. Their own stable changed
VertexIds and FaceIds (including inserted/replaced/retired IDs)
form the local dirty evidence. The M40 private-copy work and final
dirty-owner count are computed before M02 admission; exact bounded
M01 old/candidate BVH contact reports are then evaluated before
the one external original-root CAS, mirror changes, history append,
final renderer mailbox and any accepted contact-hysteresis credit.

- `Clean` and strictly `UnchangedPreexisting` are provisionally
  accepted by this conservative M02 contact **only** gate. M03
  will require local mesh-scale clearance and resolve legitimate
  contact/sliding movement; M04 will attenuate normal displacement.
- Definite `NewContact`, `Uncertain` (including changed old pair)
  and all invalid/over-budget queries veto the ENTIRE completed
  private candidate with a diagnostic beginning `V18 OFF-mode`.
  No Boolean welding, unsupported old-source cleanup or global
  XYZ deduplication is attempted.
- M02 refusals use the EXISTING worker's exact same pre-CAS
  rollback and rejected-stamp path, report actually performed
  COW/dirty and BVH query work as attempted, grant zero
  published bytes/history/dirty partitions and are recoverable.
  A later queued Begin/End is not consumed by a failed candidate.
- Failure after CAS remains V17-M14 terminal quarantine. M02
  introduces NO new post-CAS behavior; refusal is before CAS.
- Fully unchanged Step, ordinary initial settings and V17 ON
  source transactions do not invoke the M02 contact admission.

Note the finite detection is a static end-state classification,
NOT swept-volume CCD, minimum mesh-scale clearance or a proof
against extremely large motions tunneling between endpoints.
Those are explicit M03/M04/M07 issues. M02 must not silently
claim that crossings are now prevented in the shipped UI.

## Genuine executable tests

`tests/offmode_step_gate_tests.cpp`, CTest
`offmode_private_step_gate`, builds actual independently closed
tetrahedra, applies real normalized-normal Inflate, and searches
a small fixed positive-strength set for a **genuine newly
intersecting post-stamp pair**, certified by the M01 ledger.
The exact same supported candidate is then run with the M02
option enabled and must refuse without leaking its root,
picking identity, history, checkpoint mirrors or renderer dirt.

A third distant tetrahedron provides a safe subsequent stamp:
one direct brush and one real `AsyncSculptWorker` 4D FIFO each
recover from the original crossing refusal and publish exactly
one legitimate new original-root/history entry. Pointer-exact
undo/redo is checked. A separate test confirms unrelated pre-existing overlapping source
tetrahedra remain unchanged during a valid remote edit; there is
no global cleanup/refusal requirement. A single clean tetrahedron supplies
a positive accepted stamped candidate and all source/geometry
credentials remain untouched. A zero collision seed budget
refuses an otherwise clean, actually staged Step with real
attempted-work billing and a named budget refusal. Both
invalid combinations with V17 collision/physicalExact are
rejected during construction.

## Acceptance and unchanged boundaries

Exact-head GCC, Clang, Windows MSVC Vulkan-enabled CTest/
stress/benchmarks and independent post-merge `main` workflow
are required. **V18-M03 is next**: mesh-scale local minimum
clearance and sliding proof; later M04 normal attenuation, M05
symmetry/dual-sheet source lifetime, M06 desktop opt-in and M07
integrated real-GPU V18 acceptance remain separately OPEN.
No transition to new default-on sculpt behavior or changes
to V17 M29 authority, import/recovery policy, checkpoint
binary format or universal Boolean semantics.
