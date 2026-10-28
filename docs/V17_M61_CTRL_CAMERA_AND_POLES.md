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

## Three-platform implementation evidence (October 20, 2026)

On [PR #114](https://github.com/logiclrd/Mesher/pull/114), the initial
Vulkan-enabled GCC/Clang build caught an X11 macro named `None`
colliding with the gesture enum. The enum member was renamed
`Inactive` (purely mechanical, no gesture-state changes).
Exact corrected implementation commit `6a7f2535` passed full
[GitHub Actions #39503101018](https://github.com/logiclrd/Mesher/actions/runs/39503101018)
Linux GCC, Linux Clang, and Windows MSVC /W4 /WX with all 40
registered Vulkan-enabled CTests and applicable worker stress
and benchmarks. Final documentation-exact PR head and separately
merged main must be checked independently. Real GUI visual and
input feel are not asserted by this compiled acceptance.

## M64 additional Alt+RMB world pan (October 27, 2026)

The user reports a preference for Alt+Right Drag to grab/pan the world
instead of Ctrl+Left Drag. Both gestures are supported; the original
Ctrl+Left remains for compatibility and Linux window managers that
intercept Alt+RMB. Unmodified RMB continues turntable orbit and
Ctrl+RMB continues depth dolly, taking precedence when Ctrl and Alt
are held together. Alt+LMB retains its normal non-camera handling.

The pure mouse-down latch records whether a Strafe started on LEFT
or RIGHT. Only release of that initiating button ends the gesture;
Alt/Ctrl release, late modifier presses or the other mouse button
cannot convert a pan to orbit/dolly/sculpt. Two buttons initially down
cannot start camera movement. Worker-busy/active-sculpt suppression
and framebuffer scaling, inverse source picking, cursor wrap and
display-only translations remain unchanged. CPU gesture and actual
GLFW-source contract tests cover both keys, both pan buttons, priority
and suppression. Physical Linux/Windows gesture acceptance and
three-platform CI are required before main.

## M63 interaction correction (October 20, 2026)

After actually trying M61 on Linux, the user preferred moving the
world along with the Ctrl+LMB cursor, rather than dragging the camera.
M63 reverses both pan axes only, retaining source-space inverse
picking, exact modifier-button latching, W/G and Ctrl+RMB dolly.
Existing M61 tests are updated to enforce the world-grab sign and
reverse translation identity. See `docs/V17_M63_WORLD_GRAB_STRAFE.md`.
