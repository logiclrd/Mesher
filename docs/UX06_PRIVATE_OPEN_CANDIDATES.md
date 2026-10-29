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
- OBJ material and metadata-colour variants, textured/normal-indexed faces,
  polygons, byte-range colours, mixed coloured/uncoloured vertices, and
  `.ply`/`.stl` are **explicitly refused**, not silently converted.
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
`Wavefront OBJ (*.obj)` invokes the OBJ parser; `All Files` detects
`MESHRV12` magic or plausible OBJ record headers before invoking a
strict parser. `MSHRCAS1` belongs to a distinct R06 replay archive
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

Remaining UX06 work: PLY/STL and additional explicit OBJ colour variants,
more complex polygon/texture conventions, complete interactive Windows/
macOS/Linux replacement testing (including GPU error behavior, camera/floor
framing, symmetry and multiple consecutive Opens), and state-replacement
acceptance tests. UX06 remains unchecked until full acceptance.
