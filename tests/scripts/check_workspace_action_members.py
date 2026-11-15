#!/usr/bin/env python3
"""Ratchet: workspace_action_t does not grow flat members; each domain owns its own request struct.

workspace_action_t (src/ui/frontend/actions.h) is the per-frame result of the UI. Flat members of one
domain (metadata_*, timing_*, ...) belong in that domain's request struct (for example
metadata_edit_request_t), held by one member, so a dispatcher can take just its request instead of
the whole frame action.

This lists the direct data members of workspace_action_t and compares them with
tests/scripts/workspace_action_member_baseline.txt. A member missing from the baseline is a failure
(add a field to a domain struct, or add a new struct); a baseline entry that no longer exists is also
a failure so the list shrinks as members move out. Run with --update after moving members.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/ui/frontend/actions.h"
BASELINE = pathlib.Path(__file__).with_name("workspace_action_member_baseline.txt")


def struct_body(source: str) -> str:
    start = source.index("struct workspace_action_t {") + len("struct workspace_action_t {")
    depth = 1
    index = start
    while depth:
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        index += 1
    return source[start:index - 1]


def members(body: str) -> list[str]:
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    names: list[str] = []
    statement = ""
    depth = 0
    for char in body:
        if char == "{" and depth == 0:
            if re.search(r"(\)|\bconst|\bnoexcept)\s*$", statement):  # inline function body
                depth = 1
                statement = ""
                continue
        if depth:
            depth += {"{": 1, "}": -1}.get(char, 0)
            continue
        if char == ";":
            names.extend(declared_names(statement))
            statement = ""
        else:
            statement += char
    return names


def declared_names(statement: str) -> list[str]:
    statement = re.sub(r"\{[^{}]*\}", "", statement)
    statement = re.sub(r"=[^,]*", "", statement)
    if "(" in statement:
        return []
    found = []
    for part in statement.split(","):
        match = re.search(r"([A-Za-z_]\w*)\s*$", part.strip())
        if match:
            found.append(match.group(1))
    return found


def read_baseline() -> list[str]:
    if not BASELINE.exists():
        return []
    return [line.strip() for line in BASELINE.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")]


def main() -> int:
    current = members(struct_body(HEADER.read_text(encoding="utf-8")))
    if len(set(current)) != len(current) or not current:
        print("workspace_action_t member scan found no members or duplicates; fix the checker")
        return 1
    if "--update" in sys.argv:
        BASELINE.write_text(
            "# flat workspace_action_t members; may only go down (see check_workspace_action_members.py)\n" +
            "\n".join(current) + "\n", encoding="utf-8")
        return 0
    baseline = set(read_baseline())
    added = [name for name in current if name not in baseline]
    stale = sorted(baseline - set(current))
    if added:
        print("New flat workspace_action_t members (put them in a domain request struct):")
        print("\n".join("  " + name for name in added))
    if stale:
        print("Baseline lists members that moved out; run check_workspace_action_members.py --update:")
        print("\n".join("  " + name for name in stale))
    return 1 if added or stale else 0


if __name__ == "__main__":
    sys.exit(main())
