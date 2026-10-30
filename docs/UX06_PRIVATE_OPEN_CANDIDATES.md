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
- Unsupported OBJ material and non-MRGB metadata-colour variants,
  textured/normal-indexed faces, polygons, byte-range inline colours and
  mixed inline RGB(A) plus MRGB are **explicitly refused**, not silently
  converted. (Supported STL encodings and facet-colour conversion are
  documented separately below.)
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

Remaining UX06 work: optional advanced PLY variants, OBJ conventions
beyond inline RGB(A) and ZBrush MRGB, complex polygon/texture conventions,
and interactive Windows/macOS/Linux replacement testing (including GPU
error behavior, camera/floor framing, symmetry and multiple consecutive
Opens). UX06 remains unchecked until complete acceptance.


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
- ASCII STL is colourless. Unknown *encoding conventions* are refused;
  a recognised dialect with conflicting colours is **not** refused.

Mesher has per-vertex RGBA, whereas STL colours are per facet. UX06D
supersedes the original strict colour matching rule: conflicting facet
colours are now converted approximately using three benchmarked candidates.
They share the same welded mesh, never split logical vertices, and keep
geometry validation strict. Missing facet colours in a partly coloured
VisCAM mesh use explicitly documented opaque white for those facets.
An entirely colourless STL still produces an empty colour stream.

Following comparison of the three methods on the regenerated castle STL,
**diffusion is the production default** for facet-to-vertex approximation.
The image-based visual test favoured its detail retention over averaging and
its reduced noise over stochastic selection; it is not guaranteed to be optimal
for every mesh. Specify `--stl-colour-method average` or `stochastic` for
explicit comparison, or `--stl-colour-method diffusion` to select the default
explicitly. `--stl-colour-seed N` controls repeatability. A normal coloured
STL Open runs and profiles only the selected method (not all three); the
dedicated comparison benchmark still measures all three. See
[UX06D design](UX06_STL_COLOUR_COMPARISON.md). The current *vertex* attribute
model is unchanged: lossless per-facet discontinuities would require separate
per-face/per-corner attributes. Magics RGBA is preserved as editable vertex
RGBA where supported, but actual translucency is a separate renderer feature.

Bounds: the existing 256 MiB file limit, plus finite 2,000,000 facets
and 1,000,000 welded vertices. Source geometry/units and the previous
live worker remain unchanged until a fully validated candidate commits.
Checkpoint round trips preserve successfully converted vertex colours.

Dedicated stl_import CTest covers both encodings, misleading extensions,
both colour conventions, closed-fan welding and explicit refusal cases.
UX06 remains partly open until physical Ctrl+O testing and the remaining
advanced format/convention requirements have been accepted.


### Legacy castle fixture: degenerate latitude-pole construction

The originally checked-in `test-files/castle.stl` came from a version of
`tools/img_to_stl.py` which emitted ordinary longitude rings at phi=0 and
phi=pi. All north-ring vertices share the pole position, so the north cap
and first main-band triangles collapse after float32 welding. The
main-band triangle winding was also opposite to the two pole fans.
MeshLab can display the remaining surface after tolerating such facets;
Mesher's exact manifold validation properly refuses the original stream.

The generator now creates strictly interior latitude rings with a single
north and south pole, and outward-facing ACB and BCD main triangles.
Before writing, it validates the actual float32 vertex positions,
nonzero facet area, outward orientation and two opposite uses per edge.
A positive CTest runs this generator with a synthetic colour image without
requiring Pillow, and imports its resulting STL through the C++ importer.

Regenerate the legacy checked-in binary fixture from its source image:

    python3 tools/img_to_stl.py -i tools/castle-192.png -o test-files/castle.stl

The generator now uses Mesher's standard-library PNG reader for PNG inputs
(Pillow is required only for other image formats). CTest also regenerates
the actual castle image into a temporary STL and imports it through the
normal C++ candidate path. Until the checked-in binary is replaced,
the old castle binary still contains malformed pole geometry and remains
a failure reproducer, not a valid STL acceptance fixture.

## UX06E — ZBrush OBJ `#MRGB` vertex colours

The OBJ importer now recognizes two common nonstandard per-vertex colour
encodings: normalized inline `v x y z r g b [a]` and ZBrush `#MRGB`
comment records. Each ZBrush record encodes up to **64** sequential
eight-hex-digit `MMRRGGBB` words. The first byte is the ZBrush editor
**mask**; it is neither colour nor transparency and Mesher intentionally
does **not** import it. The remaining bytes are RGB scaled from [0,255]
to [0,1], with editable Mesher alpha fixed at **1.0** and no separate
source-alpha metadata. No mask values affect the rendered appearance.

