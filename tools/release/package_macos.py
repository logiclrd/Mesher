#!/usr/bin/env python3
"""Produce a relocatable, ad-hoc-signed native Mesher.app release ZIP.

Runs on native Apple Clang macOS runners. No Developer ID credentials are
assumed: packages are NOT notarized, and Gatekeeper may prompt at first use.
All non-system .dylib dependencies and MoltenVK's ICD are bundled so the
recipient does not need Homebrew, the Vulkan SDK, or environment variables.
"""
import argparse
import json
import os
import plistlib
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/", "/Library/Apple/System/")


def execute(*args, capture=False):
    return subprocess.run([str(value) for value in args], check=True,
                          text=True, capture_output=capture)


def brew_prefix(package=None):
    args = ["brew", "--prefix"]
    if package:
        args.append(package)
    return Path(execute(*args, capture=True).stdout.strip())


def dylib_dependencies(binary):
    result = execute("otool", "-L", binary, capture=True).stdout.splitlines()
    return [line.strip().split(" (", 1)[0]
            for line in result[1:] if " (" in line]


def find_library(name, source, executable, roots):
    """Resolve a linked install name without depending on the user's host."""
    if name.startswith(SYSTEM_PREFIXES):
        return None
    if name.startswith("/"):
        candidate = Path(name)
        if not candidate.exists():
            raise RuntimeError(f"Missing linked library {name} in {source}")
        return candidate
    if name.startswith("@loader_path/"):
        candidate = source.parent / name[len("@loader_path/"):]
        if candidate.exists():
            return candidate
    if name.startswith("@executable_path/"):
        candidate = executable.parent / name[len("@executable_path/"):]
        if candidate.exists():
            return candidate
    basename = name.rsplit("/", 1)[-1]
    candidates = [source.parent / basename]
    for root in roots:
        candidates += [root / "lib" / basename, root / basename]
    candidates += [Path("/usr/lib") / basename]
    for candidate in candidates:
        if candidate.exists():
            if str(candidate).startswith(SYSTEM_PREFIXES):
                return None
            return candidate
    raise RuntimeError(f"Unable to resolve {name} linked by {source}")


def embed_libraries(executable, frameworks, moltenvk):
    roots = [brew_prefix()]
    roots += [brew_prefix(formula) for formula in
              ("glfw", "qhull", "vulkan-loader", "molten-vk")]
    frameworks.mkdir(parents=True, exist_ok=True)
    sources = {executable: executable}
    pending = [executable]
    # The MoltenVK ICD is loaded dynamically, not listed in Mesher's otool.
    pending.append(moltenvk)
    molten_dest = frameworks / "libMoltenVK.dylib"
    shutil.copy2(moltenvk, molten_dest)
    sources[molten_dest] = moltenvk

    while pending:
        source = pending.pop()
        destination = next((path for path, original in sources.items()
                            if original == source), None)
        if destination is None:
            raise RuntimeError(f"Missing destination for {source}")
        for dependency in dylib_dependencies(source):
            # otool -L reports the dylib's own install name first, too.
            if source != executable and dependency == dylib_dependencies(source)[0]:
                continue
            original = find_library(dependency, source, executable, roots)
            if original is None:
                continue
            target = frameworks / dependency.rsplit("/", 1)[-1]
            if target not in sources:
                if target.exists():
                    raise RuntimeError(f"Ambiguous library basename {target.name}")
                shutil.copy2(original, target)
                sources[target] = original
                pending.append(original)

    # Rewire *all* non-system imports to copies inside Contents/Frameworks.
    for destination, source in sources.items():
        dependencies = dylib_dependencies(source)
        for index, dependency in enumerate(dependencies):
            if source != executable and index == 0:
                continue  # Own LC_ID_DYLIB; changed via -id below.
            original = find_library(dependency, source, executable, roots)
            if original is None:
                continue
            new_name = "@rpath/" + dependency.rsplit("/", 1)[-1]
            if dependency != new_name:
                execute("install_name_tool", "-change", dependency,
                        new_name, destination)
        if destination != executable:
            execute("install_name_tool", "-id",
                    "@rpath/" + destination.name, destination)
            # @loader_path gives each copied dylib a resolution path for
            # transitive dependencies even outside launch services.
            execute("install_name_tool", "-add_rpath", "@loader_path", destination)
    execute("install_name_tool", "-add_rpath",
            "@executable_path/../Frameworks", executable)
    return list(sources)


def create_icon(source, resources):
    iconset = resources / "Mesher.iconset"
    iconset.mkdir()
    sizes = ((16, "icon_16x16.png"), (32, "icon_16x16@2x.png"),
             (32, "icon_32x32.png"), (64, "icon_32x32@2x.png"),
             (128, "icon_128x128.png"), (256, "icon_128x128@2x.png"),
             (256, "icon_256x256.png"), (512, "icon_256x256@2x.png"),
             (512, "icon_512x512.png"), (1024, "icon_512x512@2x.png"))
    try:
        for size, name in sizes:
            execute("sips", "-s", "format", "png", "-z", size, size,
                    source, "--out", iconset / name)
        execute("iconutil", "-c", "icns", iconset,
                "-o", resources / "Mesher.icns")
    finally:
        shutil.rmtree(iconset)
    if not (resources / "Mesher.icns").is_file():
        raise RuntimeError("Native Dock icon generation failed")


