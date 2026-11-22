#include <wormedit/core/release_version.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

namespace {
auto version(std::string_view text) -> release_version_t {
    const auto parsed = release_version_t::from(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}
} // namespace

TEST_CASE("release versions parse and round-trip", "[core][updates]") {
    CHECK(version("1.0.0").to_string() == "1.0.0");
    CHECK(version("v1.2.3-rc4").to_string() == "1.2.3-rc4");
    CHECK(version("1.0.0-nightly.20261122.abcd1234").is_nightly());
    CHECK(version("1.0.0+meta").to_string() == "1.0.0");
    CHECK_FALSE(version("1.0.0-rc4").is_nightly());
    for (const auto* bad : {"", "1", "1.0", "1.0.0.0", "01.0.0", "1.0.x", "1.0.0-", "1.0.0-r c", "-1.0.0"})
        CHECK_FALSE(release_version_t::from(bad).has_value());
}

TEST_CASE("release candidates order by number, then below the final release", "[core][updates]") {
    CHECK(version("1.0.0-rc3").compare(version("1.0.0-rc4")) < 0);
    CHECK(version("1.0.0-rc9").compare(version("1.0.0-rc10")) < 0);
    CHECK(version("1.0.0-rc4").compare(version("1.0.0")) < 0);
    CHECK(version("1.0.0").compare(version("1.0.1-rc1")) < 0);
    CHECK(version("1.1.0").compare(version("1.0.9")) > 0);
    CHECK(version("1.0.0-rc4").compare(version("1.0.0-rc4")) == 0);
    CHECK(version("1.0.0-alpha").compare(version("1.0.0-alpha.1")) < 0);
    CHECK(version("1.0.0-alpha.1").compare(version("1.0.0-alpha.beta")) < 0);
}
