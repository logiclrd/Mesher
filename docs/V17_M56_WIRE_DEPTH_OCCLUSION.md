# V17-M56 — Wireframe hidden-edge depth leak and solid-only diagnostic

The user's rotated M51 reflected-original and accepted-Boolean screenshots
revealed long lines that appear to pierce nearer solid surfaces, although
many trace to unrelated faces behind the visible shell.

## Root-cause inspection

The Vulkan viewport already chooses a 32-bit floating depth attachment
(`VK_FORMAT_D32_SFLOAT`) when available, otherwise a 24-bit fixed-point
depth/stencil attachment (`VK_FORMAT_D24_UNORM_S8_UINT`).
Both solid and wire pipelines depth-test, the solid pipeline writes
depth and the wire pipeline does not; wire comparison is
`VK_COMPARE_OP_LESS_OR_EQUAL`.

The actual `shaders/wire.vert` nevertheless applied:

```glsl
gl_Position.z -= 0.00012 * gl_Position.w;
```

That always displaced the wire depth toward the camera by a **fixed
0.00012 in NDC**, regardless of how far behind the visible solid
surface it originated. Around depth 1 this is approximately a thousand
32-bit floating precision steps, or roughly 2,013 24-bit fixed-point
steps. Perspective depth is nonlinear (current near=.05, far=100),
so a small normalized-depth displacement can misorder a meaningful
amount of unrelated rear geometry, especially in the zoomed-out
reflected tetra fixture. A high-precision attachment **cannot**
compensate for a deliberately excessive shader depth shift.

## Implemented correction

The wire vertex shader now uses exactly the same
`constants.viewProjection * vec4(position,1.0)` projected vertex
depth as the solid geometry. It no longer modifies clip-space Z.
The existing depth test remains enabled for both and
`VK_COMPARE_OP_LESS_OR_EQUAL` for coplanar boundaries; solid draws
continue first, writing depth, and wire lines render second without
changing the depth buffer. The UI HUD is still depth-independent.

Press **W** (rising-edge only) in the actual GLFW/Vulkan viewport to
toggle wire overlay versus **solid-only** viewing; the window title
and console report which mode is active. This only skips the line
draws, without mutating the mesh, COW partitions, stored FaceIds,
worker state, proofs or history. The wire buffer and the displayed
immutable root remain available while wire visibility is OFF.

The removal of the large bias could permit occasional near-coplanar
edge dropout because rasterized line and filled polygon sample
positions are not guaranteed bit-for-bit identical. This is
preferable to displaying rear/internal wire *through* nearer solid,
and is an explicit target for real GPU observation in M52. A future
tight renderer-only polygon offset, if demonstrably necessary, must
be independently justified and bounded; do not restore an arbitrary
global 0.00012 clip displacement.

## Independent verification and remaining gates

`tests/verify_wire_occlusion.cmake` runs as a real CTest in the
Vulkan-enabled matrix and checks actual wire shader projection,
absence of clip-Z modification, solid/wire depth-test/write/compare
state and the `wireVisible_` draw guard. Normal shader compilation,
all CPU tests, existing 32 desktop CTests and the new source
contract (33 total), repeated history/checkpoint thread stress
and applicable benchmarks passed across Linux GCC, Linux Clang
and Windows MSVC /W4 /WX on substantive PR #108 head `d788ce87`:
[Actions #39464142614](https://github.com/logiclrd/Mesher/actions/runs/39464142614).
This checks the actual source shader and compiled pipeline, not an
observed GPU session. The final documentation-head and independently
merged-main CI remain separate integration gates.

**Still unresolved:** This fixes a demonstrated screen-space
occlusion hazard, but is not a proof of the reflected Boolean's
closedness or absence of residual internal fragments. The independently
planned **V17-M54** must audit real accepted source-positive roots for
directed edge incidence, component topology and nonadjacent triangle
intersections; it remains open. **M52** owns actual human-observed
Linux/Windows Vulkan graphics and wire visibility checks. Original
M15 remains open until both are satisfied.
