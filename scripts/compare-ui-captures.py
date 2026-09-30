#!/usr/bin/env python3
"""Compare exact RGB pixels for paired captures or matching directory trees."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help="BEFORE AFTER pairs, or two directories with matching relative image names")
    args = parser.parse_args()
    if len(args.paths) % 2:
        parser.error("provide an even number of paired paths")
    try:
        from PIL import Image, ImageChops
    except ImportError:
        parser.exit(2, "error: Pillow is required; install it in your chosen Python environment before running this script\n")
    mismatch = False
    count = 0
    try:
        pairs = []
        for before, after in zip(args.paths[::2], args.paths[1::2]):
            if before.is_dir() and after.is_dir():
                extensions = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"}
                names = [{p.relative_to(directory) for p in directory.rglob("*") if p.is_file() and p.suffix.lower() in extensions} for directory in (before, after)]
                for name in sorted(names[0] ^ names[1]):
                    print(f"FAIL missing paired capture: {name}")
                    mismatch = True
                pairs.extend((before / name, after / name) for name in sorted(names[0] & names[1]))
            elif before.is_dir() or after.is_dir():
                raise ValueError("both paths in a pair must be directories or files")
            else:
                pairs.append((before, after))
        for before, after in pairs:
            count += 1
            with Image.open(before) as a, Image.open(after) as b:
                a, b = a.convert("RGB"), b.convert("RGB")
                if a.size != b.size:
                    print(f"FAIL {before} / {after}: dimensions {a.size} != {b.size}; pixel count and bbox unavailable")
                    mismatch = True
                    continue
                delta = ImageChops.difference(a, b)
                bbox = delta.getbbox()
                channels = delta.tobytes()
                pixels = sum(any(channels[index:index + 3]) for index in range(0, len(channels), 3))
                print(f"{'FAIL' if bbox else 'PASS'} {before} / {after}: different pixels={pixels}, bbox={bbox}")
                mismatch |= bbox is not None
        if not count:
            raise ValueError("no paired captures found")
        print(f"Compared {count} capture pairs")
        return int(mismatch)
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
