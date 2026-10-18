# V17-M27 — First-original lifetime source patch fidelity

## Scope

M27 is the final independently-complete peer of the previously planned
**M24 → M25 → M26 → M27 → original M20 integration** sequence. It does not
close M20 or M12. M24 source-oriented local splits, M25 certified
2→2 flips and protected-safe collapses, and M26 full original-contact
c1/c2/c3/M03/d2 exact joins remain their independent authorities.
M27 **composes** their output through accepted revisions and samples
protected geometry against the **first nominated original root**. It
does not publish, modify the renderer/picker/history/checkpoints, infer
intended brush motion, or enable desktop Merge geometry.

## Fixed-original provenance and immutable certification

`beginOriginalPatchLifetime` captures a permanently shared first
original topology index and the nominated original protected FaceIds,
their exact ordered original vertices, seven immutable XYZ witnesses
(corners, edge midpoints and centroid), and each original triangle's
minimum nonzero edge length. These reference values are NOT replaced
by the last committed mesh, the last edited source patch or the
shortening/lengthening edge scale after refinement.

Each `advanceOriginalPatchLifetime` overload invokes the existing
independently strict M24/M25/M26 verifier against the previous
**actual terminal revision pointer**. A new source face may be added
only by its committed permanent-ID child source evidence; a removed
source is legal only if independently c3/M03-certified entirely
Buried/Outside, and its original FaceId remains a tombstone. An
otherwise missing Exposed or ReversedBoundary original descendant
is NOT treated as retired or replaced by another nearby face.
Mirror partners, X=0 singletons and distinct exactly coincident
closed sources inherit their original certified ownership, not
nearest-world-space geometry.

The lifetime object retains an **opaque immutable certificate**,
constructible only within the M27 implementation, sealing the
first and terminal pointer identities, source FaceId → living FaceId
ownership graph, explicitly authorized retired-original list,
previous source split count, accepted transition count, and total
source evidence cost. Every read/advance rechecks that certificate
and independently reconstructs all seven witnesses and original
edge scales from the first index. Consequently merely changing
a public FaceId vector, cumulative-cost counter, root, or source
coordinates cannot turn an unverified candidate into a successful
audit. The same certificate can be copied as an immutable reader
snapshot without modifying the old index or COW partition graph.

Already certified retirement is carried across later transitions
without resurrecting its original FaceId. Active sources are
verified against the complete stage journal, then restored in the
same original nomination order alongside previously retired
sources. No M21 intentional-scope permission is fabricated.

## Honest, finite one-sided original witness evidence

`auditOriginalPatchLifetime` compares EACH active original source's
seven first-original points ONLY against that original source's
living **certified permanent FaceIds**. It never searches arbitrary
nearby world-space triangles, so two independently closed
coincident surfaces remain distinct ownership domains.
Every distance is normalized in that one original source's
original minimum edge units, not by brush radius or a later remesh.

Preflight consumes exactly `7 * sum(living source descendant count)`
point-to-triangle comparisons, with a hard caller-provided cap. The
exact count is accepted; one below **refuses the entire audit**
before sampling any original patch. A distinct lifetime
`maxCumulativeFaceEvidence` cap spans the already-consumed
M24/M25/M26 stage journals across ALL accepted Steps; it is never
reset per Step, and the opaque certificate rejects a caller's
attempted counter reset. A finite living-face cap is also enforced.

The response includes per-original witness count/maximum/mean/RMS
in original local edge units, total sampled work and deviation,
active original count, and an explicit separate list of completely
certified retired originals. Retired sources are NOT counted as
geometry-preserved or given false zero-distance scores. The
audit returns a diagnostic measurement; M21 defines intentional
scope, M22 sets and enforces terminal acceptance thresholds, and
M23 controls complete clean-contact episode behavior.

**No continuous two-sided Hausdorff bound, exact curvature
preservation, universal imported topology or arbitrary coincident
sheet weld is implied by seven finite samples.**

## Real accepted positive evidence and fail-closed refusals

- Two actual source-proven normal-driven octahedron split stamps
  produce distinct correctly published CAS revisions and measure
  increasing **cumulative** deviation from one unchanged original.
  The first root, old picker and prepublished readers remain stable.
  The cumulative face evidence is 2 after the first stamp and
  4 after the second; 42 and 56 sampled comparison tests reflect
  actual living descendant counts.
- Actual source-proven refinements with valid 2→2 flips and actual
  directional collapsed edges preserve separately nominated original
  untouched features. Collapse face-halo work and witness budgets
  accept their exact counts and refuse one below.
- Genuinely joined M26 original-root exact Steps retain the original
  protected surfaces via living child sources, separately report
  completely buried M03 originals, and retain real reversed cavities
  and full mirrored original source orbits.
- Actual source-proven X=0 Step keeps one permanent self-mirrored
  fixed-point source orbit and independent reciprocal off-plane
  partners; it does not duplicate seam vertices.
- Two independent, completely coincident closed octahedra retain
  separate source FaceIds in the lifetime audit, and only the
  genuinely split original source sheet acquires two children.
  An attempted cross-sheet child-ID substitution refuses.
- A forged previous root, replayed Step, altered immutable original
  witness, independently replaced living descendant list, duplicated
  or unclassified lost protected source, invented resurrection,
  manipulated evidence counter or accepted-Step counter, exhausted
  source halo or exhausted point-triangle work budget refuses
  without exposing an accepted partial lifetime record.

Substantive PR #71 CI:
[GitHub Actions #39167334610](https://github.com/logiclrd/Mesher/actions/runs/39167334610)
passed Linux GCC, Linux Clang and Windows MSVC warnings-as-errors,
all **23 CPU CTest suites** and applicable 100K/brush benchmarks.

## Next original acceptance gates

**Original V17-M20 integrated acceptance** still requires a final
combined review of all independently merged M24–M27 provenance
contracts and their refusal boundaries. The already recorded
**M21** explicit intended sculpt feature-scope/cavity policy,
**M22** active pre-original-root CAS cumulative terminal guard,
**M23** conclusive clean episode semantics, and original **M12**
final integrated acceptance all remain unchanged and open.
The desktop Merge geometry switch remains default disabled.
