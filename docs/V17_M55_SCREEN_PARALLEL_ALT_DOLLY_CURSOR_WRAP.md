# V17-M55 — Screen-parallel background orbit pivot, Alt+RMB dolly and viewport-edge wrapping

This flat, independently completable peer refines the M53 viewport after
the user's actual reflected-fixture inspection. It does not claim the M51
accepted geometry is clean or manifold: M54 remains the next independent
CPU topology/residual-intersection audit, followed by real Linux/Windows
Vulkan host observations in M52 and unchanged original M15.

## Background pivot is now screen-parallel

On ordinary RMB press over a source triangle, the precise displayed
immutable COW BVH source hit remains the fixed pivot. On a **background**
press, the current camera's *display-space* visual ray intersects the
plane passing through the **scene-transformed centroid** and perpendicular
to the camera's centre-viewport forward vector. This plane is always
parallel to the screen, even after accumulated Z/X rotation or view
translation. Only the resulting point is inverted through the existing
rigid display model into first-original source coordinates and held
fixed across the drag. If the intersection is parallel/nonfinite or
behind the visual eye, use the current source centroid as a finite
fallback. A centroid scan still occurs only when explicitly starting
a background gesture; no render-frame or worker traversal is added.

This supersedes M53's original fixed world-Y XZ-plane heuristic,
which rotated along with the displayed geometry and was not actually
screen-parallel. Source hit identity, physical sculpt input, immutable
face picking, M29 first-original identity and worker source credentials
remain entirely unchanged.

## Alt + right mouse: view-ray dolly

Holding either Alt key **at the instant RMB is pressed** selects the
dolly gesture until RMB release, regardless of later modifier changes.
Horizontal pointer movement controls forward/back camera movement
along the centre-viewport vector, scaled by framebuffer pixels, original
45-degree field of view and current startup camera distance. Positive
pointer X moves the *viewer forward*: the rigid displayed scene shifts
in the opposite view-forward direction. Negative X reverses it.
Vertical movement is deliberately inert during dolly (it does not
quietly become a pan). Normal RMB without Alt still rotates around the
previous fixed source pivot via world Z and X angles; no implicit
translation mode switching mid-gesture.

Mesher's existing Vulkan camera/projection uses a rigid scene-model
matrix; storing equivalent view-relative translation in that display
matrix avoids changing the real worker mesh and preserves the same
inverse transform for source-space picking. Dolly never repositions a
mesh vertex, alters checkpoint, changes first-original FaceId or
creates a history Step.

## Edge wrap and compositor limitation

During either held-RMB gesture ONLY, a pointer at the left/right or
top/bottom client edge is requested to warp to a five-pixel inset on
the opposite edge. GLFW mouse positions are **window-client**
coordinates; motion is converted to framebuffer pixels for DPI
independence. The genuine motion up to the boundary is processed once;
an expected synthetic warp is recorded and any stale pre-warp reading
is ignored until the far-side position appears. The next real
movement starts from that destination, not from the old boundary.
Resize restarts the delta baseline. Other gestures and idle mouse
movement are never wrapped.

A compositor may refuse programmatic GLFW cursor positioning
(e.g. a security-constrained Wayland session). The code checks the
error, prints a precise diagnostic once and disables wrapping for
that RMB gesture while preserving ordinary local drag behavior.
Do **not** represent CPU tests or CI compilation as proof that warp
works on a real Wayland/Linux, X11/Linux or Windows Vulkan host.
M52 owns actual desktop observations and remaining UI acceptance.

## Acceptance evidence

The shared pure helper in \`include/mesher/viewport_orbit.hpp\` now
handles screen-parallel plane calculation, exact inverse ray/pivot
mapping, view-forward dolly, and synthetic-delta-safe cursor
wrap planning. \`tests/picking_tests.cpp\` checks rotated-and-dollied
screen-space plane alignment, exact visual-ray incidence, surface
precedence, parallel/behind fallbacks, source-ray inverse, directional
dolly/reverse and no-effect invalid span, wrap in both axes, stale
pre-warp suppression, repeated reverse crossings, resize, compositor
warp refusal and release.

Substantive PR head `763573d9` passed full Linux GCC, Linux Clang
and Windows MSVC /W4 /WX Vulkan-enabled CI, 32 registered desktop-
enabled CTests, repeated history/checkpoint threading stress and
applicable benchmarks:
[Actions #39460369523](https://github.com/logiclrd/Mesher/actions/runs/39460369523).
This establishes the source-preserving mathematical and compiled
runtime path, **not** observed physical cursor wrapping or Windows/
Linux input feel. No human GPU run is asserted. The exact PR head and
independently merged-main verification are separate gates. The M54
manifold audit, M52 actual GPU observations and original M15 checkbox
stay open.
