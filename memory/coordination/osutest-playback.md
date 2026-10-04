# osu! playback regression

User reported Space playback blocked by an "event occurs before audio frame zero"
warning after bounded imports. Root reproduced seven of eight supplied charts
failing schedule construction: the actual fatal diagnostic is metronome pulse
row divisibility in shortened unmetered import measures. Earlier warnings obscure
that error in preview feedback.

Root owns `src/core/timing.cpp`, `tests/core/timing_tests.cpp`,
`src/sys/chart_preview_state.cpp`, and existing preview runtime regression tests.
Supporting `src/sys/timing_settings.h` keeps the count-in UI's pulse validation
consistent with partial unmetered measures; existing timing settings tests cover it.
Owned test paths are `tests/core/timing_tests.cpp`,
`tests/sys/sdl_preview_output_tests.cpp`, `tests/sys/timing_settings_tests.cpp`.
Notification coordinator has been informed; no overlap with delegated edits.
Keep imported documents, source timestamps and raw files unchanged. Use ordinary
quarter-note pulses in unmetered partial measures, include the final partial
beat's onset, and retain count-in quarter-note spacing. Preserve explicit meter,
negative-frame omission, queue limits and fatal-error behavior. Select an actual
error diagnostic when compilation fails rather than the first preceding warning.

Verify real eight-chart preview schedules, actual runtime compilation/Space route,
core and focused runtime tests, configured clang-tidy, full integration checks.
Isolated real-window verification remains blocked by denied sandbox escalation.

Verified: application/core/SDL/frame targets build; 1,370 core cases pass,
76 focused preview/count-in/SDL cases pass, plus three timing-settings cases.
Five new schedule/runtime cases pass (27 assertions). Configured clang-tidy
reports no production diagnostics or new test complexity violations; existing
large Catch cases retain their previous scores. All eight real chart schedules
now succeed. Independent runtime probe registers the real `Hybris.ogg` and
checks start at the cursor, clock advancement, pause, resume, and music binding
for all eight native imports with SDL dummy audio. Logs are under
`/tmp/wormedit-batch-implementation/osutest-playback-*`.
