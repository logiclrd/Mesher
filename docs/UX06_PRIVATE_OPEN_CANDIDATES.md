# UX06 — Private mesh-open candidates (first implementation slice)

The first UX06 change provides `readMeshOpenCandidate(path)`: a **private**
candidate that cannot update the live worker, renderer, mouse picking,
undo/redo, current checkpoint path, or file picker. Construction failure
leaves the entire current editing session untouched.

- `.mshr`: reuse the existing checksummed V12–V15 checkpoint reader with
  complete stable IDs, retired IDs, vertex RGBA and reflection identities.
  Fidelity-required metadata does **not** authenticate or synthesize
  original-source lineage.
- `.obj`: strict, consistently wound closed triangle meshes with bare
  vertex indices (positive or relative negative). Accept bare XYZ,
  normalized inline XYZ RGB, or XYZ RGBA vertex records. Default unpainted
  XYZ to opaque white; preserve explicit RGBA on the indexed candidate.
- `.ply`: ASCII and binary little-/big-endian PLY 1.0, closed,
  consistently wound triangle meshes with vertex `x/y/z` plus optional
  `uchar red/green/blue[/alpha]` properties. Channel values are divided
  by 255 into Mesher's linear, unpremultiplied RGBA representation;
  missing alpha defaults to opaque. Scalar normals/other unrelated vertex
  properties may be skipped, but alternate/incomplete vertex colours,
  facet colours, extra elements, nontriangular polygons, unknown list
  properties and unsupported colour types are **explicitly refused**
  to avoid silently dropping colour information.
- OBJ material and metadata-colour variants, textured/normal-indexed faces,
  polygons, byte-range colours, mixed coloured/uncoloured vertices, and
  `.stl` are **explicitly refused**, not silently converted.
- Bounded file size, line counts, vertex/face counts, finite coordinates,
  triangle quality, and exact undirected/directed manifold edge checks.

The initial **Ctrl+O** path now selects a supported file through the
native OS picker (Shell, Cocoa, kdialog or zenity), prepares its immutable
candidate and replacement worker, and rebinds all old/new render partitions
before changing the live worker. Cancellation and validation failures
leave the old worker/picking/history intact, and failures show a native
error dialog rather than appearing only in the console.

The explicit Open filter is authoritative **regardless of filename**:
`Mesher mesh files (*.mshr)` invokes the checksummed checkpoint parser;
`Wavefront OBJ (*.obj)` invokes the OBJ parser; `Stanford PLY (*.ply)`
invokes the PLY parser. `All Files` detects `MESHRV12` checkpoint magic,
`ply` header, or plausible OBJ records before invoking the corresponding
strict parser. None of these parsers trust the filename extension. `MSHRCAS1` belongs to a distinct R06 replay archive
(`.r06case`) and is explicitly identified and refused for interactive
mesh editing, with `--repair-replay` guidance. Unknown headers or
corrupt contents are refused; there is no extension-based fallback.

Windows and macOS carry the actual filter selection from the Open panel;
macOS uses a native accessory to update the visible filters. Linux now
uses Qt 6 `QFileDialog` for **both Open and Save As**, preserving one
in-dialog selector and returning `selectedNameFilter()` directly.
A `QApplication` is initialized lazily on the first modal dialog and
kept for the process lifetime. Qt respects any existing platform theme
setting without forcibly overriding it; the deployed Linux binary
requires system Qt Widgets and QPA plugins. `QMessageBox` is used for
native-style Open errors; no external kdialog/zenity subprocess remains.
Qt's native helpers may hide the final wildcard from their displayed
label, so the filter description repeats it to retain the visible
`Mesher mesh files (*.mshr)` and `Wavefront OBJ (*.obj)` labels.

On X11/XWayland, `QWindow::fromWinId(glfwGetX11Window(...))` creates
a non-owning transient-parent proxy for the GLFW window. Open, Save As
and error messages use `setTransientParent()` so they are related
top-level windows, not embedded children. The native QPA file picker may ignore all
QFileDialog widget geometry and create its own window on the primary
monitor. Therefore Linux Open and Save As deliberately use the Qt
widget-based QFileDialog (with system Qt style) so Mesher controls the
actual top-level geometry. Qt selects the screen containing the GLFW
window centre and places the dialog within its available area, then
reapplies the position just after the modal loop starts, in case the
window manager initially overrides placement.
Focus returns to GLFW after each modal dialog. QPA backends without
foreign windows (e.g. native Wayland) retain the previous unparented
fallback.

The Xvfb-backed lifetime regression now also checks that repeated
widget file dialogs are positioned near an off-centre GLFW owner,
rather than centered on the display.

The lazy `QApplication` lifetime is explicitly ended by the viewport
destructor **before** `glfwDestroyWindow` and `glfwTerminate`. The prior
static function-local QApplication was destroyed during C++ process exit,
after GLFW's X11 teardown, which is a plausible cause of the reported
exit segfault. No repeated QApplication initialization is supported;
the Xvfb-backed `linux_qt_dialog_lifecycle` regression now exercises
multiple Open, Save and error dialogs, then shuts down Qt before GLFW
and exits normally. It does not replace local multi-monitor testing.

