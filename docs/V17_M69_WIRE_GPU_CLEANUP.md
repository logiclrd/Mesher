# V17-M69 — Retire dead detached Vulkan wire resources

M64 diagnosed the projection-depth mismatch of M58/M62's
screen-space expanded edge quad. M67 shades mesh edges inside
their actual depth-tested solid owning triangle using
noperspective barycentric physical-pixel distance. M68 shades
the work floor's independently-spaced unit XY grid on the
actual depth-winning plane using its perspective-correct
world XY pixel derivatives (no false floor diagonal).

M69 removes the now-unused Vulkan wire buffer and pipeline
resource lifecycle: no `uploadWire`/`replaceWire`, no
`wirePipeline`, `floorLines`, legacy model lines, or
per-COW-partition GPU wire buffer allocations/replacements/
retirements. `buildDrawing` and `buildWorkFloor` now stage
only solid triangle vertices. The two pipelines are opaque,
depth-writing solid (including optional W-controlled edge/grid
AA coverage) and transparent HUD. The old `wire.vert`,
`wire.frag`, CPU quad helper and its geometric reference test
remain in the repository as an explicitly historical baseline
for M64 but are no longer compiled into Vulkan.

The source-owned CPU `PartitionRenderGeometry::wire` data
STAYS: it is essential for edge uniqueness, immutable
source-positive manifold diagnostics and accepted source
staging audits, and doesn't allocate GPU buffers.
M59's intentionally empty legacy COW startup still avoids
zero-byte Vulkan uploads. Original/explicit Merge, undo/redo,
source certification, history and worker logic are unchanged.
The M56/57/58/59/62 CMake contracts are updated to certify
the actual, stronger depth-owning surface renderer and W/G
visibility, not obsolete line-quads. A new M69 contract
forbids stale GPU resources and confirms source wire auditors.

Build and CTest, history/checkpoint threaded stress and
benchmarks must pass on Linux GCC/Clang and Windows MSVC /W4
/WX with Vulkan; also test the independent merged-main commit.
Actual human-reviewed Linux and Windows GPU visual appearance,
including normal/near/exact 90-degree thickness and hidden
edge occlusion, remains M52. M66 optional backface culling
remains deferred: two-sided interior debugging stays enabled.
