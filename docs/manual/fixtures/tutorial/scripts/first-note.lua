-- Your first generator: one note at the start of the chart, in its own layer.
-- Choose this file under Lua note generator, press Run, then Apply.

-- The first measure of the chart, and the first lane of its layout.
local measure = wormedit.measures[1]
local lane = wormedit.lane_profile[1].lanes[1]

return {
  layers = {
    {key = "generated", name = "Generated"},
  },
  notes = {
    {key = "first", measure = measure.id, row = 0, lane = lane.id,
     kind = "tap", layers = {"generated"}},
  },
}
