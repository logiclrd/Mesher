MESHER FOR LINUX (x86_64, built on Ubuntu 24.04)
=============================================

Unpack the archive and start Mesher from inside the extracted directory:

    ./Mesher

The binary loads its shaders and UI imagery from sibling directories.
The shipped lib/ directory includes GLFW, Qhull, and any linked OpenMP
runtime where required. The remaining desktop, C++ standard library,
Vulkan loader, X11/Wayland and GPU driver dependencies are supplied by
your Linux distribution.

On Ubuntu 24.04, install system packages if not already present:

    sudo apt install libvulkan1 libgl1 libx11-6

A working Vulkan-capable graphics driver is required to use the GUI.
For a headless executable smoke test:

    ./Mesher --version

compiler-benchmark.json contains both GCC and Clang timings collected
on one GitHub Actions runner. The faster measured Release build is used
for the archive; no compiler choice is hard-coded.

Saving interactive meshes:
    Ctrl+S opens a native Save As dialog for .mshr checkpoints.
    The picker uses kdialog on KDE Plasma and zenity on GTK desktops;
    install at least one (e.g. sudo apt install kdialog or zenity).
    The checkpoint write is queued asynchronously after selection.
