# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# Editing the starting BPM in place: double-click the BPM marker on the first row.
click_at 1444 122
click_at 1390 122
xdotool mousemove --window "$window_id" 118 724; sleep 0.4
xdotool click --repeat 2 --delay 80 1; sleep 1.2
