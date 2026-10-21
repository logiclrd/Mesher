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
