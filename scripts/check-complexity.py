#!/usr/bin/env python3
"""Compare clang-tidy cognitive complexity with the reviewed per-function limits."""

import argparse
import concurrent.futures
from dataclasses import asdict, dataclass
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
THRESHOLD = 25
CHECK = "readability-function-cognitive-complexity"
CONFIG = {"Checks": "-*," + CHECK, "CheckOptions": {
    CHECK + ".Threshold": THRESHOLD, CHECK + ".DescribeBasicIncrements": False}}


@dataclass(frozen=True)
class Finding:
    file: str
    function: str
    name: str
    score: int

    @property
    def key(self):
        return self.file, self.function


def read_baseline(path):
    metadata, findings = None, {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if line.startswith("# metadata: "):
            metadata = json.loads(line.removeprefix("# metadata: "))
        if not line or line.startswith("#"):
            continue
        data = json.loads(line)
        if (not isinstance(data, dict) or set(data) != {"file", "function", "name", "score"} or
                any(not isinstance(data[key], str) for key in ("file", "function", "name"))):
            raise ValueError(f"Invalid baseline row {path}:{number}")
        row = Finding(**data)
        relative = Path(row.file)
        if (relative.is_absolute() or ".." in relative.parts or
                not row.function or not row.name or type(row.score) is not int or
                row.score <= THRESHOLD or row.key in findings):
            raise ValueError(f"Invalid or duplicate baseline row {path}:{number}")
        findings[row.key] = row
    if (not isinstance(metadata, dict) or metadata.get("format") != 1 or
            metadata.get("threshold") != THRESHOLD or not metadata.get("clang_tidy_version")):
        raise ValueError("Missing or incompatible complexity baseline metadata")
    return metadata, findings


def comparison(findings, baseline, checked_files, complete=False):
    failures, decreases = [], []
    for key, row in sorted(findings.items()):
        previous = baseline.get(key)
        if previous is None or row.score > previous.score:
            limit = previous.score if previous else THRESHOLD
            failures.append(f"{row.file}: {row.name}: {row.score} > {limit}")
        elif row.score < previous.score:
            decreases.append(f"{row.file}: {row.name}: {previous.score} -> {row.score}")
    for key, row in sorted(baseline.items()):
        if key not in findings and (complete or row.file in checked_files):
            decreases.append(f"{row.file}: {row.name}: no longer reported above {THRESHOLD}")
    return failures, decreases


def compile_database(build):
    entries = json.loads((build / "compile_commands.json").read_text())
    by_file = {}
    for entry in entries:
        file = Path(entry["file"])
        if not file.is_absolute():
            file = Path(entry["directory"]) / file
        file = file.resolve()
        if file.suffix == ".cpp" and is_owned(file):
            by_file.setdefault(file, []).append(entry)
    return by_file


def is_owned(file):
    return any(file.is_relative_to(ROOT / directory) for directory in ("src", "include", "tests"))


def changed_cpp(base):
    diff = subprocess.check_output(
        ["git", "diff", "--name-only", "--diff-filter=ACMRD", "-z", base, "--"], cwd=ROOT)
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "-z"], cwd=ROOT)
    return {ROOT / os.fsdecode(file) for file in (diff + untracked).split(b"\0")
            if file and Path(os.fsdecode(file)).suffix == ".cpp"}


def compiler_arguments(entry, resource):
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    result, skip = [], False
    for argument in arguments[1:]:
        if skip:
            skip = False
        elif argument == "-o":
            skip = True
        elif argument not in ("-c", entry["file"]):
            result.append(argument)
    return result + ["-Wno-unknown-warning-option", "-working-directory=" + entry["directory"],
                     "-resource-dir=" + resource]


def token_text(tu, file, start, end, clang):
    return " ".join(token.spelling for token in tu.get_tokens(extent=tu.get_extent(file, (start, end)))
                    if token.kind != clang.TokenKind.COMMENT)


