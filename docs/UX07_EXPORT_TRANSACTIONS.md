# UX07D — Immutable mesh export snapshot and failure-safe output

## Scope and integration contract

This peer establishes the mesh-export transaction and ordered worker without
adding Ctrl+E or changing the checkpoint/worker/renderer/undo paths. Export
formats are intentionally supplied as a callable
`MeshExportEncoder(std::ostream&,const Mesh&)`. This permits current UX07B
PLY (integrated via `writePlyExportAtomic`) and the independently reviewed
UX07A OBJ / UX07C STL serializers to use exactly one transaction once merged.
No encoder is inferred from a filename or file extension.

### Accepted-root snapshot

* On UI acceptance, call `captureMeshExportRequest(acceptedPath,acceptedIndex)`
  where `acceptedIndex` is the **accepted** immutable
  `RegionTopologyIndex` at that instant, not a mutable intermediate edit
  or a later worker snapshot. This function retains shared ownership and
  makes the destination absolute immediately, before any process-CWD changes.
  Empty filenames or null roots are hard errors.
* `materializeMeshExport` walks the **captured** `MeshRevision` partitions
  once, records all live VertexIds and FaceIds, sorts those IDs for
  deterministic flattening, then uses `reconstructFlatMesh` for whole-root
  integrity verification, exact indexed triangles, double positions,
  editable RGBA and source-alpha provenance. It does **not** assume
  imported order, drop changed vertices or modify persistent identities.
* Later edits, undo/redo, painting, CWD changes and newly accepted
  snapshots cannot alter this request. All heavy flattening, serialization
  and file I/O run on a background worker when the UI uses
  `AsyncMeshExportWriter`. Submission itself only copies the immutable
  request and encoder selection; the editor never waits for output.

### Failure-safe disk transaction

1. Validate the captured request and materialize the complete frozen mesh
   **before** creating any file. Serialize to a uniquely random-named
   temporary file in the **same directory** as the absolute destination.
2. Reserve each temporary filename with exclusive OS file creation
   (POSIX `open(O_EXCL)`; Windows `_wsopen_s(_O_EXCL)`).
   Keep the underlying handle open throughout serialization, with a
   buffered C++ `std::ostream`. No fixed `filename.tmp` collisions and
   no full output-sized memory buffer.
3. Check stream flush, C stdio flush, OS file sync, and OS close. If any
   stage fails (including an exception inside an encoder), destroy only
   this transaction's temporary file and propagate the failure. Existing
   destinations are unchanged.
4. Only after successful close, perform an adjacent
   `std::filesystem::rename(temporary,destination)`. On an OS that refuses
   replacement, report an error and **preserve** the original file. Never
   delete an existing destination as a fallback. A successful rename is
   the publication point.
5. Directory metadata sync is not promised here: file bytes are synced
   before rename, but a sudden power loss may still lose the directory
   rename on filesystems without synced directory metadata. The guarantee
   is atomic visibility/failure safety during ordinary execution, not a
   universal crash-durable filesystem transaction.

### Background writer

The `AsyncMeshExportWriter` owns one thread. Each successfully enqueued
explicit request runs in FIFO order without coalescing, at most 32 pending
requests. `statistics()` reports requested/completed/failed, pending/busy
and the last error; `drain()` is for deterministic testing/shutdown.
Destruction drains remaining accepted requests instead of discarding them.
The UI will display/report these outcomes in UX07E. This peer intentionally
does not change `AsyncCheckpointWriter` or auto-save semantics.

### Regression proof

`mesh_export_atomic_snapshot_and_fifo` covers:

- PLY RGBA source-alpha export through existing serializer and strict
  importer, exact four-vertex/four-face identity and unchanged indexed root.
- Replacement of a preexisting destination, plus an injected encoder
  failure **after** writing a prefix; verify old bytes are byte-for-byte
  intact and no temporary file remains.
- A rename refusal when destination is a directory; verify the directory
  still exists and staging files are cleaned.
- Capture of a relative pathname in one CWD, switch to another CWD, then
  writer completion at the original absolute destination.
- A blocked asynchronous encoder while an independent live mesh revision
  changes, proving the captured old RGB/alpha is what gets exported.
- Two queued explicit exports to the same destination, proving FIFO
  processing and no coalescing, and a background failure reflected in stats.
- Invalid roots, filenames, absent encoders, nonexistent destination parent,
  no output on failed preflight, and multi-megabyte buffered writes.

### Remaining acceptance gates

* Exact-head full Linux GCC, Linux Clang, Windows MSVC and both macOS Clang
  CI, with warnings as errors and dedicated CTest green.
* Physical/native file-dialog acceptance remains UX07E, including visible
  encoding/loss choices, CWD transitions, platform overwrite prompts and
  error reporting.
* Cross-serializer OBJ/PLY/STL full end-to-end acceptance remains UX07F.
