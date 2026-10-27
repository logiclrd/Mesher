# Mesher

Mesher is a **C++20 / Vulkan / GLFW** experimental 3D mesh sculptor
with an immutable, versioned, partitioned geometry engine. The
interactive desktop offers a normal Inflate sculpt mode and a
**separately authorized, default-OFF, source-certified Merge mode**
for a deliberately bounded set of proven original-source contact
geometries. It is not yet a general-purpose Boolean mesh editor.

**Current state (2026-10-21):** finite supported-domain V17 Merge
and Linux/Windows physical GPU acceptance are complete.
[V18](todo.md) now offers separately authorized
**OFF-mode collision avoidance** with `--avoid-collisions`,
backed by M01–M05 finite worker admission and backtracking.
Its independent integrated Linux/Windows GPU acceptance
(M07) remains pending; this is not protected Merge or swept CCD.
Complex imported meshes and large-device/performance validation
remain V19. For exact completion status see the
[active roadmap](todo.md) and [documentation guide](docs/INDEX.md).
The earlier README's complete prototype-era text is archived
[unchanged here](docs/archive/README_PRE_ROADMAP_CLEANUP.md).

## Certified automatic repair (R06/R07)

The accepted repair constructor uses **advancing-front triangulation (AFM)**
for both ordinary caps and the joined contours of the M23B unified zipper,
followed by the shared fixed-rim graph Poisson solver and M24 shape-based
zipper refinement. The retired ring-and-hub and mirrored-ribbon constructors
and M17D–M21A ring-specific optimizations are **not** production fallbacks.

```sh
# Live AUTO: certified repair is published atomically and Inflate continues
./build/Mesher --repair-auto

# Opt-in frozen, unpublished inspection
./build/Mesher --repair-inspect --repair-auto

# Exact archived-case debugger, with selected unified zipper
./build/Mesher --repair-replay repair-cases/refusal-2.r06case \
  --repair-step-debug --repair-unified-zipper
```

Historical M17D–M21A ring optimizer flags now fail explicitly instead of
silently having no effect. `--repair-scale-aware` remains an independent,
optional downstream refinement policy; the accepted M24 zipper shape rules
do not require it. Saved-case archive bytes remain unchanged; the current
positive tests require certified closures in all five reviewed cases.
See [AFM retirement](docs/R06_AFM_RETIREMENT.md),
[R06 zipper shape rules](docs/R06_M24_ZIPPER_SHAPE_REFINEMENT.md),
and [R07 publication](docs/R07_LIVE_AUTO_PUBLICATION.md).
Broad live GPU self-contact acceptance remains separate from CPU/CI tests.

## Repair architecture rebaseline (developer diagnostic R01)

The [geometry repair rebaseline](docs/GEOMETRY_REPAIR_REBASELINE.md) defines the intended iterative, approximate manifold-preserving Merge pipeline and independent nonpenetrating brush response, superseding *product* assumptions that every local residual intersection must be an exact certified Boolean refusal. Historical V17/V18 implementation and tests remain supported only for their documented domains. The new [R01 diagnostic acceptance protocol](docs/R01_GPU_ACCEPTANCE.md) describes the opt-in `--repair-inspect` read-only first-intersection freeze, red vertices/edges and physical GPU verification. No automatic repair is enabled by R01; see `todo.md` for the flat R01–R08 plan.

The [R05 forced private cap inspection](docs/R05_FORCED_CAP_ACCEPTANCE.md) runs with `--repair cap`: independently closes each R03 loop with actual green faces and bounded neighbor-aware subdivision in a frozen unpublished private mesh, never using a zipper.

The [R04 forced private zipper](docs/R04_FORCED_ZIPPER_ACCEPTANCE.md) is enabled by `--repair zipper`: it stitches compatible R03 hole loops with actual blue new faces and bounded edge refinement, then freezes on an unpublished private closed mesh. It never silently caps unmatched loops.

The [R03 private open-hole excision inspection](docs/R03_PRIVATE_EXCISION_ACCEPTANCE.md) can be enabled with `--repair-excise`; it actually removes the selected face fans and vertices in a **private, frozen, unpublished mesh** so the holes can be viewed without allowing normal editing of a non-closed mesh.

The [R02 read-only excision preview](docs/R02_EXCISION_PREVIEW_ACCEPTANCE.md) can be enabled with `--repair-preview` to show **conservative contact-vertex removal, complete incident-face fans, and oriented cut boundary loops** without actually changing mesh topology. The existing R01 detector remains available with `--repair-inspect`.

The [G01 topology-test geometry fixtures](docs/G01_GEOMETRY_FIXTURES.md) provide `--torus` and `--hollow-sphere`, with tunable `--vertices`, `--seed`, `--relax` and optional R01 `--repair-inspect`. These deliberately test topology without enabling automatic repair.

## Building and testing

