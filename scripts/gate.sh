#!/usr/bin/env bash
# GATE: the standard verification for a change (docs/chart-editor-plan/tasks.md).
#
#   scripts/gate.sh [build-dir] [--agents N] [--jobs N] [--whole-machine] [--print-jobs] [--complexity-base REF]
#
# Builds, runs every CTest test (event-harness and documentation labels included), checks changed
# C++ complexity and whitespace. A test binary that fails to list its tests counts as a failure:
# ctest reports "Error listing tests", which a search for failed tests misses.
#
# Job count: the machine is often shared, so the default leaves slack (owner, 2026-10-17):
# min(8, nproc / agents), at least 1, where agents is how many runs share the machine
# (--agents or WORMEDIT_GATE_AGENTS, default 1). --jobs or WORMEDIT_GATE_JOBS sets it exactly.
# --whole-machine uses every core; pass it only when the owner allows the whole computer.
# --print-jobs prints the chosen job count and exits.
#
# Logs go to a temporary directory that the summary prints; the tree is never written.
set -u

build_dir=build
agents=${WORMEDIT_GATE_AGENTS:-1}
jobs=${WORMEDIT_GATE_JOBS:-}
whole_machine=false
print_jobs=false
complexity_base=HEAD
while [[ $# -gt 0 ]]; do
    case $1 in
        --agents) agents=${2:?--agents needs a number}; shift 2 ;;
        --jobs) jobs=${2:?--jobs needs a number}; shift 2 ;;
        --whole-machine) whole_machine=true; shift ;;
        --print-jobs) print_jobs=true; shift ;;
        --complexity-base) complexity_base=${2:?--complexity-base needs a git reference}; shift 2 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        -*) echo "gate: unknown option $1" >&2; exit 2 ;;
        *) build_dir=$1; shift ;;
    esac
done

is_count() { [[ $1 =~ ^[1-9][0-9]*$ ]]; }
if ! is_count "$agents"; then echo "gate: --agents must be a positive integer" >&2; exit 2; fi
if [[ -n $jobs ]] && ! is_count "$jobs"; then echo "gate: --jobs must be a positive integer" >&2; exit 2; fi

cores=$(getconf _NPROCESSORS_ONLN)
if [[ -n $jobs ]]; then
    reason="set explicitly"
elif $whole_machine; then
    jobs=$cores
    reason="whole machine"
else
    jobs=$(( cores / agents ))
    (( jobs > 8 )) && jobs=8
    (( jobs < 1 )) && jobs=1
    reason="min(8, $cores cores / $agents agent(s))"
fi

if $print_jobs; then
    echo "$jobs job(s), $reason"
    exit 0
fi

root_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ! -f $build_dir/CMakeCache.txt ]]; then
    echo "gate: $build_dir is not a configured build directory" >&2
    exit 2
fi
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/wormedit-gate.XXXXXX")
failed=()
summary=()

stage() {
    local name=$1; shift
    local log="$log_dir/$name.log"
    if "$@" >"$log" 2>&1; then
        summary+=("pass  $name")
        return 0
    fi
    summary+=("FAIL  $name  ($log)")
    failed+=("$name")
    return 1
}

check_ctest_log() {
    local log=$1
    # These lines mean a test binary never ran, even when the counts look clean.
    if grep -q -e "Error listing tests" -e "Not Run" -e "Subprocess aborted" "$log"; then
        echo "gate: ctest could not run every test binary (see $log)" >&2
        return 1
    fi
    grep -q "100% tests passed" "$log"
}

run_ctest() {
    ctest --test-dir "$build_dir" -j "$jobs" --output-on-failure >"$log_dir/ctest-output.log" 2>&1
    local status=$?
    cat "$log_dir/ctest-output.log"
    check_ctest_log "$log_dir/ctest-output.log" && (( status == 0 ))
}

if stage build cmake --build "$build_dir" --parallel "$jobs"; then
    stage ctest run_ctest
else
    summary+=("skip  ctest  (build failed)")
fi
stage whitespace git -C "$root_dir" diff --check HEAD
stage complexity "$root_dir/scripts/check-complexity.py" --build-dir "$build_dir" \
    --jobs "$jobs" --base-ref "$complexity_base"

echo "gate: $jobs job(s), $reason; logs in $log_dir"
printf '  %s\n' "${summary[@]}"
if [[ -f $log_dir/ctest-output.log ]]; then
    grep -E "tests passed|tests failed" "$log_dir/ctest-output.log" | sed 's/^/  /'
fi
if (( ${#failed[@]} > 0 )); then
    echo "gate: FAILED (${failed[*]})"
    exit 1
fi
echo "gate: passed"