def validate_bundle(app, version, arch):
    contents = app / "Contents"
    executable = contents / "MacOS" / "Mesher"
    frameworks = contents / "Frameworks"
    resources = contents / "Resources"
    required = [executable, resources / "Mesher.icns",
                resources / "shaders" / "mesh.vert.spv",
                resources / "shaders" / "mesh.frag.spv",
                resources / "shaders" / "hud.vert.spv",
                resources / "shaders" / "hud.frag.spv",
                resources / "images" / "Icon.rgba",
                resources / "images" / "Splash.rgba",
                resources / "vulkan" / "icd.d" / "MoltenVK_icd.json",
                frameworks / "libMoltenVK.dylib"]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError(f"Bundle missing required files: {missing}")
    with (contents / "Info.plist").open("rb") as file:
        plist = plistlib.load(file)
    if plist["CFBundleExecutable"] != "Mesher" or plist["CFBundleIconFile"] != "Mesher.icns":
        raise RuntimeError("Invalid app bundle metadata")
    numeric = re.match(r"^[0-9]+(?:[.][0-9]+)*", version)
    if not numeric or plist["CFBundleShortVersionString"] != numeric.group(0):
        raise RuntimeError("Wrong bundle version")
    actual = execute(executable, "--version", capture=True).stdout.strip()
    if actual != f"Mesher {version}":
        raise RuntimeError(f"Packaged version mismatch: {actual!r}")
    icd = json.loads(required[8].read_text(encoding="utf-8"))
    if icd.get("ICD", {}).get("library_path") != "../../../Frameworks/libMoltenVK.dylib":
        raise RuntimeError("MoltenVK ICD points outside bundle")
    if icd["ICD"].get("is_portability_driver") is not True:
        raise RuntimeError("MoltenVK ICD requires portability declaration")
    # All copied dynamic dependencies must be native architecture and
    # have no unresolved Homebrew references (rpaths alone are not enough).
    for binary in [executable, *sorted(frameworks.glob("*.dylib"))]:
        architectures = execute("lipo", "-archs", binary, capture=True).stdout.split()
        if arch not in architectures:
            raise RuntimeError(f"{binary} lacks {arch}: {architectures}")
        for linked in dylib_dependencies(binary):
            if linked.startswith(("/opt/homebrew/", "/usr/local/", "/opt/local/")):
                raise RuntimeError(f"Non-relocatable library: {binary}: {linked}")
    execute("codesign", "--verify", "--deep", "--strict", app)
    print(f"Validated native {arch} Mesher.app v{version}: "
          f"{len(list(frameworks.glob('*.dylib')))} bundled dylibs")


def package(args):
    build = args.build.resolve()
    app = (build / "Mesher.app").resolve()
    if not app.is_dir():
        raise RuntimeError(f"CMake must build a native Mesher.app: {app}")
    if execute("uname", "-m", capture=True).stdout.strip() != args.arch:
        raise RuntimeError(f"Not running on a native {args.arch} runner")
    contents = app / "Contents"
    executable = contents / "MacOS" / "Mesher"
    resources = contents / "Resources"
    resources.mkdir(parents=True, exist_ok=True)
    for subdir, extension in (("images", "*.rgba"), ("shaders", "*.spv")):
        source = build / subdir
        matches = list(source.glob(extension))
        if not matches:
            raise RuntimeError(f"Build missing {subdir}/{extension}")
        target = resources / subdir
        target.mkdir(exist_ok=True)
        for file in matches:
            shutil.copy2(file, target / file.name)
    # Prevent fallback into Contents/MacOS/images after packaging. All
    # runtime artwork belongs to Contents/Resources and is resolved there.
    shutil.rmtree(contents / "MacOS" / "images", ignore_errors=True)
    create_icon(Path("images/Icon.png").resolve(), resources)
    frameworks = contents / "Frameworks"
    molten_home = brew_prefix("molten-vk")
    mol = molten_home / "lib" / "libMoltenVK.dylib"
    if not mol.is_file():
        raise FileNotFoundError(mol)
    embed_libraries(executable, frameworks, mol)
    source_icd = molten_home / "etc" / "vulkan" / "icd.d" / "MoltenVK_icd.json"
    if not source_icd.is_file():
        raise FileNotFoundError(source_icd)
    icd = json.loads(source_icd.read_text(encoding="utf-8"))
    icd.setdefault("ICD", {})["library_path"] = "../../../Frameworks/libMoltenVK.dylib"
    icd["ICD"]["is_portability_driver"] = True
    icd_directory = resources / "vulkan" / "icd.d"
    icd_directory.mkdir(parents=True, exist_ok=True)
    (icd_directory / "MoltenVK_icd.json").write_text(
        json.dumps(icd, indent=2) + "\n", encoding="utf-8")
    shutil.copy2("tools/release/README-macos.txt", resources / "RUNNING.txt")
    shutil.copy2("VERSION", resources / "VERSION")

    # Modified Homebrew bottles are no longer signed by their original
    # authors. Explicitly ad-hoc sign nested code first, then the bundle.
    for file in sorted(frameworks.glob("*.dylib")):
        execute("codesign", "--force", "--sign", "-", file)
    execute("codesign", "--force", "--sign", "-", app)
    validate_bundle(app, args.version, args.arch)
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f"Mesher-{args.version}-macos-{args.arch}.zip"
    execute("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive)
    # Re-extract and validate the actual distributable rather than merely
    # checking the original staging directory.
    with tempfile.TemporaryDirectory() as temp:
        execute("ditto", "-x", "-k", archive, temp)
        validate_bundle(Path(temp) / "Mesher.app", args.version, args.arch)
    print(f"Created {archive} ({archive.stat().st_size:,} bytes)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=Path("build-macos-release"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    parser.add_argument("--output", type=Path, default=Path("packages"))
    args = parser.parse_args()
    package(args)


if __name__ == "__main__":
    main()
