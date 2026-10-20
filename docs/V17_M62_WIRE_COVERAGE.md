# V17-M62 — Narrower smoothed AA wires, preserve strict occlusion

## Actual user observation

The user's October 20 Linux Vulkan screenshot confirmed M58's
formerly broken subpixel wires are now continuous and previously
hidden rear edges remain correctly occluded, but the strokes are
too dark/thick and appear narrower alongside certain rear-facing
neighbor triangles. The user independently confirmed **G** hides
and restores the whole floor. This is real Linux-observed evidence
for these specific M58/M57 features, **not** complete M52
Windows/Linux desktop source/selection/recovery acceptance.

## Why width alone cannot guarantee identical silhouettes

M58 expands the TRUE 3D projected topological line into a two-triangle
GPU quad with equal on-screen width on both sides. Vulkan tests
each quad fragment against the solid depth buffer: where an adjacent
nearer/rear-facing surface projects over part of the line's widened
footprint, that side fails the legitimate `LEQUAL` test. The
visible half can therefore be narrower **even when the underlying
quad has mathematically uniform thickness**. Unlike line aliasing,
this clipping is correct visibility. Forcing the occluded half to
render by a blanket forward clip-Z bias recreates M56's hidden
backside leak, so M62 explicitly leaves clip-Z and depth state
unchanged. Pixel-perfect apparent symmetry at face/silhouette
junctions requires a separately designed surface-aware
silhouette/depth-eligibility pass with its own occlusion proof,
rather than an arbitrary widening or unconstrained polygon
offset. M62 does not claim that unsupported geometry solution.

## Calibrated physically antialiased stroke

The M58 outer halfwidth of **2.0 physical framebuffer pixels**
with a linear one-pixel shoulder corresponds to **3.0 pixels of
integrated opaque-equivalent ink** for a completely visible edge,
and roughly **1.5 pixels** if exactly half its depth-tested
quad is clipped.

M62 reduces the outer halfwidth moderately to **1.5 pixels**
(not halved), retains a full one-pixel shoulder on each side,
and uses symmetric monotonic GLSL `smoothstep` coverage. The
full stroke now integrates to **2.0 pixels of effective ink**;
one clipped half still contributes approximately **1.0 pixel**.
The centerline stays fully opaque, screen-space coverage remains
smooth even for shallow diagonals and HiDPI, and floor/grid and
indexed COW mesh use exactly the same shared raster parameters.
No stroke will be deliberately exposed through an occluding
closer face to simulate visually symmetric widths.

The CPU reference `wireCoverage()` and shared
`kWireOuterHalfWidthPixels` are checked by
`screen_space_aa_wire_quad_encoding`; independent
`viewport_wire_coverage_calibration_contract` and extended
M58 shader contract cover correct full/half ink,
fragment shader / CPU agreement, M56 zero-depth-bias
`LEQUAL` read-only overlay, viewport W and scenic G controls,
and real Vulkan source wiring. The M54 accepted-worker manifold
and CPU wire edge identities remain untouched.

## Remaining visual/device and integration acceptance

Vulkan-enabled GCC, Clang, Windows MSVC /W4 /WX, CPU CTests,
history/checkpoint stress and benchmarks must pass at exact PR
head and independently merged main. A subsequent **actual**
Linux/Windows GPU screenshot/visual inspection is required in M52
to judge preference for the narrower width and screen-space
coverage at representative scales. No CI test proves all
driver-specific raster appearance or eliminates the legitimate
depth-clipped half-line phenomenon. M15 and V17 remain open.

## Initial implementation verification and integration

PR [#115](https://github.com/logiclrd/Mesher/pull/115) head `dbf45018`
passed [Actions #39503750610](https://github.com/logiclrd/Mesher/actions/runs/39503750610)
on Linux GCC, Linux Clang and Windows MSVC /W4 /WX,
including all 41 registered CTests on GCC, the new shader/Vulkan
coverage contract, all existing historical proof/safety tests,
threaded history/checkpoint regressions and applicable benchmarks.
The branch is reconciled with M61's merged-main source and accepted
work; exact final PR-head and merged-main CI are separately checked.
No driver appearance is claimed without physical host testing.
