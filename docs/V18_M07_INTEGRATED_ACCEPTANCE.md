# V18-M07 — Integrated acceptance evidence and physical GPU protocol

Status: **CPU/CI integration in review. Real Linux and Windows V18
GPU observations NOT YET RECORDED. M07 and original V18 remain OPEN.**

## Identity and acceptance boundary

The original V18 objective in [todo.md](../todo.md) is preserved
unchanged: a separately selected **Merge geometry: OFF** policy that
prevents newly introduced nonadjacent crossings, maintains
**local mesh-scale** clearance, retains demonstrably legal tangent
movement, does not repair unrelated existing bad surfaces and handles
moving both opposing sheets and reflected symmetry with finite
work. The scope is a conservative **endpoint candidate**, not
continuous swept CCD. V19 owns complex imported surfaces and
large-density general robustness. M72 separately owns checkpoint
persistence of settings and feature flags.

No amount of CPU CI is itself proof of a physical GLFW/Vulkan
window displaying the new mode or permitting real mouse input.
V17-M52 and V17-M15 GPU observations concern the earlier protected
Merge system; they cannot be relabeled as M07 V18 evidence.

## Previously verified exact implementation evidence

| Requirement | Independently tested implementation |
| --- | --- |
| Complete changed-face before/after M01 contact ledger | `offmode_poststate_contact_ledger`; all six finite BVH budgets; real COW roots and prior unchanged collisions |
| Private pre-CAS M02 refusal, FIFOs and undo isolation | `offmode_private_step_gate`; refusal neutral for renderer, mirror and history |
| Local scale / tangent proof vs near-gap approach | `offmode_mesh_scale_clearance`; min shortest of two local edges and positive-gap rigid slide; numerically unsupported **zero-gap contact** still refuses |
| Bounded normal-only retry and large-step policy | `offmode_bounded_normal_backtracking`; five fractions 1/2^k (hard maximum eight), conservative ≤0.5 old shortest incident edge; all private work accounted |
| Both opposing sheets on the SAME candidate and reflected fixed plane | `offmode_reflected_opposing_sheet_transactions`; reciprocal stable vertex/face IDs, four inward sheets, genuine X=0 seam midpoint/journals, same-root refusal and recovery |
| Truly opt-in desktop and old checkpoint semantics | `desktop_offmode_policy_and_recovery` and real GPU-free `desktop_offmode_cli_fail_closed`, explicit default and inapplicable modes |
| Independent physical-visual near-gap fixture with real CPU equivalent | `v18_integrated_near_sheet_checkpoint_acceptance`; 3 independent faces, repeated approach/refusal, safe remote edit, checkpoint, exact undo/redo |

The new near-sheet GPU fixture is **only** `--avoid-test-sheets`,
and it requires `--avoid-collisions --asymmetric` plus interactive
Inflate. This geometry is completely independent of V17's protected
`--merge-test-tetra` source authorization; no M29 certificate,
Boolean weld, or hidden runtime Merge toggle exists. It contains a
lower triangle (Z=0), nearby independently owned upper triangle
(Z=0.14, opposite winding), and remote editable triangle at X=3.5.
The normal displacement at a vertex of either close triangle points
toward the other. There are **no** globally welded coordinates.
The third face gives a distinct recovery target without resetting.

For this deliberately open, low-resolution display geometry,
disable refinement, collapse and curvature with the three existing
`--no-*` flags in the physical fixture experiment. The production
default sphere remains **all three enabled**; this fixture-only
isolation does not constitute acceptance of every remesh/repair
behavior (the M02–M05 CPU tests already cover the remesh variants).

## Physical hardware recording template

Record both separately for **Linux** and **Windows**:
- Exact 40-character `git rev-parse HEAD`, clean/dirty working-tree
  state and SHA256 of the executable being launched.
- OS version, GPU, actual driver and `vulkaninfo --summary`.
  `vulkaninfo` enumerates available GPUs, not proof of which GPU
  the application selected; record Vulkan adapter if console shows it.
- Include dated screenshots showing the actual window/title, front
  and strongly oblique views of the near sheets, the separate remote
  triangle, and after the first edit; preserve stdout/stderr logs.
- Whether the session is direct display or remote desktop; a
  remote client's pointer-wrap limitation is **not** evidence of
  a Vulkan rendering fault.

Example fresh unarmed/default sanity run (both platforms):

```text
Mesher
Mesher --avoid-collisions
```

Expected observations: regular unarmed sphere still edits normally;
the second run's window title says
`collision avoidance ON (Merge OFF)` and the console reports
`[collision] ON` separately from `[merge]`. Do not claim a
particular count/fraction for arbitrary sphere strokes.
Test LMB, orbit RMB, Ctrl+LMB strafe, Ctrl+RMB dolly,
W/G, Ctrl+Z/Y. Verify new policy does not trigger protected Merge
when pressing M. Reject unexpected crashes or nonfinite geometry;
a conservative refusal is not itself evidence of a defect.

