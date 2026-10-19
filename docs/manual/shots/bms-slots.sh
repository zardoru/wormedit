# mode: wizard
# The Sounds dock in a newly created BME project, with the empty slot 09 selected as an import target.
# Open the layout list, scroll to the BMS family, and choose BME 7K (outside scratch).
click_at 1015 455
xdotool mousemove --window "$window_id" 800 650
for _ in {1..40}; do xdotool click 5; done
click_at 750 544; sleep 0.5
click_at 960 496; sleep 4
click_at 666 55; sleep 1
# New projects show Sounds and the inspector. Hide the inspector to give the slot list room.
click_at 1417 122; sleep 1
click_at 1500 189; sleep 1
click_at 1330 548
xdotool mousemove --window "$window_id" 1000 860; sleep 0.5
