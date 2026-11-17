#pragma once

#include "sys/app/frame_timing.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

// State shared by every frame-timing test file. The global allocation hook in
// frame_timing_hooks.cpp counts only while a test enables it, and the Compose input selector
// records its UI-only resource-placement notification here.
inline std::vector<uint64_t> placed_resource_notifications;
inline thread_local bool count_allocations = false;
inline thread_local std::size_t allocations = 0;

namespace {
inline void expect_compose_test(bool condition,
                                std::source_location source = std::source_location::current()) {
    INFO("Compose expectation at " << source.line());
    REQUIRE(condition);
}

inline void expect_gpu_work(bool value) { REQUIRE(value); }

template<class Actual, class Expected>
void check_heap_counter(const Actual& actual, const Expected& expected,
                        std::source_location location = std::source_location::current()) {
    INFO("heap assertion at " << location.file_name() << ':' << location.line()
         << ", actual=" << actual << ", expected=" << expected);
    CHECK(actual == expected);
}

inline void expect_static_style(bool condition,
                                std::source_location source=std::source_location::current()) {
    INFO("static style expectation at " << source.line());
    REQUIRE(condition);
}

inline void t475_check(bool value, std::source_location source = std::source_location::current()) {
    INFO("dense projection assertion at " << source.line());
    CHECK(value);
}

inline void t475_require(const auto& value) { REQUIRE(static_cast<bool>(value)); }
} // namespace
