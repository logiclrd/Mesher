# V17-M68 — Screen-stable unit XY grid ON the depth-visible work floor

The M64 screenshots included a discontinuity in the floor grid
at exactly the turntable's +90-degree pole. This grid is NOT the
edges of the two triangles used to draw the large work floor;
it consists of integer X and integer Y lines at unit spacing
over [-8,8]^2. M67's per-triangle barycentric mesh approach
cannot reproduce it without a false floor triangulation diagonal.

M68 colors those grid lines in the actual depth-tested floor
triangle fragment using the already available
perspective-correct source `worldPosition.xy`. The distance
from each coordinate to its nearest integer, divided by the
physical pixel derivative magnitude (`dFdx/dFdy`), is the
perpendicular distance to a genuine unit XY grid line in
screen pixels. The same 1.5px M62 smoothstep coverage and
linear 0.073239 gray are blended into the floor fill.
No new clip-depth or polygon bias exists: nearest model
surfaces keep hiding the floor via the existing LEQUAL
solid-only raster. The false shared floor triangle
diagonal is not drawn. W suppresses all mesh and grid
coverage; G suppresses the entire floor and its grid.
Source-relative floor Z and original XY extent persist.
Floor wire buffers and the old wire pipeline remain
allocated but NO LONGER DRAW until M69 retires them.

The new `visible_floor_grid_pixel_distance` CPU suite
tests all 15x15 interior integer grid lines in both
orientations, one-pixel shoulder coverage, the absence of
the diagonal on non-grid midpoints, and physical-pixel
scale under changing local projection. The
`viewport_surface_attached_floor_grid_contract` verifies
actual GLSL and W/G solid draw routing, and forbids
any remaining detached wire GPU draw. Older M56/57/58/62
tests are reconciled to preserve their core invariants,
not outdated pipeline syntax. Full Vulkan-enabled
GCC/Clang/MSVC CI and independent merged-main required.
Actual Linux/Windows appearance remains M52.

## Full implementation matrix — October 20, 2026

M68 implementation at `b00067ee` passed
[Actions #39515619434](https://github.com/logiclrd/Mesher/actions/runs/39515619434)
on Linux GCC, Linux Clang and Windows MSVC /W4 /WX with
Vulkan GLSL compilation, 47 registered CTests on GCC,
threaded history/checkpoint stress and applicable benchmarks.
The M67 independently merged-main acceptance has been folded
into the integration branch. Final documentation-exact PR head
and independently merged main are checked separately. Real
Linux/Windows visual smoothness is not claimed by CPU tests.
