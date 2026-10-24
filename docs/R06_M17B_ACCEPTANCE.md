# R06-M17B — Parameterized conforming edge split: CPU acceptance

**Accepted:** October 24, 2026. **Exact code commit:** `e8900035096174268dcf27cbdae4ac426f9d63f4` on `r06-integration`, draft PR #162 (no merge to `main`).

**Full three-platform CI:** [GitHub Actions #40129178071](https://github.com/logiclrd/Mesher/actions/runs/40129178071), attempt **1**, **SUCCESS** on Linux GCC, Linux Clang, and Windows MSVC `/W4 /WX`. All **110 Windows CPU tests passed**, including the new M17B test and existing M17A, M16 snapshot/stepper, exact replay, M39 async-worker, repair classification and private closure regressions.

## API and compatibility

`MixedClosureStaging::splitEdge(..., bool optionalQualityOnly=false, double canonicalT=0.5)` extends the original M15B staging-owned conforming editor, rather than creating another splitter. The default `canonicalT=0.5` keeps the original midpoint formula and existing mandatory/optional preflight behavior. It remains the sole split algorithm used by ordinary R06 closure today: the M17A read-only planner is **not** connected to normal repair until M17C.

The canonical parameter is always measured from `min(VertexId)` to `max(VertexId)`, independent of argument ordering. Requests must be finite and separated from either endpoint by `MixedClosureSplitSettings::minimumSplitFraction` (default `0.12`; a M17B safety bound, not a new R06 admission/quality floor). Invalid or near-endpoint requests fail before stage mutation.

## Exact transaction properties

- Evaluate the **complete current effective two-sided edge**: original source-backed survivor handles (including remote faces not loaded in the initial rim halo), generated cap/zipper faces, exact orientation and at most two incident sides.
- Perform exact split-point checks against tracked generated and survivor vertices. Query immutable R03 for remote original vertices that have the **same XYZ but a different stable VertexId**. Coordinate coincidence *rejects* the request; it is never treated as permission to weld.
- Preserve the existing strict minimum original-survivor child quality rule. For an off-center request, also validate BOTH child triangles on every incident cap and zipper side: keep the accepted hard quality floor for healthy parents; for a parent already under floor require a finite positive quality improvement rather than silently generating a worse needle. Optional density-refinement requests retain their conservative absolute floors. Legacy mandatory **midpoint** decisions stay unchanged.
- Discover the complete reflected orbit before mutation. When partner stable-ID order is reversed, use `1-t` on the partner edge; construct its exact new position by reflecting the original proposed XYZ and check geometric consistency. A single self-reflected edge whose endpoints exchange on reflection only admits its midpoint. Any missing/ambiguous partner or exceeded complete-orbit budget refuses before private edits.
- A permitted edit introduces one or two unique permanent private `VertexId`s, replaces both effective sides including original survivor overrides and cap/zipper generated children, retains all original `FaceId` ancestry, and leaves authoritative roots/history/checkpoints unpublished.
- Record each new ID's stable canonical parameter and exact XYZ in `MixedClosureSplitEvent`. Emit ONE M16 debugger `splitEdge` event after the entire mirrored transaction completes, listing canonical request `t`, per-orbit `t`, stable IDs and full-precision coordinates. No intermediate half-mirrored step is observable.

## Independent regression

`tests/repair_split_parameter_tests.cpp`, CTest `repair_r06_m17b_canonical_two_sided_mirrored_parameter_split`:

1. Real open-tetra R03 source plus generated cap sharing an edge: off-center t=.35 creates exactly one vertex; both sides use it conformingly, source-original FaceId remains unchanged and the debugger shows the exact transaction.
2. Reversing the caller's edge endpoint arguments yields the SAME actual point, not `1-t` by accident.
3. Invalid, nonfinite and near-endpoint `t`, excessively poor cap children, and exact staged duplicate-coordinate requests refuse without any split event or partial source-face edit.
4. A two-half reflected open-tetra with mirrored **opposite endpoint-ID ordering** enforces t/.7 on the partner edge, exact reflected XYZ, two source/cap neighborhood edits and ONE atomic observer event. An insufficient full-orbit budget and an absent reflection partner both refuse before edits.
5. A disconnected original source component has a VertexId exactly at the proposed split point but outside the initial staging halo. The transaction correctly rejects a **remote R03 source XYZ collision** without implicit welding.
6. Existing midpoint R06, M16 source-backed snapshots, archived saved-case execution, CI and repair quality/publishing behavior remain passing.

## Still open

**M17C** decides whether and when to use this operation during real repair, introducing the bounded midpoint/off-center/hybrid execution and scale-aware shape-repair handoff. It must evaluate both incident sides and complete mirrors again on each subsequent step of a hybrid, and cannot assume that a midpoint-then-corrective split cures the original skinny child. **M17D** addresses `refusal-1`'s upstream fold-back at initial spatial cap construction. **M17E** remains three-platform and real Vulkan physical acceptance. The current acceptance is CPU/topology/editor correctness only, NOT improved physical refusal-1 geometry or a changed runtime split policy.
