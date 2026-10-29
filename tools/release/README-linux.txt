MESHER FOR LINUX (x86_64, built on Ubuntu 24.04)
=============================================

Unpack the archive and start Mesher from inside the extracted directory:

    ./Mesher

The binary loads its shaders and UI imagery from sibling directories.
The shipped lib/ directory includes GLFW, Qhull, and any linked OpenMP
runtime where required. The remaining desktop, Qt 6 Widgets/runtime plugins, C++ standard
library, Vulkan loader, X11/Wayland and GPU driver dependencies are
supplied by your Linux distribution.

On Ubuntu 24.04, install system packages if not already present:

    sudo apt install libvulkan1 libgl1 libx11-6 libqt6widgets6t64 qt6-qpa-plugins

On older distributions the Qt Widgets runtime package may be called
libqt6widgets6. Install the version appropriate to your distribution.
For matching the GTK theme, install qt6-gtk-platformtheme where offered;
Qt honours any already configured QT_QPA_PLATFORMTHEME. On KDE desktops,
the installed Qt/KDE integration typically supplies native styling.

A working Vulkan-capable graphics driver is required to use the GUI.
For a headless executable smoke test:

    ./Mesher --version

compiler-benchmark.json contains both GCC and Clang timings collected
on one GitHub Actions runner. The faster measured Release build is used
for the archive; no compiler choice is hard-coded.

Saving and opening interactive meshes:
    Ctrl+S opens a Qt-integrated Save As dialog for .mshr checkpoints.
    Ctrl+O opens the same style of file dialog, with a file-type selector
    for .mshr, OBJ, and All Files. The selected type is passed directly
    to the parser; All Files detects supported formats by content.
    Error messages use the same Qt dialog integration. No kdialog or
    zenity command or subprocess is required.
    The checkpoint write is queued asynchronously after selection.
