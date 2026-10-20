# V17-M64 — Pole-dependent wire thickness: analytic diagnosis and renderer design

## Actual observation and inspected implementation

The real Linux Vulkan screenshots supplied October 20 show both
source topology wires and the separately drawn XY floor grid much
darker/thicker at EXACTLY the +90-degree turntable pole than at an
adjacent nearly-overhead angle, despite M62's fixed
`kWireOuterHalfWidthPixels=1.5f`. The screenshot confirms the
phenomenon, but does not establish a precise GPU coverage curve.

In the old screen-space wire pipeline (`shaders/wire.vert`),
`projected.xy` is moved +/-1.5 physical framebuffer pixels
perpendicular to the mathematical edge while clip z and w retain
the respective original 3D edge endpoint depth. At fixed position
along the edge, QUAD fragment depth is therefore independent of
perpendicular pixel offset. The filled `VK_CULL_MODE_NONE`
triangle, however, stores depth from its true geometric plane,
whose derivative perpendicular to that edge is ordinarily nonzero
after projection. A wire with flat centerline depth inevitably
fails `VK_COMPARE_OP_LESS_OR_EQUAL` on whichever side of its own
filled face is nearer to the viewer. At horizontal floor +90-degree
pole, the visible floor plane has zero perpendicular screen-space
depth gradient: both halves pass. Approaching the pole, that
gradient becomes nonzero and one half may disappear. Pixel depth
quantization determines the exact transition threshold. Switching
sign of the plane slope switches the clipped side.

The new `wire_pole_depth_self_occlusion_diagnosis` CPU CTest
integrates the calibrated M62 screen-space coverage on a
constant-depth sample and two positive/negative sloped surface
examples. The old flat-depth wire produces about 2.0 pixels of
opaque-equivalent coverage at zero slope, but only about 1.0
at nonzero slope. The test also demonstrates the candidate
**surface-attached** coverage remains symmetric on visible
interior edges while a genuinely nearer occluding plane still
removes every hidden fragment. Its goal is to prove the
mechanism; it is not GPU screenshot acceptance.

## Bounded replacement design, no global depth offset

A geometry-agnostic Z bias cannot distinguish the owning face's
self-masking from genuinely hidden edges and would regress the
original M56 repair. Instead:

1. **M67 (mesh):** Compute AA distance to all three actual edges
   from noperspective barycentric coordinates in EACH DEPTH-TESTED
   surface triangle. The solid fragment colors only fragments
   belonging to the nearest visible rasterized surface; never
   draws an unattached line quad in front of it. The existing
   GPU nonindexed `vkCmdDraw` calls and solid staging emit
   exactly three vertices per triangle (legacy and partition
   COW), allowing `gl_VertexIndex % 3` as a derivative-ready
   corner identifier without changing the seven-float vertex
   layout. Preserve floor as separately shaded material and
   defer its grid to M68. W gates coverage through a push
   constant, avoiding extra topology edits or FaceId changes.
   Maintain M62 full combined ~2px/half ~1px visual ink
   without claiming unsupported perfectly symmetric geometric
   silhouettes if only one face is visible.

2. **M68 (floor):** The floor is only two solid triangles,
   whereas its visible independent 17-by-17 XY grid has many
   lines NOT coinciding with the triangles' edges. Do not
   replace its lines with generic mesh barycentric triangle
   edges (would remove the grid and draw a false diagonal).
   Evaluate per-pixel distance to the ORIGINAL unit XY grid
   lines using the floor fragment's perspective-correct
   world/surface XY varying and screen derivatives; blend
   on the true depth-tested filled floor only. Continue to
   honor source-relative floor Z, G whole-floor toggle,
   W grid visibility, HiDPI physical pixel width, and
   nearer source occluders.

3. **M69 (cleanup):** After independent mesh and floor checks,
   retire obsolete standalone wire quads/pipeline/vertex
   buffers and update all M56/57/58/59/62 source contracts,
   preserving M59 empty optional indexed startup, W/G, dirty
   COW partitions and all source / worker / history invariants.
   Benchmark and test on three compilers; physical Linux/
   Windows visual approval remains M52.

M64/M67/M68/M69 are separate independently completable
milestones explicitly recorded in `todo.md` before any
renderer replacement. This M64 diagnosis is complete when
its CPU/source regression passes; the visual defect is NOT
claimed fixed until implementation and real GPU observation.
The eventual M66 backface culling remains deferred and will
need its own interaction with depth-correct per-face shading.

## Safety and acceptance

The mathematical reference uses standard LEQUAL depth comparison
with no clip-Z pull. It preserves genuine nearer occlusion and
one-sided physical silhouettes. The original manifold/winding,
FaceId, source-original proof, worker queue, persistent checkpoint
and selection behavior are unaffected. Three-compiler CI verifies
the independent analytic tests and source assumptions, not
physical Vulkan appearance.

## Automated analytic acceptance — October 20, 2026

M64 implementation head `bcd1c86b` passed full
[GitHub Actions #39514057701](https://github.com/logiclrd/Mesher/actions/runs/39514057701)
on Vulkan-enabled GCC, Clang and Windows MSVC /W4 /WX,
including the new analytic diagnosis CPU test and real source
staging contract, repeated history/checkpoint stress and the
applicable benchmarks. The final documentation-exact head and
independent merged-main CI are separate gates. This is a
verified mathematical reproduction, **not** a screenshot of a
corrected GPU renderer. M67/M68/M69 implement the full fix.