Requires C++20, CMake 3.22+, Vulkan SDK including a shader compiler,
GLFW 3.3+, and a suitable graphics driver. Qhull is recommended/needed
for large sphere generation. Supported CI compilers include Linux
GCC, Linux Clang and Windows Visual Studio MSVC.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher
```

For CPU-only use, without Vulkan or GLFW:

```sh
cmake -S . -B build-cpu -DMESHER_BUILD_VIEWPORT=OFF
cmake --build build-cpu --parallel
ctest --test-dir build-cpu --output-on-failure
```

Windows Visual Studio builds use the `Release` configuration and
typically produce `build/Release/Mesher.exe`.

## Interactive mode and controls

Ordinary launch creates an editable triangulated sphere
(default 5,000 vertices, symmetric) and enters Inflate mode;
the protected Boolean Merge operation is **not** provisioned.

```text
Mesher
Mesher --asymmetric --vertices 5000 --seed 1
Mesher --no-collapse --no-curvature-correction --no-refine
Mesher --checkpoint session.mshr
Mesher --recover session.mshr
Mesher --avoid-collisions
Mesher --avoid-collisions --recover session.mshr
Mesher --avoid-collisions --asymmetric --avoid-test-sheets --no-refine --no-collapse --no-curvature-correction
```

With `--avoid-collisions`, ordinary Inflate retains a session-only
collision-avoidance policy: risky normal movement may be attenuated
or refused before publication. The title shows `collision avoidance
ON (Merge OFF)`; `[collision]` and `[sculpt]` report
refusals and trial work. This mode is explicitly incompatible with
`--merge-geometry`, `--demo`, `--no-inflate` and comparison modes.
**Recovery never silently re-enables the policy**; repeat the flag
on `--recover` to opt in again. The current policy conservatively
refuses unproven geometry and is not swept-volume CCD.

The `--avoid-test-sheets` flag is an **M07 developer-only visible
acceptance fixture**, usable solely with `--avoid-collisions
--asymmetric`. It displays two closely spaced independent
opposing faces plus a remote third face. The documented
[M07 Linux/Windows GPU protocol](docs/V18_M07_INTEGRATED_ACCEPTANCE.md)
uses it to observe normal attenuation, refusal, continued editing,
undo/redo and checkpoint recovery; it is not the V17 Merge test fixture.

Directional collapse, local refinement and curvature correction
are ON by default; `--no-*` options disable them independently.
The old positive `--collapse`, `--curvature-correction` and
`--refine` options are no longer accepted.

The mouse sculpts with ordinary LMB. RMB orbits with a Z-up
turntable; Ctrl+LMB grabs/pans the world; Ctrl+RMB dollies along
the viewing direction. W controls surface-edge and grid ink;
G independently shows/hides the floor. Ctrl+Z / Ctrl+Y
undo/redo, Ctrl+S queues an asynchronous checkpoint.

The **protected** source-positive Merge examples are:

```text
Mesher --merge-geometry --asymmetric --merge-test-tetra
Mesher --merge-geometry --symmetric --merge-test-reflected-tetra
```

Even when `--merge-geometry` is explicitly authorized, visible
Merge selection starts OFF. M toggles selection while idle,
LMB picks immutable first-original FaceIds, A nominates all
original faces on these **named test fixtures only**,
Enter queues source-certified Merge and Backspace clears.
Incomplete, unsupported, stale, ambiguous or over-budget
sources refuse without a partial new root. Protected
`--merge-geometry --recover` does **not** reconstruct
unserialized M29 source credentials from a disk checkpoint
and intentionally refuses. Do not confuse this with ordinary
checkpoint recovery, which is supported.

## Architecture, guarantees and limitations

- An accepted edit publishes one immutable original-root
  transaction; retained older revisions, stable IDs, permanent
  tombstones, COW partitions, renderer dirty owners and bounded
  undo/redo remain consistent.
- V17 uses FIRST-original ownership/provenance, complete bounded
  source-contact discovery, positive signed winding and explicit
  protected source/cavity preservation; coincident XYZ is not
  identity. Source-positive joins have independent manifold and
  residual-contact audits. Unproven geometry refuses.
- Post-CAS exceptional auxiliary failures are **terminal
  fail-stop**, not fictional rollback; ordinary failed
  asynchronous checkpoint writes are separately recoverable.
- V18's explicitly selected OFF-mode avoidance is finite and
  conservative, not exact swept CCD or an automatic imported
  mesh repair. M07 physical GPU acceptance remains pending;
  V19 owns broad robustness, imported meshes,
  high-density workloads and benchmarks.
- No universal Boolean, general two-sided Hausdorff,
  peak-process-memory bound or hard real-time guarantee is
  implied by the accepted finite-domain results.

Start with [docs/INDEX.md](docs/INDEX.md) for the source
contract, accepted V17 evidence, active V18 work and the
unaltered original historical documentation.
