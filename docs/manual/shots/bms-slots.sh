# mode: wizard
# The Sounds dock in a newly created BME project, with the empty slot 09 selected as an import target.
# Open the layout list, scroll to the BMS family, and choose BME 7K (outside scratch).
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_new_bme
manual_compose_sounds
click_at 1450 190; sleep 1
click_at 1310 558
xdotool mousemove --window "$window_id" 1000 860; sleep 0.5
