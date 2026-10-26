--- Project entry point and reusable module workflow.
-- Select a file named `project.lua` in the existing generator inspector, then Run and Apply.
-- These declarations are available inside that entry point and its included modules.
-- Ordinary standalone generators and SV curves do not receive them.
-- @module project

--- Name this script group once. This does not rename or create a native project.
-- @string name Nonempty display name.
-- @function worm_project

--- Evaluate a project-local Lua module once, in the stated include order.
-- Paths resolve relative to the including file. Cycles, missing files, absolute paths
-- and canonical/symlink escapes outside the native project directory reject the batch.
-- Modules returning API-v1 notes/layers contribute to one current-chart proposal;
-- stable keys must be unique across modules. Helper modules may return ordinary Lua values.
-- The same returned proposal table is collected once, including a forwarded include result.
-- @string path Relative Lua source path; no glob expansion.
-- @return The included chunk's first return value (cached per canonical source file).
-- @function include

--- Declare an audio file without importing or decoding it.
-- @param declaration Table `{id = "music", file = "song.ogg"}`.
-- @return Named asset metadata; also stored at `project.assets.music`.
-- @see Asset
-- @function audio

--- Declare an image file without importing or decoding it.
-- @param declaration Table `{id = "cover", file = "cover.png"}`.
-- @return Named asset metadata in `project.assets`.
-- @see Asset
-- @function image

--- Declare a MIDI file without importing or decoding it.
-- Use the existing UI to import MIDI and select generator inputs before running scripts
-- that inspect `wormedit.midi_assets`. A declaration alone does not expose event data.
-- @param declaration Table `{id = "score", file = "score.mid"}`.
-- @return Named asset metadata in `project.assets`.
-- @see Asset
-- @function midi

--- Named asset metadata. IDs are unique across all declaration kinds (1–128 bytes).
-- @field kind `audio`, `image` or `midi`.
-- @field file Canonical project-relative file path.
-- @field content_hash SHA256 of the declared file's bytes.
-- @field asset_id Optional existing imported asset ID string, matched by kind and canonical path.
-- Absent when the file has not been imported. This identifies imported media.
-- NoteProposal.sound takes a sound resource ID; asset IDs cannot be assigned directly.
-- @table Asset

--- SV output in included modules.
-- Included proposal tables may contain sv_points and sv_spans alongside notes/layers.
-- The entry point collects each returned table once, in include order, under the same
-- aggregate limits and deterministic replay as standalone generator output.
-- @table SVOutput
