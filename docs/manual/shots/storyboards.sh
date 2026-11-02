# mode: compose
# project: docs/manual/fixtures/storyboards/storyboards.wormedit
# Verified on the current720px production layout in isolated Xvfb.
xdotool windowsize --sync "$window_id" 1280 720
sleep 1.5
click_at 746 55
sleep 0.5
click_at 200 123
click_at 200 155
sleep 0.5
click_at 75 329
sleep 0.5
click_at 174 569
sleep 0.4
xdotool mousemove --window "$window_id" 0 0
sleep 0.3
