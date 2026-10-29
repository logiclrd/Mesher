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
- The renderer uses the same **fixed 45-degree field of view** at
  every slider exponent. The slider directly changes a model/view transform
  around the imported source-space AABB centre. The virtual camera is
  translated to follow one selected original-space anchor, preventing
  the camera from crossing the mesh at large positive exponents.
- On the first actual scale change of a gesture, a 4x4 array of framebuffer
  ray probes finds the **nearest visible surface hit** using the existing
  CPU pick API. The anchor is latched for that gesture and never reprobed
  while the slider is moving. If no hit exists, the same screen-parallel
  orbit plane provides the fallback distance, using the mesh centroid
  as anchor. An idle interval of **two seconds** terminates the gesture.
- The camera-to-anchor vector is recorded in full 3D when the operation
  begins. At each subsequent exponent, the new target camera position is
  the displayed, newly scaled anchor PLUS that original vector. Each frame
  moves 80% of the remaining displacement at the 60 FPS reference rate
  (`1 - 0.2^(60*dt)`), with frame-time correction. A manual orbit,
  strafe or dolly cancels the ongoing camera follow immediately.
- Rendering keeps the original constant view orientation, and subtracts
  the current camera-follow offset from the view model translation.
  CPU picking adds this same offset to the eye before applying the inverse
  orbit/source-scale transform. This preserves ray/solid congruence without
  modifying immutable source geometry or checkpoint data.
  GPU vertex positions are rebased to the source-centred local coordinate
  system in double precision before conversion to float, avoiding the
  translation precision loss that previously affected extreme scaling.
- Mesh wires now fade *smoothly* with the median projected triangle altitude,
  not the shortest altitude, preventing isolated skinny faces from turning
  the wireframe on/off abruptly. Wires retain a faint minimum coverage at
  silhouettes instead of vanishing on the original sphere. The floor grid
  fades independently at subpixel density. W remains the existing wireframe
  toggle; G remains the separate floor-grid visibility toggle.
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
GPU render positions are rebased to the scene centre in double precision
before float conversion, preserving features on widely translated models.
The original source remains untouched in double precision.
