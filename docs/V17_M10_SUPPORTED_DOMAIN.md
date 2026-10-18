# V17-M10 — complete-source supported-domain geometry matrix

**Status:** M10's bounded source-based fixture contract; acceptance is tied to
the accompanying `revision_collision_tests`, `repair_surgery_tests`,
`async_sculpt_tests` and other V17 regressions, not to a claim that all
input meshes support arbitrary exact Boolean reconstruction.

**Provenance rule:** Discover EVERY real original V13 FaceId contact pair
before committing to classification, even when a local subset of two curves
can be conformingly cut. Only independently source-proven surviving finite
sections with exact endpoint matching, opposite winding, cavity-safe
c1/c2/c3/M03 removal, and final closed edge/vertex links permit a weld.
Never deduplicate vertices by XYZ. V17-M16 separately validates deliberate
versus accidental coincident vertex identities and is required AFTER M10
BEFORE M11/M15, per [the normative identity contract](COINCIDENT_VERTEX_TOPOLOGY.md).

## Acceptance register

| Original geometry / test | Complete original V13 contacts | Decision | Authority and expected observable result |
|---|---|---|---|
| Three tetrahedra, generic dyadic 3-way transverse junction; `testGenericCompleteThreeBodyTransverseGraph` | **12** | **Supported exact positive union** | M01 source-shared point, M02 seam alignment, M03/c1/c2/c3/M04 one original-root 3→1 manifold, full final collision, historic XYZ, one history/checkpoint. Unequal private child density is positive only AFTER source-certified refinement; an unaligned raw join must refuse |
| Orthogonal three-tetra triple junction; `testOrthogonalTripleJunctionProbe` | **16** | **Supported exact positive union** | One terminal 3→1, source-proven fixed triple point, complete original graph and closed manifold |
| Reflected generic + orthogonal triples, away from X=0; `testReflectedThreeBodyCompleteOriginalContacts` | **24 / 32** | **Supported mirrored positive unions** | TWO independently connected unions in ONE original-root Step, exact new/old reciprocal vertex and FaceId mirror orbits, protected reverse cavities and checkpoint journals |
| Two closed symmetric tetrahedra meeting across X=0; `testX0InvariantExactCutProbe`, `testWorkerInvariantX0OneOriginalRootHistoryAndCheckpoint` | **3** | **Supported X=0 positive union** | M06 source-fixed identity, M07 singleton/paired c3, M08 private 2→1 welded full-disk, M09 one original-root Step/worker/undo/checkpoint; no published intermediate root |
| Closed three-body with wholly buried unchanged source base and separate reversed cavity; `testWhollyBuriedUncutOriginalFace` | **3** | **Supported union, protected cavity** | Additional uncut base retires ONLY after separate M03 whole-face occupancy/source proof; cavity faces survive with exact original XYZ; outer joined shell and cavity remain TWO legitimate connected closed components |
| Historical original three-tetra crossing on ONE face; `testSupportedDomainAdversarialMatrix` | **21** | **Unsupported: safely refused** | Selected 2 transverse curves do NOT license a whole-union. Remaining 19 contacts contain genuinely nontransverse/grazing original pairs. Complete V13/cut refuses, including in reversed contact order; no `ExactCutStep` candidate, retirement, mirrors, dirties, root or history authority |
| Historical interior T-junction on ONE face; same test | **21** | **Unsupported: safely refused** | Same full-source restriction and refusal in either contact order. Private isolated T triangulation is NOT complete original-root Boolean success |
| Independent closed tetrahedra touching at a single geometric vertex; same test | Full bounded discovery (may contain point-only contacts) | **Unsupported seam request: safely refused** | No positive transverse original interval, no forced shared VertexId, no fake connected manifold |
| Two independent exactly coincident whole tetra surfaces; same test | Full bounded discovery, coplanar | **Unsupported: safely refused** | Coplanar overlapping FaceIds do not supply unique transverse two-sheet orientation; preserve BOTH independent roots and vertices |
| A smaller wholly nested closed tetrahedron with NO surface intersection; same test | **0** | **No-contact / unsupported explicit union** | No source pair means no licensed M03 retirement and no synthetic Boolean Step; both source shells remain |
| Two closed cubes: V16 volumetric overlap, near-coplanar 1e-7 overlap, exact tangent plane; `testSupportedDomainAdversarialMatrix` and `testNearTangentAndFoldedClassificationStress` | Recompute ALL original V13 faces for EACH case | **Strict decision gate** | Accept ONLY on complete verifiable 2→1 closed oriented connected manifold with no residual contacts and valid vertex links; otherwise require stage-diagnosed atomic refusal. A local approximate V16 scar is not exact source-certified Boolean proof |
| Rotated/sheared folded THREE-shell V16 geometry; `testSupportedDomainAdversarialMatrix`, `testFoldedDeepMultiShellCollisionMatrix` | Recompute ALL original V13 faces | **Strict decision gate, no presumed general support** | Multiple oblique/coplanar/nonplanar intersections must satisfy terminal 3→1 global link/edge/collision proof or refuse with NO partially usable root |
| Mispaired historical original/missing mirrored V13 source partner; M05/M06/M09 controls | Complete source and exact orbit required | **Refused: provenance** | Invalid source FaceId or mirror involution cannot generate a final root or half-orbit history |
| Raw unequal original-contact edge density; `testGenericCompleteThreeBodyTransverseGraph` | **12** | **Refuse raw d2; positive after M02** | Missing counterpart interval refuses even if vertices are near/exact in world coordinates; source-authorized added splits plus fresh c1/c2/c3 permit a positive join |
| Strict net-ID / seam trace / final changed-halo work cap; same generic 12-contact fixture | **12** | **Refused: bounded work** | Actual otherwise-positive union cannot be published if source seam trace, terminal collision halo, net stable-ID or dirty work is incomplete; zero leaked Step journals |

