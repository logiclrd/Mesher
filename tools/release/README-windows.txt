MESHER FOR WINDOWS (x64)
========================

Extract the entire ZIP file and start Mesher.exe. Do not run the
executable from inside an unopened ZIP archive.

The ZIP contains the Mesher.exe Windows icon, required vcpkg-built DLLs,
Vulkan SPIR-V shaders and UI imagery under images/. Keep all files in
their original relative paths.

Requires Windows 10/11 x64, an installed Vulkan-capable graphics driver
and the Microsoft Visual C++ 2015-2022 x64 Redistributable if not already
installed. The graphics driver or Vulkan runtime provides vulkan-1.dll;
it is intentionally not replaced by an SDK DLL.

For a no-GPU smoke test:

    Mesher.exe --version

The version displayed must match VERSION in this directory.
