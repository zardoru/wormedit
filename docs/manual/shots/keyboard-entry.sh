# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# The keyboard loop: Draw tool (Alt+2), arm a sound by double-clicking it, then lane keys with
# Up stepping the cursor one snap at a time.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
xdotool key --window "$window_id" alt+2; sleep 0.8
manual_arm_c4
for lane in 2 3 4 3 2; do
    xdotool keydown --window "$window_id" "$lane"; sleep 0.3
    xdotool keyup --window "$window_id" "$lane"; sleep 0.6
    xdotool keydown --window "$window_id" Up; sleep 0.15
    xdotool keyup --window "$window_id" Up; sleep 0.6
done
xdotool keydown --window "$window_id" Next; sleep 0.15
xdotool keyup --window "$window_id" Next; sleep 0.8
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
