if(NOT DEFINED WORMEDIT_SOURCE_ROOT)
    message(FATAL_ERROR "WORMEDIT_SOURCE_ROOT is required")
endif()

file(READ "${WORMEDIT_SOURCE_ROOT}/src/sys/window.cpp" window_source)
if(NOT window_source MATCHES "SDL_CreateRenderer\\(window->handle, \"gpu\"\\)" OR
   NOT window_source MATCHES "SDL_GetGPURendererDevice\\(window->renderer\\)")
    message(FATAL_ERROR "window layer must require SDL3's GPU renderer and verify its GPU device")
endif()

file(READ "${WORMEDIT_SOURCE_ROOT}/src/ui/nuklear/player_preview_texture.cpp" preview_source)
if(NOT preview_source MATCHES "SDL_CreateTexture" OR
   NOT preview_source MATCHES "nk_(sub)?image_ptr")
    message(FATAL_ERROR "player preview must retain SDL textures through pointer image handles")
endif()

file(READ "${WORMEDIT_SOURCE_ROOT}/src/ui/nuklear/frontend.cpp" frontend_source)
if(NOT frontend_source MATCHES
   "SDL_SetRenderLogicalPresentation\\(g_renderer, window_width, window_height,")
    message(FATAL_ERROR
        "Nuklear frontend must map logical window coordinates across the full high-DPI backbuffer"
    )
endif()

file(READ "${WORMEDIT_SOURCE_ROOT}/CMakeLists.txt" root_cmake)
if(root_cmake MATCHES "OpenGL::GL" OR root_cmake MATCHES "find_package\\(OpenGL")
    message(FATAL_ERROR "application must not restore the retired OpenGL link dependency")
endif()

file(GLOB_RECURSE application_sources
    "${WORMEDIT_SOURCE_ROOT}/include/wormedit/*.h"
    "${WORMEDIT_SOURCE_ROOT}/src/sys/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/*.cpp"
    "${WORMEDIT_SOURCE_ROOT}/src/ui/*.h"
)
foreach(source IN LISTS application_sources)
    file(READ "${source}" contents)
    if(contents MATCHES "SDL_opengl|OpenGL/gl|SDL_GL_|gl[A-Z][A-Za-z0-9_]*\\(")
        message(FATAL_ERROR "retired OpenGL API remains in application source: ${source}")
    endif()
endforeach()
