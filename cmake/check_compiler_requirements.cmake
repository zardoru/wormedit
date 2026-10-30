# Diagnose the compiler/library pair before fetching or compiling dependencies.
include(CheckCXXCompilerFlag)

if(NOT WORMEDIT_HAS_REQUIRED_CXX23_LIBRARY)
    try_compile(WORMEDIT_HAS_REQUIRED_CXX23_LIBRARY
        SOURCE_FROM_CONTENT wormedit_cxx23.cpp [=[
    #include <expected>
    #include <functional>
    int main() {
        std::expected<int, int> value{1};
        auto add = std::bind_back([](int a, int b) { return a + b; }, 2);
        return add(*value) == 3 ? 0 : 1;
    }
]=]
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        OUTPUT_VARIABLE WORMEDIT_CXX23_PROBE_OUTPUT
    )
endif()
if(NOT WORMEDIT_HAS_REQUIRED_CXX23_LIBRARY)
    message(FATAL_ERROR
        "Wormedit requires C++23 std::expected and std::bind_back. "
        "See docs/building.md for tested compiler/library pairs. "
        "Compiler probe output:\n${WORMEDIT_CXX23_PROBE_OUTPUT}"
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    check_cxx_compiler_flag("-Wmissing-designated-field-initializers"
        WORMEDIT_HAS_DESIGNATED_INITIALIZER_WARNING)
    if(NOT WORMEDIT_HAS_DESIGNATED_INITIALIZER_WARNING)
        message(FATAL_ERROR
            "Wormedit requires Clang's separate designated-initializer warning "
            "(Clang 19 or a capable AppleClang). Upgrade Clang; the positional "
            "missing-field warning must remain enabled."
        )
    endif()
endif()
