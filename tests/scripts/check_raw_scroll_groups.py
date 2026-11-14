#!/usr/bin/env python3
"""Ratchet: scrolling groups go through chrome::scroll_group_begin / chrome::scroll_list_view_begin.

Nuklear reserves the scrollbar's width (and the footer strip's height) inside every group and list
view that does not set NK_WINDOW_NO_SCROLLBAR, whether or not the content overflows, so short lists
show an empty gutter. The wrappers in src/ui/nuklear/chrome/scroll_group.h drop the scrollbar while
the content fits.

This counts raw nk_group_begin / nk_group_begin_titled / nk_group_scrolled_* / nk_list_view_begin
calls per file under src/ui/nuklear (chrome/ implements the wrappers and is exempt) and compares
them with tests/scripts/raw_scroll_group_baseline.txt. Calls that pass NK_WINDOW_NO_SCROLLBAR are
fixed-size containers and do not count; a call that must stay raw for another reason can carry a
`// scroll-ok: <why>` comment on its line. A file may use fewer raw calls than its baseline, never
more, and a file missing from the baseline may use none. Lower the baseline whenever a count
drops: run with --update.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).with_name("raw_scroll_group_baseline.txt")
PATTERN = re.compile(r"\bnk_(?:group_begin(?:_titled)?|group_scrolled_begin|group_scrolled_offset_begin|list_view_begin)\s*\(")


def call_text(source: str, start: int) -> str:
    """Text of the call whose opening parenthesis is the last character before `start`."""
    depth = 1
    index = start
    while index < len(source) and depth:
        depth += {"(": 1, ")": -1}.get(source[index], 0)
        index += 1
    return source[start:index]


def count_raw_groups() -> dict[str, int]:
    counts: dict[str, int] = {}
    for path in sorted((ROOT / "src/ui/nuklear").rglob("*")):
        if path.suffix not in (".cpp", ".h") or "chrome" in path.relative_to(ROOT / "src/ui/nuklear").parts:
            continue
        source = path.read_text(encoding="utf-8")
        found = 0
        for match in PATTERN.finditer(source):
            line_end = source.find("\n", match.end())
            line = source[source.rfind("\n", 0, match.start()) + 1:line_end if line_end >= 0 else len(source)]
            if "scroll-ok" in line or "NO_SCROLLBAR" in call_text(source, match.end()):
                continue
            found += 1
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
    counts = count_raw_groups()
    if "--update" in sys.argv:
        lines = ["# file raw-scrolling-group-call-count; may only go down (see check_raw_scroll_groups.py)"]
        lines += [f"{name} {number}" for name, number in counts.items()]
        BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 0
    baseline = read_baseline()
    failures = [f"{name}: {number} raw scrolling group calls, baseline {baseline.get(name, 0)}; use "
                f"chrome::scroll_group_begin/end or chrome::scroll_list_view_begin/end (or pass "
                f"NK_WINDOW_NO_SCROLLBAR for a fixed container)"
                for name, number in counts.items() if number > baseline.get(name, 0)]
    for line in failures:
        print(line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
