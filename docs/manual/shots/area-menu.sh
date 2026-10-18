# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# The area menu: drag across empty rows with the Select tool, then right-click the area.
click_at 1444 122
click_at 1390 122
xdotool mousemove --window "$window_id" 640 700; sleep 0.3
xdotool mousedown 1; sleep 0.3
for y in 660 620 580 540 520; do xdotool mousemove --window "$window_id" 760 "$y"; sleep 0.2; done
xdotool mouseup 1; sleep 0.8
xdotool mousemove --window "$window_id" 700 600; sleep 0.3
xdotool mousedown 3; sleep 0.3
xdotool mouseup 3; sleep 1.2
