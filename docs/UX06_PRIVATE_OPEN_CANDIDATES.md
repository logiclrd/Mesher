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

This is **not yet Ctrl+O**. Remaining UX06 work: native multi-format open
dialogs, private worker and viewport rebind with atomic success/failure,
PLY/STL and additional explicit OBJ colour variants, compatibility and
source-provenance policy, and interactive cross-platform testing. UX06
remains unchecked until that complete functionality is accepted.
