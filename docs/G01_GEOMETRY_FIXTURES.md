# G01 — Interactive topology test geometries

Developer-only startup fixtures, independent of R01 diagnostics, V17 exact Boolean and future approximate repair. They are normal immutable, editable and checkpointable starting meshes.

## CLI

```sh
./build/Mesher --torus --vertices 5000 --seed 1 --relax 65
./build/Mesher --hollow-sphere --vertices 5000 --seed 1 --relax 65
./build/Mesher --repair-inspect --torus --vertices 5000
./build/Mesher --repair-inspect --hollow-sphere --vertices 5000
```

Default sphere behavior remains unchanged. Both fixtures use *independent asymmetric mesh topology*, with no fabricated exact mirror pairing. `--symmetric` is explicitly refused for these randomized fixtures until the generic operation-reflection policy is implemented. Other legacy protected source fixtures, recovery, demos and compare modes are mutually exclusive. The torus has exactly N vertices. The hollow sphere starts with **N/2 vertices per sphere**, then removes two south-pole cap sets and adds two connector-ring populations, so its final count will differ from N. It requires even N; pathological seeds with zero/multiple non-simple openings fail with a clear diagnostic instead of silently building a damaged mesh.

## Torus

Major radius **0.55**, minor radius **0.45**, hole axis Y. The torus extends to radius 1.0 at its outside but leaves an inner opening of radius **0.10**, easier to pinch shut with the Inflate brush. Because the minor radius nearly equals the major radius, inverse-area sampling uses a bracketed Newton/bisection solve to remain monotone even near the narrow opening. Create roughly equally spaced minor-circle rings, but vary each ring's vertex count in proportion to its exact toroidal surface-area band. Jitter every vertex within an ordered parameter stratum, then relax on the analytic surface. This preserves **comparable triangle edge lengths on the inside and outside**, rather than crowding the narrow hole with equal ring populations. Inverse transformation of the cumulative density `(R*v+r*sin(v))/(2*pi*R)` yields unbiased marginal area allocation; `--relax` iteratively regularizes positions on the analytic torus with each vertex retained in its original stratum. Periodically connect neighbor rows using arclength zipper triangulation; do NOT compute a convex hull (which would fill the central tunnel). Expected: exactly N vertices, 2N triangles, one closed orientable connected manifold, Euler characteristic 0 (genus 1). The axis and hole are initially visible from the default viewport perspective.

**Physical exercise:** orbit to view the open central tunnel; sculpt the inner circumference toward itself until opposite sides collide. With `--repair-inspect`, the private candidate should freeze with incident red markers and *no topology surgery*. Later R02–R06 can use the identical geometry to establish whether the resulting material loses the tunnel correctly (one connected sphere-like genus-0 outer boundary).

## Hollow sphere

Build two independently randomized and relaxed convex sphere triangulations, **N/2 original vertices each**, radii 1 and 0.95. Invert the winding of every inner sphere triangle. Delete every original vertex satisfying `Y < 0` and `sqrt(X²+Z²) < 0.1`, and delete faces incident to any retired vertex. Verify exactly one simple topological hole per shell. Create two polygonal tube rings of radius **0.095** around the Y axis: outer end **Y = −0.96**, inner end **Y = −0.93**, same ring population but azimuthal phase offset of half a vertex interval. Zipper original outer opening→outer ring, outer ring→inner ring, inner ring→original inner opening. Preserve surviving original triangle IDs/vertices in the mesh construction and reorient every final face consistently.

Expected: one closed, connected, consistently oriented genus-0 manifold of thickness, Euler characteristic 2. The inner sphere must point *toward the hollow cavity* until the mouth closes. A physical viewport can inspect both surfaces through the opening, including from inside.

**Future repair expectation, explicitly corrected:** after sculpting seals the mouth, the now-completely-enclosed inner shell represents a **redundant interior surface**, not an intentional void. The repair must identify and delete the entire redundant component; this **requires neither capping nor zippering**. The surviving outer material boundary is a single closed genus-zero component, not an exterior-plus-inner-cavity pair.

## Acceptance and diagnostics

CPU test `geometry_torus_hollow_sphere_fixtures` verifies surface-area strata, deterministic seeds, manifold vertex links, edge incidence, connected components, Euler characteristic, oriented outer/inner normals, tube Y/radius/phase, and invalid-count refusals. The library audit is generic, not specialized to a particular vertex count. CLI options must be visible in `Mesher --help`, and incompatible combinations must refuse before creating a Vulkan window.

Physical Linux and Windows smoke acceptance: use default flags, orbit in/out, confirm no missing triangles or unwanted tube bridge, run several ordinary stamps, and recover a checkpoint for each. These physical observations and merged-main CI are NOT implied by the mere success of the CPU test.

