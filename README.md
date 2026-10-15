# Mesher

An experimental **C++20/Vulkan** mesh editor foundation. The application generates a random-point triangulated sphere, animates a
symmetric Inflate-brush demonstration by default, and displays each stamp in a
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
       [--no-inflate] [--step D] [--strength D] [--segments N] [--brush-radius R]
       [--stamps-per-second HZ]
```

Defaults are `--symmetric --vertices 5000 --seed 1 --relax 65`. The initial
viewport uses the primary monitor in full-screen mode. **Escape** exits.
**Space** pauses/resumes, **Right** applies one stamp while paused, and
**R** restores the original sphere and replays the animation.

By default Mesher inflates two mirror-image half-circle strokes across the
front of the generated sphere, with spherical influence radius **0.4** and
hardness **0.3**. The default stroke Step is **0.02** mesh units, strength is
**0.012** mesh units per full-influence stamp, and each arc is approximated
with **128** input segments. `--no-inflate` displays the unmodified sphere;
`--step`, `--strength`, `--segments`, and `--brush-radius` allow experimentation. The demo is
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
triangles, evaluated once at the beginning of the stroke by default.
This avoids a feedback loop in which heavily deformed face normals turn inward,
folding triangles across the symmetry plane under wide overlapping strokes.
The brush now uses live face normals again: these are recomputed from the
modified mesh on every stamp. This deliberately exposes the folding effect
for diagnosis. Normal strength is a displacement **per stamp**.

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
- CPU-visible vertex buffers are refreshed after each animated stamp for the
  small diagnostic demo. Efficient dirty-region GPU updates, persistent
  device-local buffers and staging remain future work.
- Interactive sculpting, painting, selection, undo, mesh import, and local
  remeshing are not yet implemented. The animated Inflate driver demonstrates the brush-stroke foundation without
  changing triangle connectivity. Interactive mouse picking remains future work.

## Status

The CPU geometry and topology tests run without a GPU. The Vulkan viewport
requires a machine with Vulkan/GLFW development packages to build and an
actual Vulkan implementation to run. See the GitHub Actions workflow for the
Linux full-build test configuration.
### Wide-brush seam regression

The equatorial ring is shared by vertex **index** across both hemispheres.
With live per-stamp normals and overlapping radius-0.4 brush influences, the
mesh can nonetheless fold through its reflection plane. The experiment is now
played in the Vulkan viewport at **10 stamps per second**, initially showing
the undeformed sphere. Every stamp uploads updated solid and wireframe data;
the window title reports stamp progress and the number of radially inverted
triangles. Inversion-count changes are logged to standard output.

**Space** pauses/resumes playback, **Right** single-steps while paused, and
**R** resets and replays. The speed can be changed with
`--stamps-per-second HZ`. The animation is intentionally destructive at
large brush sizes so that the failure can be diagnosed; there are no safety
guards against self-intersection yet.

The path is recorded using the ordinary distance-sampled `BrushStroke`
engine and applied one stamp at a time by `InflateDemoAnimation`, whose
`advance()` method is independent of the Vulkan renderer. An animated run
and a synchronous run yield identical final positions (CPU regression).

## Experimental directional edge collapse

The default animated Inflate brush remains **unchanged**: it recomputes normals
at every stamp and performs no topology edits. Add `--collapse` to run an
experimental, conservative remeshing pass immediately after each stamp:

```sh
./build/Mesher --collapse --minimum-edge-ratio 0.65 --max-collapse-pairs 12
```

An edge becomes a candidate **only if it shrank during the current stamp**
and is now shorter than the configured threshold. The threshold is a fraction
of the nominal initial sphere edge length, not a request to normalize the
mesh globally. Candidates are tried shortest-first. Every collapse is subject
to the 2-manifold edge-link condition, outward-face noninversion for
previously outward faces, and closed-sphere topology validation. The two
reflected collapses are atomic when symmetry is enabled; the seam is shared,
never duplicated. Midpoint placement deliberately makes this an experiment
in whether simplification smooths the developing pinch, rather than a
geometry-error-optimized production simplifier.

The viewport animation title includes vertex count, shrinking edges below the
threshold, removed vertices, minimum edge length, minimum triangle altitude,
and inverted faces. Pause with Space and use Right for one-stamp diagnosis.
R resets **both** vertex positions and triangle connectivity.

A repeatable headless comparison (no Vulkan window) prints CSV rows for both
modes on the **same initial sphere and stamp sequence**:

```sh
./build/Mesher --compare-remesh > comparison.csv
```

The `--minimum-edge-ratio` default is 0.65 and
`--max-collapse-pairs` is 12 per stamp. Set `--max-collapse-pairs 0` to
disable removals while retaining diagnostics.

**Limitations:** This is deliberately edge-collapse-only, with no splitting,
vertex recolouring, geometric error metric or collision detection. It cannot
fix a valley when the opposing patches approach without shrinking their own
edges. The first version rescans the mesh and checks trial topology on CPU,
so it is not intended for high-resolution scan editing. The nominal spacing
is defined for the generated unit sphere, not arbitrary imported meshes.

## Measured 5,000-vertex regression (2026-10-15)

Linux CI runs the reproducible `--compare-remesh` experiment with the default
5,000-vertex unit sphere, two radius-0.4 mirrored Inflate influences, 105
stamps, default minimum-edge ratio 0.65 and up to 12 symmetric collapse
operations per stamp:

| Measurement | No collapse | Directional collapse |
| --- | ---: | ---: |
| First shrinking edge below threshold | 11 | 11 |
| First radially inverted face | 98 | 105 |
| Final inverted faces | 22 | 8 |
| Final vertices | 5,000 | 4,898 |
| Minimum edge length | 0.0176079 | 0.0305529 |
| Minimum triangle altitude | 0.00323476 | 0.0117284 |

There is improvement, **not a guarantee of fold-free geometry**. The current
conservative constraints preserve closed, oriented mesh connectivity but do
not detect geometric self-intersections or forbid already existing folds.
The observations support investigating compression-triggered refinement and
collapse before adding curvature-aware movement or collision constraints.
