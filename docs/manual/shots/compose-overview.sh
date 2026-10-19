# mode: wizard
# Compose on a newly created BME project, with the BMS branches dock and inspector hidden.
click_at 1015 455
xdotool mousemove --window "$window_id" 800 650
for _ in {1..40}; do xdotool click 5; done
click_at 750 544; sleep 0.5
click_at 960 496; sleep 4
click_at 666 55; sleep 1
click_at 1417 122; sleep 1
xdotool mousemove --window "$window_id" 1420 650; sleep 0.5
