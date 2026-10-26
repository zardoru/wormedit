-- A helper module: returns a function instead of notes, for other files to include.
-- local stream = include "../lib/stream.lua"
-- return stream {name = "verse", first = 1, count = 2, snap = 8, lanes = {1, 3, 5, 7}}

local function playable_lanes()
  local lanes = {}
  for _, lane in ipairs(wormedit.lane_profile[1].lanes) do
    if lane.role == 0 then lanes[#lanes + 1] = lane.id end
  end
  return lanes
end

return function(options)
  local all = playable_lanes()
  local order = options.lanes or {1, 2, 3, 4}
  local step = wormedit.divisions_per_standard_measure // options.snap
  local notes = {}
  local index = 0
  for number = options.first, options.first + options.count - 1 do
    local measure = assert(wormedit.measures[number], "the chart has no measure " .. number)
    for row = 0, measure.row_count - 1, step do
      local lane = assert(all[order[index % #order + 1]], "lane number out of range")
      index = index + 1
      notes[#notes + 1] = {
        key = options.name .. "-" .. measure.id .. "-" .. row,
        measure = measure.id, row = row, lane = lane, layers = {options.name},
      }
    end
  end
  return {layers = {{key = options.name, name = options.name}}, notes = notes}
end
