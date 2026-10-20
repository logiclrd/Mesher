# V17-M65 — Repair M11 asynchronous 16-step undo/redo observer race

## Reproducible CI evidence

Following M63's sign-only camera PR #116, whose exact PR-head run
[Actions #39507461218](https://github.com/logiclrd/Mesher/actions/runs/39507461218)
passed GCC/Clang/Windows /W4 /WX, its independent
[merged-main run #39508068531](https://github.com/logiclrd/Mesher/actions/runs/39508068531)
failed Windows CTest #31 `asynchronous_sculpting` with:

`Async worker test failed: M11 each real FIFO physical union is independently undoable`

All other 40 CTests passed, including M63's updated `mouse_picking`.
GCC and Clang passed. This does not implicate camera movement:
the M11 test exercises unmodified physical workers, source-authorized
union and immutable history.

## Exact test-observer race

The original M11 16-stage positive physical-tower regression called
`worker.undo()`, polled ONLY
`worker.status().history.redoDepth`, and immediately read
`worker.takeSnapshot()` expecting the matching root. The worker's
history/status update and coalesced renderer mailbox publish are
different asynchronous events. Thus a legitimate temporary old frame
can appear immediately after an advanced history counter; this
matches the separately repaired mirrored-worker race in M57
and the mixed-case M34 observer gate.

M65 changes ONLY the M11 test. Its bounded thirty-second helper
waits for (a) a `takeSnapshot()` carrying the exact expected
`std::shared_ptr` immutable root, (b) `initialRevision()`
identical to that root, AND (c) matching BOTH undoDepth and
redoDepth. The helper retains all sixteen physical undo and all
sixteen redo steps, and reports timed-out action, step, expected
and observed depths and whether the exact frame was seen.

This is not a retry of failed geometry, relaxed history expectation,
replacement identity, random seed change or production fix. The
worker, source certificates, COW repository, Boolean geometry,
mesh rendering and M63 pan code are unchanged.

## Integration gate

Run full Vulkan-enabled Linux GCC, Linux Clang and Windows
MSVC /W4 /WX CPU/CLI/CTests, original history/checkpoint
stress, benchmarks on exact PR head and independently merged
`main`. The original M64 pole-wire depth investigation and
M52 Linux/Windows physical GUI observations remain open,
as does original M15.

## Reproduction acceptance (October 20, 2026)

The exact M65 implementation PR #117 head `5b6d003e` passed
[Actions #39509119650](https://github.com/logiclrd/Mesher/actions/runs/39509119650):
full Vulkan-enabled GCC, Clang and Windows MSVC /W4 /WX build/test,
**41/41 CTests** including Windows CTest #31 `asynchronous_sculpting`
(the previously observed failure, now passed in 29.02 seconds),
repeated worker history/checkpoint tests and applicable benchmarks.
Subsequent documentation-exact PR-head and merged-`main` CI
are independent integration gates. No source, render or physical
worker behavior changed.
