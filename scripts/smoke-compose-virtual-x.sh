#!/usr/bin/env bash
set -euo pipefail

# Exercise the real SDL/Nuklear window without taking focus from the desktop.
# Xvfb uses an authenticated loopback listener because this machine's shared
# /tmp/.X11-unix directory is not owned by root and rejects new Unix sockets.
root_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
app="$root_dir/build/wormedit"
output=${1:-/tmp/wormedit-compose-smoke.png}
mode=${2:-compose}
if [[ $mode != compose && $mode != timing && $mode != recipe && $mode != recipe_apply ]]; then
    echo "Usage: $0 [capture.png] [compose|timing|recipe|recipe_apply]" >&2
    exit 1
fi
temp_dir=$(mktemp -d /tmp/wormedit-compose-smoke.XXXXXX)
xvfb_pid=
app_pid=
cleanup() {
    if [[ -n $app_pid ]]; then
        kill "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    if [[ -n $xvfb_pid ]]; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
    fi
    rm -r -- "$temp_dir"
}
trap cleanup EXIT

if [[ ! -x $app ]]; then
    echo "Build Wormedit first: $app" >&2
    exit 1
fi
for utility in Xvfb xauth mcookie xdpyinfo xdotool import; do
    if ! command -v "$utility" >/dev/null; then
        echo "Missing test utility: $utility" >&2
        exit 1
    fi
done
if [[ $mode == recipe_apply ]] && ! command -v tesseract >/dev/null; then
    echo "Missing test utility: tesseract" >&2
    exit 1
fi

export XAUTHORITY="$temp_dir/Xauthority"
touch "$XAUTHORITY"
chmod 600 "$XAUTHORITY"
for display_number in {140..159}; do
    export DISPLAY="localhost:$display_number"
    xauth -f "$XAUTHORITY" add "$DISPLAY" . "$(mcookie)" >/dev/null
    Xvfb ":$display_number" -screen 0 1280x1024x24 -nolisten unix -listen tcp \
        -auth "$XAUTHORITY" >"$temp_dir/Xvfb.log" 2>&1 &
    xvfb_pid=$!
    for attempt in {1..20}; do
        if xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
            break 2
        fi
        if ! kill -0 "$xvfb_pid" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done
    wait "$xvfb_pid" 2>/dev/null || true
    xvfb_pid=
done
if [[ -z $xvfb_pid ]] || ! xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
    cat "$temp_dir/Xvfb.log" >&2
    echo "Could not start an isolated X server." >&2
    exit 1
fi

export SDL_VIDEODRIVER=x11
export SDL_AUDIODRIVER=dummy
export XDG_CONFIG_HOME="$temp_dir/config"
export XDG_DATA_HOME="$temp_dir/data"
"$app" >"$temp_dir/wormedit.log" 2>&1 &
app_pid=$!
window_id=
for attempt in {1..50}; do
    window_id=$(xdotool search --onlyvisible --name '^wormedit$' 2>/dev/null | head -n 1 || true)
    if [[ -n $window_id ]]; then
        break
    fi
    if ! kill -0 "$app_pid" 2>/dev/null; then
        cat "$temp_dir/wormedit.log" >&2
        echo "Wormedit exited before showing a window." >&2
        exit 1
    fi
    sleep 0.1
done
if [[ -z $window_id ]]; then
    cat "$temp_dir/wormedit.log" >&2
    echo "Wormedit did not show a window." >&2
    exit 1
fi

geometry=$(xdotool getwindowgeometry --shell "$window_id")
if [[ ! $geometry =~ WIDTH=([0-9]+) ]]; then
    echo "Could not read Wormedit window width." >&2
    exit 1
fi
window_width=${BASH_REMATCH[1]}
click_at() {
    xdotool mousemove --window "$window_id" "$1" "$2" mousedown 1
    sleep 0.12
    xdotool mouseup 1
    sleep 0.2
}
# Compose is the middle of seven equal-width tabs.
click_at "$((window_width / 2))" 85
if [[ $mode == timing || $mode == recipe || $mode == recipe_apply ]]; then
    # Timing is the third tab in the same seven-column row.
    click_at "$((window_width * 5 / 14))" 85
fi
if [[ $mode == recipe || $mode == recipe_apply ]]; then
    # Empty test data keeps the recipe button at this fixed toolbar row.
    click_at "$((window_width / 5))" 290
    # Choose a nonempty interval in the Scroll / SV lane.
    xdotool mousemove --window "$window_id" "$((window_width / 5))" 350 mousedown 1
    sleep 0.12
    xdotool mousemove --window "$window_id" "$((window_width / 5))" 550
    sleep 0.12
    xdotool mouseup 1
    sleep 0.25
    if [[ $mode == recipe_apply ]]; then
        click_at "$((window_width / 4))" 665
    fi
fi
import -window "$window_id" "$output"
if [[ $mode == recipe_apply ]] &&
    ! tesseract "$output" stdout 2>/dev/null | rg -qi 'SV recipe added'; then
    echo "The applied SV recipe did not leave a visible success frame: $output" >&2
    exit 1
fi
echo "$output"
