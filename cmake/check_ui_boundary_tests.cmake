cmake_minimum_required(VERSION 3.20)

set(fixture_root "${CMAKE_CURRENT_BINARY_DIR}/ui-boundary-guard-fixtures")
set(checker "${CMAKE_CURRENT_LIST_DIR}/check_ui_boundary.cmake")

function(check_boundary_case name source content rejected diagnostic)
    set(root "${fixture_root}/${name}")
    file(REMOVE_RECURSE "${root}")
    get_filename_component(directory "${root}/${source}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${root}/${source}" "${content}")
    # A real target makes the relative escape case independent of include spelling.
    file(MAKE_DIRECTORY "${root}/src/core")
    file(WRITE "${root}/src/core/private_state.h" "// Domain state\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DWORMEDIT_SOURCE_ROOT=${root}" -P "${checker}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(rejected)
        if(status EQUAL 0 OR NOT "${output}${errors}" MATCHES "${diagnostic}")
            message(FATAL_ERROR "${name}: expected rejection (${diagnostic}), got ${status}: ${output}${errors}")
        endif()
    elseif(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: valid vocabulary rejected: ${output}${errors}")
    endif()
    file(REMOVE_RECURSE "${root}")
endfunction()

foreach(domain IN ITEMS core audio formats)
    check_boundary_case("private-${domain}" "src/${domain}/internal/private.h"
        "#include <nuklear.h>\n" TRUE "UI toolkit types or includes")
endforeach()
foreach(header IN ITEMS document command interaction_types)
    check_boundary_case("frontend-${header}" "src/ui/frontend/private.h"
        "#include <wormedit/core/${header}.h>\n" TRUE "Non-vocabulary imports")
endforeach()
check_boundary_case(frontend-relative src/ui/frontend/private.h
    "#include \"../../core/private_state.h\"\n" TRUE "Non-vocabulary imports")
check_boundary_case(frontend-vocabulary src/ui/frontend/private.h
    "#include <wormedit/core/vocabulary.h>\n#include <wormedit/core/position_types.h>\n#include <vector>\n"
    FALSE "")
message(STATUS "Frontend boundary guard: private toolkit, domain import, relative escape and vocabulary cases pass")
