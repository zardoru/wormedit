# Resolve the PE imports recursively at install time, including FFmpeg's own imports.
# Only Windows-provided libraries are excluded; missing third-party DLLs fail installation.
set(wormedit_windows_system_dlls
    advapi32 avrt bcrypt cfgmgr32 crypt32 d3d11 d3d12 dcomp dsound dwmapi dxgi
    gdi32 hid imm32 iphlpapi kernel32 mf mfplat mfreadwrite msvcrt ncrypt
    ole32 oleaut32 opengl32 powrprof propsys psapi secur32 setupapi shell32
    shlwapi user32 userenv ucrtbase version winmm winspool ws2_32)
set(wormedit_windows_system_patterns "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-")
foreach(dll IN LISTS wormedit_windows_system_dlls)
    string(LENGTH "${dll}" length)
    math(EXPR last "${length} - 1")
    set(pattern "^")
    foreach(index RANGE ${last})
        string(SUBSTRING "${dll}" ${index} 1 character)
        string(TOUPPER "${character}" upper)
        string(APPEND pattern "[${character}${upper}]")
    endforeach()
    list(APPEND wormedit_windows_system_patterns "${pattern}[.][Dd][Ll][Ll]$")
endforeach()
set(wormedit_windows_runtime_dirs "$<TARGET_FILE_DIR:SDL3::SDL3>")
foreach(directory IN LISTS WORMEDIT_FFMPEG_LIBRARY_DIRS)
    list(APPEND wormedit_windows_runtime_dirs "${directory}/../bin")
endforeach()
get_target_property(wormedit_sdl_runtime_target SDL3::SDL3 ALIASED_TARGET)
install(TARGETS wormedit ${wormedit_sdl_runtime_target}
    RUNTIME_DEPENDENCY_SET wormedit_windows_runtime
    RUNTIME DESTINATION "."
    ARCHIVE EXCLUDE_FROM_ALL
    LIBRARY EXCLUDE_FROM_ALL)
if(CMAKE_CROSSCOMPILING)
    # Install scripts otherwise infer ELF from the Linux host, despite the PE inputs.
    install(CODE [[
        set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
        set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
    ]])
endif()
install(RUNTIME_DEPENDENCY_SET wormedit_windows_runtime
    DIRECTORIES ${wormedit_windows_runtime_dirs}
    # SDL is installed and scanned as an explicit root above. Avoid resolving
    # its mixed-case import through the PE resolver's lowercase Linux lookup.
    PRE_EXCLUDE_REGEXES ${wormedit_windows_system_patterns} "^[Ss][Dd][Ll]3[.][Dd][Ll][Ll]$"
    RUNTIME DESTINATION ".")
install(FILES "${sdl3_SOURCE_DIR}/LICENSE.txt" DESTINATION "third-party/SDL3")
install(FILES "${nuklear_SOURCE_DIR}/LICENSE" DESTINATION "third-party/nuklear")
install(FILES "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION "third-party/nlohmann-json")
if(WORMEDIT_ENABLE_AUDIO_DECODER)
    install(FILES "${wormedit_miniaudio_source_dir}/LICENSE" DESTINATION "third-party/miniaudio")
endif()
install(FILES "${wormedit_zlib_SOURCE_DIR}/LICENSE" DESTINATION "third-party/zlib")
