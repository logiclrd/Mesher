# V17-M49 — Default-OFF Vulkan Merge desktop integration and compiler acceptance

**Independent third prerequisite after M47 and M48.**
Original V17-M15 remains a separate, unchecked
integrated desktop acceptance review. V18
off-mode collision and V19 broad/human-observed
acceptance remain independent.

## The actual UI state machine, not a test-only surrogate

`DesktopMergeInput` in
`include/mesher/desktop_merge_input.hpp` is
directly called by `src/viewport.cpp`. The
same public methods are exercised by
`testM49DesktopModeSelectionAndBoundedWorkerIntegration`,
rather than approximating input behavior in
an unrelated mock. `DesktopMergeKeyEdges`
consumes GLFW M, Enter and Backspace key
states, triggering only rising-edge
mode, submit or clear events.

Ordinary desktop launch defaults to legacy
Inflate and has NO latent authenticated
Merge. Explicit `--merge-geometry` on a
fresh interactive in-memory original opts
into provisioning an authenticated M29
physicalExact worker, **but the actual
Merge selection mode still begins OFF**.
M enables it only when the worker is
idle. In ON mode LMB no longer inflates:
it uses the current displayed immutable
indexed ray BVH to capture stable
`FaceId` and its exact displayed root.
Selection is nonempty, unique, bounded
to 32 per revision, and never silently
substituted from later XYZ coincidence.
Backspace, toggling OFF, another
published root, and stale submissions
clear the selected list.

Enter packages exactly the selected
immutable expected root and nominated
first-original FaceIds, then queues
M47 `DiscoverMerge` on the FIFO worker.
The GLFW render thread does not perform
global triangle contact discovery.
Worker M29, fully bounded contour/join
and all M40–M46 source/geometry proof
and finite work limits stay unchanged.

CPU acceptance checks default OFF,
authorization denial, busy/sculpting
refusal, repeated/held key edges,
no-hit and zero stable IDs, duplicate
selection, 33rd-face selection refusal,
clear and OFF transitions, stale and
changed displayed roots, source
nomination transport, and pending
request gating.

## Real geometry, source and history

The test passes this SAME UI-controller
submission through a real M47 worker
and `desktopMergeExactLimits(false)`
on a genuine source-authorized
intersecting asymmetric two-tetra
original. The complete bounded contact
graph and source original witness
approve one queued original-root COW
publication, one history entry and
a dirty-owner immutable viewport frame;
the old displayed picker index is
preserved for consumers. An explicitly
sealed reciprocal reflected source
independently passes
`desktopMergeExactLimits(true)`
without substituting sphere-index
mirror rules for an unrelated mesh.

A stale original selected root is
refused by UI or worker without
rebasing history; invalid foreign
first-original FaceId nomination
fails on the authenticated worker,
not as an accidental blanket
all-originals consent. Real undo
restores the identical original
picker/index root, redo restores
the accepted union; cumulative
accepted work does not double.
A V13 checkpoint write/read
preserves a source-fidelity
requirement but cannot fabricate
opaque first-original M29 credentials
or allow unverified protected recovery.

## Four real desktop executable CLI checks

M49 adds four CTest cases calling
`Mesher` itself—without creating
a Vulkan instance/window:

- `Mesher --help`: output names
  Mouse Inflate as the default;
- `Mesher --merge-geometry --demo`:
  rejects unsupported demo mode;
- `Mesher --merge-geometry --recover ...`:
  rejects disk provenance upgrade;
- `Mesher --merge-geometry --no-inflate`:
  rejects noninteractive mode.

These supplement the original 23
CPU suites, giving **27 CTest
cases** in a complete viewport build.
They are proof of command-line
gate behavior, **NOT proof** of
rendered Vulkan desktop experience.

The first MSVC PR build compiled all
targets and passed 23 existing CPU
tests, but these four executable
tests failed at Windows process
load with `0xc0000135`: the
CI runner's stripped Vulkan SDK
provides `vulkan-1.lib` for linking
but no `vulkan-1.dll` at runtime
(`Vulkan SDK loader present: False`).
The final code links the MSVC
desktop target with genuine
`/DELAYLOAD:vulkan-1.dll`
and `delayimp`. Consequently,
GPU-free `main()` argument checks
return before the Vulkan loader
is required. A real interactive
Vulkan session still requires the
actual Vulkan loader/driver;
no mock or substitute was introduced.

## Verified substantive acceptance

[PR #102 substantive Actions #39365774949](https://github.com/logiclrd/Mesher/actions/runs/39365774949)
completed **SUCCESS** on Linux GCC,
Linux Clang and Windows MSVC
`/W4 /WX`; all **27/27 CTest
cases** passed, with 100-repeat
persistent checkpoint/worker
history stress and applicable
benchmarks. Final documentation
head and independent merged
`main` CI are required before
reporting M49 merged-complete.

**Next**: original V17-M15
criterion-by-criterion integrated
desktop acceptance review, followed
by separate V18 off-mode collision
control and V19 broad/human-observed
end-to-end acceptance. Do not
claim actual Vulkan interaction
was observed in headless CI,
or enable exact source Boolean
by default on ordinary mouse-down.
