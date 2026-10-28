#!/usr/bin/env python3
"""Assemble a relocatable Linux Mesher archive from the chosen Release build."""
import argparse
import os
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

BUNDLED_LIBRARIES = ("libglfw.so", "libqhull_r.so", "libgomp.so", "libomp.so")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("packages"))
    args = parser.parse_args()
    name = f"Mesher-{args.version}-linux-x86_64"
    stage = args.output / name
    stage.mkdir(parents=True, exist_ok=True)
    executable = args.build / "Mesher"
    if not executable.is_file():
        raise FileNotFoundError(executable)
    shutil.copy2(executable, stage / "Mesher")
    (stage / "Mesher").chmod(0o755)
    for subdir, pattern in (("shaders", "*.spv"), ("images", "*.rgba")):
        origin = args.build / subdir
        files = sorted(origin.glob(pattern))
        if not files:
            raise ValueError(f"Missing {pattern} assets at {origin}")
        dest = stage / subdir
        dest.mkdir(exist_ok=True)
        for file in files:
            shutil.copy2(file, dest / file.name)
    for item in ("VERSION", "README.md"):
        shutil.copy2(item, stage / item)
    shutil.copy2(args.selection, stage / "compiler-benchmark.json")
    shutil.copy2("tools/release/README-linux.txt", stage / "RUNNING.txt")
    shutil.copy2("images/Icon.png", stage / "Icon.png")

    # Bundle the Mesher-specific non-platform shared dependencies. Leave
    # the libc/graphics-driver/Vulkan loader stack to the host OS.
    dependencies = subprocess.run(["ldd", str(executable)],
                                  capture_output=True, text=True, check=True)
    if "not found" in dependencies.stdout:
        raise RuntimeError("Unresolved Linux library:\n" + dependencies.stdout)
    libs = stage / "lib"
    for line in dependencies.stdout.splitlines():
        match = re.search(r"^\s*(\S+)\s+=>\s+(/\S+)\s+\(", line)
        if not match:
            continue
        dependency_name, dependency_path = match.groups()
        if dependency_name.startswith(BUNDLED_LIBRARIES):
            libs.mkdir(exist_ok=True)
            shutil.copy2(dependency_path, libs / dependency_name)
    # Use the local bundled libs regardless of current working directory.
    subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/lib", str(stage / "Mesher")],
                   check=True)
    # Release CLI probes must run without opening an X11/Vulkan window.
    result = subprocess.run([str((stage / "Mesher").resolve()), "--version"],
                            text=True, capture_output=True, check=True)
    if result.stdout.strip() != f"Mesher {args.version}":
        raise RuntimeError(f"Packaged binary has wrong version: {result.stdout!r}")
    args.output.mkdir(exist_ok=True)
    archive = args.output / f"{name}.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(stage, arcname=stage.name)
    print(f"Linux package: {archive} ({archive.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
