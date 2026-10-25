# mode: wizard
# Compose on a newly created BME project, with the BMS branches dock and inspector hidden.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_new_bme
manual_compose_sounds
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
