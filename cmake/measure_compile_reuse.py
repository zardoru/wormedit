#!/usr/bin/env python3
"""Count compile entries and exact redundant commands; normalize only -o output.

Pass --root for a database belonging to another checkout. Source paths are
resolved for classification; compiler arguments retain their order and spelling.
"""
import argparse
import fnmatch
from collections import defaultdict
import json
import hashlib
import subprocess
from pathlib import Path
import shlex


def arguments_of(entry):
    return list(entry.get('arguments') or shlex.split(entry['command']))


def without_output(entry):
    arguments = arguments_of(entry)
    output = arguments.index('-o')  # Fail rather than silently broaden normalization.
    if output + 1 == len(arguments):
        raise ValueError('Missing output after -o')
    del arguments[output:output + 2]
    return arguments


def preprocessed_digest(entry):
    arguments = without_output(entry)
    arguments.remove('-c')
    result = subprocess.run(arguments + ['-E', '-P'], cwd=entry['directory'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f'{entry["file"]}: preprocessing failed\n' +
                           result.stderr.decode(errors='replace'))
    return hashlib.sha256(result.stdout).hexdigest()


def compare_preprocessed(before, after, target):
    previous = defaultdict(list)
    for entry in json.loads(before.read_text()):
        previous[entry['file']].append(entry)
    selected = [entry for entry in json.loads(after.read_text())
                if fnmatch.fnmatch(' '.join(arguments_of(entry)), '*' + target + '.dir/*')]
    if not selected:
        raise ValueError(f'No compile entries for target {target}')
    comparisons = 0
    mismatches = []
    for entry in selected:
        if not previous[entry['file']]:
            raise ValueError(f'Missing baseline source: {entry["file"]}')
        expected = preprocessed_digest(entry)
        for old in previous[entry['file']]:
            comparisons += 1
            if preprocessed_digest(old) != expected:
                mismatches.append(old)
    return {'sources': len(selected), 'comparisons': comparisons, 'mismatches': mismatches}


def compile_closure(build_dir, target, root):
    commands = subprocess.check_output(
        ['ninja', '-C', str(build_dir), '-t', 'commands', target], text=True)
    sources = []
    for command in commands.splitlines():
        arguments = shlex.split(command)
        if '-c' in arguments:
            sources.append(Path(arguments[arguments.index('-c') + 1]))
    owned = [str(source.relative_to(root)) for source in sources
             if source.is_relative_to(root) and source.relative_to(root).parts[0] in ('src', 'tests')]
    return {'all_compile_actions': len(sources), 'first_party_compile_actions': len(owned),
            'first_party_sources': owned}


def measure(database, root):
    groups = defaultdict(list)
    entries = []
    for entry in json.loads(database.read_text()):
        source = Path(entry['file'])
        if not source.is_absolute():
            source = Path(entry['directory']) / source
        source = source.resolve()
        if not source.is_relative_to(root):
            continue
        relative = source.relative_to(root)
        if relative.parts[0] not in ('src', 'tests'):
            continue
        arguments = without_output(entry)
        entries.append(str(relative))
        groups[(str(relative), entry['directory'], tuple(arguments))].append(entry)
    duplicates = [
        {'source': source, 'compilations': len(commands), 'extra': len(commands) - 1}
        for (source, directory, arguments), commands in groups.items()
        if source.startswith('src/') and len(commands) > 1
    ]
    return {
        'entries': len(entries), 'unique_sources': len(set(entries)),
        'production_entries': sum(source.startswith('src/') for source in entries),
        'identical_extra_production_compilations': sum(item['extra'] for item in duplicates),
        'duplicated_production_sources': len({item['source'] for item in duplicates}),
        'duplicates': sorted(duplicates, key=lambda item: item['source']),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('database', type=Path)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--before', type=Path,
                        help='Also compare preprocessed tokens against this baseline database')
    parser.add_argument('--preprocess-target', default='wormedit_ui_*')
    parser.add_argument('--ninja-target', action='append', default=[],
                        help='Measure required compiler-action closure, including dependencies')
    parser.add_argument('--build-dir', type=Path, help='Ninja build directory (default: database parent)')
    args = parser.parse_args()
    result = measure(args.database, args.root.resolve())
    if args.before:
        result['preprocessed'] = compare_preprocessed(args.before, args.database,
                                                     args.preprocess_target)
    if args.ninja_target:
        result['target_closures'] = {
            target: compile_closure(args.build_dir or args.database.parent, target, args.root.resolve())
            for target in args.ninja_target}
    print(json.dumps(result, indent=2))
    if args.before and result['preprocessed']['mismatches']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
