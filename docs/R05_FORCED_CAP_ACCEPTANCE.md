# R05 — Always Cap: independent green private disks

**Status: implemented on stacked R05 branch; CPU and physical Vulkan acceptance still required.** This milestone builds directly on R01 detection, R02 conservative vertex-star cut selection, and R03 actual private excision. It is a separate forced alternative to R04 Always Zipper, not an automatic policy.

## Launch and inspect

```sh
git fetch origin pull/140/head
git switch --detach FETCH_HEAD
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher --repair cap
./build/Mesher --repair cap --torus
./build/Mesher --repair cap --hollow-sphere
```

`--repair-cap` is an alias. On the first newly detected post-sculpt contact, the brush **freezes for that run**. The original post-stamp candidate, editable worker root, undo/redo, checkpoints, history, symmetry, stable IDs and persisted document are untouched. Only the **private R03 hole geometry** is changed. Orbit, zoom, W/G and Escape remain available; no further sculpt, undo or save while frozen.

Every surviving R02 directed boundary is capped **independently**. One contour gets one disk, two contours get two disks, four get four, and so forth, with no implicit zipper/correspondence pairing. The **new faces are green and visible from both sides**, and are real faces in the displayed *private* closed mesh. A successful readout includes:

```text
[repair-r05] CAPPED forced=1 private_closed_mesh=1 independent_caps=...
             green_faces=... inserted_vertices=... new_faces=...
             refinement_steps=... min_cap_quality=...
             no_zippers=1 unpublished=1
```

The CPU capper reverses each original survivor-oriented contour and uses bounded **best-quality projected ear clipping**, not a center-fan or a circular-frustum assumption. It rejects self-crossing projections, degenerate ears and unusable contours rather than inventing an unsuitable spanning surface. Subsequent subdivision acts on cap triangles and the neighboring retained face across each shared edge, without producing T-junctions. The final result is independently audited for exactly two opposed incident faces per edge and one cyclic fan per vertex, including disconnected closed portions. Deleted face tombstones stay retired, surviving IDs stay stable, newly inserted IDs are fresh.

The cap algorithm is intentionally conservative for extreme nonplanar, twisted or pinched projected holes. R05 does **not** promise mathematically minimal area, zero local self-intersections, automatic orphan interior removal, or live/unfrozen editing. R06 will select cap vs zipper per isolated repair region and separately remove truly redundant enclosed disconnected geometry. A zero-loop case is **not** a cap and is refused in this forced diagnostic mode.

A refusal produces `[repair-r05] CAP_REFUSED ... reason=...`, and the **R03 private open cut with gold loops** stays visible. It is not silently replaced with a zipper or a partially capped mesh.

## Physical acceptance

1. **Ordinary sphere, opposing protrusions:** collide the probosci as in R04. Check that both independent holes are covered with two **green** disks, without a bridge between the protrusions. Inspect both sides with wireframe on/off and confirm `R05: CAP COMPLETE`.
2. **Torus, pinched hole:** cap each opposing loop **separately**. Look into the central opening from several angles. This should be significantly shorter than the long zipper bridge previously observed.
3. **Hollow sphere mouth pinch:** cap the available loops independently, without zippering. A completely isolated inner shell, if present, remains until the R06 redundant-component elimination step; the absence of that deletion is **not** an R05 bug.

Take console lines with `independent_caps`, `green_faces`, `refinement_steps`, `min_cap_quality`, or exact refusal reason, and screenshots of the actual faces from inside and outside. Identify any excessively elongated new triangles or accidental caps crossing retained geometry. A complete CPU matrix on GCC, Clang, MSVC does not replace Linux and Windows GPU visual acceptance.

**Exit criterion:** correct private green closure for sphere, torus, hollow sphere, stable manifold structure, no worker publication, and separately recorded physical Linux/Windows acceptance.
