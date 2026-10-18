# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# The keyboard loop: Draw tool (Alt+2), arm a sound by double-clicking it, then lane keys with
# Up stepping the cursor one snap at a time.
click_at 1444 122
click_at 1390 122
xdotool key --window "$window_id" alt+2; sleep 0.8
xdotool mousemove --window "$window_id" 1290 470; sleep 0.3
xdotool click --repeat 2 --delay 80 1; sleep 1
for lane in 2 3 4 3 2; do
    xdotool keydown --window "$window_id" "$lane"; sleep 0.3
    xdotool keyup --window "$window_id" "$lane"; sleep 0.6
    xdotool keydown --window "$window_id" Up; sleep 0.15
    xdotool keyup --window "$window_id" Up; sleep 0.6
done
xdotool keydown --window "$window_id" Next; sleep 0.15
xdotool keyup --window "$window_id" Next; sleep 0.8
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
