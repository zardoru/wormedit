# mode: compose
# project: docs/manual/fixtures/tutorial/tutorial.wormedit
# Editing the starting BPM in place: double-click the BPM marker on the first row.
source "$root_dir/docs/manual/shots/lib/compose.sh"
manual_compose_sounds
xdotool mousemove --window "$window_id" 118 724; sleep 0.4
xdotool click --repeat 2 --delay 80 1; sleep 1.2
