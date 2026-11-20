#!/usr/bin/env bash
set -euo pipefail

# Render snapshots of the real application on Linux and compare them with the PNGs in
# tests/visual/linux/. The references are rendered by Mesa software Vulkan (lavapipe) under an
# isolated Xvfb, so they are only valid for that renderer; Metal/GPU output will differ.
#
#   scripts/visual-snapshots.sh [--app PATH] [--out DIR]   compare (exit 77 if tools are missing)
#   scripts/visual-snapshots.sh --update                   rewrite the references
#
# Failing captures and their diffs stay in the output directory (default build/visual-snapshots).
# Needs: the app binary, Xvfb, xdotool, ImageMagick and Pillow (see scripts/setup-cloud-env.sh app).
root_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
app=${WORMEDIT_APP:-$root_dir/build/wormedit}
out_dir=$root_dir/build/visual-snapshots
update=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --app) app=$2; shift 2 ;;
        --out) out_dir=$2; shift 2 ;;
        --update) update=1; shift ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
ref_dir=$root_dir/tests/visual/linux
fixture=$root_dir/tests/fixtures/native/compose-smoke.wormedit

for utility in Xvfb xauth mcookie xdpyinfo xdotool import identify; do
    if ! command -v "$utility" >/dev/null; then
        echo "SKIP: missing $utility"; exit 77
    fi
done
if [[ ! -x $app ]]; then echo "SKIP: no application binary at $app"; exit 77; fi
if ! python3 -c 'import PIL' 2>/dev/null; then echo "SKIP: Pillow is not installed"; exit 77; fi
export WORMEDIT_APP=$app

# name:mode[:project]. Timing is left out: its F3/F4 transitions animate and differ run to run.
captures=(landing:landing wizard:wizard gallery:gallery compose:compose:"$fixture" audio:audio:"$fixture")
rm -rf "$out_dir"; mkdir -p "$out_dir" "$ref_dir"
for entry in "${captures[@]}"; do
    IFS=: read -r name mode project <<<"$entry"
    "$root_dir/scripts/smoke-compose-virtual-x.sh" "$out_dir/$name.png" "$mode" ${project:+"$project"} >/dev/null
done

if [[ $update -eq 1 ]]; then
    cp "$out_dir"/*.png "$ref_dir/"
    echo "Updated $(ls "$ref_dir" | wc -l) references in $ref_dir"
    exit 0
fi
# Same-machine runs are bit-exact except for a few animated pixels; allow a small, faint residue.
python3 "$root_dir/scripts/compare-ui-captures.py" --channel-tolerance 8 --max-different-pixels 200 \
    "$ref_dir" "$out_dir"
