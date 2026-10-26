-- Measures 1-2: a slow staircase. Paths are relative to this file.
local stream = include "../lib/stream.lua"
return stream {name = "verse", first = 1, count = 2, snap = 4, lanes = {1, 2, 3, 4}}
