#!/usr/bin/env python3
"""Ratchet: plain buttons go through wormedit::ui::chrome::Button / chrome::button_label.

Counts raw nk_button_* calls per file under src/ui/nuklear (chrome/ implements the component and
is exempt) and compares them with tests/scripts/raw_button_baseline.txt. A file may use fewer raw
calls than its baseline, never more, and a file missing from the baseline may use none. Lower the
baseline whenever a count drops: run with --update.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).with_name("raw_button_baseline.txt")
PATTERN = re.compile(r"\bnk_button_(?:label|text|symbol|image|color)\w*\s*\(")


def count_raw_buttons() -> dict[str, int]:
    counts: dict[str, int] = {}
    for path in sorted((ROOT / "src/ui/nuklear").rglob("*")):
        if path.suffix not in (".cpp", ".h") or "chrome" in path.relative_to(ROOT / "src/ui/nuklear").parts:
            continue
        found = len(PATTERN.findall(path.read_text(encoding="utf-8")))
        if found:
            counts[path.relative_to(ROOT).as_posix()] = found
    return counts


def read_baseline() -> dict[str, int]:
    baseline: dict[str, int] = {}
    if BASELINE.exists():
        for line in BASELINE.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                name, _, number = line.rpartition(" ")
                baseline[name] = int(number)
    return baseline


def main() -> int:
    counts = count_raw_buttons()
    if "--update" in sys.argv:
        lines = ["# file raw-nk_button_*-call-count; may only go down (see check_raw_buttons.py)"]
        lines += [f"{name} {number}" for name, number in counts.items()]
        BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 0
    baseline = read_baseline()
    failures = [f"{name}: {number} raw nk_button_* calls, baseline {baseline.get(name, 0)}; use "
                f"chrome::button_label or chrome::Button" for name, number in counts.items()
                if number > baseline.get(name, 0)]
    for line in failures:
        print(line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
