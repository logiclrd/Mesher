# V17-M24 — Runtime-certified local split lineage

## Accepted independent scope

M24 is the first flat positive gate in the M20 original remesh-patch
ownership sequence, which was completely recorded in todo.md before
M24 implementation. **M20 and M21–M23/M12 stay open.** This gate
does not turn on the desktop or active exact-collision guard, and
does not authorize retiring original protected exposed/cavity faces.

VersionedLocalRefiner::apply now publishes a **private proof journal**
of committed original-face-local splits, with one source record per
incident original FaceId, the corresponding appended stable child
FaceId, and the newly allocated midpoint VertexId. Each event binds
the immutable pre/post RegionTopologyIndex roots of its actual
validated RegionEdit. A mirrored accepted pair emits records for
both reflected source faces; a true fixed X=0 shared-edge split emits
one source pair with a self-mirrored midpoint orbit. No journal
exists for an edit that did not actually commit.

The verifier consumes the first-original face nomination from M19,
tracks ALL living child FaceIds of each protected parent, and
advances only when the LocalStampBaseline's true pre-stamp immutable
root is exactly the prior accepted terminal root. Inflate may move
existing source vertices before local splitting, but cannot change
their ordered face vertex incidence. Each claimed split validates:

- An unallocated midpoint and appended permanent FaceId before
  the event and the exact stable FaceIds present in its after root.
- Exactly two source incident faces per midpoint (two/four source
  records per actual accepted unsymmetric/paired reflected split).
- Both children have the exact orientation and vertex incidence of
  the source's directed edge subdivision; the new midpoint is the
  source edge arithmetic midpoint, with only the established bounded
  source-proven X-only fixed-plane normalization.
  Both original triangles must use the SAME permanent edge endpoints
  in opposite directions; merely identical world-space midpoints do
  not merge the provenance of independent coincident edges.
- Per-event root continuity and exact final-result journal
  agreement for split count, changed faces and inserted vertices.
- In symmetric mode, actual reciprocal new midpoint orbits,
  exact reflected XYZ and X=0 singleton where applicable, with
  the complete declared mirror pair record. No coordinate-derived
  mirror or Boolean seam equivalence is manufactured.
- No two independent original source patches ever receive the
  same living FaceId. A child may be assigned ONLY through the
  source's own committed record.

Work is finite. The caller declares a maximum number of source
face proof records and a maximum number of retained protected
living child FaceIds. An exact bound passes; one fewer fails before
accepting incomplete evidence. An unchanged refine pass with
no edits is allowed but cannot invent a face rewrite. Retired,
changed-incidence, omitted, stale, reordered, forged or extra
source records refuse. A success over several split generations
retains the SAME immutable first-original root and cumulatively
grows that original face's living child-ID set.

## Out-of-scope positive coverage preserved as future flat peers

- **M25:** certified 2-to-2 edge flips may transfer original patch
  area across a shared source edge; directional collapse may retire
  original protected area. M24 refuses any flip and does not
  authorize unjournaled collapse instead of inferring ancestry.
- **M26:** source-proven original V13 c1/c2/c3/M03/d2 cut and union
  descendant mapping; M24 cannot replace it with a spatial match.
- **M27:** first-original patch witness comparison across recorded
  remesh/union history and bounded cumulative evaluation.
- **Original M20:** integrated proof of all the above for local
  split/flip/collapse and d2. M21 handles deliberate sculpt intent,
  M22 active terminal pre-CAS gate, M23 episode behavior.

The evidence is finite geometric/provenance bookkeeping and
does **not** prove collision-free Boolean welding, cavity survival,
a continuous two-sided Hausdorff bound, or automatic UI readiness.

## Verified independently green CI checkpoint

The substantive PR #68 source/test head passed GitHub Actions
[39161099327](https://github.com/logiclrd/Mesher/actions/runs/39161099327):
Linux GCC, Linux Clang and Windows MSVC /W4 /WX, all 23 CPU CTest
suites and applicable benchmarks. Positive regressions include
two accepted real local source-split generations from one retained
first-reference root, a genuinely reflected multi-face split,
a genuine X=0 self-mirrored midpoint from actual seam deformation,
and two independent closed coincident octahedra whose unrelated
source FaceIds never share child ancestry. Negative cases include
a missing incident source record, a substituted face/edge ID,
omitted mirror journal, stale root, unproven flip and work one below
the exact evidence or living descendant limit.

This verifies M24 ONLY. The original M20/M12 objectives and the
M25–M27/M21–M23 execution peers remain open pending their own
positive and negative acceptance; the Merge geometry desktop feature
is still default-off.
