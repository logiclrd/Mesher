# V17-M52 — Accepted Linux/Windows real GLFW/Vulkan desktop observations

**Report initiated:** October 20, 2026 (Linux-only observations
after M64–M69); **completed:** October 20/21 with Windows physical
tests and independently reported Linux/Windows executable and driver
provenance. The original Linux-only report preceded the binary SHA
and OS/GPU details. Its then-current `main` was `6c6748bd`, a
historical code-line expectation and **not** proof of that earlier
running executable. The later independently reported tested Linux
and Windows binaries are both from the exact M73 source revision
`c3123987d6f45e5f0eb946c94b40ac1fc99695fd`. Chronological
partial-stage text is preserved below.


## Final targeted real-GPU acceptance — user reports October 20/21, 2026

**M52 disposition: ACCEPTED at the targeted, source-positive desktop scope.**
Both tested binaries were independently identified as built from Git
`c3123987d6f45e5f0eb946c94b40ac1fc99695fd` (M73, PR #125).
These are user-reported physical UI observations plus separately named
CPU/worker proofs, not a captured GPU trace or universal Boolean claim.
Earlier "partial/still open" descriptions below document the chronological
Linux-only stage and are superseded by this final section.

### Reproducibility and actual host environment

| Detail | Linux | Windows |
| --- | --- | --- |
| OS | Ubuntu-based Linux x86_64, host `visor`, kernel `7.0.0-34-generic` (`#34-Ubuntu SMP PREEMPT_DYNAMIC Wed Sep 2 14:29:37 UTC 2026`); KDE Plasma 6/X11 noted during physical testing | Microsoft Windows 10 Pro, 10.0.19045, build 19045; ordinary accelerated desktop controlled through RustDesk |
| Host executable | `build/Mesher` | `C:\code\Mesher\build\Release\Mesher.exe` |
| SHA-256 | `93ba9936f42528e8b3aaf2abe3e283ae92a7ff7a03a041f50ef2d1f49ed9d955` | `5A19D5C5A3547E532EFB4CF77BC8AABC240E5CCE2CD138B8873B6CC4B00C4A72` |
| Hardware available | AMD Radeon 780M Graphics (RADV PHOENIX), PCI vendor `0x1002`, device `0x15bf`; `llvmpipe` also enumerated as software device | NVIDIA GeForce RTX 3080, PCI vendor `0x10de`, device `0x2206` |
| Vulkan loader instance | 1.4.341 | 1.4.363 |
| Hardware device API | AMD 1.4.335 | NVIDIA 1.3.280 |
| Driver | Mesa RADV `26.0.8-1ubuntu0.3` | NVIDIA `560.94` (`Win32_VideoController` version `32.0.15.6094`, date 2024-08-13) |

Linux `vulkaninfo --summary` also listed `llvmpipe (LLVM 21.1.8,
256 bits)`, driver Mesa 26.0.8, as a CPU Vulkan implementation. The
device inventory itself does **not** independently log Mesher's
`VkPhysicalDevice` selection. Linux loader warnings about unsupported
display-plane/property functions, and Windows warnings about older
`VK_LAYER_NV_optimus`/`VK_LAYER_OBS_HOOK` layer API levels, occurred
during `vulkaninfo`; no corresponding rendering failure was reported.

Representative reproducible commands (`Mesher` means the executable
above; Linux equivalent uses `build/Mesher`):

```text
Mesher
Mesher --merge-geometry --asymmetric --merge-test-tetra
Mesher --merge-geometry --symmetric --merge-test-reflected-tetra
Mesher --checkpoint m52-checkpoints/ordinary.mshr
Mesher --recover m52-checkpoints/ordinary.mshr
Mesher --checkpoint m52-checkpoints/nonexistent-parent/failed.mshr
Mesher --checkpoint m52-checkpoints/protected.mshr --merge-geometry --symmetric --merge-test-reflected-tetra
Mesher --merge-geometry --recover m52-checkpoints/protected.mshr
Mesher --no-collapse --no-curvature-correction --no-refine
```

### Windows user-observed acceptance (W1–W7)

- **W1 PASS — ordinary desktop:** Default 5000-vertex sphere opens and
  renders; ordinary left-button sculpt edits geometry; unarmed M does
  not authorize Merge. W/G independently control mesh/floor ink and
  the solid floor. Oblique viewpoints occlude rear geometry properly,
  including formerly problematic reversed-facing views. An actual
  Windows screenshot of the obliquely viewed wire-shaded sphere was
  provided; no aberrant line thickness/depth ordering was reported.
- **W2 PASS — viewpoint controls and poles:** RMB upright orbit,
  Ctrl+LMB world-grab pan, Ctrl+RMB dolly, interior two-sided view,
  positive/negative 90-degree attitudes and W inside/outside behaved
  correctly with stable line thickness. **Limitation:** RustDesk
  does not propagate synthetic cursor warps to its remote client,
  therefore edge-wrapping on this Windows host is **not directly
  observed**. Prior Linux/X11 `glfwSetCursorPos` wrapping worked;
  this is supporting platform evidence, not a Windows wrap PASS.
- **W3 PASS — supported asymmetric complete original:** M, A, Enter
  selected all eight source-original fixture faces and produced a
  visually joined union with `[merge] A 1 R 0 P 0`. Ctrl+Z and
  Ctrl+Y restored/reapplied correct appearance from several angles.
  No direct console history-depth or root-pointer record was made.
- **W4 PASS — supported independent reflected originals:** M, A,
  Enter applied the operation on both reflected geometric islands,
  yielding `[merge] A 1 R 0 P 0`; inspected multiple angles
  including both poles. Floor did not intersect the original test
  source. Ctrl+Z/Ctrl+Y worked; after undo, reselect-and-remerge
  worked as a fresh history branch.
- **W5 PASS — conservative original intent refusal/recovery:**
  manually selected first-original FaceIds `8, 10, 1, 9`,
  without fixture A. Worker completed with `[merge] A 0 R 1 P 0`
  and exact reason
  `Queued first-original exact terminal policy refused: Caller original intent is not closed under source-proven reflection`.
  Subsequent A, Enter on the same healthy session correctly
  accepted a supported union. Earlier Linux hand-selected set
  `8, 10, 1, 14, 13` independently produced the same kind of
  conservative refusal. GUI history-depth/root-pointer equality
  for this refusal was not separately logged by the user; the
  existing CPU worker/atomicity regressions cover those properties.
- **W6A PASS — ordinary disk checkpoint:** real sculpt edit saved
  `ordinary-windows.mshr` (543,048 bytes); recovery restored
  editable geometry; subsequent sculpting worked.
- **W6B PASS — failed save:** bad parent-directory checkpoint
  requests incremented `checkpoint_failures` in worker diagnostics.
  The session remained operational and editable.
- **W6C PASS — protected recovery refusal:** after saving
  `protected-windows.mshr`, `--merge-geometry --recover`
  produced the expected
  `--merge-geometry refuses V13 disk recovery without sealed first-original M29 authority`.
  This is the **pre-deserialization CLI guard**, not proof of
  serializer-level reconstitution of the opaque M29 credential.
- **W7 PASS — M73 supplemental smoke:** ordinary default-enabled
  sculpting and a separate all-three-disabled run both launched
  and worked; old `--collapse`, `--curvature-correction`,
  `--refine` positive options were rejected. Comparison-mode
  policies, library defaults and checkpoint settings persistence
  are outside the scope of this smoke observation.

No complete console capture was made during the Windows sessions.
The reported worker statuses and save/recovery results are recorded
verbatim or as explicitly indicated above; **do not synthesize
unrecorded counters, device selection, timestamps or source roots**.

### Acceptance synthesis and exact limits

The Linux human reports below also confirm stable pole ink and
occlusion, correct camera/W operation, both Merge fixture results,
GUI Ctrl+Z/Ctrl+Y and branching, a separate reflection-closure
refusal and correct later recovery, an ordinary edited checkpoint
(521,380 bytes) successfully reloaded and edited, two bad saves
(`checkpoints_saved=0 checkpoint_failures=2`), and an independently
saved 1,249-byte protected checkpoint correctly denied Merge-armed
disk reauthorization. These were reported against the source line
whose executable hash is now supplied above; not every historical
Linux screenshot has separately captured binary metadata.

The **separate** M54 independent worker/CPU acceptance proves that
the exact supported asymmetric and reflected named fixture roots
have correctly oriented closed edge- and vertex-manifold results,
zero residual nonadjacent contacts, stable owned CPU wire edges,
and pointer-exact worker/history undo/redo. M47/M49 and other
CPU regressions establish typed displayed-root selection and
refusal preservation. This does **not** turn GUI success into
an observed immutable pointer value or expand accepted Boolean
geometry beyond the finite supported source domain.

**M52 is accepted with two explicit observation qualifiers:** the
actual selected Linux Vulkan physical device was not logged
independently of the available GPU inventory; and Windows remote
cursor-wrap reflection could not be assessed through RustDesk.
Neither was a separately required positive predicate of the
targeted M52 source-positive Merge/render/transaction gate.
M52 does **not** imply Windows native cursor-wrap acceptance,
universal multi-shell/grazing/coplanar Boolean validity,
protected V13 disk-only resume, or V19 high-density performance.
Original V17-M15 remains an **independent** integrated review gate;
V18/V19 and optional M66/M70–M72 remain OPEN.

## Earlier Linux-only observations (historical partial stage)

The sections below were written before full Windows/provenance acceptance. Their earlier pending-state statements are retained as historical evidence and are superseded by the final acceptance section above.

### Positively reported physical observations

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

### Historical checklist before Windows testing (now satisfied at stated scope)

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

### Additional undo/redo and reflection refusal observation — 2026-10-20

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

### Ordinary checkpoint, controlled failure, and V13 armed-recovery refusal — user PASS 2026-10-20

The user performed the complete checkpoint triad on the live
Linux desktop, supplied the following actual file/log evidence,
and explicitly marked all three tests PASS. These are
physical program tests, not merely CPU simulated checkpoints.

1. **Ordinary checkpoint / recovery PASS.** A fresh ordinary
   Inflate session wrote `m52-checkpoints/ordinary.mshr`:

   ```text
   -rw-rw-r-- 1 logiclrd logiclrd 521380 Oct 20 21:51 ordinary.mshr
   ```

   The file reopened correctly in Mesher, displayed the
   recovered geometry and allowed further sculpting. The
   observed symmetric/asymmetric editing state survived
   recovery. Startup feature flags `--curvature-correction`,
   `--collapse`, and `--refine` did NOT persist; see separate
   deferred feature M72 rather than treating this as a failure
   of the current checkpoint format.

2. **Failed-save handling PASS.** The user provided a nonexistent
   parent path, submitted Ctrl+S twice, and observed:

   ```text
   [checkpoint] queued m52-checkpoints/nonexistent-parent/failed.mshr
   [checkpoint] queued m52-checkpoints/nonexistent-parent/failed.mshr
   [sculpt] ... undo_depth=26 redo_depth=0 history_roots=27 ... checkpoints_saved=0 checkpoint_failures=2 renderer=dirty_partitions
   ```

   This confirms both background write failures were counted,
   no successful checkpoint was reported, and the application
   was still running and logging active sculpt state. The user
   labeled the safe-failure case PASS. An explicit subsequent
   before/after root pointer is not captured by this log.

3. **Protected Merge-armed recovery refusal PASS.** Launched:

   ```text
   build/Mesher --checkpoint m52-checkpoints/protected.mshr --merge-geometry --symmetric --merge-test-reflected-tetra
   ```

   The first-original reflected fixture reported 16 original
   vertices/16 faces and enabled background save via Ctrl+S.
   The user verified a 1249-byte `protected.mshr` file, then:

   ```text
   build/Mesher --merge-geometry --recover m52-checkpoints/protected.mshr
   Mesher: --merge-geometry refuses V13 disk recovery without sealed first-original M29 authority
   ```

   This is an exact expected CLI guard in `src/main.cpp` that
   rejects any Merge-armed disk recovery request **before**
   deserializing the file; do not misrepresent it as proof of
   a later serializer-level source-recertificate validation.

**New deferred user requests:** M70 symmetric-to-asymmetric
runtime editing switch; M71 symmetric/mirrored brush editing
on geometrically asymmetric meshes (distinguishing dual-side
spatial strokes from proof of exact mirrored topology); M72
persistence or explicit restore of run-time sculpt feature
flags/settings. None block M52, original M15 or V18/V19.

**Still open for M52:** exact executable SHA, GPU/Vulkan driver,
Windows physical host verification; any remaining explicit
accepted/refused immutable root/history identity assertions
and requested diagnostics. The earlier geometry AA, camera,
W, both named Merge fixtures, user-observed undo/redo and
reflection-closure refusal remain accepted at their reported
scope. M52 is NOT checked complete.
