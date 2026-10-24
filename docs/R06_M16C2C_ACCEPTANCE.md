# R06-M16C2c — Complete staged-operation event coverage (CPU acceptance)

**Accepted:** 2026-10-24. **Code head:** `6510bc77a314f998748b1ccbcdece59dba420216` on `r06-integration` (draft PR #162).

**Exact-code CI:** [GitHub Actions #40110431559](https://github.com/logiclrd/Mesher/actions/runs/40110431559), attempt **2**: Linux GCC, Linux Clang, and Windows MSVC `/W4 /WX` all **SUCCESS**. 103 CPU tests ran on Windows. The original first Windows attempt passed all new M16 tests, but failed the unrelated, already known intermittently nondeterministic `asynchronous_sculpting` M39 prior-root/FIFO-undo test. Retrying the failed Windows job **without any source change** gave all-green CI on the second attempt. Keep the M39 nondeterminism independently tracked; do not mislabel it an M16C2c failure or mark it fixed.

## Covered stage-owned logical operations

Opt-in `MixedClosureStaging::OperationObserver` reports a self-consistent **post-operation** staging state for:

- `trackSurvivor`: a new original FaceId handle; repeated identical handle is a no-op. During a reflected/remote edge split, nested imports are suppressed until the whole split finishes.
- `addVertex`: permanent private vertex ID, owner, full-precision XYZ; a nested midpoint allocation is suppressed inside a complete split-orbit callback.
- `ensureFace`: original generated oriented corner IDs, CAP/ZIPPER/SURVIVOR kind, owner, target edge, ordinal. A cyclic repeat is an identity-preserving no-op.
- `splitEdge`: one full indexed incident-side or mirrored orbit update, original endpoints, optional flag and all new midpoint IDs.
- `stageProvisionalVertex`: provisional ID (never to be treated as a permanent mesh ID), owner, full-precision position.
- `resolveProvisionalVertices`: **one** state after every provisional ID has been replaced in all affected face corners. Report count and first 32 exact provisional-to-permanent ID mappings; the full mapping can also be extracted from owned consecutive geometry snapshots. No intermediate half-converted mesh frame.
- `improveInteriorDiagonal`: after full reflected orbit, exact changed face ordinals and old/new oriented triangles plus quality/edge-factor arguments; zero-improvement calls emit nothing.
- `collapseInteriorCapFan`: after complete one-or-mirrored-orbit 3-to-1 replacement, the removed center IDs, face counts, and relevant policy. Subsequent private-view `retirePrivateVertex` calls have separate records.
- `fairInteriorVertex`: full after-state, changed IDs, before/after full-precision XYZ and quality/edge-factor arguments, without moving old surviving boundary vertices.
- `retirePrivateVertex`: exact removed private VertexId and owner, after deleting the view record.

Read-only staging queries and unsuccessful/no-op calls do not advertise topology changes. The observer is **null by default**: none of this new copying, formatting or waiting runs in ordinary R01–R06 closure paths.

## Terminal outcomes, including failures before a stage exists

`MixedPrivateClosureLimits::terminalObserver` receives a typed `MixedClosureTerminalEvent` with `Completed`, `Refused` or `ObserverFailed`, phase, description, and the explicit `untrustedWarningPreview` flag. The report describes the private M04B closure executor, **not** final M04C material/enclosure certification. Exceptions preserve their original type/message. Debugger `RepairStepCancelled` propagates without generating a geometry-refusal outcome. A failing terminal observer cannot replace an underlying geometry-refusal exception.

An observer's `std::logic_error` or `std::length_error` (such as snapshot memory refusal) is wrapped in `RepairStepObserverFailure : std::runtime_error`: the projected-cap triangulator's `catch(std::logic_error)` must **not** mistake a failed diagnostic packet for genuine cap geometry inability and silently invoke spatial-cap fallback.

## Independent test evidence

`repair_r06_m16c2c_remaining_stage_operations_and_terminal_error` positively exercises:

1. Provisional IDs and a generated face, one complete stable-ID remap, exact before/after identity differences, no-op remap and private ID retirement.
2. One legal bad-interior-diagonal flip: both cap faces replaced, one exact indexed diagonal removed/replaced; second flip makes no event.
3. One legal skinny three-face cap-fan collapse: center retires, resulting triangle and effective diff captured, later private-view retirement and no-op guard.
4. One legal constrained fairing move: exactly one interior ID changes position, old boundary IDs remain bitwise unchanged.
5. An ordinary privately closed oriented tetra cap: terminal `Completed`.
6. An intentional snapshot-observer failure in that same closure: terminal `ObserverFailed`, NOT `Refused`.
7. A pre-stage malformed-plan failure: original `logic_error` and `Refused` report with `initial_staging` phase.
8. An observer-thrown cancellation: distinct `RepairStepCancelled` exception, plus a diagnostic memory refusal that cannot be interpreted as geometric fallback.

Existing `repair_r06_m16c2b_owned_stage_snapshot_and_id_diff`, `repair_r06_m16c1_two_snapshot_step_gate`, `repair_r06_m15a_stage_owned_face_and_live_boundary`, the five unchanged `.r06case` replays, source invariants, and M15D refusal witness reporting also remain in the full CI.

**Still open:** M16C3 wires callbacks and bounded immutable step gate into real private repair execution; M16D exposes Previous/Current/Next/Continue and top-left third-row operation HUD in Vulkan; M16E live-frozen pause integration; M16B forced R04/R05 common-executor migration. The current code is **not** yet a runnable single-step UI. M15D4/M14F physical acceptance and any later R07/R08 publication remain open.
