#!/usr/bin/env python3
"""Prepare an unpacked Wormedit release for the gh-pages root."""

import argparse
import hashlib
from pathlib import Path
import shutil


def stage(package: Path, output: Path, revision: str) -> None:
    browser_script = Path(__file__).resolve().parents[1] / "packaging" / "wasm"
    entry_point = (package / "index.html").read_text(encoding="utf-8")
    wasm = package / "wormedit.wasm"
    with wasm.open("rb") as stream:
        fingerprint = hashlib.file_digest(stream, "sha256").hexdigest()
    version = f"{revision[:12]}-{fingerprint[:12]}"

    entry_point = entry_point.replace(
        '<script src="fflate.js"></script>',
        '<script src="enable-isolation.js"></script>\n<script src="fflate.js"></script>',
    )
    anchor = "    if (typeof WebAssembly"
    if entry_point.count(anchor) != 1 or entry_point.count('src="enable-isolation.js"') != 1:
        raise ValueError("Unexpected browser entry point in release archive")
    entry_point = entry_point.replace(anchor, "    if (!await enableWormeditIsolation()) return;\n" + anchor)
    for script in ("fflate.js", "editor-files.js", "editor-assets.js", "wormedit.js"):
        reference = f'src="{script}"'
        if entry_point.count(reference) != 1:
            raise ValueError(f"Missing or duplicate script in browser entry point: {script}")
        entry_point = entry_point.replace(reference, f'src="{script}?v={version}"')
    anchor = "      editorStatus,"
    if entry_point.count(anchor) != 1:
        raise ValueError("Missing module options in browser entry point")
    entry_point = entry_point.replace(
        anchor, anchor + f'\n      locateFile: (file, prefix) => prefix + file + "?v={version}",'
    )

    shutil.copytree(package, output, ignore=shutil.ignore_patterns("serve-wasm.py"))
    for script in ("enable-isolation.js", "isolation-worker.js"):
        shutil.copyfile(browser_script / script, output / script)
    (output / "index.html").write_text(entry_point, encoding="utf-8")
    (output / ".nojekyll").touch()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("revision")
    arguments = parser.parse_args()
    stage(arguments.package, arguments.output, arguments.revision)
