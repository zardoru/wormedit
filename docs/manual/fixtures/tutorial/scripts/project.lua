-- A song's scripts split across files. Choose this file in the generator inspector.
-- Every included file runs once, in this order, and their notes are applied together.
worm_project "Tutorial song"
include "patterns/verse.lua"
include "patterns/chorus.lua"
