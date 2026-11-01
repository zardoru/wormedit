# Quaver `.qua` format reference

Source-derived from [Quaver/Quaver.API](https://github.com/Quaver/Quaver.API) at
commit `a921d561b2ec` (2026-07-27), license MPL-2.0. The authoritative code is
`Quaver.API/Maps/Qua.cs`, `Quaver.API/Maps/Structures/*.cs`, and
`Quaver.API/Enums/*.cs`. Test files under `Quaver.API.Tests/Quaver/Resources/`
(for example `keysounds.qua`, `sound-effects.qua`, `sample.qua`) are useful
fixture models; do not copy them without keeping the MPL notice.

## Container

- UTF-8 YAML, one document, deserialized by YamlDotNet 5.1 with
  `IgnoreUnmatchedProperties()` (unknown keys are ignored, not errors).
- Written with CRLF newlines. YamlDotNet 5.1 omits default values, so absent
  keys mean "default". Property order in files follows declaration order in
  `Qua.cs`.
- Polymorphic timing groups use the YAML tag `!ScrollGroup`.
- All times are **milliseconds**. Hit objects use `int`; timing points, scroll
  velocities, scroll speed factors, and sound effects use `float`.

## Top-level keys (declaration order)

| Key | Type | Default / meaning |
| --- | --- | --- |
| `QuaVersion` | int | 0. Writers set the minimum needed: 1 when any mine exists. Readers reject a version above `CurrentQuaVersion` (1). |
| `AudioFile` | string | Song audio, relative to the map folder. |
| `SongPreviewTime` | int | Preview start, ms. |
| `BackgroundFile`, `BannerFile` | string | Relative image paths. |
| `MapId`, `MapSetId` | int | -1 when unsubmitted. |
| `Mode` | enum | `Keys1`…`Keys10` (enum values: Keys4=1, Keys7=2, Keys1=3, Keys2=4, Keys3=5, Keys5=6, Keys6=7, Keys8=8, Keys9=9, Keys10=10). Serialized by name. |
| `Title`, `Artist`, `Source`, `Tags`, `Creator`, `DifficultyName`, `Description`, `Genre` | string | Metadata. `Tags` is one space-separated string. |
| `LegacyLNRendering` | bool | false. |
| `BPMDoesNotAffectScrollVelocity` | bool | false. **false = denormalized SVs** (BPM changes also change scroll speed, osu!-style); true = normalized SVs. |
| `InitialScrollVelocity` | float | SV before the first SV point; only meaningful when normalized. |
| `HasScratchKey` | bool | false. Adds one scratch lane (for 5K+1 / 7K+1 play). |
| `EditorLayers` | list | See below. Maps naturally to wormedit semantic layers. |
| `Bookmarks` | list | Omitted when empty. |
| `CustomAudioSamples` | list | Keysound/effect sample table. **1-based** when referenced. |
| `SoundEffects` | list | Timed samples not tied to a note (BGM-like). |
| `TimingPoints` | list | BPM changes. At least one required. |
| `SliderVelocities` | list | SV points of the default scroll group. |
| `ScrollSpeedFactors` | list | Default-group SSFs; omitted when empty. |
| `HitObjects` | list | At least one required. |
| `TimingGroups` | map | id → `!ScrollGroup`. `$Default` is never written; `$Global` is written only when it has SVs or SSFs. |

## Structures

- **TimingPointInfo:** `StartTime` (float ms), `Bpm` (float), `Signature` (`Quadruple` = 4 or `Triple` = 3; written as 0/omitted for 4), `Hidden` (bool; hides the measure line).
- **SliderVelocityInfo:** `StartTime` (float ms), `Multiplier` (float).
- **ScrollSpeedFactorInfo:** `StartTime` (float ms), `Multiplier` (float). A second, multiplicative speed track applied on top of SV.
- **HitObjectInfo:** `StartTime` (int ms), `Lane` (int, **1-based**), `EndTime` (int ms; 0 = not a long note; must be > `StartTime` when set), `HitSound` (flags: Normal=1, Whistle=2, Finish=4, Clap=8; 0 and 1 both mean Normal and are written as omitted), `Type` (`Normal` or `Mine`; mine requires `QuaVersion: 1`), `KeySounds` (list), `EditorLayer` (int index into `EditorLayers`; 0 = default layer), `TimingGroup` (string id; omitted = `$Default`).
- **KeySoundInfo:** `Sample` (1-based index into `CustomAudioSamples`), `Volume` (1–100; 100 written as omitted/0).
- **CustomAudioSampleInfo:** `Path` (relative file), `UnaffectedByRate` (bool).
- **SoundEffectInfo:** `StartTime` (float ms), `Sample` (1-based), `Volume` (1–100; 100 omitted).
- **BookmarkInfo:** `StartTime` (int ms), `Note` (string), `ColorRgb` (`"r,g,b"`, default `"255,255,0"`).
- **EditorLayerInfo:** `Name`, `Hidden` (bool), `ColorRgb` (`"r,g,b"`).
- **ScrollGroup (`!ScrollGroup`):** `InitialScrollVelocity`, `ScrollVelocities` (SV list), `ScrollSpeedFactors` (SSF list), `ColorRgb`, `Hidden`. Notes in a group scroll with that group's SVs; `$Global` SVs/SSFs apply to every group.

## Load rules (`Qua.Parse`)

1. Deserialize; ignore unknown keys.
2. Restore defaults: `Signature` 0 → Quadruple; `HitSound` 0 → Normal; missing `TimingGroup` → `$Default`; keysound and sound-effect `Volume` 0 → 100; add an empty `$Global` group if absent.
3. Validate (when requested): at least one hit object and one timing point; `Mode` defined; every `Sample` index in `1..CustomAudioSamples.Count`; sound-effect volume 1–100; long notes need `EndTime > StartTime`; keysound volume ≥ 1.
4. Reject `QuaVersion` > 1.
5. Stable-sort every list by start time.

## Write rules (`Qua.Serialize`)

Sort all lists; compute `QuaVersion`; write default-valued fields as 0/null so
they are omitted (HitSound Normal, Volume 100, Signature 4, `$Default` group id,
empty bookmarks/SSFs/`$Global`); CRLF newlines.

## Mapping notes for wormedit

- Absolute millisecond times, like osu!mania: reuse the osu!mania measure-rewiring
  approach (timing points rebuild measures; `Signature` gives 3/4 or 4/4 only).
- SV has two conventions. When `BPMDoesNotAffectScrollVelocity` is false, the
  effective scroll is `SV × BPM / base BPM` as in osu!; convert explicitly and
  keep the flag for round trips.
- `ScrollSpeedFactors` are a second multiplicative track; import them as a
  separate scroll family rather than folding them into SV.
- Timing groups are per-note scroll groups; wormedit has no equivalent yet.
  Import must retain them losslessly (group table plus per-note id) and export
  must refuse or diagnose edits that would need a new model.
- `EditorLayers` + `HitObject.EditorLayer` ↔ semantic layers (one layer per note,
  matching open question 10's current default).
- `HitSound` flags ↔ the typed osu!-style hitsound sample (see
  [`compose-ux.md`](../chart-editor-plan/tasks/compose-ux.md) T-215).
- `KeySounds` + `CustomAudioSamples` ↔ note sound resources; `SoundEffects` ↔ BGM events.

## Experimental post-1.0 adapter

The paired reader/writer is intentionally outside the 1.0 supported-output catalog.
Open a `.qua` using the experimental file filter or drag/drop it. The headless
converter accepts `target: "quaver"`, `target_profile: "qua-v1-experimental"`,
and emits one `.qua` per selected difficulty through normal transactional publication.
This work builds on the original fixture corpus in PR #56; neither PR should merge
into the 1.0 release without a separate release decision.

The adapter uses pinned yaml-cpp 0.8.0 (MIT; owner authorized 2026-10-28).
It accepts one UTF-8 YAML document, ignores unknown properties, rejects duplicate
mapping keys and aliases, and checks source bytes, event count, node count, nesting
and derived measures. Paths must be relative, use forward slashes, and remain
inside the map folder. Unsupported versions, invalid samples/volumes, lane/layer
references and hold bounds reject without publishing a partial document.

Timing points rebuild signature-aware measures, with a 960,000-row standard
measure and explicit audio-time origin. Partial measures use a bounded rational
BPM adjustment when necessary; this produces a diagnostic. Notes retain source
milliseconds in provenance. Denormalized SV creates explicit normalized editor
scroll events at BPM changes; SSFs use the independent instantaneous speed track.
Editor layers, bookmarks, mines, hitsound additions, sample resources and sound
effects have native counterparts.

Existing format metadata/provenance retain Quaver-only flags and IDs, hidden timing
lines, bookmark colors, rate-independent samples, additional keysounds/volumes,
and timing groups with per-note references. These are ordinary existing native
fields, so no document schema change is needed. Timing groups produce an import
warning because the editor cannot preview per-note scroll groups. Retention does
not establish complete Quaver gameplay or audio-preview parity.

Export updates editable data, preserves retained fields, computes the mine version,
sorts timelines stably, omits defaults, emits CRLF and validates the result before
publication. New notes and moved notes must fall on integer milliseconds. Roll,
stop/delay, warp, BGA, Kiai, changing layouts, unresolved sample references, sample-set
overrides, continuous SPEEDS transitions and other unsupported modeled data reject
with a classified diagnostic instead of being silently discarded. Absolute audio
paths must retain their imported identity or be staged as safe relative paths.
