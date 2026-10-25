# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# Select tool and selected notes; Layer is the third icon in the same left tool column.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
click_at 950 600
xdotool key --window "$window_id" alt+1
xdotool key --window "$window_id" ctrl+a
sleep 0.6
xdotool mousemove --window "$window_id" 1100 860
sleep 0.8
