# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# file-dialog-driver: zenity
# Use an isolated Zenity picker (never a host portal) to stage one bundled sound.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
click_at 1358 217
picker=
for attempt in {1..50}; do
    picker=$(xdotool search --onlyvisible --class zenity 2>/dev/null | head -n 1 || true)
    [[ -n $picker ]] && break
    sleep 0.1
done
[[ -n $picker ]] || { echo 'Sound-import picker did not open.' >&2; return 1; }
xdotool windowfocus --sync "$picker"
sleep 1
xdotool key ctrl+l
xdotool type --clearmodifiers --delay 10 "$root_dir/docs/manual/fixtures/tutorial/chord.ogg"
xdotool key Return
sleep 0.8
if xdotool search --onlyvisible --class zenity 2>/dev/null | rg -q "^$picker$"; then
    xdotool key Return
fi
for attempt in {1..50}; do
    xdotool search --onlyvisible --class zenity 2>/dev/null | rg -q "^$picker$" || break
    sleep 0.1
done
if xdotool search --onlyvisible --class zenity 2>/dev/null | rg -q "^$picker$"; then
    xdotool key Escape
    echo 'Sound-import picker did not accept the fixture path.' >&2
    return 1
fi
sleep 2
xdotool windowfocus --sync "$window_id"
xdotool mousemove --window "$window_id" 1100 860
sleep 0.8
