-- Run from Lua note generator on a chart with at least one measure.
local m = wormedit.measures[1].id
return {
  sv_points = {
    sv_point {key='base', at=at_measure(m,0), value=ratio(3,2)},
    sv_point {key='reset', at=at_measure(m,480), value=1},
  },
  sv_spans = {
    sv_span {key='ramp', from=at_measure(m,0), to=at_measure(m,480),
      value_from=1, value_to=2, points=5},
  },
}
