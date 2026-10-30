# UX09B — Private New Model source and modal decision core

## Scope

This is a deliberately headless peer of UX09, built on UX09A. It does **not**
yet bind Ctrl+N or draw a Vulkan overlay. Both public APIs reject invalid
requests before modifying the current editor: the live worker, checkpoint
identity, history and renderer are not arguments to these functions.

`generateNewModelSource(request,startupOptions)` produces a privately owned
`Mesh`, `ViewportSceneBounds`, and symmetry flag. Asymmetric and symmetric
spheres follow `generateSphere()` with the original seed, relaxation and
seam settings; torus follows `generateTorusFixture()`. The symmetric flag
is false for torus. Spheres pass the watertight genus-zero validation;
torus passes the one-component genus-one topology audit. Parser accepts
positive decimal vertex counts and the generator's actual constraints:
spheres 8–1,000,000, torus 128–1,000,000.

## Modal state transitions

- `Closed` → `ChoosePrimitive` when Ctrl+N opens, displaying the three
  previews in asymmetric/symmetric/torus order, the numeric Vertices field
  at lower left and Cancel at lower right.
- Choosing a model validates the numeric field before any generation.
  If clean, return `Create` and close. If dirty, retain the complete
  pending choice and enter `ConfirmDiscard`; do not generate or replace yet.
- `ConfirmDiscard`: direct Save exists only for a valid native `.mshr`
  destination. Save and Save As return request actions, never `Create`.
  No returns `Create`; Cancel/Escape closes and drops the pending choice.
- When the host really queues a save, call `saveQueued` and move to
  `AwaitSave`; no replacement is permitted while the write is pending.
  A confirmed successful completed write calls `saveCompleted(true)`
  and returns `Create`. Failure calls `saveCompleted(false)` and
  restores `ConfirmDiscard`. Picker cancellation calls `saveCancelled`.
- A host must ensure a captured revision and the exact save ticket
  correspond to the incoming completion. A separate outstanding prior
  writer or document generation mismatch is **not** a valid success.

## Integration peer UX09C

1. Render the in-window overlay using the existing HUD pipeline;
   overlay must intercept mouse and keyboard before sculpt, paint, orbit,
   global shortcuts or the existing Escape-to-exit behavior.
2. Route click cards, digits, Backspace, accelerators, Cancel and Escape
   into `NewModelDialog`. Capture the **startup** `--vertices` count,
   not an imported/edited mesh vertex count.
3. On `Create`, construct a new *private* AsyncSculptWorker and mesh
   revision root before calling the same atomic `refreshPartitions`/commit
   protocol used by UX06 Open. Frame bounds, reset brush calibration,
   paint selections and dirty history, and clear document identity only
   after a successful renderer/worker commit.
4. Bind dirty state to accepted revision identity and completed checkpoint
   baseline. Poll the asynchronous writer without blocking the Vulkan UI
   and permit New only after actual durable completion, never enqueue.
5. Support the agreed filename provenance rules: no direct Save to
   imported OBJ/PLY/STL, inherited Save As filename/filter, optional
   export-seeded stem converted to `.mshr`, and preserve the session-wide
   Ctrl+E codec preference across native Open and New.
6. Perform physical GPU checks on X11, Windows and macOS, including
   modal focus, multimonitor, failed generator, cancelled writer and
   repeated New/Undo/Save/Export paths.

## Tests

`ux09_new_model_private_candidate_and_modal_policy` runs each real
primitive generator and checks vertex counts, topology, validation
limits, modal transitions and button layout; existing mesher tests
must remain green. No test asserts broken or unimplemented UI behavior.
