# Mesher

An experimental **C++20/Vulkan** mesh editor foundation. The application generates a random-point triangulated sphere, runs a
symmetric Inflate-brush demonstration by default, and displays the result in a
full-screen 3D viewport with flat face lighting, a 30%-gray wire overlay, and
a gridded XY floor. **Z is up.** The geometry engine is independent of Vulkan/GLFW and can
be tested without a graphics device.

## Build

Requirements: a C++20 compiler, CMake 3.22+, Vulkan SDK/development headers,
GLFW 3.3+, and either `glslc` or `glslangValidator` (for compiling the included
GLSL shaders). On Ubuntu/Debian, install `libvulkan-dev`, `libglfw3-dev`,
`glslang-tools`, `cmake`, and `g++`. A Vulkan-capable graphics driver is required
to run the viewport.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher
```

For CPU-only builds, with no Vulkan/GLFW dependencies:

```sh
cmake -S . -B build-cpu -DMESHER_BUILD_VIEWPORT=OFF
cmake --build build-cpu --parallel
ctest --test-dir build-cpu --output-on-failure
```

## Command line

```text
Mesher [--symmetric|--asymmetric] [--vertices N] [--seed N] [--relax N]
       [--no-inflate] [--step D] [--strength D] [--segments N]
```

Defaults are `--symmetric --vertices 5000 --seed 1 --relax 65`. The initial
viewport uses the primary monitor in full-screen mode. **Escape** exits.

By default Mesher inflates two mirror-image half-circle strokes across the
front of the generated sphere, with spherical influence radius **0.1** and
hardness **0.3**. The default stroke Step is **0.02** mesh units, strength is
**0.012** mesh units per full-influence stamp, and each arc is approximated
with **128** input segments. `--no-inflate` displays the unmodified sphere;
`--step`, `--strength`, and `--segments` allow experimentation. The demo is
precomputed before the viewport starts (not interactive yet).

- **Asymmetric:** Uniformly distributed random samples on the unit sphere,
  followed by iterative tangential repulsion using a spatial grid.
- **Symmetric:** Compute
  `m = min { integer > 2π / sqrt(8π / (n sqrt(3))) : (n-m) is even }`.
  Generate `m` points on a unit circle with randomly initialized spacings,
  relax the gaps toward equal spacing, then fix that circle. Generate
  `(n-m)/2` random hemisphere points and relax them against one another,
  the fixed ring, and a mirrored ghost population. Reflect the hemisphere's
  vertices and triangles, sharing the ring so there is **no doubled seam**.
  The initial reflection plane is `X=0`: its normal is perpendicular to the
  camera direction, and the ring lies in the `YZ` plane.
- **Triangulation:** An incremental 3D convex hull triangulates the relaxed
  points without Catmull–Clark smoothing. For symmetric meshes, each half is
  triangulated separately and the planar cap is discarded before reflection.
  Outward winding and manifold topology are verified.

For 5,000 vertices the result has **9,996 triangles** (Euler characteristic 2).

## Rendering

The view looks toward the origin from negative Y at a slight elevation, with
an XY grid at `Z=-1.22`. The camera is perspective, with 45° vertical field of
view, and its distance is recalculated on viewport size changes so the sphere's
projected diameter is 2/3 of the **smaller viewport dimension**. One point light
is above/behind/to the camera's left, and a second is to the far right. Faces
are lit separately (flat shaded), and unique mesh edges are drawn as
`RGB(0.30,0.30,0.30)` lines with depth testing. The floor has a second
material. The viewport rebuilds Vulkan swapchain-dependent resources on
resolution changes.

## Brush foundation

`include/mesher/brush.hpp` defines `BrushArea`, `Brush`, `BrushStroke`, and
`InflateBrush` independently of Vulkan. A stroke starts with one or more
`(center, radius, hardness)` spheres and samples their combined **maximum**
influence. Every center has full influence through `hardness × radius`, then
falls to zero at `radius` according to complementary quintic *smootherstep*.
Hardness is a fraction in `[0, 1]`.

`BrushStroke::start` stamps immediately. `update` accepts new positions for
exactly the same number of centers, validates that each has travelled the same
3D distance, and samples **all** centers simultaneously at constant arc-length
`Step`. Leftover fractional steps carry across updates, including segment
corners. `finish` processes the last update, stamps the terminal position if
not already sampled, and releases brush state. `InflateBrush` moves influenced
vertices along the **normalized sum of the unit face normals** of incident
triangles, computed on the mesh before each stamp. It constructs vertex-to-face
adjacency once per stroke. Normal strength is a displacement **per stamp**.

This milestone intentionally keeps existing topology unchanged and performs
no adaptive subdivision, decimation, brush picking, interactive painting,
undo, or acceleration for hundred-million-vertex meshes. The Inflate demo
is deliberately small and deterministic, for validating the stroke engine.

## Architectural notes and limitations

- The generator returns a simple `Mesh` containing double-precision vertex
  positions and indexed triangles; it has **no dependency on Vulkan**.
- Topology validation checks vertex indices, outward winding, edge incidence,
  connectivity, and the sphere's Euler characteristic.
- Random generation is seeded and reproducible within the same standard
  library implementation; C++ distributions are not guaranteed to give the
  same sequence on different toolchains.
- The hull algorithm currently scans active faces for visibility, making
  it approximately **quadratic** in the number of vertices. This is suitable
  for the initial 5,000-vertex baseline, **not** yet the planned many-million-
  vertex editing engine. Replace it with an accelerated robust hull/meshing
  algorithm when scaling up.
- CPU-visible buffers are used for the small initial demo. Persistent,
  device-local buffers/staging and incremental rendering are future work.
- Interactive sculpting, painting, selection, undo, mesh import, and local
  remeshing are not yet implemented. The offline Inflate driver introduces
  the brush-stroke foundation without changing triangle connectivity.

## Status

The CPU geometry and topology tests run without a GPU. The Vulkan viewport
requires a machine with Vulkan/GLFW development packages to build and an
actual Vulkan implementation to run. See the GitHub Actions workflow for the
Linux full-build test configuration.