# Resolve the PE imports recursively at install time, including FFmpeg's own imports.
# Only Windows-provided libraries are excluded; missing third-party DLLs fail installation.
# Dependencies have already declared their SDK installs. Put application-owned
# rules (including the shared notices included later) in a separate component.
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME Wormedit)
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
    file(REAL_PATH "${directory}/../bin" wormedit_sdk_runtime_directory)
    list(APPEND wormedit_windows_runtime_dirs "${wormedit_sdk_runtime_directory}")
endforeach()
# The SDK staging owner colocates matching GCC runtimes with vendor DLLs.
# Keep one canonical directory so nested imports do not select a second,
# byte-identical winpthreads copy from the compiler's installation.
list(REMOVE_DUPLICATES wormedit_windows_runtime_dirs)
get_target_property(wormedit_sdl_runtime_target SDL3::SDL3 ALIASED_TARGET)
install(TARGETS wormedit ${wormedit_sdl_runtime_target}
    RUNTIME_DEPENDENCY_SET wormedit_windows_runtime
    RUNTIME DESTINATION "."
    ARCHIVE EXCLUDE_FROM_ALL COMPONENT Development
    LIBRARY EXCLUDE_FROM_ALL COMPONENT Development)
# VID-3 may not yet import the encoder object from its static boundary. Still
# ship/scan every promised encoder runtime root using the same PE dependency
# owner, rather than relying on accidental application imports or exclusions.
if(WORMEDIT_VIDEO_ENCODER_AVAILABLE)
    set(wormedit_encoder_runtime_libraries avformat avcodec avutil swscale swresample openh264 opus)
    # MinGW's upstream libvpx has no shared target. Its approved static archive
    # is private input to shared avcodec; no nonexistent vpx DLL is required.
    foreach(library IN LISTS wormedit_encoder_runtime_libraries)
        set(wormedit_encoder_runtime_candidates "")
        foreach(directory IN LISTS WORMEDIT_FFMPEG_LIBRARY_DIRS)
            file(GLOB candidates "${directory}/../bin/*${library}*.dll")
            foreach(candidate IN LISTS candidates)
                file(REAL_PATH "${candidate}" candidate)
                list(APPEND wormedit_encoder_runtime_candidates "${candidate}")
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES wormedit_encoder_runtime_candidates)
        list(LENGTH wormedit_encoder_runtime_candidates candidate_count)
        if(NOT candidate_count EQUAL 1)
            message(FATAL_ERROR "Expected one pinned video SDK ${library} DLL, found: ${wormedit_encoder_runtime_candidates}")
        endif()
        add_library(wormedit_encoder_runtime_${library} SHARED IMPORTED)
        set_target_properties(wormedit_encoder_runtime_${library} PROPERTIES
            IMPORTED_LOCATION "${wormedit_encoder_runtime_candidates}")
        install(IMPORTED_RUNTIME_ARTIFACTS wormedit_encoder_runtime_${library}
            RUNTIME_DEPENDENCY_SET wormedit_windows_runtime RUNTIME DESTINATION ".")
    endforeach()
endif()
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
