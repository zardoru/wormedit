-- A random stream of single notes that never hits the same lane twice in a row.
--
-- Optional parameters (Parameter name / Value (JSON) in the inspector):
--   first  measure number to start at, counting from 1     (default 1)
--   count  how many measures to fill                       (default 1)
--   snap   notes per measure: 4, 8, 16, ...                 (default 8)
-- Change Seed to get a different pattern; the same seed always gives the same one.

local parameters = generator.parameters
local first = parameters.first or 1
local count = parameters.count or 1
local snap = parameters.snap or 8

-- Playable lanes only: role 0. This skips scratch, BGM-style and mine lanes.
local lanes = {}
for _, lane in ipairs(wormedit.lane_profile[1].lanes) do
  if lane.role == 0 then lanes[#lanes + 1] = lane.id end
end
assert(#lanes >= 2, "the stream needs at least two playable lanes")

local resolution = wormedit.divisions_per_standard_measure
assert(resolution % snap == 0, "snap " .. snap .. " does not divide the chart grid")
local step = resolution // snap

local notes = {}
local previous
for number = first, first + count - 1 do
  local measure = wormedit.measures[number]
  assert(measure, "the chart has no measure " .. number)
  for row = 0, measure.row_count - 1, step do
    local lane
    repeat lane = lanes[math.random(#lanes)] until lane ~= previous
    previous = lane
    notes[#notes + 1] = {
      -- The key names this note across runs: same measure and row, same note.
      key = "stream-" .. measure.id .. "-" .. row,
      measure = measure.id, row = row, lane = lane, layers = {"stream"},
    }
  end
end

return {layers = {{key = "stream", name = "Stream"}}, notes = notes}
