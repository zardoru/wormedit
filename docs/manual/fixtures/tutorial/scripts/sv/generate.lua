-- Run in Inspector -> Lua note generator, against tutorial-empty.wormedit.
-- A persistent span multiplies the authored baseline only inside [from,to).
-- The explicit points set that baseline and hold until the next point.
local measures = wormedit.measures
assert(#measures >= 3, 'This example needs at least three measures')
local first = measures[1].id
local finish = measures[3].id
return {
  sv_points = {
    sv_point {key='baseline', at=at_measure(first,0), value=ratio(3,2)},
    sv_point {key='reset', at=at_measure(finish,0), value=1},
  },
  sv_spans = {
    sv_span {key='rise', name='Tutorial rise',
      from=at_measure(first,0), to=at_measure(finish,0),
      curve='linear', value_from=1, value_to=2, step_rows=60},
  },
}
