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
leave the old worker/picking/history intact. Imported OBJ changes the
new Save As default to its .mshr sibling; restored checkpoints preserve
their checkpoint destination. The new source owns a fresh undo history,
with no attempt to splice unrelated mesh lineages.

Remaining UX06 work: PLY/STL and additional explicit OBJ colour variants,
more complex polygon/texture conventions, complete interactive Windows/
macOS/Linux replacement testing (including GPU error behavior, camera/floor
framing, symmetry and multiple consecutive Opens), and state-replacement
acceptance tests. UX06 remains unchecked until full acceptance.
