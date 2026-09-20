set(WORMEDIT_STB_SOURCE_DIR "" CACHE PATH "Path to a pinned stb source checkout")

if(TARGET stb::image)
    return()
endif()

if(WORMEDIT_STB_SOURCE_DIR)
    if(NOT EXISTS "${WORMEDIT_STB_SOURCE_DIR}/stb_image.h" OR
       NOT EXISTS "${WORMEDIT_STB_SOURCE_DIR}/LICENSE")
        message(FATAL_ERROR "WORMEDIT_STB_SOURCE_DIR must contain stb_image.h and LICENSE")
    endif()
    set(wormedit_stb_source_dir "${WORMEDIT_STB_SOURCE_DIR}")
else()
    FetchContent_Declare(
        wormedit_stb_source
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
        SOURCE_SUBDIR __wormedit_dependency_source_only
    )
    FetchContent_MakeAvailable(wormedit_stb_source)
    set(wormedit_stb_source_dir "${wormedit_stb_source_SOURCE_DIR}")
endif()

add_library(wormedit_stb_image INTERFACE)
add_library(stb::image ALIAS wormedit_stb_image)
target_include_directories(wormedit_stb_image INTERFACE "${wormedit_stb_source_dir}")
