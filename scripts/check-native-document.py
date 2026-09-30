#!/usr/bin/env python3
"""Compare authored native JSON documents, reporting exact JSON Pointer differences."""
import argparse
import copy
import json
from pathlib import Path


def load_document(path, normalize):
    value = json.loads(path.read_text())
    document = value.get("document", value) if isinstance(value, dict) else value
    if not isinstance(document, dict):
        raise ValueError(f"{path}: expected a document object")
    document = copy.deepcopy(document)
    if normalize and type(document.get("revision")) is int and document["revision"] == 0:
        document["revision"] = 1
    return document


def differences(left, right, path=""):
    if type(left) is not type(right):
        yield path or "/", left, right
    elif isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            child = path + "/" + key.replace("~", "~0").replace("/", "~1")
            if key not in left or key not in right:
                yield child, left.get(key, "<missing>"), right.get(key, "<missing>")
            else:
                yield from differences(left[key], right[key], child)
    elif isinstance(left, list):
        for index in range(max(len(left), len(right))):
            child = f"{path}/{index}"
            if index >= len(left) or index >= len(right):
                yield child, left[index] if index < len(left) else "<missing>", right[index] if index < len(right) else "<missing>"
            else:
                yield from differences(left[index], right[index], child)
    elif left != right:
        yield path or "/", left, right


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--normalize-loaded-revision", action="store_true", help="Normalize only top-level document revision 0 to 1 in both inputs")
    parser.add_argument("--expected-note-count", type=int, help="Require this authored measure-note count in both inputs")
    args = parser.parse_args()
    try:
        documents = [load_document(path, args.normalize_loaded_revision) for path in (args.before, args.after)]
        mismatch = False
        for path, left, right in differences(*documents):
            print(f"DIFF {path}: {json.dumps(left, ensure_ascii=False)} -> {json.dumps(right, ensure_ascii=False)}")
            mismatch = True
        for path, document in zip((args.before, args.after), documents):
            count = sum(len(measure.get("notes", [])) for chart in document.get("charts", []) for measure in chart.get("measures", []))
            print(f"{path}: authored notes={count}")
            if args.expected_note_count is not None and count != args.expected_note_count:
                print(f"FAIL expected {args.expected_note_count} notes")
                mismatch = True
        print("FAIL documents differ or note count mismatches" if mismatch else "PASS authored documents match")
        return int(mismatch)
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
