# mode: wizard
# Pasting four resource-free mines copied in iBMSC into a new BME project.
# The preview follows the pointer until Enter or a click.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_new_bme
manual_compose_sounds
printf 'iBMSC Clipboard Data\r\n6 0 10000 0 0 -1\r\n7 48 10000 0 0 -1\r\n8 96 10000 0 0 -1\r\n9 144 10000 0 0 -1' |
    xsel --clipboard --input --nodetach &
clipboard_owner=$!
sleep 0.5
xdotool mousemove --window "$window_id" 600 700; sleep 0.3
xdotool key --window "$window_id" ctrl+v; sleep 1
sleep 1
kill "$clipboard_owner" 2>/dev/null || true
