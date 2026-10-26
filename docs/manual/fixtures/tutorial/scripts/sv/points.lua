-- Explicit output: sample your own formula here; no persistent curve is created.
local m = assert(wormedit.measures[1], 'This example needs a measure')
local points = {}
for i=0,7 do
  points[#points+1] = sv_point {
    key='wave-'..i, at=at_measure(m.id,i*120),
    value=1 + 0.5*math.sin(2*math.pi*i/8),
  }
end
-- Points hold their last value; always place your intended reset explicitly.
points[#points+1] = sv_point {key='reset', at=at_measure(wormedit.measures[2].id,0),value=1}
return {sv_points=points}
