# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial-empty.wormedit
# Pasting notes copied in iBMSC after adding one BGM column (BGM columns +) for its BGM event.
# The preview follows the pointer until Enter or a click.
click_at 1444 122
click_at 1390 122
click_at 1350 122
printf 'iBMSC Clipboard Data\r\n6 0 10000 0 0 0\r\n7 48 20000 0 0 0\r\n8 96 30000 0 0 0\r\n9 144 20000 0 0 0\r\n27 0 40000 0 0 0' |
    xsel --clipboard --input --nodetach &
clipboard_owner=$!
sleep 0.5
xdotool mousemove --window "$window_id" 600 700; sleep 0.3
xdotool key --window "$window_id" ctrl+v; sleep 1
xdotool mousemove --window "$window_id" 610 640; sleep 1
kill "$clipboard_owner" 2>/dev/null || true
