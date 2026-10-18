# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# The measure lane: show it (toolbar), then with the Draw tool click the rail to split a measure;
# the new bar line is selected (gold) and later measures renumber.
click_at 1444 122
click_at 1390 122
click_at 329 122
xdotool key --window "$window_id" alt+2; sleep 0.8
click_at 70 590
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
