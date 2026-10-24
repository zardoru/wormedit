# mode: landing
# project: docs/manual/fixtures/comparison/comparison.wormedit
# The project opens on Baseline; Variation is the other default comparison pane.
xdotool key --window "$window_id" F7
sleep 0.8
xdotool mousemove --window "$window_id" 1590 890
