# V17-M57 — Work-floor/model separation and bounds-based fixture clearance

## Observed problem and evidence

The user's Linux screenshots of the reflected tetra source and
accepted Merge looked increasingly cluttered after M56 removed
the excessively biased wire clip depth. Independent M54
[merged-main CI #39473016071](https://github.com/logiclrd/Mesher/actions/runs/39473016071)
now confirms that actual M51 reflected accepted workers produce
**two closed, non-self-intersecting, correctly mirrored manifold
components**, each with 2.3434 signed volume, and that the CPU
renderer stages exactly the live topology edges (zero ghost wires).
No geometry failure is currently supported by those test results.

The Vulkan viewport, however, still generated an actual depth-writing
2-triangle, 16×16 XY *floor*, fixed at **Z=−1.22** inside
`buildDrawing()`. That level was chosen below a unit sphere,
not below tetra source geometry. The M50/M51 source triangles
reach **Z=−3**, so the floor **physically passes through the
geometry**. Because the floor is a depth-writing solid plus
depth-tested grid lines, the displayed picture can show apparent
mesh-to-floor intersections and portions of the model obscured.
It is misleading to identify this as a Boolean failure.

## Implemented separation

`buildDrawing()` now builds only actual flat model triangles
and unique model edges; in COW sessions it receives an empty
legacy model, so there are no phantom duplicate source triangles.
The **separate** `buildWorkFloor(z)` creates floor-only solid
and grid-line vertices. Actual Vulkan vertex allocations
`floorSolid` and `floorLines` are not reused by
the mutable legacy model buffers or the indexed partition renderer.
They are destroyed separately and never touched by
`refreshPartitions()`, `refreshMesh()`, worker steps, undo or redo.

The pure `viewportWorkFloorZ(initialMesh)` reads the immutable
startup mesh **once** in the viewport constructor. If the model
fits entirely above the original -1.22 floor, the precise
legacy level is retained. If any source vertex is below it,
the floor moves to the minimum source Z **minus 0.5** in
the same original model coordinates. Both supported asymmetric
and reflected developer fixtures therefore use **Z=−3.5**
rather than cutting them at -1.22; ordinary unit-sphere
startup is unchanged at Z=−1.22. The scenic floor remains
purely visual and is transformed with the current M53/M55
rigid viewport orbit/dolly matrix, just like the mesh.
No source geometry/FaceId/mirror pair, exact-contact search,
CAS, work limit, history or checkpoint path is changed.

**G** is an actual rising-edge GLFW viewport key that hides
and shows *both* the floor's solid triangles and its grid
line segments. It does not touch the legacy model
triangles, the indexed COW partitioned solids, or their
topological wire lines. **W** still independently toggles
only wire edges including the grid lines. One can therefore
inspect the exact same worker revision from the same
camera angle in solid-only/mesh-wire/grid-off modes.
The title and console report the floor toggle state/level.

For future freeform sculpting, a static startup floor can again
be approached or crossed by subsequent geometry; that is a
separate user-facing display decision to address if encountered,
not a reason to make the render thread traverse every partition
each frame. **M52** must still inspect real GPU line-depth
behavior on Linux and Windows.

## Regression acceptance

`tests/viewport_floor_tests.cpp` uses the actual named
first-original fixture factories. It checks the original
minimum Z=−3 for both, expected floor exactly −3.5,
sufficient vertical separation and stable fixture triangle
counts, the unmodified legacy sphere floor −1.22,
empty scenes and a newly crossing source fixture.
`tests/verify_viewport_floor.cmake` is a distinct real
Vulkan-enabled CTest guarding the separate floor buffers,
lifetime cleanup, the pure floor calculation and the
independent actual G rendering branches for both floor
triangles and grid wires. Other source-positive,
renderer-staging and entire CI history/checkpoint tests
remain unchanged.

All Linux GCC, Linux Clang and Windows MSVC Vulkan-enabled
warning-clean CTest matrices, original PR-head and
independently merged-main builds must succeed.
M54's real geometric proof stays complete, while M52
and original M15 remain explicitly open. This milestone
does **not** claim to have human-observed actual GPU/driver
output on every OS.
