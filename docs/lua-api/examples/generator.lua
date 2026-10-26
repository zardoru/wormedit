--- Place a note on the first lane in a generated layer.
local measure = wormedit.measures[1].id
local lane = wormedit.lane_profile[1].lanes[1].id
return {
    layers = {{key = "generated", name = "Generated"}},
    notes = {{key = "first", measure = measure, row = 0,
        lane = lane, kind = "tap", layers = {"generated"}}}
}
