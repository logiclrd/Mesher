MESHER FOR macOS (Apple Silicon arm64 or Intel x86_64)
=======================================================

Extract the ZIP in Finder, then open Mesher.app. Choose the archive
matching your Mac's architecture (Apple Silicon or Intel).

The .app contains the native Dock icon, all SPIR-V shaders, splash
artwork, MoltenVK (Vulkan over Metal), a Vulkan loader and required
non-system dynamic libraries. Homebrew and the Vulkan SDK are NOT
needed on the recipient's Mac.

IMPORTANT: these automated preview builds use AD-HOC code signing.
They are NOT Developer ID signed or notarized. macOS Gatekeeper may
block opening a downloaded app. If you trust the source, use the
standard macOS Finder / Privacy & Security "Open Anyway" controls.
Do not disable System Integrity Protection or Gatekeeper system-wide.

The CI process checks bundled library architectures, relocatable
linkage, the native icon and all required resources, verifies the
ad-hoc signature, and runs --version after ZIP extraction. It does
NOT prove GPU rendering on actual Intel or Apple Silicon hardware;
native GPU smoke tests remain a follow-up.

System requirement: macOS 13 or newer and a Metal-capable Mac.
The archives are architecture-specific (not Universal 2).
