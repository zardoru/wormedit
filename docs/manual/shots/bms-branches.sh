# mode: wizard
# The authored RANDOM range and playback choice are independent settings.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_new_bme
click_at 150 518
click_at 150 548
click_at 150 242
click_at 150 560
xdotool key --window "$window_id" ctrl+a
xdotool type --window "$window_id" 2
click_at 100 589
click_at 150 642
xdotool key --window "$window_id" ctrl+a
xdotool type --window "$window_id" 2
click_at 100 672
xdotool mousemove --window "$window_id" 1100 860
sleep 0.8
