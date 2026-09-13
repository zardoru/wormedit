# BMS audio fallback audit

Wormedit preserves each authored `#WAVxx` token separately from the path selected for preview. Resolution is deterministic and path-safe:

1. Try the authored relative path exactly.
2. If it is absent, try the same directory and stem with `.wav`, `.ogg`, then `.flac`, skipping the extension already attempted.
3. Match these extensions case-insensitively but preserve the selected on-disk spelling.
4. If several files match under case folding or another ambiguity exists, report all candidates and require a choice.

A present but corrupt or unsupported exact file produces a decode diagnostic; it is not silently replaced. Previewing a fallback does not rewrite the BMS definition. A separate normalize/package command can materialize the selected name after the author reviews the change.

The current Raindrop `src/sndio/Audiofile.cpp` is useful reference behavior: it supports WAV/FLAC through libsndfile plus MP3 and Ogg decoders, and its filename rearrangement tries Ogg for a missing WAV and WAV for other missing extensions. It does not provide the required full WAV/Ogg/FLAC search order, ambiguity reporting, or authored/resolved identity split, so wormedit owns this resolver and its tests.

Positive fixtures include exact WAV/Ogg/FLAC paths, missing authored extensions with each fallback present, mixed-case extensions, and retained source tokens. Negative fixtures include traversal/absolute paths, missing candidates, corrupt exact files, ambiguous case-folded names, several fallback candidates requiring policy, and unsupported media. Failed resolution never mutates the resource catalog.
