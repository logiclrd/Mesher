# V17-M48 — Explicit default-OFF Vulkan Merge geometry input

## Independent scope

M48 is the second flat M15 prerequisite, after
merged M47's accepted finite, first-original
worker contact discovery. It adds a user-visible
opt-in mode and source selection *only*.
Original V17-M15, M49, V18 and V19 remain OPEN.

## Opt-in and zero phantom authority

A normal Mesher desktop launch still constructs
the previous Inflate worker, without
`physicalExact` M29 source credentials,
automatic union discovery or Boolean execution.
The new `--merge-geometry` startup flag is
an explicit **authorization to provision** a
protected original source worker. It is only
supported on a freshly generated in-memory
interactive mesh; `--recover` is deliberately
rejected, since V13 disk persistence stores a
source-required marker but NOT opaque first
M29 authorization. Demo/comparison mode also
refuses the flag. Even with explicit
authorization the *edit mode* starts OFF.

Press **M** to turn Merge source selection
ON/OFF. In ON, LMB cannot accidentally stamp;
a click ray-picks the current displayed
immutable indexed root and selects one stable
first-original `FaceId` (up to 32 unique
IDs per revision). Enter explicitly sends
the exact selected immutable root and IDs
to M47's bounded, worker-only discovery;
Backspace, switching OFF, and new published
renderer root clear selections. A second
current-root check on the worker catches
asynchronous stale input. Unknown or
no-longer-first-original IDs are never
substituted by world-coordinate coincidence.

`RegionTopologyIndex::FaceRayHit` /
`firstRayFaceHit` use the SAME existing
path-shared two-level ray BVH, candidate
triangles, numeric ray barycentrics and
minimum-distance ordering as the previous
`firstRayHit`, now returning stable ID
with point in one traversal. The legacy
point API continues through the same helper
and retains its numerical picking behavior.
A CPU regression directly compares typed
hit vs position-only hit for many
hit/miss rays and confirms `maybeFace`
on the immutable selected root. No
unbounded all-face scan on render thread.

The GPU HUD now displays MERGE DISABLED,
OFF or ON and the number selected. Window
title and console expose accepted/refused,
actual pending queued/executing command
count, and terminal M14 reason. A new
atomic outstanding-command counter is
incremented at enqueue and decremented on
all worker completion or error paths, unlike
attempt/accepted counters which do not
represent an unprocessed queue. Queued
merge blocks ordinary user strokes until
it is no longer outstanding.

M47 continues to independently validate
original FaceId nominations, exact expected
root, full bounded source-contact graph,
mirror involution, first-original M29
lineage and M40–M46 finite surgery budget.
A no-contact/ambiguous/unsupported/over
budget result simply refuses and does
not change the renderer/history.

## Tested evidence and remaining limits

Substantive M48 source & pending command
changes are included in PR #101; full
GCC/Clang/MSVC Linux/Windows Vulkan build,
23 CPU suites, stress, and applicable
benchmarks are acceptance gates before
merging. CI cannot run a user's GPU
interactions; actual live desktop
picking/keyboard/mode/symmetric/asymmetric
acceptance remains M49 followed by original
M15. Do not advertise a universal
multi-body exact Boolean, exact CCD,
strict time/RSS or automatic joining of
any arbitrary selected surface pair.
Desktop capability is restrictive:
an unarmed normal session cannot be
silently upgraded to protected M29
authority; a specially armed-but-OFF
session retains opted-in protected
physics for Inflate, while normal
startup remains legacy.

**NEXT:** M49 CPU-mode semantics +
Linux/Windows targeted Vulkan input,
selection, accepted/refused history,
render/picker and storage integration.
Original M15 and V18/V19 remain OPEN.
