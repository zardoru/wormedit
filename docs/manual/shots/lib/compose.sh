# Shared setup for the manual's fixed 1600×900, default-layout captures.
manual_new_bme() {
    click_at 650 325       # BMS / BME family
    click_at 650 365       # BME 7K
    click_at 1100 580      # Create project, outside scratch
    xdotool key --window "$window_id" F4
    sleep 2
}
manual_compose_sounds() {
    click_at 1525 122      # Hide BMS branches; the content target remains Root / Chart 1.
    click_at 1575 197      # Show Sounds alone using the right dock strip.
    xdotool mousemove --window "$window_id" 1100 860
    sleep 0.4
}
manual_arm_c4() {
    xdotool mousemove --window "$window_id" 1330 522
    xdotool click --repeat 2 --delay 120 1
    sleep 0.6
}