def cursor_path(cursor, file, offset):
    for child in cursor.get_children():
        extent = child.extent
        if (extent.start.file and str(extent.start.file) == file and
                extent.start.offset <= offset < extent.end.offset):
            return [child] + cursor_path(child, file, offset)
    return []


def function_kinds(clang):
    kinds = clang.CursorKind
    return {kinds.FUNCTION_DECL, kinds.CXX_METHOD, kinds.CONSTRUCTOR, kinds.DESTRUCTOR,
            kinds.CONVERSION_FUNCTION, kinds.FUNCTION_TEMPLATE}


def lambda_context(cursor, tu, clang):
    file, offset = str(cursor.location.file), cursor.extent.start.offset
    path = cursor_path(tu.cursor, file, offset)
    owners = [(i, node) for i, node in enumerate(path) if node.kind in function_kinds(clang)]
    if not owners:
        raise ValueError("lambda has no named enclosing function")
    index, owner = owners[-1]
    function, name = function_identity(owner, tu, clang)
    anchors = []
    for node in path[index + 1:]:
        if node == cursor:
            break
        if node.kind == clang.CursorKind.VAR_DECL:
            anchors.append("var:" + node.spelling)
        if node.kind in (clang.CursorKind.CALL_EXPR, clang.CursorKind.INIT_LIST_EXPR):
            anchors.append("context:" + token_text(tu, file, node.extent.start.offset, offset, clang))
    body = next((node for node in cursor.get_children()
                 if node.kind == clang.CursorKind.COMPOUND_STMT), None)
    if not anchors or body is None:
        raise ValueError("lambda has no stable variable/call context and body")
    signature = token_text(tu, file, offset, body.extent.start.offset, clang)
    return ("lambda:" + function + ":" + json.dumps(anchors) + ":" + signature,
            name + " / lambda " + ", ".join(anchors) + " " + signature, owner)


def lambda_nodes(cursor, file, clang):
    for child in cursor.get_children():
        source = child.extent.start.file
        if source and str(source) != file:
            continue
        if child.kind == clang.CursorKind.LAMBDA_EXPR:
            yield child
        yield from lambda_nodes(child, file, clang)


def lambda_identity(cursor, tu, clang):
    identity, name, owner = lambda_context(cursor, tu, clang)
    positions = set()
    # Check every sibling declaration, including those below the warning threshold.
    # Otherwise a new callback could borrow an old one's budget after that old
    # callback was simplified and stopped producing a diagnostic.
    for candidate in lambda_nodes(owner, str(cursor.location.file), clang):
        try:
            candidate_identity, _, _ = lambda_context(candidate, tu, clang)
        except ValueError:
            continue
        if candidate_identity == identity:
            positions.add(candidate.extent.start.offset)
    if positions != {cursor.extent.start.offset}:
        raise ValueError("ambiguous lambda context: " + name)
    return identity, name


def function_identity(cursor, tu, clang):
    # An instantiated template can contain a lambda's source offset in its USR.
    # Use its source template, but keep explicitly specialized definitions distinct.
    while cursor.specialized_template:
        primary = cursor.specialized_template
        if primary.location != cursor.location:
            break
        cursor = primary
    if cursor.kind == clang.CursorKind.LAMBDA_EXPR:
        return lambda_identity(cursor, tu, clang)
    if cursor.kind in function_kinds(clang):
        at = clang.Cursor.from_location(tu, cursor.location)
        if at.kind == clang.CursorKind.MACRO_INSTANTIATION:
            cursor = at
    if cursor.kind == clang.CursorKind.MACRO_INSTANTIATION:
        literals = [token.spelling for token in cursor.get_tokens()
                    if token.kind == clang.TokenKind.LITERAL and '"' in token.spelling]
        if not literals:
            raise ValueError("macro has no stable literal identity: " + cursor.spelling)
        return ("macro:" + cursor.spelling + ":" + ":".join(literals),
                cursor.spelling + " " + literals[0])
    if cursor.kind not in function_kinds(clang):
        raise ValueError("not an attributable function: " + str(cursor.kind))
    identity = cursor.get_usr()
    if not identity or re.search(r"@\d+@", identity):
        raise ValueError("unstable or missing function identity: " + identity)
    parts, parent = [], cursor
    while parent and parent.kind != clang.CursorKind.TRANSLATION_UNIT:
        parts.append(parent.displayname or "<anonymous>")
        parent = parent.semantic_parent
    return identity, "::".join(reversed(parts))


