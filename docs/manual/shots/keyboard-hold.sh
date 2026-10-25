# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# A keyboard hold: keep lane key 4 down while Up steps the cursor three snaps, then release.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
xdotool key --window "$window_id" alt+2; sleep 0.8
manual_arm_c4
xdotool keydown --window "$window_id" 4; sleep 0.5
for step in 1 2 3; do
    xdotool keydown --window "$window_id" Up; sleep 0.15
    xdotool keyup --window "$window_id" Up; sleep 0.6
done
xdotool keyup --window "$window_id" 4; sleep 0.8
xdotool keydown --window "$window_id" Next; sleep 0.15
xdotool keyup --window "$window_id" Next; sleep 0.8
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
