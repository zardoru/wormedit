if(NOT DEFINED WORMEDIT_SOURCE_ROOT)
    message(FATAL_ERROR "WORMEDIT_SOURCE_ROOT is required")
endif()

file(GLOB_RECURSE protected_sources
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/core/*.h"
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/audio/*.h"
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/formats/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/core/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/audio/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/formats/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/application/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/application/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/editor/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/editor/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/field/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/field/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/sys/main.cpp"
)

set(violations "")
foreach(source IN LISTS protected_sources)
    file(STRINGS "${source}" lines)
    foreach(line IN LISTS lines)
        string(TOLOWER "${line}" lower_line)
        if(lower_line MATCHES "^[ \t]*#[ \t]*include.*(nuklear|imgui|clay)" OR
           line MATCHES "(^|[^A-Za-z0-9_])(nk_context|nk_[a-z0-9_]+|ImGui::|Clay_[A-Za-z0-9_]+)([^A-Za-z0-9_]|$)")
            list(APPEND violations "${source}")
            break()
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " formatted)
    message(FATAL_ERROR
        "UI toolkit types or includes crossed the frontend boundary:\n  ${formatted}"
    )
endif()