def attribute_diagnostics(file, diagnostics, entries, resource, clang):
    pending = list(diagnostics)
    rows, errors = [], []
    for entry in entries:
        tu = clang.Index.create().parse(str(file), args=compiler_arguments(entry, resource),
                                       options=clang.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
        ast_errors = [str(d) for d in tu.diagnostics if d.severity >= clang.Diagnostic.Error]
        if ast_errors:
            errors = ast_errors
            continue
        unresolved = []
        for diagnostic in pending:
            message = diagnostic["DiagnosticMessage"]
            try:
                location = tu.get_location(message["FilePath"], message["FileOffset"])
                cursor = clang.Cursor.from_location(tu, location)
                identity, name = function_identity(cursor, tu, clang)
                score = re.search(r"complexity of (\d+) \(threshold 25\)", message["Message"])
                if score is None:
                    raise ValueError("unrecognized complexity diagnostic: " + message["Message"])
                row = Finding(str(Path(message["FilePath"]).relative_to(ROOT)), identity, name, int(score[1]))
                rows.append((row, message["FileOffset"]))
            except ValueError as error:
                errors = [str(error)]
                unresolved.append(diagnostic)
        pending = unresolved
        if not pending:
            return rows
    details = []
    for diagnostic in pending:
        message = diagnostic["DiagnosticMessage"]
        source = Path(message["FilePath"])
        line = source.read_bytes()[:message["FileOffset"]].count(b"\n") + 1
        details.append(f"{source.relative_to(ROOT)}:{line}: {message['Message']}")
    raise ValueError("new/unattributed finding(s), no baseline allowance: " +
                     "; ".join(details + errors))


def inspect_file(item, build, entries, resource, log_dir, tidy, yaml, clang):
    number, file = item
    fixes = log_dir / f"{number:04}.yaml"
    # GCC build directories carry GCC-only warning flags that Clang does not know.
    command = [tidy, "-p", str(build), str(file), "--config=" + json.dumps(CONFIG),
               "--extra-arg=-Wno-unknown-warning-option", "--export-fixes=" + str(fixes)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=300)
    output = result.stdout + result.stderr
    (log_dir / f"{number:04}.log").write_text(output)
    if result.returncode:
        raise ValueError(f"clang-tidy failed for {file.relative_to(ROOT)}:\n" + output[-3000:])
    diagnostics = (yaml.safe_load(fixes.read_text()) or {}).get("Diagnostics", []) if fixes.exists() else []
    if any(d["DiagnosticName"] == "clang-diagnostic-error" for d in diagnostics):
        raise ValueError("Compiler error in " + str(file))
    diagnostics = [d for d in diagnostics if d["DiagnosticName"] == CHECK and
                   is_owned(Path(d["DiagnosticMessage"]["FilePath"]))]
    return attribute_diagnostics(file, diagnostics, entries[file], resource, clang) if diagnostics else []


def merge_findings(rows):
    findings, positions = {}, {}
    for row, offset in rows:
        if row.key in positions and positions[row.key] != offset:
            raise ValueError(f"Ambiguous function identity in {row.file}: {row.name}")
        positions[row.key] = offset
        if row.key not in findings or row.score > findings[row.key].score:
            findings[row.key] = row
    return findings


def scan(files, entries, args, metadata):
    import yaml
    from clang import cindex

    version = subprocess.check_output([args.clang_tidy, "--version"], text=True)
    match = re.search(r"LLVM version ([^\s]+)", version)
    if not match or match[1] != metadata["clang_tidy_version"]:
        raise ValueError("clang-tidy version differs from the baseline; review a complete new measurement")
    resource = subprocess.check_output([args.clang, "-print-resource-dir"], text=True).strip()
    log_dir = Path(tempfile.mkdtemp(prefix="wormedit-complexity."))
    print(f"complexity: {len(files)} translation units, {args.jobs} jobs; logs in {log_dir}", flush=True)
    rows, errors = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(inspect_file, item, args.build_dir, entries, resource, log_dir,
                               args.clang_tidy, yaml, cindex) for item in enumerate(files)]
        for number, future in enumerate(concurrent.futures.as_completed(futures), 1):
            try:
                rows.extend(future.result())
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                errors.append(str(error))
                print("complexity: ERROR: " + str(error), flush=True)
            if number % 25 == 0 or number == len(files):
                print(f"complexity: checked {number}/{len(files)}", flush=True)
    if errors:
        raise ValueError(f"{len(errors)} translation unit(s) failed; see {log_dir}")
    return merge_findings(rows)


