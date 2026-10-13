#include "../../src/core/text_util.h"
#include <wormedit/core/natural_order.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("natural name order compares digit runs and breaks ties deterministically",
          "[unit][core][natural-order]") {
    CHECK(natural_name_less("snd2", "snd10"));
    CHECK(natural_name_less("a1b2", "a1b10"));
    CHECK(natural_name_less("snd01", "snd1"));
}

TEST_CASE("natural name order uses folded text and raw bytes for ties",
          "[unit][core][natural-order]") {
    CHECK(natural_name_less("a01", "A1"));
    CHECK(natural_name_less("Alpha", "alpha"));
    CHECK_FALSE(natural_name_less("alpha", "Alpha"));
    CHECK_FALSE(natural_name_less("snd10", "snd2"));
}

namespace {
void check_ascii_lowercase_byte(unsigned value) {
    const auto byte = static_cast<char>(value);
    const auto expected = value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
    CHECK(static_cast<unsigned char>(text_util::ascii_lowercase(byte)) == expected);
}
} // namespace

TEST_CASE("ASCII lowercase preserves every other byte", "[unit][core][text]") {
    CHECK(text_util::ascii_lowercase(std::string_view{"AZaz é"}) == "azaz é");
    for (unsigned value = 0; value < 256; ++value) check_ascii_lowercase_byte(value);
}

TEST_CASE("ASCII equality preserves length and non-ASCII distinctions", "[unit][core][text]") {
    CHECK(text_util::ascii_iequal("Sound", "sOUND"));
    CHECK_FALSE(text_util::ascii_iequal("Sound", "Sounds"));
    CHECK_FALSE(text_util::ascii_iequal("É", "é"));
    CHECK(text_util::ascii_iequal(std::string_view{"A\0Z", 3}, std::string_view{"a\0z", 3}));
}

TEST_CASE("ASCII ordering compares prefixes and unsigned bytes", "[unit][core][text]") {
    CHECK_FALSE(text_util::ascii_iless("Sound", "sOUND"));
    CHECK(text_util::ascii_iless("ab", "ABC"));
    CHECK(text_util::ascii_iless("z", "é"));
}

TEST_CASE("ASCII extension normalization preserves absent extensions", "[unit][core][text]") {
    CHECK(text_util::lowercase_extension(std::filesystem::path{"dir/File.OGG"}) == ".ogg");
    CHECK(text_util::lowercase_extension(std::filesystem::path{"no-extension"}).empty());
}
