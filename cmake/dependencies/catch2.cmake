set(WORMEDIT_CATCH2_SOURCE_DIR "" CACHE PATH "Path to a Catch2 v3 source checkout")

find_package(Catch2 3 CONFIG QUIET)

if(NOT TARGET Catch2::Catch2WithMain)
    if(WORMEDIT_CATCH2_SOURCE_DIR)
        FetchContent_Declare(Catch2 SOURCE_DIR "${WORMEDIT_CATCH2_SOURCE_DIR}")
    else()
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG 2b60af89e23d28eefc081bc930831ee9d45ea58b
        )
    endif()

    FetchContent_MakeAvailable(Catch2)
endif()

if(NOT TARGET Catch2::Catch2WithMain)
    message(FATAL_ERROR "Catch2 v3 did not provide Catch2::Catch2WithMain")
endif()

if(DEFINED catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
