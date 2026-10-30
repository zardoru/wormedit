cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED WORMEDIT_SOURCE_ROOT)
    message(FATAL_ERROR "WORMEDIT_SOURCE_ROOT is required")
endif()

file(GLOB_RECURSE protected_sources
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/core/*.h"
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/audio/*.h"
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/formats/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/core/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/core/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/audio/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/audio/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/formats/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/formats/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/application/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/application/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/editor/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/editor/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/field/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/field/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/sys/app/main.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/frontend/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/frontend/*.cpp"
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

# The frontend contract may import shared value vocabulary, never model/history,
# application services or backend implementation headers. New shared contracts
# require an explicit seam review here; a new name is not implicitly vocabulary.
set(shared_frontend_headers
    wormedit/core/export_audio.h
    wormedit/core/generator_types.h
    wormedit/core/geometry.h
    wormedit/core/id.h
    wormedit/core/metadata_inheritance_types.h
    wormedit/core/metadata_limits.h
    wormedit/core/osu_hitsample.h
    wormedit/core/position_types.h
    wormedit/core/preview_mix.h
    wormedit/core/resource_vocabulary.h
    wormedit/core/shortcut_vocabulary.h
    wormedit/core/timing_edit_policy.h
    wormedit/core/timing_links_types.h
    wormedit/core/ui_layout_preferences.h
    wormedit/core/visual_layout.h
    wormedit/core/vocabulary.h
)
file(GLOB_RECURSE frontend_sources
    "${WORMEDIT_SOURCE_ROOT}/src/ui/frontend/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/frontend/*.cpp")
set(violations "")
foreach(source IN LISTS frontend_sources)
    get_filename_component(source_dir "${source}" DIRECTORY)
    file(STRINGS "${source}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
    foreach(line IN LISTS includes)
        string(REGEX MATCH "include[ \t]*[<\"]([^>\"]+)[>\"]" matched "${line}")
        set(header "${CMAKE_MATCH_1}")
        if(header IN_LIST shared_frontend_headers)
            continue()
        endif()
        if(header MATCHES "^wormedit/")
            list(APPEND violations "${source}: ${header}")
            continue()
        endif()
        # Resolve first-party quoted/project paths as well as canonical includes,
        # so ../core and src/sys escape paths cannot bypass the direct seam.
        foreach(candidate IN ITEMS "${source_dir}/${header}"
                "${WORMEDIT_SOURCE_ROOT}/src/${header}"
                "${WORMEDIT_SOURCE_ROOT}/include/${header}")
            if(EXISTS "${candidate}")
                file(REAL_PATH "${candidate}" resolved)
                file(RELATIVE_PATH local "${WORMEDIT_SOURCE_ROOT}/src/ui/frontend" "${resolved}")
                if(local MATCHES "^\\.\\./" OR IS_ABSOLUTE "${local}")
                    list(APPEND violations "${source}: ${header}")
                endif()
                break()
            endif()
        endforeach()
    endforeach()
endforeach()
if(violations)
    list(JOIN violations "\n  " formatted)
    message(FATAL_ERROR "Non-vocabulary imports crossed the frontend boundary:\n  ${formatted}")
endif()
