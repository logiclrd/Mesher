# R06-M02 — Bounded enclosed-component evidence

**Status:** R06-M02 CPU implementation accepted on stacked [PR #142](https://github.com/logiclrd/Mesher/pull/142), based on R06-M01. [CI run 39726664591](https://github.com/logiclrd/Mesher/actions/runs/39726664591) passed **70/70 registered CTests on Linux GCC, Linux Clang and Windows MSVC**. This is a read-only certificate, not a deletion operation. The parent R06 remains open.

## Product rule

Mesher does **not** deliberately preserve sealed internal cavities. An independent inward-facing, reliably enclosed closed component is redundant and can be retired in the future R06-M04 private transaction. A disconnected outward solid is legitimate and must survive. Reversed orientation on its own is **not** proof that a shell is inside material. The hollow-mouth starter is a single connected manifold before pinch; the passage initially leaves the interior accessible.

## Contract

`inspectEnclosedComponents(index, limits)` takes the **fully closed candidate** after a prospective closure (e.g. R05-style forced caps or future R06-M04 mixed closure). It does not classify or remove open R03 cuts directly. It reuses the R06-M01 stable-ID face-component/edge/vertex-fan inventory under a zero-open-boundary contract, then computes each component's signed volume using translated long-double oriented tetrahedra (no global origin instability).

- Positive signed volume denotes an outward shell; negative signed volume denotes an inward shell. Degenerate or nonfinite volume stays **Ambiguous**.
- For each candidate nested component, AABB enclosure is merely a cheap necessary filter. Three deterministic surface triangle centroids are evaluated against the *other component's triangles* with long-double signed solid angles. All three must confidently agree on winding +1 for the candidate to be inside an outward shell. A winding sample disagreement or noninteger result is **Ambiguous**.
- An inward component can be marked `RedundantEnclosedCavity` only if precisely one enclosing component is proved, that enclosure has positive volume, and a finite indexed candidate-face triangle-intersection query finds no contacts/self-intersections. Unreliable contact results or work exhaustion refuse completion, never certify an incomplete result.
- Positive exterior components without enclosing proof are preserved as `Exterior`. An uncontained inverted component, a nested outward solid, or a multi-level ambiguous combination remains `Ambiguous`. No geometry is deleted here.
- Work bounds cover faces/edges/components, signed-angle triangle visits, triangle-collision seeds, index box/partition visits, candidates, narrow tests and reported pairs. Entire computation is detached from CAS, worker/history and checkpoint. Unknown conditions remain explicit, not guessed.

This classifies a *closed* hypothetical R06 result, not the R03 open cut. R06-M03 owns deciding closure regions, R06-M04 will rebuild/audit a single private closed result and may remove only proven redundant components, and R06-M05 introduces the developer viewport.

## Regression acceptance

`repair_r06_m02_enclosed_component_evidence` tests:

1. Two disconnected outward solids both preserved.
2. Concentric outer outward and inner inward closed shells: exactly one exterior and one read-only redundant cavity candidate, with distinct stable IDs and unchanged tombstones.
3. Inverted independent exterior: not deleted.
4. Nested outward shell: not incorrectly treated as inward cavity; leave ambiguous pending separate burial evidence.
5. Geometrically coincident opposite-wound components with independent vertices: not certified redundant.
6. Original hollow mouth fixture: one connected topological component, not an isolated removable inner cavity.
7. Explicit bounded face, solid-angle and indexed collision-work exhaustion.

**Proof limitations:** Deterministic witnesses and the indexed collision query establish a conservative admissible subset; they do not solve arbitrary many-to-many intersections, exact material reconstruction, imported nested pathologies or forced deletion of an ambiguous component. Every refusal leaves existing roots unchanged. Physical Linux/Windows evidence belongs to later integrated milestones.
