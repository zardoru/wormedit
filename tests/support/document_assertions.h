#pragma once

#include <wormedit/formats/native_project.h>
#include <catch2/catch_test_macros.hpp>
#include <source_location>

namespace wormedit::test {
inline void check_same_document(const song_document_t& actual, const song_document_t& expected,
                               std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    const auto actual_text = write_native_project(actual);
    const auto expected_text = write_native_project(expected);
    REQUIRE(actual_text.value);
    REQUIRE(expected_text.value);
    CHECK(*actual_text.value == *expected_text.value);
    CHECK(actual.revision == expected.revision);
}
} // namespace wormedit::test
