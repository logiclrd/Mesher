# V17-M61 — Ctrl+LMB strafe, Ctrl+RMB dolly and both 90° camera poles

## User request and intended gesture mapping

Following M60, the user requested view-relative horizontal/vertical
camera strafing with Ctrl+LMB (originally Alt+LMB), a switch of
existing forward/backward dolly from Alt+RMB to Ctrl+RMB, and retention
of Alt for future inverting brush action. Ordinary RMB remains upright
yaw/pitch orbit. Unmodified LMB continues sculpt or authenticated
first-original FaceId selection in Merge ON mode. Alt-only LMB is
currently just ordinary LMB; this milestone does **not** enable an
inverse brush or new Boolean privilege.

The same `ViewportCameraGestures` pure input latch is called from
`VulkanViewport::updateMouseOrbit` and verified in CPU tests.
Only mouse-down edges choose Orbit/Dolly/Strafe. Modifier release,
change, or another button during a gesture cannot change its mode;
Ctrl pressed after LMB down cannot hijack an ongoing sculpt. Both
buttons pressed simultaneously do not initiate a camera gesture.
The actual `updateMouseSculpt` blocks all active camera gestures,
therefore pan cannot enqueue brush input or select Merge FaceIds.
All three gestures share the M55 GLFW cursor wrap and HiDPI
physical framebuffer conversion. If a compositor refuses warping,
ordinary local drag continues.

## View-relative camera translation and picking

Ctrl+LMB moves the viewer along the display camera's right and up
axes, measured with the 45-degree vertical field of view,
current startup camera eye distance and actual framebuffer height.
Right/down pointer travel moves the camera right/down (the scene
left/up); the opposite gesture exactly undoes that displacement.
This is UI-only display translation, including after orbit and dolly:
`translation -= cameraRight*dx*worldPerPixel`,
`translation += cameraUp*dy*worldPerPixel`.
There is no source-mesh mutation, staging duplicate, new worker
stamp, change to stable source IDs, authenticated original histories,
checkpoint, source mirrors or GPU topology. Real pixel conversion
remains consistent across HiDPI client and framebuffer sizes;
invalid/overflowing operations leave the display unchanged.
`ViewportOrbit::pickingRay` remains the true inverse of the model
transform, also after interleaved pan, dolly and orbit.

## Full pole views without inverted Z-up

M60 used +/-65 degrees **of scene X rotation**, which is not the
same as allowing the viewer to reach a top-down or bottom-up angle.
The perspective camera starts ~18.26 degrees above the XY plane
(`atan(0.33)`). The displayed scene rotation is now clamped to
`[-pi/2 - atan(0.33), +pi/2 - atan(0.33)]`. Thus the CAMERA'S
source-relative elevation reaches exactly +/-90 degrees while the
world Z-up projection never becomes negative. At an exact pole,
Z-up projects to zero screen length (a necessary mathematical
singularity, not a model flip), but stored two-angle yaw stays
defined. Moving away from either pole immediately recovers
the upright vertical reference. It is **not** valid to simply clamp
the previous scene X rotation to +/-90 degrees: the existing
elevated eye would cross a pole at +~71.74 degrees scene rotation.

## Acceptance boundary

The existing `mouse_picking` suite now tests the two camera poles,
reverse motion, 360-degree yaw at a pole and M60's circular no-roll
regression, Ctrl press-edge latching/late-button/mid-drag blocking,
view-right/up motion and exact opposite round-trip, HiDPI scaling,
invalid finite work rejection, background screen-parallel pivot and
inverse source picking after interleaved view translation/orbit.
A viewport-enabled source-contract test additionally verifies
that the real GLFW input routing is wired to the shared latch,
Ctrl+LMB pan, Ctrl+RMB dolly and sculpt/FaceId suppression.

Three-platform Vulkan-enabled GitHub Actions CI on the final PR
head and independently merged main is required. Actual observed
Linux/Windows native graphics input and cursor wrapping remain
part of M52; original M15, V17 and V18/V19 remain open.
