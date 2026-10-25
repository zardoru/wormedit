# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# The measure lane: show it (toolbar), then with the Draw tool click the rail to split a measure;
# the new bar line is selected (gold) and later measures renumber.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
click_at 356 122
xdotool key --window "$window_id" alt+2
sleep 0.8
click_at 70 590
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
