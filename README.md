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
# Normal mouse Inflate automatically uses certified R07 AUTO.
./build/Mesher

# Opt-in frozen, unpublished AUTO inspection
./build/Mesher --repair-inspect

# Exact archived-case debugger; diagnostic choices remain available
./build/Mesher --repair-replay repair-cases/refusal-2.r06case \
  --repair-step-debug --repair-unified-zipper
```

The former `--repair auto`, `--repair cap`, `--repair zipper`,
`--repair-auto`, `--repair-cap` and `--repair-zipper` selectors
have been removed. The production worker always selects cap or zipper
from the R01/R03 contact and rim evidence; an uncertifiable repair still
refuses the stamp atomically. `--repair-preview` and `--repair-excise`
remain frozen private intermediate-stage diagnostics, not production
fallback modes. Explicit protected Merge and collision avoidance retain
their separate policies.

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

The [historical R05 forced-cap](docs/R05_FORCED_CAP_ACCEPTANCE.md)
and [R04 forced-zipper](docs/R04_FORCED_ZIPPER_ACCEPTANCE.md)
acceptance documents record development-only operations. Their
`--repair cap` and `--repair zipper` switches no longer exist;
the active AUTO planner selects the appropriate certified closure.

The [R03 private open-hole excision inspection](docs/R03_PRIVATE_EXCISION_ACCEPTANCE.md) can be enabled with `--repair-excise`; it actually removes the selected face fans and vertices in a **private, frozen, unpublished mesh** so the holes can be viewed without allowing normal editing of a non-closed mesh.

The [R02 read-only excision preview](docs/R02_EXCISION_PREVIEW_ACCEPTANCE.md) can be enabled with `--repair-preview` to show **conservative contact-vertex removal, complete incident-face fans, and oriented cut boundary loops** without actually changing mesh topology. The R01 contact detector and current frozen AUTO inspector remain
available through `--repair-inspect`.

The [G01 topology-test geometry fixtures](docs/G01_GEOMETRY_FIXTURES.md) provide `--torus` and `--hollow-sphere`, with tunable `--vertices`, `--seed`, `--relax` and optional R01 `--repair-inspect`. Ordinary interactive sculpting on these fixtures also uses AUTO repair by default.

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
typically produce `build/Release/Mesher.exe`. Its Explorer/taskbar file
icon is embedded from `images/Icon.ico` using a Win32 resource; the
running GLFW window icon uses `images/Icon.png` (native Wayland
compositors may override it).

At interactive startup, `images/Splash.png` opens as a separate
borderless, nonmodal splash in front of the main window. It fades in
for 250ms and closes after four seconds, or immediately on any keypress,
mouse click, or loss of splash focus. On X11, placement uses the actual
WM-positioned owner rather than assuming the primary monitor, follows
late initial configure/move events during the first 50ms, and recenters
on subsequent owner fullscreen/size transitions. Native Wayland
compositors control exact top-level positioning. PNG assets are converted
with Python's standard library when building the viewport and copied to
the executable's `images/` directory; no runtime PNG library is needed.

## Versioned desktop releases

The root [`VERSION`](VERSION) file is the release version source of
truth. `Mesher --version` prints the same value without loading Vulkan.
CMake uses the numeric prefix (for example, `0.5` from `0.5b`) for its
project-version metadata, preserving the full identifier in the binary.

Changing `VERSION` on `main` automatically starts
[`Versioned desktop release`](.github/workflows/release.yml).
The workflow **checks the remote for `release-$VERSION` before building**
and refuses a duplicate tag; CI failures also prevent publication.
Only after **all four packages succeed** does it create the tag on the
triggering commit and publish a GitHub prerelease/release with:

- `Mesher-$VERSION-windows-x64.zip` — MSVC Release executable, embedded
  `.ico` resource, required vcpkg GLFW/Qhull DLLs, SPIR-V shaders and UI
  images. A Vulkan-capable graphics driver and VC++ Redistributable are needed.
- `Mesher-$VERSION-linux-x86_64.tar.gz` — optimized Linux Release executable,
  shaders and images, bundled GLFW/Qhull/OpenMP libraries, and
  `compiler-benchmark.json`. Built on Ubuntu 24.04; graphics drivers,
  Vulkan loader, and system libraries remain external.
- `Mesher-$VERSION-macos-arm64.zip` — native Apple Silicon `Mesher.app`.
- `Mesher-$VERSION-macos-x86_64.zip` — native Intel `Mesher.app`.
  Both carry their own Dock icon, MoltenVK, Vulkan loader, dynamic libraries,
  shader binaries and splash artwork; Homebrew is not required on the
  recipient's Mac. macOS 13 or later is specified in the app metadata.
  The two architecture-specific packages are **ad-hoc signed, not
  Developer ID signed or notarized**. macOS may require explicit user
  approval through Finder / Privacy & Security. Real GPU behavior has
  not been validated on end-user Mac hardware.

The Linux packaging job compiles the **same benchmark with both G++ and
Clang++ on one runner** and chooses using five rounds of alternating tests.
The score combines median generation (10%), plain stamping (10%) and
remeshing stamping (80%) after normalization, so the selected compiler is
measured afresh for each release rather than guessed from its name.
Benchmark data is retained inside the Linux archive.

The release workflow also runs on pull requests touching the workflow,
`VERSION`, or associated packaging code. PRs validate the version syntax
and build archives but **cannot publish tags or releases**; the unused-tag
check remains mandatory for real release runs. A manual run can reattempt a failed
release but still refuses any already-published version. To publish a new
version, edit `VERSION` and merge to `main`.

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

Ordinary LMB inflates; **Alt+LMB deflates** along the same
pre-stamp normals at **two-thirds the normal strength**. The direction
is latched on mouse-down and stays fixed when Alt is released mid-drag.
Deflate reverses the sign of curvature compensation as well as normal
motion, so convex peaks shrink faster while concave troughs shrink more
slowly instead of developing unstable ridges and pockets. Both modes
retain the same refinement, collision repair, mirror and undo/redo path. RMB orbits with a Z-up
turntable; Alt+RMB or Ctrl+LMB grabs/pans the world; Ctrl+RMB dollies
along the viewing direction. Ctrl+RMB takes precedence over Alt+RMB.
A camera gesture is latched on mouse-down and does not change when a
modifier is released mid-drag. On some Linux desktop environments,
Alt+mouse gestures may be reserved for window management, including
Alt+LMB; Ctrl+LMB remains an
alternative. W controls surface-edge and grid ink;
G independently shows/hides the floor. Ctrl+Z / Ctrl+Y
undo/redo. **Ctrl+S** opens the operating system's native **Save As**
dialog for a durable `.mshr` checkpoint. On macOS, Command+Z /
Command+Y / Command+S also work. The first suggestion is the
`--checkpoint` filename (default `mesher-checkpoint.mshr`); subsequent
saves suggest the most recently accepted path. Cancel leaves the mesh,
history, and destination unchanged. Saving is queued on the existing
asynchronous worker and uses its atomic temporary-file-and-rename
mechanism; an accepted dialog does not imply that disk I/O has finished.
The writer's checkpoint status/errors remain in the console. On Linux,
**Open, Save As and error messages use Qt 6 Widgets** and the system's
installed KDE/GTK/portal integration, with a file-type selector inside
Open rather than a separate preliminary prompt. Qt is a required desktop
dependency, but no Qt dependency is added to headless mesh tests.
The Linux archive relies on the host's Qt 6 runtime and QPA plugins;
`QT_QPA_PLATFORMTHEME` is respected if configured rather than overwritten.
Frozen repair inspection
retains its specialized Ctrl+S / Ctrl+Shift+S / Ctrl+Alt+S case captures
without opening a regular checkpoint Save As dialog.

Printable keyboard shortcuts (W, G, M, A, Z, Y, S, R, N, P and C)
follow the active operating-system keyboard layout rather than physical
US/QWERTY key positions. For example, to use **A** on an AZERTY layout,
press the key which produces A, regardless of which key occupies that
position on a US keyboard. Non-printable controls (Escape, F8, arrows,
Enter and modifiers) retain their standard meanings. Layout changes and
window focus loss clear or release held key state safely; non-Latin text
entry is not part of the viewport key-binding system.

**B** toggles the RGBA **Paint brush** while sculpting is idle.
Paint mode exposes a native Vulkan HUD colour wheel, a RGB / HSV / HSL mode
selector with three linked channel sliders, and independent alpha and
hardness sliders. Click a mode tab to switch the slider interpretation;
the stored colour does not change just because its representation changes.
Drag on the wheel to choose hue and saturation or drag the sliders to tune
values. Left-dragging on the mesh paints visible vertices within the
brush radius; the existing radius slider works in BOTH Paint and sculpt modes.
The soft edge has a linear radial falloff after the hardness plateau; the
picked triangle's vertices receive full centre strength so interpolation
at the cursor is saturated, and mirrored dabs without a picked FaceId
use the nearest visible surface samples for the same correction.
Colour dabs are applied on the background sculpt worker as immutable
revisions, and a complete mouse drag creates ONE Ctrl+Z/redo history action
without delaying intermediate visual updates. Save As preserves painted colours.
Brush alpha is the deposited paint's opacity: a low-alpha colour
is composited **over the current vertex colour**, rather than replacing
the vertex's alpha. Repeated low-alpha strokes can build coverage;
zero-alpha dabs are no-ops. Vertex RGBA remains linear and unpremultiplied,
and the rendered surface remains opaque.
Visibility rays normally originate above the surface by at least one-quarter
of the nominal mesh edge length and rise with brush radius to avoid
convex-surface horizon clipping. A source-side clearance ray caps that
lift before any intervening surface, even below the quarter-edge offset
when a narrow fold or opposing torus wall would otherwise be crossed.
Visible targets must also lie behind the lifted origin along the dab's
outward normal: an opposing torus wall is not painted merely because its
front faces can see the brush from beyond that origin.
The brush footprint itself is centred on the surface hit with the exact
selected radius; existing faces still occlude other/hidden surfaces, and
back-facing vertices are rejected. Mirrored mode paints both symmetric
sides without double-opacity from overlapping mirror dabs.
While Paint mode is idle, a subtle flat #2000C000 (AARRGGBB) overlay
highlights depth-visible faces incident to vertices within the current
brush's actual shadow-limited area. It follows the cursor and radius,
ignores hardness and alpha, and disappears during strokes, orbiting,
over UI controls, and outside Paint mode. It never enters vertex colours,
immutable roots, history or checkpoint files.
Legacy V12/V13 unpainted checkpoints remain compatible; coloured saves
use checksummed V14/V15 records.

**F8** performs exactly one diagnostic sculpt stamp at the surface under
the cursor. Unlike briefly clicking LMB, F8 never opens a timed 4D stroke
or queues catch-up steps. Each request waits for the previous stamp's
published renderer snapshot; press F8 again to see the next state. In
mirrored mode, each F8 stamp still contains the complete reflected brush
pair as one atomic step. Use it when examining post-zipper seam evolution.
A refused stamp counts as one attempt, leaves the mesh unchanged and
does not cause an automatic retry.

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
