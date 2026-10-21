# V18-M06 — Explicit desktop OFF-mode collision avoidance

## Selection, trust boundary and defaults

M06 exposes the independently completed V18-M01–M05 *source-independent*
collision-avoidance policy to a desktop session with ONE explicit flag:

```text
Mesher --avoid-collisions
Mesher --avoid-collisions --asymmetric
Mesher --avoid-collisions --recover prior-ordinary.mshr
```

**Unarmed `Mesher` and `Mesher --recover FILE` retain their previous
ordinary unguarded Inflate semantics.** The flag acts as
`SculptCollisionSettings::offModeConstraints.enabled=true`, with
the existing finite M01–M05 geometric, clearance, backtracking and
mirror work limits. It does **not** enable `physicalExact` or V16
approximate scar/Boolean repair. Nothing is globally welded or
cleaned on an imported geometry source with old intersections.

`--avoid-collisions` is NOT `--merge-geometry`, which continues to
require the separate V17 first-original source-certified authorization,
M key nomination and restricted original supported fixtures. The two
flags refuse together, in either order, BEFORE any Vulkan/GLFW
initialization or `--recover` file read. Avoidance also requires
interactive mouse Inflate; `--demo`, `--no-inflate`, every comparison
mode and protected Merge-only tetra fixtures refuse. The flag is an
upfront immutable session mode, NOT an undocumented runtime toggle.
The visible window title is permanently marked
`collision avoidance ON (Merge OFF)` while selected.

## Refusals, work, continued interaction

With avoidance enabled, a real desktop background
`AsyncSculptWorker` is constructed with the separately armed M02
private pre-CAS admission and M04 bounded normal retries.
Each trial uses the fully staged post-Inflate/refine/collapse root.
For symmetric sources M05 audits stable-ID mirror ownership
and orientation. Source uncertainty, exhausted work, unproven
grazing and unrecoverable contact **refuse**; some unsafe full
normal stamps are reduced and accepted with a smaller fraction.
There is NO numerical slider or new persistence schema in M06.
M72 separately owns settings/feature-flag checkpoint persistence.

The title identifies mode, and the console prints one
`[collision]` line when its worker status changes, even if
an unsuccessful Step publishes no new root. The status includes:
`ON R <rejectedStamps> A <completedStamps> P <publishedRevisions>
trials <count> fraction <chosenFraction>`, followed by
`REFUSED <source-reason>` when applicable, or terminal
quarantine diagnostics when a post-CAS irrecoverable error
occurs. The existing periodic `[sculpt]` diagnostics expose
`collision_avoidance=ON`, checked/rejected totals, private
trial count, declined trials, accepted fraction, actual
trial copied bytes/changed-ID references/triangle distance
tests, aggregate private copied bytes, budget refusals and
the last refusal reason. These are measured **worker**
work counters, not GPU timings or process RSS bounds.

On refused input, the worker must not publish an unsafe root,
history entry, mirror journal or renderer dirty owner.
Subsequent valid input can be accepted and undone/redone in
the same session. Existing protected Merge console lines remain
under `[merge]`; OFF-mode collision messages never pretend
to be Boolean Merge.

## Checkpoint and recovery

V12/V13 disk checkpoints store immutable mesh, tombstones and
stable mirror pairs, not session-only collision activation.
The *same* old checkpoint may therefore be recovered with
or without the flag. Recovery with `--avoid-collisions`
explicitly opts into the current M02–M05 finite guard on
the recovered immutable source; any invalid or unsupported
mirrored source refuses rather than guessing identity.
Recovery WITHOUT that flag does **not** silently re-enable
collision avoidance, even if a previous session ran with it.
No user-selected scalar/mitigation options are persisted here.
Unverifiable first-original M29 protected Boolean credentials
are not inferred from disk; `--merge-geometry --recover`
continues to refuse.

## Evidence, tests and nonclaims

- `desktop_offmode_cli_fail_closed` runs the **real
  Mesher executable** without Vulkan loader or display,
  checking help/default and both protected-conflict orderings,
  invalid demo/compare/noninteractive combinations, protected
  fixtures and unknown flag refusal before checkpoint I/O.
- `desktop_offmode_policy_and_recovery` tests the actual
  selected/default/protected worker settings; writes and
  recovers an unchanged disk checkpoint; proves unarmed
  recovery remains unguarded while explicitly armed
  recovery refuses a real too-close Step with unchanged
  original indexed root/history/dirty owners and subsequently
  accepts a safe Step with exact undo/redo.
- M01–M05 finite contact, clearance, two moving sheets,
  reflected identities and real asynchronous FIFO tests
  remain separately required in GCC/Clang/MSVC CI.

Only M07 accepts the **integrated GPU desktop behavior**
on actual Linux/Windows hardware. A headless successful
parser check proves neither working Vulkan nor visible
frame presentation. Nor is the mesh-local static endpoint
and half-edge large-step bound equivalent to exact
swept-volume CCD: sufficiently thin unsupported geometry
can still tunnel. V19 retains more complex imported mesh
and high-density robustness/performance acceptance.

M06 is complete only after exact PR-head and separate
merged-main three-platform full CI. Original V18 remains
OPEN pending M07 independent integrated acceptance.
