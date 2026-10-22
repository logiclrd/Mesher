# R06-M05 — Frozen developer automatic repair: inspection and acceptance

**Scope:** the first physical Vulkan-facing use of the R06-M01–M04
private topology pipeline. This is a developer opt-in only, not R07
continuous repair or R08 production Merge ON. Worker/history/checkpoint
publication, mutable source root and undo are untouched.

## Launch

From the Mesher build:
```sh
./build/Mesher --repair auto
./build/Mesher --repair auto --torus
./build/Mesher --repair auto --hollow-sphere
```

A compatibility alias `--repair-auto` is also accepted. The three
forced debug modes are still available as `--repair zipper`,
`--repair cap`, or their hyphenated aliases, and must remain
mutually exclusive with `auto`. The standard interactive Inflate
stamp behavior applies; do not use `--merge-geometry` or protected
`--avoid-collisions`. The named torus/hollow fixtures initialize
asymmetric topology. Relaunch after each freeze to make another stamp.

The first qualifying new triangle-triangle contact triggers a freeze
in the normal *pre-CAS validator*: R01 obtains the speculative
immutable candidate and trusted contact FaceId pairs, R02 previews
full incident vertex stars, R03 makes a private open cut, M03 chooses
per-region closures by source face provenance, M04 constructs one
quality-bounded globally audited private closed root and deletes only
M02-certified enclosed inward shells. The original worker root and
all history/checkpoint paths remain unchanged.

On success the viewport displays the ACTUAL final M04 private closed
mesh, including any post-retirement deleted interior, with genuine
surviving caps **green** and zippers **blue**, both double-sided for
interior/exterior camera inspection. No synthetic seam surfaces are
painted if M04 refuses. On refusal (including incomplete contact
query, invalid R02/R03 cut, planner refusal or M04 quality/material
refusal), the viewport displays the true R03 private open cut with
gold surviving rims, or the unchanged candidate if R03 did not
succeed. HUD/title distinguish `R06: AUTO COMPLETE` vs
`R06: AUTO REFUSED`, always frozen.

## Console

Prior R01–R03 diagnostics remain. After an attempted auto closure:

```text
[repair-r06] AUTO_CLOSED private_closed_mesh=1 regions=... cap_loops=... zipper_pairs=... green_faces=... blue_faces=... retired_cavity_components=... retired_cavity_faces=... retired_cavity_vertices=... exterior_components=... post_closure_faces=... final_faces=... trusted_contact_links=... estimated_area=... unpublished=1 worker_history_checkpoint_unchanged=1
```

Each independent region also prints caps, zippers, excision
patches, trusted interpatch contact links, a zero-rim flag and
estimated area. The estimate is NOT a geometric quality certificate;
R04/R05/M04's triangle and global manifold audits are authoritative.

Failure prints `[repair-r06] AUTO_REFUSED private_closed_mesh=0
plan_status=... reason=... (R03 cut retained where available;
unpublished=1)`. A refusal is a legitimate result if the mesh is too
folded, surfaces cross ambiguously, the finite limits are exhausted,
or there is insufficient evidence to delete enclosed material.

## Physical inspection acceptance for R06-M05/M06

With the exact PR-head build and physical GPU (record OS, driver,
`git rev-parse HEAD`, shader build and `ctest` result):

1. On default sphere collide two Inflate-grown tentacles. Inspect
   both sides of any genuine green caps and blue zipper seams;
   check that only M03-authorized regions connect.
2. On the tight torus pinch the central passage until R01 freezes.
   Inspect inside/outside and all final new faces. The final topology
   and Euler behavior depend on whether the actual R03 excision
   removed the throat (genuine annular cut) or only local disks.
3. On the hollow sphere collapse the negative-Y mouth. Check the
   exterior, interior and camera between hulls. Any wholly
   M02-certified inward enclosed component must be absent in the
   final displayed mesh, leaving a closed outward-only shell;
   ambiguous incomplete classification must refuse without
   silently deleting an external solid.
4. For a deliberate unsupported/narrow crossing verify
   `AUTO_REFUSED`, actual gold R03 rims rather than invented blue/
   green faces, orbiting and wireframe still working, no further
   sculpt, undo/history/checkpoint changes.
5. Where viable exercise a mixed multiple-region cap+zipper
   configuration and confirm both colors, no repeated/
   cross-region edges, no ghost old faces, no detached caps.
6. Repeat on physical Windows Vulkan before signing off M06.

## CPU verification

`repair_r06_m05_frozen_auto_inspection` tests actual M03/M04
tight-torus private cap overlays, double-sided green triangles,
gold R03 refusal rims, real crossing candidate zero-rim refusal,
the one-shot freeze contract and an actual worker pre-CAS stamp
whose candidate is never published. Earlier forced-mode and M04
tests remain part of the complete warnings-as-errors CI matrix.

R06-M05 is an opt-in visual integration milestone. R06-M06
cross-platform physical checks and stacked PR integration remain
separate; do not check the parent R06 until they are finished.