Imported OBJ changes the
new Save As default to its .mshr sibling; restored checkpoints preserve
their checkpoint destination. The new source owns a fresh undo history,
with no attempt to splice unrelated mesh lineages.

Remaining UX06 work: STL, advanced PLY variants, and additional explicit OBJ colour variants,
more complex polygon/texture conventions, complete interactive Windows/
macOS/Linux replacement testing (including GPU error behavior, camera/floor
framing, symmetry and multiple consecutive Opens), and state-replacement
acceptance tests. UX06 remains unchecked until full acceptance.


## UX06B PLY import verification

- Existing `mesher_mesh_tests` runner covers ASCII, little-endian and
  big-endian PLY payloads, normalized vertex RGBA and alpha, colourless
  default-white, `.mshr` checkpoint round-trip, and All Files detection
  with intentionally misleading extensions.
- Tests refuse malformed headers, incomplete colour channels, unsupported
  component types, facet-colour data, nontriangular faces, invalid indices
  and truncated binary payloads, leaving earlier indexed candidates intact.
- Additional PLY element/property variants are not silently interpreted.
  The current PLY importer intentionally handles precisely closed
  triangle meshes with standard uchar vertex RGB(A).


## PLY alpha ambiguity (UX06B follow-up)

PLY `uchar alpha` is retained independently as `VertexRGBA::sourceAlpha`
(an optional, normalized, full-precision scalar), even if the user does
not treat it as opacity. RGB is visible with opaque editable alpha by
default. A strictly sub-1.5% span of source bytes is automatically treated
as non-opacity; larger spans prompt **Yes** (copy source into editable
alpha), **No** (keep editable alpha opaque) or **Cancel** (retain the live
session). All choices preserve the source stream. Paint modifies mesh RGBA,
not the source scalar. Source alpha interpolates across edge splits,
collision cuts and advanced cap interiors and survives durable V16/V17
checkpoints (legacy V12–V15 remain readable). UX07 future PLY export will
automatically use mesh alpha if the original source values were absent or
identical; the per-vertex sourceAlphaOriginallyVaried bit persists that
original decision even when edited geometry no longer spans the range. Otherwise the operator will choose From File or From Mesh. This
export chooser is **not** implemented by UX06B.

Mesher's current fragment shader uses vertex alpha for paint-coat coverage
on an opaque surface; true physically transparent rendering is a separate
future renderer requirement. Do not claim that opting into opacity has
introduced translucent geometry in this PR.


## UX06C — STL import (draft PR #179)

STL uses the existing atomic private Ctrl+O candidate pipeline. A selected
STL filter invokes that parser regardless of filename extension; All Files
recognizes ASCII solid headers and exact-size binary STL (80-byte header,
4-byte little-endian facet count, 50 bytes per facet). A binary header may
itself start with solid: the exact binary layout takes precedence.

Facet coordinates weld only on EXACT XYZ equality. Normals are advisory;
the imported indexed mesh must be closed, consistently wound, nondegenerate,
have exactly two oriented uses of each edge and one connected fan per
welded vertex. Near-coincident points do not merge implicitly: tolerance
welding could silently join unrelated sheets. Point-touch disconnected
fans, open or nonmanifold surfaces and damaged input refuse before Open.

Binary STL facet-colour extensions are interpreted explicitly:
- VisCAM/SolidView: without the Magics header, attribute bit 15 set marks
  BGR555 (R high, B low). Attribute zero is uncoloured. Other nonzero
  attributes without bit 15 are refused as ambiguous.
- Materialise Magics: an 80-byte header containing COLOR= and four binary
  RGBA bytes selects the dialect. Bit 15 set uses the object RGBA;
  bit 15 clear uses per-facet BGR555 (R low, B high), even when zero.
  MATERIAL= is refused: Mesher cannot faithfully represent its materials.
- ASCII STL is colourless. Unknown or conflicting facet-colour conventions
  are refused, never ignored or silently converted.

Mesher has per-vertex RGBA, whereas STL colours are per facet. Conversion
is accepted only when all facets incident to each welded vertex assign
identical colours. A per-facet colour seam, missing per-facet colour among
coloured facets or a mixed-colour shared vertex is explicitly refused.
Vertex splitting to preserve a colour seam would compromise the closed
indexed surface. Magics RGBA is preserved as editable vertex RGBA,
but actual surface translucency remains a separate renderer feature.

Bounds: the existing 256 MiB file limit, plus finite 2,000,000 facets
and 1,000,000 welded vertices. Source geometry/units and the previous
live worker remain unchanged until a fully validated candidate commits.
Checkpoint round trips preserve successfully converted vertex colours.

Dedicated stl_import CTest covers both encodings, misleading extensions,
both colour conventions, closed-fan welding and explicit refusal cases.
UX06 remains partly open until physical Ctrl+O testing and the remaining
advanced format/convention requirements have been accepted.
