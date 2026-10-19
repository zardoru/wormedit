# mode: wizard
# Pasting four resource-free mines copied in iBMSC into a new BME project.
# The preview follows the pointer until Enter or a click.
click_at 1015 455
xdotool mousemove --window "$window_id" 800 650
for _ in {1..40}; do xdotool click 5; done
click_at 750 544; sleep 0.5
click_at 960 496; sleep 4
click_at 666 55; sleep 1
click_at 1417 122; sleep 1
printf 'iBMSC Clipboard Data\r\n6 0 10000 0 0 -1\r\n7 48 10000 0 0 -1\r\n8 96 10000 0 0 -1\r\n9 144 10000 0 0 -1' |
    xsel --clipboard --input --nodetach &
clipboard_owner=$!
sleep 0.5
xdotool mousemove --window "$window_id" 600 700; sleep 0.3
xdotool key --window "$window_id" ctrl+v; sleep 1
sleep 1
kill "$clipboard_owner" 2>/dev/null || true
