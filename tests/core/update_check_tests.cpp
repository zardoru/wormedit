#include <wormedit/core/update_check.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

namespace {
constexpr auto good_manifest = R"({
  "schema": 1,
  "channels": {"stable": {"version": "1.0.0-rc5", "url": "https://example.test/rc5",
                          "notes_url": "https://example.test/notes", "published": "2026-11-22"}},
  "future_key": true
})";
} // namespace

TEST_CASE("the manifest yields the stable entry and ignores unknown keys", "[core][updates]") {
    const auto entry = parse_update_manifest(good_manifest, update_channel_t::stable);
    REQUIRE(entry.has_value());
    CHECK(entry->version == "1.0.0-rc5");
    CHECK(entry->url == "https://example.test/rc5");
    CHECK(entry->notes_url == "https://example.test/notes");
}

TEST_CASE("the manifest rejects malformed or unsafe content", "[core][updates]") {
    const auto stable = update_channel_t::stable;
    CHECK_FALSE(parse_update_manifest("", stable).has_value());
    CHECK_FALSE(parse_update_manifest("[]", stable).has_value());
    CHECK_FALSE(parse_update_manifest(R"({"schema":2,"channels":{}})", stable).has_value());
    CHECK_FALSE(parse_update_manifest(R"({"schema":1,"channels":{}})", stable).has_value());
    CHECK_FALSE(parse_update_manifest(
        R"({"schema":1,"channels":{"stable":{"version":"nope","url":"https://x.test/"}}})", stable).has_value());
    CHECK_FALSE(parse_update_manifest(
        R"({"schema":1,"channels":{"stable":{"version":"1.0.0","url":"http://x.test/"}}})", stable).has_value());
    CHECK_FALSE(parse_update_manifest(
        R"({"schema":1,"channels":{"stable":{"version":"1.0.0","url":"https://x.test/","notes_url":"file:///etc"}}})",
        stable).has_value());
    CHECK_FALSE(parse_update_manifest(std::string(maximum_update_manifest_bytes + 1, ' '), stable).has_value());
}

TEST_CASE("an update is offered only for a newer published version", "[core][updates]") {
    const update_manifest_entry_t rc5{.version = "1.0.0-rc5", .url = "https://x.test/"};
    CHECK(evaluate_update("1.0.0-rc4", rc5) == update_decision_t::available);
    CHECK(evaluate_update("1.0.0-rc5", rc5) == update_decision_t::up_to_date);
    CHECK(evaluate_update("1.0.0", rc5) == update_decision_t::up_to_date);
    CHECK(evaluate_update("1.0.0-nightly.20261122.abcd1234", rc5) == update_decision_t::unsupported_build);
    CHECK(evaluate_update("garbage", rc5) == update_decision_t::unsupported_build);
}

TEST_CASE("the check runs at most once a day", "[core][updates]") {
    CHECK(is_update_check_due(0, 1000));
    CHECK(is_update_check_due(1000, 1000 + 24 * 3600));
    CHECK_FALSE(is_update_check_due(1000, 1000 + 24 * 3600 - 1));
    CHECK(is_update_check_due(5000, 100)); // The clock went backwards.
    CHECK(parse_update_check_state(serialize_update_check_state(1234)) == 1234);
    CHECK(parse_update_check_state("junk") == 0);
    CHECK(parse_update_check_state("-5") == 0);
}
