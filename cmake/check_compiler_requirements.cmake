# Diagnose the compiler/library pair before fetching or compiling dependencies.
include(CheckCXXSourceCompiles)
include(CheckCXXCompilerFlag)

check_cxx_source_compiles([=[
    #include <expected>
    #include <functional>
    int main() {
        std::expected<int, int> value{1};
        auto add = std::bind_back([](int a, int b) { return a + b; }, 2);
        return add(*value) == 3 ? 0 : 1;
    }
]=] WORMEDIT_HAS_REQUIRED_CXX23_LIBRARY)
if(NOT WORMEDIT_HAS_REQUIRED_CXX23_LIBRARY)
    message(FATAL_ERROR
        "Wormedit requires C++23 std::expected and std::bind_back. "
        "Use a capable compiler AND standard library (Linux CI uses GCC 14, or "
        "Clang 19 with libstdc++ 14). Check CMake's configure log for the failed probe."
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
