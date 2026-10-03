# osu!mania version-profile evidence

This note bounds Wormedit's stable-v14 and lazer-v128 profiles to behavior supported by pinned official sources. It is evidence for fixtures and codec tests, not a claim that every syntactically accepted field has identical meaning in every osu! release.

## Stable v14

The official file-format reference at osu-wiki revision [`723f473aae7f3894f9f2d8431c6316f29926c297`](https://github.com/ppy/osu-wiki/blob/723f473aae7f3894f9f2d8431c6316f29926c297/wiki/Client/File_formats/osu_%28file_format%29/en.md) identifies v14 as the latest stable format and documents the standard General, Editor, Metadata, Difficulty, Events, TimingPoints, Colours, and HitObjects inventories.

Wormedit uses that inventory as positive stable-profile evidence. A field omitted from the wiki is not, by omission alone, classified as forbidden or lazer-only.

## Lazer v128

At the current osu! `master` head checked on 2026-10-02, revision [`c834803ea9988159d67e1dc5d99cfaccada685d9`](https://github.com/ppy/osu/blob/c834803ea9988159d67e1dc5d99cfaccada685d9/osu.Game/Beatmaps/Formats/LegacyBeatmapEncoder.cs), `LegacyBeatmapEncoder` still declares `FIRST_LAZER_VERSION = 128` and writes that version in `Encode`. Thus v128 remains the latest version emitted by the current lazer encoder; no newer profile is needed. Its emitted mania metadata includes the standard sections plus `General.SpecialStyle`, `General.WidescreenStoryboard`, optional `General.SamplesMatchPlaybackRate`, and `Editor.VelocityPresets`.

The paired fixture exercises that observed encoder inventory together with fractional tap and hold timestamps. Wormedit must retain the declared version, ordered safe fields, and exact v128 timestamp spelling across import/export/re-import. Explicit conversion to stable v14 may quantize timestamps only with a typed diagnostic; the current evidence does not authorize silently deleting metadata fields solely because the target header changes.
