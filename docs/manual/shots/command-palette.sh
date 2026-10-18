# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# The command palette (Ctrl+Shift+P) filtered by "measure".
click_at 1444 122
click_at 1390 122
xdotool key --window "$window_id" ctrl+shift+p; sleep 2
xdotool type --window "$window_id" --delay 60 "measure"; sleep 1
