# V17-M54 — Independent accepted-desktop-Boolean manifold and render-staging audit

## Why this is necessary

The user's real Vulkan screenshots showed (1) the reflected first-original
two-tetra source, (2) an accepted `M → A → Enter` output whose wire edges
appear to cross or penetrate the near-facing solid, and (3) substantially
more distracting crossings after M56 removed the shader's arbitrary
`-0.00012 * clip.w` depth pull. Neither visual result nor
`[merge] A 1 R 0 P 0` proves a watertight union. The M56 correction
addressed one demonstrable wire occlusion hazard but must not be confused
with a complete diagnosis.

Prior private exact-cut/reflected tests and M51 real controller-to-worker
tests showed source-positive and pointer-exact publication. M54 fills the
gap by independently inspecting the **actual source-positive immutable
worker roots produced by the desktop fixture and input route** rather
than merely checking status.

## Reproducible structural acceptance

`tests/accepted_union_audit_tests.cpp` is an independent CTest binary,
not folded into the existing asynchronous test bundle. It executes the
same named M50 asymmetric and M51 reflected source fixture factories
and production desktop Merge input controller (`toggle ON → nominate
all original FaceIds → submit current indexed root`) and real M47
worker finite-contact-discovery and original-source exact Boolean.

An original (before) positive control, accepted (after) root and
reflected one-step undo/redo roots each undergo independent exhaustive
CPU audit, including:

* Enumerated current immutable `MeshRevision` partitions, stable
  vertex/face identity, degeneracy and duplicate sorted triple tests.
* **Canonical undirected edge incidence** rebuilt directly from
  stable `FaceId` triangle corners, independently checked against
  the `RegionTopologyIndex`. Two incident, oppositely directed
  faces per edge are required. Expose boundary/nonmanifold/winding
  failures distinctly.
* Connected shell count and per-component Euler `V − E + F = 2`,
  signed closed-shell volume and exact reflected equal-volume pair
  expectation. Original asymmetric = two closed tetra components;
  accepted asymmetric = one closed union. Original reflected =
  four components; accepted reflected = exactly two, symmetrically
  paired, closed unions.
* **Complete post-state nonadjacent triangle crossing query** using
  the real immutable indexed triangle BVH, with explicitly finite
  but comfortably fixture-sized resource limits. Require original
  intersecting source control to have contacts and accepted union
  to have none, including ambiguous contacts.
* Full *CPU renderer staging* regenerated using the actual
  `buildPartitionRenderGeometry` for every live face-owner partition:
  exactly the live solid-triangle count, only topological face-edge
  wire coordinates, globally unique wires, and no missing, extra,
  ghost or stale line fragments.

The UI still uses the worker's immutable dirty-frame mailbox. The
audit requires exact frame/root publication, one accepted request
with no refusal, correct history depth, and pointer-exact undo/redo
round trips, rather than accepting a status counter alone.

## Boundaries and follow-up

This exhaustive 16-face source-fixture audit is TEST-ONLY. It adds
no unbounded collision query to the GUI render frame, real worker
transaction, source-certificate path, interactive default OFF, or
persistent checkpoint code. It does not infer stable FaceIds from XYZ.

**If the accepted worker mesh passes** but the user's view still shows
crossing/hidden lines, the remaining issue is in camera/clip/line
rasterization, material shading, depth or GPU/driver behavior, not
necessarily actual source triangle crossings. Real Linux/Windows
Vulkan observation with **W** solid-only vs overlay in M52 remains
required; screenshot-only inference cannot certify a fix.

**If the accepted worker mesh fails**, M54 must identify and document
the defect with observed metrics. Any necessary fix too large for
one pass must be planned as independently completable flat TODO items
before proceeding. Do not mark M54 accepted until its genuine
geometry checks pass. Original M15 remains unaccepted.

Require the exact final PR-head and independent merged-main full
GCC/Clang/Windows MSVC Vulkan-enabled warning-clean CTest matrix
before any merge/completion claim.
