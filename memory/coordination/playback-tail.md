# T-376 playback tail and navigation

## Reviewed cause and correction

The imported Hybris charts finish their bounded timing index near 157.215 s
(BEGINNER near 157.598 s), while the unchanged original Hybris.ogg contains
7,314,712 frames at 44,100 Hz (165.866485 s). Audio after the chart endpoint is
legitimate. `musical_position_at` intentionally rejects times past chart bounds;
`audio_transport_frame.cpp` previously published no playhead row during that
tail. Playing wheel/keyboard navigation routes through `skip_playing_playhead`,
which then rejected navigation with “The playhead row is not available yet.”

The consumer now retains exact in-chart inversion and clamps the endpoint and
legitimate audio tail to `total_rows - 1`. Fallback applies only to the core
position-out-of-range result after the exact ceil-converted final after-stops
sample frame. Invalid timing and arithmetic failures remain rejected. Audio
schedule/session/output lifecycle code is unchanged.

## Evidence

- Root swept 256-frame increments through 150–158 s in all eight original imported
  charts: no in-chart inverse arithmetic failures; every failure followed the
  actual chart endpoint. `/tmp/wormedit-playback-tail-root/inverse-frames-before.log`.
- Root standalone actual runtime used unchanged main audio libraries and SDL
  objects read-only plus the changed consumer source: all eight charts play at
  160 s, publish their final editable row, accept a backward skip into the chart,
  and naturally finish after seeking to `end_frame - 1024`. Both output playing
  and session active become false; no paused state remains. Full original music
  end frame remains 7,314,712.
  `/tmp/wormedit-playback-tail-root/runtime-before-integration.log`.
- Worktree Debug targets built: `wormedit_sdl_audio_tests`, `wormedit_audio_tests`,
  `wormedit_core_tests`, and `wormedit_frontend_event_harness_tests`.
- Coordinator independently ran SDL audio runtime: 32 cases / 3,491 assertions
  pass. `/tmp/playback-tail-coordinator-sdl.log`.
- Windowless real frontend event harness: 134/134 CTest cases pass, including
  playing-tail and stopped backward-wheel routing.
  `/tmp/playback-tail-coordinator-events.log`.
- Pure row projection regression covers normal row, exact endpoint, tail, and
  invalid timing. Decoded music-tail regression proves the preview end includes
  a full source beyond the two-second visual chart endpoint.
- Focused built audio/preview/playhead/documentation CTest: 308/308 pass.
  `/tmp/playback-tail-coordinator-focused-built.log`. The first broad filter also
  selected three unrelated player-preview `_NOT_BUILT` placeholders; the final
  verified command explicitly excludes those unbuilt targets.
- `documentation.chart_editor_state` passes. Runtime consumer clang-tidy
  cognitive-complexity check reports no violations. Existing macro-expanded
  test complexity violations remain unchanged; the new music-tail test was
  extracted into a helper to satisfy the threshold.

## Integration and remaining verification

Root integrated the reviewed change as `d64c048`, repaired the main app build,
and repeated the actual eight-chart tail/backward-seek/natural-EOF probe against
main successfully (`/tmp/wormedit-playback-tail-root/runtime-main.log`). Three
focused main regression cases pass. Root runs the full suite after tooltip
integration. Real-window visual verification remains
pending: the prior Xvfb request was rejected by automatic sandbox approval, and
no retry or user desktop window was attempted.

Offline builds read dependency source trees in the main build's `_deps` and put
all dependency binary directories in this worktree. An initial executor
reconfiguration accidentally pointed `FETCHCONTENT_BASE_DIR` at main `_deps`;
this was detected, the build stopped, and all binary dirs were corrected before
final verification. The interrupted shared dependency build left exactly one zero-byte object,
`SDL_blendline.c.o`, causing the main relink to miss `SDL_BlendLines`. Root
removed only that empty object and rebuilt successfully. No tracked source or
CMake cache repair was needed; original chart/audio files remained unchanged.