A ZBrush export can contain multiple `#MRGB` lines; the words are assigned
to vertices in their original `v`-record order, irrespective of where the
metadata lines occur in the OBJ. Packed words and whitespace-separated
complete words are accepted. There must be **exactly one colour word for
each vertex**. The importer rejects empty/malformed markers, partial or
nonhex words, overlong records, and missing/surplus entries. It also
rejects ANY mixture of inline vertex colours and MRGB, even if identical:
the source encoding would otherwise be ambiguous. Other metadata colour
dialects and material colours remain explicitly unsupported.

Positive CTests import ZBrush-style tetrahedra, prove that varying mask
bytes do not affect RGB or alpha, check before/after-geometry metadata,
a multi-line 68-vertex colour block, and durable checkpoint preservation.
Negative tests require clear diagnostics for incomplete/ambiguous records
without altering an earlier private candidate. All existing atomic Ctrl+O
replacement and topology rules remain unchanged.

### ZBrush 2025.0.1 OBJ termination compatibility

A real ZBrush 2025.0.1 export in `test-files/Duck-ZBrush.obj` uses
19,247 `v` records, 38,490 `f` records and 301 correctly packed
`#MRGB` lines (300 full 64-word lines plus a 47-word line). It appends
one literal NUL byte (0x00) **after** its last blank line. Earlier Mesher
rejected that terminal byte as an unsupported OBJ record on line 58,049.

The OBJ reader now permits trailing NUL bytes **only at end of file**,
including immediately after the last valid record. Embedded NUL bytes
or NUL padding followed by further source records are still rejected;
the file is never silently truncated at an internal NUL. This is a
file-termination compatibility detail, not a new OBJ field or colour
interpretation.

CTest `obj_zbrush_mrgb_2025_export` opens both checked-in
`test-files/Duck-ZBrush.obj` and `test-files/Duck-xyzrgb.obj` through
the real private Ctrl+O candidate parser. Both must retain the same
19,247 vertices, 38,490 faces, and opaque vertex RGB, and their complete
RGB-byte histograms must agree. Source-record comparison confirms that
their RGB bytes match in original vertex order as well. Additional small
positive and negative OBJ tests distinguish valid terminal NUL padding
from malformed NUL bytes embedded within source records.

ZBrush `MM` remains an editor mask, wholly ignored on import. Neither
the terminal NUL nor that mask is interpreted as vertex alpha.


## UX06F/UX06G — Default supported Open filter and CWD dialogs

The Ctrl+O file picker now defaults to **All supported files
(`*.mshr, *.obj, *.ply, *.stl`)**, showing only these extensions. Its
selection is interpreted exactly like unrestricted **All Files**: detect
format from file contents, not the apparent filename or the filter label.
Each individual format filter remains available and uses its explicit
parser; the unrestricted All Files filter also remains available. The
Linux Qt, Windows Shell COM and macOS Cocoa pickers all implement the
same initial filter and content-based semantics.

Every Ctrl+O and Ctrl+S file dialog starts in the process's **current
working directory**. The last suggested checkpoint filename is used only
as a basename, and must never silently choose the starting directory;
Windows Shell and Cocoa remembered-folder defaults are also overridden.
After the user accepts a file selection (not on Cancel), the picker
resolves the returned name to an **absolute, normalized filename before**
changing process CWD to its parent. That absolute path is passed to the
private Open candidate reader or durable asynchronous checkpoint worker,
so later CWD changes cannot redirect an in-flight operation.

CWD follows the accepted **file-dialog selection**, even if a later
content validation or optional PLY alpha decision refuses the imported
mesh. The existing editor state remains untouched on those refusals.
Cancelled dialogs, error alerts and failed path selection never change
CWD. If an accepted parent folder cannot be entered, that acceptance
reports a file-dialog error instead of returning a path with stale CWD.

Positive regression coverage includes CWD versus suggestion precedence,
normalizing relative paths before CWD change, successive different
directories, cancellation/failed parent transitions, default filtered
Qt picker on virtual X11, and actual accepted Save As/Open selections
through the modal Qt lifecycle. Physical Windows/macOS and multimonitor
checks remain necessary before marking UX06F/G accepted.
