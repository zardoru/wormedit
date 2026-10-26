# Warning flags for Wormedit's own targets. Dependencies never get these.
#
# Designated initializers that leave members to their default member initializers
# (`{.diagnostics = d}`) are the project idiom (owner decision, T-669), so their warning is off.
# Clang reports that case separately and still flags positional braces that omit trailing
# members; GCC has no separate switch, so it turns the whole missing-field check off.

option(WORMEDIT_WARNINGS_AS_ERRORS "Treat warnings in Wormedit's own targets as errors" ON)

function(wormedit_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(WORMEDIT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        return()
    endif()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-designated-field-initializers>)
    else()
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
    if(WORMEDIT_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
        # GCC's optimizer-dependent flow warnings report false positives inside libstdc++
        # (string destructors on constructor unwind paths with MinGW GCC 16, vector
        # assignment bounds with GCC 14) that change with inlining. They stay visible as
        # warnings but do not fail release builds; Clang still checks the same code.
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE
                -Wno-error=maybe-uninitialized -Wno-error=array-bounds)
        endif()
    endif()
endfunction()
