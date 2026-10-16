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
    "core.BMS WAV slot batches accept 1200 files with one-step undo and redo"
    "core.BMSON lane_mode packs a whole channel beyond the interactive selection bound"
    "core.chart comparison keeps fifty-thousand-note equal difficulties bounded"
    "core.lane assignment remains bounded for fifty thousand selected notes"
    "core.loss matrix classifies every diagnostic for every fixture and writer")

# audio: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_audio_tests_TESTS
    "audio.BMSON channel waveform maps exact source cuts into its musical viewport"
    "audio.Large keysound inventories register every asset and metronome accent"
    "audio.audio.time_stretch all supported rates are deterministic for partial stereo transients"
    "audio.pending registration opens no decoder until a probe runs"
)

# sdl-audio: preserve regression sizes and matrices; keep these in the full gate.
wormedit_label_slow_tests(wormedit_sdl_audio_tests_TESTS
    "sdl-audio.Loop PCM stays continuous without frontend service at every rate and pitch mode"
    "sdl-audio.One-frame and callback-sized loops remain continuous with either pitch mode"
    "sdl-audio.SDL BGM lane mixing mutes and resumes a live stem without restarting playback"
    "sdl-audio.SDL keep-pitch output preserves sine and supports seeks during EOF drain"
    "sdl-audio.SDL preview completes 2000 concurrent seek start Stop and rate transitions"
    "sdl-audio.SDL preview repeatedly closes idle and stopped refill workers"
    "sdl-audio.SDL refill worker advances through prime windows without frame service and hard Stop stays silent"
    "sdl-audio.SDL selection loops restart at every rate with either pitch mode"
)