Named near-sheet fixture, from the repository root (Linux):

```bash
mkdir -p m07-checkpoints m07-logs
./build/Mesher --avoid-collisions --asymmetric --avoid-test-sheets \
  --no-refine --no-collapse --no-curvature-correction \
  --strength 0.12 --brush-radius 0.5 \
  --checkpoint m07-checkpoints/near-linux.mshr 2>&1 \
  | tee m07-logs/near-linux.log
```

Windows PowerShell:

```powershell
New-Item -ItemType Directory -Force m07-checkpoints,m07-logs | Out-Null
& .\build\Release\Mesher.exe --avoid-collisions --asymmetric --avoid-test-sheets `
  --no-refine --no-collapse --no-curvature-correction `
  --strength 0.12 --brush-radius 0.5 `
  --checkpoint .\m07-checkpoints\near-windows.mshr 2>&1 |
  Tee-Object -FilePath .\m07-logs\near-windows.log
```

**Step A — Visible initial candidate.** The title identifies
OFF-mode avoidance. Orbit to a view from **above Z** so the two
slightly separated triangles at X≈0 are visible; they can largely
overlap when seen exactly from above. A low side/oblique angle
makes the vertical 0.14-unit gap visible. A third independent
triangle is clearly offset at X≈3.5. Verify face occlusion, W
edges and G floor do not alter geometry.

**Step B — Bounded approach.** With the cursor over the close
overlapping pair, click/hold or make repeated short LMB strokes
toward the other triangle. A stamp over either visible
source-facing triangle moves its normal toward the opposing
one. Inspect the `[collision]` and `[sculpt]` logs for
`trials > 1` and `0 < normal_fraction < 1` at least once,
and an accepted root/publication. Record actual values only
after observing them, never invent expected exact fractions.
The corresponding CPU regression uses exactly the same
three-face geometry and makes an accepted reduced-fraction
stamp in the stable near-gap scenario.

**Step C — Terminal refusal, then recovery.** Continue short
near-sheet strokes. Eventually the clearance policy should
refuse a stamp rather than introduce a new sheet collision.
Look for `R`/ `rejected_stamps` increment and
`REFUSED`/ `refusal_reason`. On refusal verify the prior
rendered mesh does not change and there is no partial
geometry/dirty partition update. If repeated clicks cannot
reproduce refusal, note the view, hit positions, last logged
stamps, and whether mouse pickup was successful; this is an
unresolved physical acceptance case, not a pass inferred
from CPU tests. Then sculpt the **remote third triangle**.
Verify a subsequent accepted root, clean visuals, and
Ctrl+Z/Y. Check no terminal worker quarantine occurred.

**Step D — Checkpoints.** Ctrl+S, wait for
`checkpoints_saved` to increment and file to exist, then
close. Recover the file twice in fresh processes, first
`Mesher --recover <file>` (ordinary, avoidance not armed)
and then `Mesher --avoid-collisions --recover <file>`
(armed). The latter should remain usable and preserve the
edited mesh; no manual reapplication of a protected Merge
certificate is permitted. Recovered numeric mitigation
settings use command-line **defaults**, not necessarily the
three fixture `--no-*` opt-outs: re-pass them explicitly
if preserving that editing setup matters. The fixture
creation flag MUST NOT accompany `--recover`.

**Step E — Headless negative CLI.** Verify
`--avoid-collisions --merge-geometry` and
`--avoid-test-sheets` without the required avoidance/
asymmetry refuse before opening any Vulkan window. Check
the separately accepted V17 protected startup remains
unchanged. The exact parser contract is already enforced
in Vulkan-enabled CI.

## Exit criteria and honest limitations

Do **not** mark M07 or original V18 checked until:
1. Real Linux and real Windows Vulkan display observations
   include the new armed title and accepted/rejected/recovered
   edit behavior, tied to exact executable hashes and GPU
   driver records. Record a device-specific refusal/failure
   instead of asserting success from the CPU test.
2. Every finite CPU acceptance case above, new M07 tests
   and independent end-to-end inspection are green on
   exact PR-head Linux GCC, Linux Clang, Windows MSVC.
3. A **separate merged-main** compiler/CTest/benchmark workflow
   succeeds for the same accepted commit.
4. The complete acceptance table is updated with observed
   outcome, source screenshots/logs and unresolved caveats.
   Only then check both original V18 and flat M07 in
   `todo.md`. Keep V19, M66, M70–M72 deferred.

Remaining conservative limitations are material: exact
independent triangle **zero-gap grazing** is not generally
permitted by M01's terminal contact guard, although a
positive-gap non-approaching **rigid tangential slide** is
supported in the tested domain. The half-local-edge motion
cap limits but does not prove away all intersample
tunneling; neither exact swept CCD nor universal
nonmanifold/imported geometry guarantees follow.
