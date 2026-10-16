# CTest reads this after Catch's PRE_TEST discovery includes have populated TEST_LIST.
# Its LABELS setter appends to the discovered labels; keep every other property intact.
function(wormedit_label_slow_tests discovered_tests)
    foreach(test_name IN LISTS ARGN)
        if(test_name IN_LIST ${discovered_tests})
            set_tests_properties("${test_name}" PROPERTIES LABELS slow)
        endif()
    endforeach()
endfunction()

# Core: explicit stress sizes, the full format matrix, and the 4096+1 selection bound.
wormedit_label_slow_tests(wormedit_core_tests_TESTS
    "core.BMSON lane_mode packs a whole channel beyond the interactive selection bound"
    "core.lane assignment remains bounded for fifty thousand selected notes"
    "core.loss matrix classifies every diagnostic for every fixture and writer")

# audio: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_audio_tests_TESTS
    "audio.Large keysound inventories register every asset and metronome accent"
)

# sdl-audio: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_sdl_audio_tests_TESTS
    "sdl-audio.Loop PCM stays continuous without frontend service at every rate and pitch mode"
    "sdl-audio.One-frame and callback-sized loops remain continuous with either pitch mode"
    "sdl-audio.SDL selection loops restart at every rate with either pitch mode"
)

# frame-timing: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_frame_timing_tests_TESTS
    "frame-timing.Affine waveform clock preserves BPM pause warp boundaries during600 playback frames"
    "frame-timing.BMSON editor export publishes sliced audio and reports conversion losses"
    "frame-timing.Compose BGM arena rows preserve fresh and retained projection labels and clipping"
    "frame-timing.Compose BGM arena storage remains stable across 1000 reused frames"
    "frame-timing.Compose controls retain measure geometry until the document revision changes"
    "frame-timing.Compose grid arena exhaustion publishes only complete paired units"
    "frame-timing.Compose marker arena exhaustion publishes complete per-anchor prefixes"
    "frame-timing.Compose pinch dispatch preserves anchor and cancels prior viewport motion"
    "frame-timing.Compose pinch rejects invalid ratios and anchors before motion cancellation"
    "frame-timing.Compose semantic arena exhaustion publishes only complete named rows"
    "frame-timing.Frame timing projects real play follow on a 50000 note chart"
    "frame-timing.Indexed retained scalar publication matches full composition while following and reversing"
)

# frame-arena: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_frame_projection_arena_tests_TESTS
    "frame-arena.Metadata arena list storage stays bounded through 1000 dense resets"
)

# frontend-state: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_frontend_event_harness_tests_TESTS
    "frontend-state.Bundled CJK fallback covers Japanese Chinese and Hangul before submission"
    "frontend-state.CJK discovery bounds unsupported invalid and excessive text"
    "frontend-state.Missing CJK bundle retains usable Latin fallback"
    "frontend-state.Numeric display font keeps its 28px stable handle through atlas rebakes"
)
