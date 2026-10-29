# UX08 — Logarithmic global view scale and import framing

UX08 is a **view-only** transformation and does not change any source-space
vertex coordinate, stable ID, mirror identity, paint colour, worker geometry,
mesh revisions or durable checkpoints.

- The new lower-right scale slider spans 10^-5 to 10^+5, with eleven
  uniformly spaced power-of-ten ticks and a default generated-mesh 10^0
  centred knob. Dragging continuously adjusts the exponent; Ctrl reduces
  sensitivity by 15x. Hovering anywhere on the control displays the
  four-decimal signed exponent immediately to the left of the knob.
- On Ctrl+O, successful private candidate validation supplies source AABB
  bounds to the viewport. **After** replacement, the camera recentres,
  resets orientation and chooses a fitting exponent based on the actual
  framebuffer aspect and the existing 45-degree perspective.
- A startup `--recover` checkpoint is framed similarly; default generated
  geometry retains the legacy original camera orientation and scale 10^0.
- The renderer maps original source coordinates through a **fixed initial
  fit scale**, orbit basis, and source-centre translation. Subsequent slider
  decades above the initial fit exponent change optical magnification
  (the X/Y projection rows), never pushing the camera through the mesh.
  Below that exponent, the camera retreats along its original viewing
  direction instead. The FOV remains 45 degrees, avoiding the extreme
  perspective distortion of negative optical magnification. Each zoom-out
  decade makes the model centre approximately one tenth as large on screen.
  Picking applies the inverse optical ray mapping and then the inverse
  orbit/source fit, returning original source-space rays to the immutable
  BVH and brush worker. Orbit, dolly, strafe and symmetry remain intact.
  GPU vertex positions are first rebased to the scene centre in double
  precision, then converted to float; this prevents cancellation at
  extreme optical zoom. An independent Open rebuilds every GPU partition
  against the new centre. CPU source positions are never rescaled.
- Mesh wireframe thickness and opacity fade as projected triangles approach
  pixel/subpixel size. The floor grid fades independently as pixel density
  increases. W remains the existing wireframe toggle; G remains the
  separate floor-grid visibility toggle. Dense meshes retain RGB legibility.
- Near/far clipping adapt to camera-to-scene distance and fitted bounds;
  this prevents the fixed 100-unit far plane from amputating very large
  displayed models after zoom/dolly. The floor is rebuilt below and centred
  under imported bounds, without becoming part of source geometry.
- CPU tests cover extrema, sample off-origin model, very large and tiny
  models, the exact Ctrl sensitivity ratio, readout format, inverse picking,
  orbit pivot anchoring and dynamic clip range.

**Acceptance limitations:** Physical GPU behaviour must be inspected under
Linux/Windows, including the coloured VCGLIB duck and repeated file Opens.
The slider range itself bounds the maximum fitting correction; models with
more than ten orders of magnitude of variation cannot all fit at once.
Large absolute source coordinates are still uploaded as float GPU vertex
positions and may need future render-local origin rebasing for precision.
The source itself is always stored unmodified in double precision.
