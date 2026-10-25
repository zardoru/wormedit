# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# Create a target layer, open Layers, and select the Layer brush.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
xdotool key --window "$window_id" ctrl+shift+l
sleep 0.5
xdotool type --window "$window_id" --delay 80 'Melody'
xdotool key --window "$window_id" Return
sleep 0.8
click_at 1575 300
sleep 0.8
click_at 195 122
xdotool mousemove --window "$window_id" 1100 860
sleep 0.8
