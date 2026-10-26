-- A whole new 4-key difficulty, written measure by measure. Run creates no notes in the
-- chart you are on: Apply adds a separate difficulty named "Easy" to the project.
difficulty "Easy"
stable_id "easy"   -- keep this the same forever, even if you rename the difficulty
resolution(960)    -- rows per 4/4 measure: a quarter note is 240 rows
columns(4)

-- An intro with a note on every beat, walking across the lanes.
measure {id = "intro",
  note {key = "beat1", row = 0,   lane = 1},
  note {key = "beat2", row = 240, lane = 2},
  note {key = "beat3", row = 480, lane = 3},
  note {key = "beat4", row = 720, lane = 4}}

-- Four more measures built in a loop: eighth notes bouncing between lanes 1 and 4,
-- and a half-measure hold on lane 2.
for number = 1, 4 do
  local id = "main" .. number
  local events = {id = id}
  for eighth = 0, 7 do
    events[#events + 1] = note {key = "bounce" .. eighth, row = eighth * 120,
                                lane = eighth % 2 == 0 and 1 or 4}
  end
  events[#events + 1] = hold {key = "hold", row = 0, lane = 2, end_at = at_measure(id, 480)}
  measure(events)
end

measure {id = "outro", note {key = "last", row = 0, lane = 3}}

bpm {key = "tempo", at = at_measure("intro", 0), value = 140}
