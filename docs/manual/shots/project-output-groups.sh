# mode: wizard
# New BME projects provide a real BMS export target without touching the bundled fixtures.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_new_bme
xdotool key --window "$window_id" F1
sleep 1
xdotool mousemove --window "$window_id" 1100 860
sleep 0.8
