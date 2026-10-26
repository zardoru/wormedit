-- An SV curve that wobbles around normal speed. Choose this file with Choose module…
-- in an SV recipe whose Curve is "Lua function". Leave Entry point empty.
--
-- Parameters (add them in the recipe inspector):
--   amount  how far the speed swings, 0.25 means between 0.75x and 1.25x  (default 0.25)
--   waves   how many full wobbles fit in the recipe's range              (default 2)
--
-- p runs from 0 at the start of the range to 1 at its end. The value returned
-- multiplies the chart's own scroll there, so 1 means "no change".
return function(p, ctx)
  local amount = ctx.parameters.amount or 0.25
  local waves = ctx.parameters.waves or 2
  return 1 + amount * math.sin(p * waves * 2 * math.pi)
end
