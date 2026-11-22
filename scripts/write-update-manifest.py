#!/usr/bin/env python3
"""Write latest.json, the update manifest the editor reads (schema 1, stable channel)."""

import argparse
import json
from pathlib import Path
import re

VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+[-.0-9A-Za-z]*$")


def manifest(tag: str, repository: str, published: str) -> dict:
    version = tag.removeprefix("v")
    if not VERSION.match(version):
        raise ValueError(f"Unsupported release version: {tag}")
    page = f"https://github.com/{repository}/releases/tag/{tag}"
    return {
        "schema": 1,
        "channels": {
            "stable": {"version": version, "url": page, "notes_url": page, "published": published[:10]},
        },
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--published", required=True, help="ISO 8601 timestamp of the release")
    arguments = parser.parse_args()
    arguments.output.write_text(
        json.dumps(manifest(arguments.tag, arguments.repository, arguments.published), indent=2) + "\n",
        encoding="utf-8",
    )
