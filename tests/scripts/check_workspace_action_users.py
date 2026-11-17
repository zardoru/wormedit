#!/usr/bin/env python3
"""Ratchet: a domain depends on its own request window, not on the whole workspace_action_t.

workspace_action_t (src/ui/frontend/actions.h) aggregates every domain's request for one frame. Code
that needs only one domain takes that domain's request (compose_request_t, metadata_edit_request_t,
timing_edit_request_t ...) and includes only its header, so a change to another domain neither
reaches it nor recompiles it, and the compiler stops it from writing another domain's request.

This lists the files under src/ and tests/ that name workspace_action_t and compares them with
tests/scripts/workspace_action_users_baseline.txt. A file missing from the baseline is a failure
(take the domain's request instead; only the frame root and a dispatcher of several domains need the
whole action); a baseline entry that no longer names the type is also a failure, so the list only
shrinks. Run with --update after narrowing a file.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).with_name("workspace_action_users_baseline.txt")
PATTERN = re.compile(r"\bworkspace_action_t\b")


def users() -> list[str]:
    found = []
    for base in ("src", "tests"):
        for path in sorted((ROOT / base).rglob("*")):
            if path.suffix not in (".h", ".cpp") or not path.is_file():
                continue
            if PATTERN.search(path.read_text(encoding="utf-8", errors="replace")):
                found.append(path.relative_to(ROOT).as_posix())
    return found


def main() -> int:
    current = users()
    if "--update" in sys.argv:
        BASELINE.write_text(
            "# files that name workspace_action_t; may only go down (see check_workspace_action_users.py)\n"
            + "".join(f"{name}\n" for name in current), encoding="utf-8")
        return 0
    allowed = {line.strip() for line in BASELINE.read_text(encoding="utf-8").splitlines()
               if line.strip() and not line.startswith("#")}
    added = [name for name in current if name not in allowed]
    removed = sorted(allowed - set(current))
    for name in added:
        print(f"{name}: names workspace_action_t; take the domain's request instead")
    for name in removed:
        print(f"{name}: no longer names workspace_action_t; run with --update to shrink the baseline")
    return 1 if added or removed else 0


if __name__ == "__main__":
    sys.exit(main())
