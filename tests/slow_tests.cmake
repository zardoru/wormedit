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
