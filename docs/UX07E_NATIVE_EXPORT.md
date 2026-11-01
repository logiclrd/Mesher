# UX07E — Ctrl+E native mesh export dialogs

## Entry and source ownership

In normal interactive sculpt/paint mode, Ctrl+E on Linux/Windows (Ctrl+E
or Command+E on macOS) opens a modal Save As picker. It does not activate while
a stroke, drag, repair-frozen inspection or merge selection is active.
The UI captures the **currently displayed accepted**
\`shared_ptr<const RegionTopologyIndex>\` at the instant the user completes
the dialog and any information-loss confirmation. It never captures a
candidate repair preview, future worker state, GPU-only mesh, or mutable view.

The selection is handed to UX07D's \`AsyncMeshExportWriter\`. Source flattening,
serializer and file sync/replacement run off the UI thread. This preserves
the live mesh, checkpoint identity, picking, undo history and painting while
export I/O proceeds. Every accepted export request is handled FIFO.

## Platform pickers and filename policy

- **Linux/X11/XWayland:** The existing Qt widget-backed QFileDialog is
  modal and transient to the GLFW owner, with its geometry positioned on
  the viewport's actual monitor, and uses the existing lazy QApplication
  lifecycle, focus restoration and shutdown discipline. Qt's name filter
  selection is tracked through \`filterSelected\` to avoid unreliable
  \`selectedNameFilter()\` values.
- **Windows:** Native COM IFileSaveDialog with 21 COMDLG_FILTERSPEC entries,
  1-based selected-file-type index, filesystem path, owner HWND, overwrite
  confirmation and strict file-type handling.
- **macOS:** Native NSSavePanel with accessory NSPopUpButton (21 items)
  controlling allowedContentTypes. Format is read from accessory index, not
  inferred from the resulting filename. The panel is modal on the UI thread.

All three start in **process CWD**, using the remembered filename. At first
opening, remove only a suffix matching the original source format (OBJ, PLY,
STL, or native MSHR), then append the remembered export filter's suffix.
A new document uses a generated basename. If no export preference exists,
binary little-endian PLY mesh RGBA is preselected. Each filter change removes
the **previous** filter's extension only when it matches the current text,
then appends the newly selected extension. A different suffix is retained:
OBJ -> PLY changes `model.stl` to `model.stl.ply`.

At acceptance, an unquoted name gets the selected suffix if not already
present (ASCII-case-insensitive). A filename entirely enclosed in double
quotes is an explicit literal override: remove its quotes and do **not**
append the selected extension, even when absent or mismatched. No mismatch
warning is displayed. The selected filter is always the encoder authority,
even for an unconventional filename. Confirm overwrite only against the
**final resolved destination**. Acceptance updates CWD through the UX06G
path helper; cancellation changes neither CWD nor export queue.

The selected dropdown index is the **only** codec authority. A path named
\`.obj\` cannot coerce a different selected PLY/STL serializer, and the
extension never selects an encoding/colour policy.

## 21 explicit entries and disclosure

Four OBJ variants: geometry-only, inline RGB, inline RGBA, ZBrush #MRGB.
Twelve PLY variants: ASCII, little-endian binary, big-endian binary; each
with no colour, RGB, RGBA from current mesh, RGBA from original source alpha.
Five STL variants: ASCII uncoloured, binary uncoloured, VisCAM 5:5:5 facet RGB,
Materialise Magics 5:5:5 facet RGB, Materialise Magics one object RGBA.

Before enqueue, each selected entry shows a **modal confirm/cancel message**
disclosing its nonstandard representation, lost alpha/RGB, 8-bit/5-bit
quantization, or vertex-to-facet averaging. Cancel leaves the editor and
destination untouched. Error messages use a distinct export-failure title.

The initial filter is detected from the *actual successfully parsed*
foreign file, not just its OBJ/PLY/STL family: STL distinguishes ASCII,
uncoloured binary, VisCAM facet RGB, Materialise Magics facet RGB and Magics
single-object RGBA. OBJ preserves geometry/RGB/RGBA/ZBrush MRGB; PLY
preserves ASCII/LE/BE and RGB/alpha presence. Generic RGBA PLY does not
encode whether its alpha came from editable mesh or original file alpha;
Mesher now marks original-source-alpha PLY exports with a comment so
subsequent Mesher imports can reselect that exact mode. Untagged RGBA PLY
chooses editable mesh RGBA for export by default. This is distinct from
the explicit PLY source-alpha opacity decision on import.

The Magics object RGBA policy deliberately averages **all current vertex
RGBA**, producing exactly one global object colour; this loss is explicitly
declared and confirmed. The original source-file alpha is only selected by
the PLY source-alpha entries, which require provenance on every vertex.
There is no silent alpha-source substitution.

## Background completion and failures

The application polls UX07D writer statistics from the UI loop. Successful
exports are logged to stdout. A failed asynchronous export is displayed
once in a native critical message box, and cannot be reported as successful.
If several failures complete between UI polls, the latest diagnostic and
count are displayed. The writer drains all queued requests on shutdown.

## Tests and acceptance

The headless \`mesh_export_native_dialog_codec_policy\` CTest exercises all
21 mappings through actual OBJ/PLY/STL serializers and existing importers,
checks RGB/alpha options and Magics object RGBA bytes, validates suffix
normalization and refuses unrelated suffixes. UX07A/B/C and UX07D tests
must remain green on five CI configurations.

Physical manual smoke tests are **mandatory** on Linux/X11, Windows, and
macOS: format dropdown contents and correct extension, CWD initialization
and navigation, Cancel and format-warning Cancel, overwrite confirmation
preserving existing output, correct surface/colour after reimport, modals
centred on non-primary monitors, no X11 QApplication teardown crash, and
background errors appearing without changing editor state. In particular,
macOS and Windows native panels cannot be fully verified by headless CTest.