The V16 near/folded entries intentionally distinguish **proof obligation** from
support guarantee; their test prints the actual observed `certified-positive`
or `safely-refused` decision and the complete contact count. A successful
classification in the approximate V16 repair engine is NOT evidence of a
successful exact original-root Boolean Step. If a later geometric change
accepts one of these inputs, a real fully validated terminal union is required.

## Refusal stages and evidence

- **Input/V13:** zero-dimensional tangent, exact face overlap, grazing,
  ambiguous or nontransverse original pair, missing section, source/FaceId
  mismatch, or an incomplete original graph.
- **Cut/arrangement:** unsupported multiple constrained segments, ambiguous
  intersections/visibility/planar graph, source-incompatible child corner,
  degenerate or inverted triangulation, exhausted arrangement work.
- **c1/c2/c3/M03:** incomplete positive signed-winding occupancy or original
  pair coverage; disputed cavity/exterior classification, unproven untouched
  wholly buried original or a reflected singleton/paired orbit conflict.
- **V15 shape / d2 seam:** poor cap/zip quality, duplicate contour ownership,
  misoriented or geometrically unequal survivor intervals, unequal source
  subdivision without certified repair, disconnected vertex fan, unmatched
  reflective class or unrepaired residual surface crossing.
- **Original-root Step/worker:** net ID/tombstone accounting, final collision
  budget, mirror-controller disagreement, stale CAS, rejected validation.
  These reject without exposing unpublished intermediate roots or undo
  history. Post-CAS unexpected exceptions remain the separate M14 gate.

Every refusal must provide a nonempty specific diagnostic, an absent final
revision/commit and empty net ID/mirror/dirty journals, and the same immutable
original picker/readers, permanent FaceId and VertexId identities and history.
Any later worker command must still drain in FIFO order; existing
`testWorkerUnionRefusalsDoNotBlockLaterInput` and the M09 fixed-plane queued
worker tests cover budget, missing-source, stale and next-valid input.
If a case is allowed as a positive, its final candidate must survive a
complete non-adjacent collision check, terminal connected surface and
vertex one-ring checks, physical mirror/fidelity and bounded work.

## Explicitly NOT accepted as a general guarantee

Arbitrary coplanar face reconstruction, multi-way/four-sheet ambiguous links,
near-parallel numerical uncertainty, tangency-to-volume transition, global
duplicate-coordinate cleanup, arbitrary nested/cavity semantics, nonmanifold
imported geometry, tunnels, strongly skewed high-degree rings and mesh-size
independent completion are **not** inferred from these examples. Safe refusal
of an unsupported operation does not mean the operation succeeded.
V17-M16 owns a separately testable off-plane and on-plane accidental
coincidence/provenance safety gate before repeated live sculpt unions;
V19 owns imported pathological topology, larger workloads and desktop stress.
V18 remains the independent Off-mode collision-constrained displacement
program. M11, M14 and M15 are not activated by this matrix.
