# V17-M63 — Ctrl+LMB world-grab strafe polarity

## Evidence and correction

Following a real Linux GPU test of M61 after PR #114 merged,
the user reported that Ctrl+LMB conceptually grabbed the camera.
Moving right/down moved the scene left/up. They prefer to grab
and drag the **world** directly in screen space: Ctrl+LMB
right/down shifts the visible world right/down. This reverses
the signs of both pan axes and changes no other gesture.

The shared `ViewportOrbit::strafe` now adds rather than subtracts
the movement computed from the actual display screen-right/up
camera axes. DPI scaling stays tied to physical framebuffer
height and camera FOV/eye distance. For dx positive it adds
screen-right translation; for dy positive it subtracts screen-up.
The camera effect is opposite, while source geometry, source
revision/FaceIds, rendering vertex buffers, worker commands,
undo/redo, history and checkpoint remain untouched.

M61's actual GLFW Ctrl+LMB press-edge latch and prevention of
accidental brush/FaceId nomination do not change. Ctrl+RMB dolly,
ordinary RMB no-roll orbit, top/bottom pole limits, cursor wrap,
Alt reserved for future inverse brush, W wires and G floor all
retain their existing semantics. CPU tests enforce both world-grab
signs, reverse-path round trip, HiDPI scale, interleaved
orbit/dolly/pan, invalid input, exact inverse picking and
screen-parallel background pivot after arbitrary translation.

Full compiler/CTest/history stress/benchmark verification is
required on the exact final PR head and independently merged
`main` on Vulkan-enabled Linux GCC/Clang and Windows MSVC.
Visual feel on real driver remains for user confirmation in
M52; a compiled test is not a physical mouse-gesture proof.
