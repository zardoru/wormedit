#!/usr/bin/env python3
"""Fail closed on external Mach-O load paths, missing files or wrong CPU slices."""
import pathlib
import re
import subprocess
import sys


def output(*command):
    return subprocess.check_output(command, text=True).strip()


def verify_reference(reference, binary, app):
    if reference.startswith(("/usr/lib/", "/System/Library/")):
        return
    roots = {"@executable_path/": app / "Contents/MacOS", "@loader_path/": binary.parent}
    for prefix, root in roots.items():
        if reference.startswith(prefix):
            resolved = (root / reference[len(prefix):]).resolve()
            if resolved.is_file() and resolved.is_relative_to(app):
                return
    raise RuntimeError(f"{binary}: external or unresolved load path: {reference}")


def verify_binary(binary, app, arch):
    architectures = output("lipo", "-archs", str(binary)).split()
    if arch not in architectures:
        raise RuntimeError(f"{binary}: expected {arch}, found {architectures}")
    dependencies = output("otool", "-L", str(binary))
    print(dependencies)
    for reference in re.findall(r"^\s+(.+?) \(compatibility version", dependencies, re.MULTILINE):
        verify_reference(reference, binary, app)
    commands = output("otool", "-l", str(binary))
    for reference in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", commands):
        # fixup_bundle removes rpaths. No ambient Homebrew search path may survive.
        raise RuntimeError(f"{binary}: unexpected residual rpath: {reference}")


def main():
    app = pathlib.Path(sys.argv[1]).resolve(strict=True)
    arch = sys.argv[2]
    executable = app / "Contents/MacOS/Wormedit"
    if not executable.is_file() or "Mach-O" not in output("file", "-b", str(executable)):
        raise RuntimeError(f"missing Mach-O executable: {executable}")
    for path in sorted(app.rglob("*")):
        if path.is_symlink() and not path.resolve().is_relative_to(app):
            raise RuntimeError(f"symlink escapes bundle: {path}")
        if path.is_file() and not path.is_symlink() and "Mach-O" in output("file", "-b", str(path)):
            verify_binary(path, app, arch)
    print(f"Verified bundle load paths and {arch} slices: {app.name}")


if __name__ == "__main__":
    main()
