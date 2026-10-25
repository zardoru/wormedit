# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# Lengthening an existing note: place a tap with lane key 3, switch to Select (Alt+1), then
# Alt-drag the note up the lane; it becomes a hold reaching the release point.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
xdotool key --window "$window_id" alt+2; sleep 0.8
manual_arm_c4
xdotool keydown --window "$window_id" 3; sleep 0.3
xdotool keyup --window "$window_id" 3; sleep 0.8
xdotool key --window "$window_id" alt+1; sleep 0.8
xdotool mousemove --window "$window_id" 572 725; sleep 0.4
xdotool keydown --window "$window_id" alt; sleep 0.3
xdotool mousedown 1; sleep 0.4
for y in 690 660 630 600 586; do xdotool mousemove --window "$window_id" 572 "$y"; sleep 0.25; done
sleep 0.6
xdotool mouseup 1; sleep 0.4
xdotool keyup --window "$window_id" alt; sleep 0.8
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