def options():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path, help="explicit compiled .cpp files (also for header consumers)")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--baseline", type=Path, default=ROOT / "cmake/complexity_baseline.txt")
    parser.add_argument("--base-ref", default="HEAD", help="git reference for changed-file discovery")
    parser.add_argument("--all", action="store_true", help="check every compiled first-party C++ unit")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--clang", default="clang", help="matching Clang driver for libclang resource headers")
    parser.add_argument("--write-baseline", type=Path, help="write a complete candidate elsewhere; does not accept increases")
    args = parser.parse_args()
    if args.jobs < 1 or (args.all and args.files):
        parser.error("jobs must be positive; choose explicit files or --all")
    if args.write_baseline and (not args.all or args.write_baseline.resolve() == args.baseline.resolve()):
        parser.error("--write-baseline requires --all and a separate candidate path")
    args.build_dir = args.build_dir.resolve()
    return args


def main():
    args = options()
    metadata, baseline = read_baseline(args.baseline)
    entries = compile_database(args.build_dir)
    requested = set(entries) if args.all else ({p.resolve() for p in args.files} if args.files else changed_cpp(args.base_ref))
    if args.files and any(not is_owned(file) for file in requested):
        raise ValueError("Explicit files must be first-party C++ translation units")
    requested = {p for p in requested if is_owned(p)}
    for file in requested:
        if (args.files or args.all) and not file.is_file():
            raise ValueError(f"Missing source file: {file.relative_to(ROOT)}")
        if file.exists() and file not in entries:
            raise ValueError(f"No compile command for {file.relative_to(ROOT)}; configure its target first")
    files = sorted(file for file in requested if file.exists())
    checked = {str(file.relative_to(ROOT)) for file in requested}
    findings = scan(files, entries, args, metadata) if files else {}
    failures, decreases = comparison(findings, baseline, checked, args.all)
    for decrease in decreases:
        print("lower baseline after review: " + decrease)
    for failure in failures:
        print("FAIL complexity: " + failure)
    if args.write_baseline:
        candidate = dict(metadata, translation_units=len(files))
        lines = ["# Candidate only: review differences before changing the tracked baseline.",
                 "# metadata: " + json.dumps(candidate, sort_keys=True)]
        lines += [json.dumps(asdict(row), ensure_ascii=False, sort_keys=True)
                  for _, row in sorted(findings.items())]
        args.write_baseline.write_text("\n".join(lines) + "\n")
    if not requested:
        print("complexity: no changed .cpp files; header edits need explicit consumers or --all")
    print(f"complexity: {'FAILED' if failures else 'passed'} ({len(files)} translation units)")
    return int(bool(failures))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, ImportError, subprocess.SubprocessError) as error:
        raise SystemExit("complexity: ERROR: " + str(error))
