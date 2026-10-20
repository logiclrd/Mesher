# V17-M67 — Depth-correct AA mesh edges on the visible solid surface

M64 analytically proves that detached AA edge quads at the
centerline's original clip depth self-occlude against their
own obliquely projected solid faces and appear suddenly thicker
at exactly the pole. M67 corrects the MESH portion only.

The nonindexed legacy and immutable COW partition mesh GPU
draws already contain exactly three consecutive vertices for
each triangle. `gl_VertexIndex % 3` supplies barycentric corner
weights with no vertex format, source-ID or topology mutation.
Noperspective interpolation and `dFdx/dFdy` give perpendicular
distances to projected triangle edges in actual framebuffer pixels.
M62 smoothstep coverage (1.5px outer halfwidth and 30% gray)
is mixed in the already depth-tested solid fragment, so it
cannot have a depth different from its own face. A genuinely
nearer occluder still hides that fragment with Vulkan LEQUAL,
without moving clip Z or using polygon offset. M66's later
culling proposal remains deferred.

W is passed to the mesh shader through `wireRaster.w`; floor
material 1 excludes triangle-edge shading, avoiding a fake
floor triangulation diagonal. The actual floor's separately
spaced unit XY grid is still drawn from legacy wire quads,
to be fixed in M68. Obsolete mesh wire buffers remain
allocated but are not DRAWN; M69 retires those resources
and source-contract expectations.

Added the CPU analytic `surface_attached_mesh_edge_distance`
CTest verifying screen-space barycentric distances against
geometric point-to-edge distances across scale and oblique
triangles; a viewport source contract checks true GLSL/COW
and W/floor routing, and forbids legacy mesh-wire draws.
Full compiler CI and independent merged-main run are required;
physical Linux/Windows visual appearance remains M52.

## Implementation acceptance (October 20, 2026)

The corrected implementation at `f4a94ae4` passed full
[Actions #39515294505](https://github.com/logiclrd/Mesher/actions/runs/39515294505)
on Vulkan-enabled GCC, Clang and Windows MSVC /W4 /WX,
with **45 CTests** on GCC, threaded history/checkpoint stress,
benchmarks, and compilation of the new mesh shader on supported
GLSL toolchains. Early source contracts expected the old two-draw
layout and were updated to enforce actual new W/G/depth semantics;
the final implementation rerun passed. M64's independently accepted
main is integrated with this branch. Device screenshots remain M52.
