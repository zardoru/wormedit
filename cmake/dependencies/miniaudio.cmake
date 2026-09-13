set(MINIAUDIO_SOURCE_DIR "" CACHE PATH "Path to the pinned miniaudio source checkout")

if(MINIAUDIO_SOURCE_DIR)
    if(NOT EXISTS "${MINIAUDIO_SOURCE_DIR}/miniaudio.h" OR
       NOT EXISTS "${MINIAUDIO_SOURCE_DIR}/extras/stb_vorbis.c")
        message(FATAL_ERROR "MINIAUDIO_SOURCE_DIR must contain miniaudio.h and extras/stb_vorbis.c")
    endif()
    set(wormedit_miniaudio_source_dir "${MINIAUDIO_SOURCE_DIR}")
else()
    FetchContent_Declare(
        wormedit_miniaudio_source
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
        # Wormedit owns the decoder target and only needs the pinned sources.
        SOURCE_SUBDIR __wormedit_dependency_source_only
    )
    FetchContent_MakeAvailable(wormedit_miniaudio_source)
    set(wormedit_miniaudio_source_dir "${wormedit_miniaudio_source_SOURCE_DIR}")
endif()

add_library(wormedit_audio_decoder
    "${CMAKE_SOURCE_DIR}/src/audio/block_cache.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio/miniaudio_backend.c"
    "${CMAKE_SOURCE_DIR}/src/audio/decoder.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio/transport.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio/waveform.cpp"
)
add_library(wormedit::audio_decoder ALIAS wormedit_audio_decoder)
target_include_directories(wormedit_audio_decoder
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
    PRIVATE "${wormedit_miniaudio_source_dir}"
)
target_compile_features(wormedit_audio_decoder PUBLIC cxx_std_20)
target_compile_definitions(wormedit_audio_decoder PRIVATE
    MA_NO_DEVICEIO
    MA_NO_ENCODING
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
    MA_NO_GENERATION
)
set_target_properties(wormedit_audio_decoder PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

find_package(Threads REQUIRED)
target_link_libraries(wormedit_audio_decoder PRIVATE Threads::Threads)
if(UNIX)
    target_link_libraries(wormedit_audio_decoder PRIVATE m)
endif()

if(MSVC)
    target_compile_options(wormedit_audio_decoder PRIVATE /W4 /permissive-)
else()
    target_compile_options(wormedit_audio_decoder PRIVATE -Wall -Wextra -Wpedantic)
endif()
