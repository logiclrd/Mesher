# V17-M59 — Guard genuinely empty legacy vertex buffers on indexed viewport startup

## Verified regression

The first M57 floor/mesh separation correctly changed
`buildDrawing(displayedIndex_ ? empty : mesh_)` so an indexed COW
worker no longer duplicates original legacy triangles and wires
over its immutable partitioned geometry. However, the viewport
constructor still unconditionally called `upload(drawing.solid)`
and `upload(drawing.lines)`. The Vulkan upload helper explicitly
refuses an empty array with the exception **"Empty vertex buffer"**.
The default interactive application uses `displayedIndex_`, so
both legacy buffers are now *intentionally empty*, and the exception
occurs before the first frame. Even removing the exception alone
would not solve everything: recording the Vulkan draw still bound
the legacy buffer handles unconditionally, which would be
`VK_NULL_HANDLE` if no allocation existed.

This was a **desktop integration regression**, not a corrupt mesh,
failed protected source proof, or Boolean algorithm failure.
The user's independent newline-literal cleanup on main commit
`18ed1278` was already present and is retained unchanged.

## Targeted repair

With indexed immutable COW source, leave the optional legacy
`solid` and `lines` default-empty, and initialize real indexed
partition buffers using the existing `initializePartitions()`.
With ordinary flat demos, allocate their legacy buffers when
the corresponding vertex list is nonempty. In both cases
`record()` only binds and draws each optional legacy buffer if
its `vertexCount` is nonzero. The actual floor and grid remain
separate, nonempty scenic GPU allocations from M57, with the
same G key; W mesh-wire toggling remains unchanged.

The strict `upload()` error for unexpected attempts to allocate
zero-size GPU buffers is preserved; the correct contract is to
**skip optional empty uploads**, not create fake dummy triangles,
hide unexpected errors or permit null Vulkan binds. Existing
`replaceBuffer` for dynamic meshes and conditional real COW
partition uploads remain unchanged.

`tests/verify_viewport_optional_buffers.cmake` registers one
headless `viewport_cow_empty_optional_buffers` CTest in the
Vulkan-enabled matrix. It checks the actual constructor's guards,
both Vulkan record-time optional draw guards, preserved partition
initialization/draws and strict zero-byte upload refusal. This is a
source-contract regression, not a real GPU process launch:
the final exact-head Linux GCC, Linux Clang and Windows MSVC
full builds and CTest/checkpoint stress/benchmarks are still
required. The user should re-test normal startup on their
actual GPU after merge, and M52 retains full real desktop
runtime acceptance.

No M58 antialiased-wire implementation, model data,
source-certified Boolean, mirror mapping, CAS/history,
checkpoint format or worker processing is changed.
