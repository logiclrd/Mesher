# V17-M53 — Source-preserving right-mouse viewport orbit

This is an independently completable usability prerequisite before
M52 real Linux/Windows Vulkan acceptance and the unchanged original M15.
It does **not** prove that the M51 reflected Boolean is manifold:
an independent M54 result audit is planned immediately after M53.

## Interaction and pivot

Hold the **right mouse button (RMB)** and move the pointer. The currently
displayed scene rotates in response; release RMB to freeze the view.
Mouse X rotates the *scene* about the fixed world Z axis. Mouse Y
rotates about the fixed world X axis. Positive right and down cursor
movements respectively move initially nearest (negative-Y) material
right and down. The speed uses framebuffer pixel delta and the smaller
of framebuffer width and height: 2π radians over that span. Mixed
X and Y movement composes yaw and pitch while remaining rigid.

At **RMB press** the actual immutable displayed-index BVH (or legacy
flat source fallback) ray-picks the triangle under the cursor. A
surface hit is the original source-space pivot for the whole gesture,
and a hit incurs no whole-revision centroid traversal.

For a background miss, the same inverse-transformed viewport ray
intersects **the XZ plane whose Y is the *current* displayed revision's
vertex centroid Y**, exactly as specified. The centroid is computed
by a single intentionally user-triggered read-only traversal of the
current immutable partition directory on RMB press; no render-frame,
stroke or worker CPU scan is added. For a parallel plane ray or a
plane entirely behind the eye, the finite current centroid is used
as deterministic fallback rather than NaN/infinity. Window-coordinate
deltas are rescaled to actual framebuffer pixel dimensions for HiDPI.

The renderer composes a rigid scene-space matrix after the unchanged
perspective and original eye camera, so all shaded partitions,
uniquely owned wire edges and the world grid respond together.
The inverse transform is applied to BOTH ordinary sculpt picking and
actual typed Merge FaceId picking. The ray still reaches the exact
unrotated immutable source root; no model coordinates, original IDs,
mirror pairs, source-proof lifetimes, budgets, publication CAS,
checkpoint, undo or redo are transformed or modified by RMB.

While RMB orbit is active, new LMB Inflate/merge selections are
suppressed. An existing LMB stroke/slider interaction prevents an
RMB gesture from starting. M, A, Enter, Backspace retain their
existing explicit Merge behavior. A diagnostic prints whether the
RMB pivot was a surface hit or centroid-Y plane, and the console's
old literal \`\\n\` A-key text is corrected.

## Independent verification

\`tests/picking_tests.cpp\` now checks origin identity, surface-hit
precedence, correct centroid-Y XZ intersection, parallel and
behind-eye fallback, matrix/ray inverse, multiple frames, long
repeated orthogonal rotations, fixed pivot, mouse-direction sign
and nonfinite/zero-framebuffer refusal. The substantive implementation head `e64d739a` passed all
three full Vulkan-enabled GCC, Clang and MSVC /W4 /WX CI jobs
([Actions #39455771691](https://github.com/logiclrd/Mesher/actions/runs/39455771691)),
including the complete CPU test suite, repeated history/checkpoint
stress and applicable benchmarks. This is source-picking/rigid scene
acceptance, **not** observed Linux/Windows GPU movement nor a
structural verification of the M51 Boolean result. Final
documentation-head and independent merged-main CI remain distinct
release gates.

**Not certified here:** the actual user's rotated Vulkan frames,
GLFW/driver feel or Windows input (M52); the internal/hidden edge
topology after accepted mirrored union (M54). Both remain explicit
open milestones. No screenshot is substituted for a topology check.
