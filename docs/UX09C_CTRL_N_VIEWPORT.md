# UX09C — Live Ctrl+N viewport chooser and atomic New

UX09A established document filename provenance and embedded vocabulary;
UX09B added validated private initial model generation and the modal
decision state machine. UX09C binds these to the interactive Vulkan viewport.

## UI operation

- Ctrl+N (Command+N also on macOS) opens a modal overlay drawn by the
  existing HUD. It blocks painting, sculpt input, orbit/strafe/dolly,
  and every normal global shortcut. Escape closes the dialog **without**
  taking the app's ordinary Escape-to-exit action.
- Three cards across the middle select Asymmetric Sphere, Symmetric Sphere
  or Torus. The cards contain triangulated wireframe previews.
- Vertices begins at the **original command-line** `--vertices` (or 5000).
  Click the field to clear it, type digits using the OS keyboard layout,
  use Backspace for corrections; validation checks generator limits.
- A clean document goes directly to private generation and indexed
  replacement. A modified document instead displays Save / Save As /
  No / Cancel, with Save hidden when no native `.mshr` Save target exists.
  S is Save if shown, otherwise Save As; A is Save As if Save is shown;
  N is No; Escape and Cancel never replace the document.

## Save and replacement safeguards

- Dirty means the displayed immutable revision differs from the most
  recently completed native checkpoint (or current initial/Open root).
  Ctrl+E export does **not** change the clean baseline.
- A native checkpoint request is tracked by an exact captured revision,
  document generation and before/after requested/completed/failed counts.
  The UI can enter New only after a successful completed write.
- Normal Ctrl+S directly writes a known native `.mshr` destination;
  otherwise it opens Save As with a suggested `.mshr` filename. A first
  completed export of an untitled model seeds its basename (e.g.
  `bob.ply` -> later `bob.mshr`), without making the export a Save.
- Export completion receipts retain FIFO success/failure and destination,
  so the session's preferred Ctrl+E codec is only updated on success;
  imports select their detected family; native `.mshr` Open and New leave
  that codec preference unchanged.
- If source generation, GPU refresh or Save fails, the current document
  remains the authoritative worker. New validates private meshes before
  the UX06-style prepare/rebind/commit transaction, resets prior input
  and edit state on success, and reframes the generated source bounds.
- Existing in-flight checkpoint/worker edits block the New dialog to avoid
  replacement while a writer or worker command is unresolved.

## Deliberately deferred to UX09D

The New-confirmation Save As currently uses the safe native `.mshr`
checkpoint picker, even after non-`.mshr` imports. UX09D must unify the
file-type dropdown so that the default filter matches the import family
(and an explicit `.mshr` option remains available), and must await the
selected foreign-format export completion before discarding the editor.
Until then no foreign source file is overwritten with checkpoint bytes.

## Physical acceptance checklist

- [ ] On Linux X11, Windows, macOS ARM and Intel: Ctrl+N modal appears
  on the viewport's monitor, Esc/Cancel does not exit or edit.
- [ ] Click each preview, validate actual topology/geometry, paint,
  undo/redo history reset, camera bounds and new document title.
- [ ] Try 128 torus, invalid 127 torus, min sphere and 5000+ vertices.
- [ ] Dirty sculpt and Paint -> Ctrl+N -> No / Cancel / Save / Save As.
- [ ] Force checkpoint write failure; New must keep old mesh and history.
- [ ] Ctrl+E first export then Ctrl+S native save suggests same stem.
- [ ] Open foreign mesh, change it, ensure direct Save is unavailable.
- [ ] Open native checkpoint, confirm direct Save and preserved Ctrl+E codec.
- [ ] Hold Escape after cancellation; Mesher must not exit.
- [ ] While modal, clicking scene, scrolling and orbiting must not edit.

Headless regression: `ux09_new_model_private_candidate_and_modal_policy`,
`ux09_filename_provenance_and_embedded_vocabulary`,
`mesh_export_atomic_snapshot_and_fifo`.
