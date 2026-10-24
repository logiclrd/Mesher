# R06-M16C1/C2a/C2b — Accepted CPU foundation for staged repair debugging

**Acceptance date:** October 24, 2026. **Scope:** source-backed, inspection-only diagnostic infrastructure. **NOT** a usable Vulkan single-step debugger.

## Verified baseline

- Existing integration branch `r06-integration`, draft PR #162; original handoff head `cac00ec5cdc2715902a6ca618c858b800a4b9a2f`.
- M16C1 and C2a foundation code passed Linux GCC, Linux Clang and Windows MSVC in [CI #40107009052](https://github.com/logiclrd/Mesher/actions/runs/40107009052), head `dc471268c66f9e195e25ba3df55dee639e916c22`.
- M16C2b source-backed immutable snapshot, exact-identity diff and new CPU test passed **Linux GCC, Linux Clang and Windows MSVC** in [CI #40108102691](https://github.com/logiclrd/Mesher/actions/runs/40108102691), code head `6b26bdee9b25dd35b57ea9ab2a995935773628a0`, workflow conclusion `success`.
- Tests: `repair_r06_m16c1_two_snapshot_step_gate`, `repair_r06_m15a_stage_owned_face_and_live_boundary` (observer assertion added), and `repair_r06_m16c2b_owned_stage_snapshot_and_id_diff`. Exact ongoing R06 `.r06case` replays remain part of the full suite. No original archive or repair policy was edited.

## Implemented

1. `include/mesher/repair_step_gate.hpp`: one producer, bounded two immutable historical frame handles; step once, Continue, Previous/Current for presentation without undoing geometry, Cancel wakeup and finish state. Headless deterministic multi-thread tests, including no accumulated Next permits.
2. `MixedClosureStaging::OperationObserver`: an opt-in postmutation callback already covering `ensureFace`, `addVertex` and full `splitEdge` orbits. Idempotent face requests, unsuccessful splits and no observer do not report new edits. The split callback is issued only after all incident sides have been rewritten, and the internal midpoint allocation is suppressed as a separate half-split event.
3. `include/mesher/repair_step_snapshot.hpp`: `captureRepairStepGeometry(stage,name,args,maxBytes)` copies the **source override ledger**, generated vertex positions and faces, and actual current boundary loops into a finite owned frame, sharing the immutable original R03 root without cloning the full revision. Broken/forked intermediate fronts are exposed as `boundaryDiagnostic`; the original stale outline is not silently reused. Default maximum frame accounting is 16 MiB and returns explicit refusal if exceeded.
4. `diffRepairStepGeometry(old,new)` compares only frames backed by the **same original R03 pointer**. It reports changed stable source FaceIds, generated face signatures with exact oriented corner VertexIds (cyclic rotations canonicalized, reverse winding preserved), added/removed/moved stable VertexIds, effective added/removed indexed edges, plus independently added/removed yellow boundary edges. Source-neighbor incidence is consulted before declaring that an edge was removed. Equal XYZ never merges different IDs.
5. Independent cap+survivor tetra test verifies initial rim, cap face creation, closing yellow edges without deleting indexed edges, full shared-edge split, remote original source-face override, generated survivor and cap face children, A–B disappearance, A–M and M–B appearance, original source unchanged, previous frame unchanged, and finite byte/error cases. An observer-to-step-gate packet path is exercised headlessly.

## Deliberate limits and remaining tasks

- **M16C2c** is NOT implemented: observer coverage must still include provisional creation/replacement, flip, fan retirement, fair, final applied patch/refusal and early exceptions, with proper complete logical-operation atomicity.
- **M16C3** is NOT implemented: neither exact case replay nor live worker currently supplies these snapshot frames to a running paused worker in the normal application.
- **M16D/E/F** remain open: no asynchronous pre-viewport replay restructuring, no physical Vulkan Previous/Current/Next controls, no upper-left operation HUD, no physical GPU acceptance.
- **M16B** is open: forced R04/R05 still have separate implementations. The R06 `MixedClosureStaging` instrumentation must not be represented as forced-mode instrumentation.
- Intermediate source-backed packets are **not** normal `RegionTopologyIndex` revisions and cannot be published, checkpointed or treated as closed certified results.
- Future render packet conversion should use shared immutable source plus *effective* changed source triangles and locally created geometry. It must not fall back to stale gray source triangles where staged source faces are overridden.
- The builder is deliberately opt-in. Snapshot and diff work are absent from the normal R06 path until an observer is connected and explicitly requested.

## Quick CPU acceptance

```sh
ctest --test-dir build --output-on-failure -R 'repair_r06_m16c1_two_snapshot_step_gate|repair_r06_m15a_stage_owned_face_and_live_boundary|repair_r06_m16c2b_owned_stage_snapshot_and_id_diff'
```

**Next independently testable development item:** M16C2c complete logical stage-operation coverage and terminal error envelope (then M16C3 exact replay callback/gating and M16D responsive Vulkan UI). Do not check the parent M16C2 until C2c is complete.
