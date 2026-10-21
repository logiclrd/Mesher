# V17-M52 — User-observed physical viewport progress (partial)

**Report date:** 2026-10-20. These are observations supplied by
the user after the M64–M69 near-pole AA wire redesign merged to
`main`. The report itself does **not** provide the executable's
exact SHA, OS build, graphics adapter or Vulkan driver. The
repository's current `main` at the time of recording is
`6c6748bd093e0859e7e9dba1d17ca1165098e8e1`; this is the
EXPECTED code line, **not** an independently identified local
running binary. Earlier screenshots were provided from a Linux
desktop, but the platform details of this report were not
repeated explicitly.

## Positively reported physical observations

- **AA wire thickness:** Lines are a little thicker than the user
  imagined, but have become very consistent. The user reports the
  camera view angle has no effect on their appearance. This is
  the previously missing human visual feedback on the M64
  depth-slope/pole discontinuity, and supports the surface-attached
  shading change. No GPU-level measured coverage or screenshot
  comparison on the new build was provided.
- **Camera controls:** The new camera movement controls behave
  exactly as expected (world-grab Ctrl+LMB, Ctrl+RMB dolly, upright
  orbit; precise individual gestures were not separately logged).
- **W visibility:** W behaves correctly from both sides of the
  surface. Double-sided interior inspection remains usable. This
  does not, by itself, certify every occlusion/silhouette case.
- **Explicit source-positive Merge fixtures:** Both asymmetric
  and reflected symmetric fixture interactions behave as the user
  expects. The report does not explicitly enumerate face counts,
  accepted/refused diagnostic messages, source identity proofs,
  one-Step root transitions or recovered saved sessions.

These are **observations**, not automatic checks, and should not be
silently elevated to broader support for arbitrary imported meshes.

## Still needed to finish the original M52 checkbox

Record the OS/GPU/Vulkan driver, exact executable SHA and launch
commands for reproducing these results, and obtain Windows real
Vulkan host observations. Explicitly test the normal default-OFF
session, correctly provisioned ON source nomination/selection and
FaceId picking; both positive fixture joins with actual accepted
worker diagnostics and one-Step Ctrl+Z/Ctrl+Y root/history transitions;
one incomplete/invalid selection refusal with unchanged root/history;
checkpoint write and successful recovery of an ordinary session;
V13-source refusal of unsupported armed recovery; and a deliberately
failed checkpoint-save diagnostic. Confirm G/W independence, an
interior view, hidden/rear edge occlusion and positive/negative pole
inspection in representative views. Preserve all M15, M52 and
resource/worker safety gates.

**No new acceptance of a thinner nominal stroke** is implied:
the user praised consistent thickness but noted a slight preference
for thinner lines. If requested, tune ink as a separate, bounded
visual change without bringing back detached quads or forward
clip-Z bias. Deferred M66 backface-culling option is unrelated.

## Additional undo/redo and reflection refusal observation — 2026-10-20

The user reports experimenting with various **Ctrl+Z/Ctrl+Y**
sequences and redoing selection/processing after an undo.
All outcomes matched their expectations. This is actual
GUI history/branching evidence, without an independently
captured immutable-root identity or precise depth record.

The user then restarted to a fresh initial state, enabled
Merge (M), **did not use fixture A**, clicked a subset of
intersecting faces, and submitted with Enter. The following
console output was provided verbatim:

```text
[merge] mode=ON (M toggles, LMB selects FaceIds, fixture A nominates, Enter submits, Backspace clears)
[merge] select immutable FaceId 8 total=1
[merge] select immutable FaceId 10 total=2
[merge] select immutable FaceId 1 total=3
[merge] select immutable FaceId 14 total=4
[merge] select immutable FaceId 13 total=5
[merge] queued 5 explicitly nominated first-original FaceIds
[merge] A 0 R 0 P 1
[merge] A 0 R 1 P 0 REFUSED Queued first-original exact terminal policy refused: Caller original intent is not closed under source-proven reflection
```

The event confirms five distinct source-original nominations,
a queued request and one completed worker refusal rather than
an accepted union. This particular refusal enforces closure
under source-proven reflection; it is not proof of every
other incomplete, noncontact, stale or budget refusal.
The accepted/rejected/pending counters after processing
are 0/1/0. The user has not independently reported
before/after root identity, explicit unchanged history
depth, or screenshot of the rejected geometry, so those
specific assertions remain open.

Remaining M52 acceptance includes exact executable SHA and
OS/GPU/Vulkan driver provenance, physical Windows GPU,
checkpoint save/reopen and failed-save diagnostics,
armed recovery refusal, and exact positive fixture worker
accepted statuses/root identity as needed for full audit.
These observations do not close M52 or original M15.
