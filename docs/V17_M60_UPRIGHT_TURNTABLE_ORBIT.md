# V17-M60 — Upright turntable RMB orbit, consistent Z-up

## Problem and mechanism

Observed on actual Mesher viewport: starting RMB rotation, moving
the mouse in repeated circles causes the displayed scene's up vector
to rotate through a full 360 degrees. The M53 transform was a
cumulative orientation matrix, multiplied by an incremental fixed-Z
yaw and fixed-X pitch for every input sample. These rotations do not
commute: even a geometrically closed pointer rectangle can accumulate
orientation holonomy (roll). Normalizing this matrix would not
solve that semantic defect.

## Source-preserving turntable solution

The shared `ViewportOrbit` now stores two angles, `yaw_` and
`pitch_`, and reconstructs its orthonormal display basis from
`R = Rx(pitch) * Rz(yaw)` every update, rather than multiplying the
previous arbitrary orientation. Rightward movement yaws the nearer
negative-Y source toward the right; downward movement pitches it
toward the bottom. The original source-space Z direction always
transforms to `(0, -sin(pitch), cos(pitch))`, independent of yaw:
there is no roll degree of freedom. The fixed camera looks from
positive Z with elevation `atan(0.33)`; the symmetric +/-65 degree
pitch clamp prevents the global Z-up vector from projecting below
the screen's horizon. Yaw wraps without imposing a horizontal
travel limit. Large finite but overflowed angular inputs and other
invalid deltas are refused with no scene mutation.

The selected source pivot is held visually fixed by capturing its
current displayed point before changing angles, then solving
`translation = displayedPivot - R * originalSourcePivot`.
This also preserves prior Alt+RMB view translation. M55 ray-plane
screen-parallel background pivot and surface-hit precedence are
unchanged. `pickingRay` continues to invert this exact orthonormal
display matrix. No scene transform edits `MeshRevision`, stable
IDs, first-original proofs, symmetry, physical brushes, worker
history, disk checkpoints, or GPU topology. Real rendering uses
the same `ViewportOrbit::basis()` and translation as before.

## Tests and acceptance boundary

Extended the existing `mouse_picking` CPU suite with hundreds
of repeated closed four-segment RMB gestures around an off-origin
pivot (no roll), a full 360-degree yaw at nonzero pitch, screen-up
alignment throughout yaw, positive and negative hard pitch limits
with immediate reverse motion, pivot stability, and exact inverse
visual-to-source ray after interleaved yaw/pitch/Alt-dolly/background
selection. Nonfinite/overflowing mouse deltas cannot contaminate
the view. Existing M53/M55 pick, cursor-wrap, and sculpt controls
remain exercised.

The CI acceptance gate is all tests under Vulkan-enabled Linux
GCC, Linux Clang, and Windows MSVC /W4 /WX on exact PR head,
including history/checkpoint stress, followed by a separate
green merged-main run. CPU/CLI CI does **not** establish real
Linux/Windows Vulkan movement feel or cursor-wrapping behavior;
that remains M52, with original M15/parent V17 open.

## Implementation CI (October 20, 2026)

The substantive PR [#113](https://github.com/logiclrd/Mesher/pull/113)
head `3c596743` passed
[Actions #39498850898](https://github.com/logiclrd/Mesher/actions/runs/39498850898):
full Vulkan-enabled GCC, Clang and Windows MSVC /W4 /WX jobs green.
The GCC job registered and passed **all 39 CTests** including
`mouse_picking` with M60 circular path, no-roll and clamp scenarios,
and retained M59 startup and M58 AA tests. Repeated original
checkpoint/history threading stress and applicable benchmarks passed.
The final documentation-exact PR head and post-merge main CI are
checked separately; this is not real GPU appearance observation.
