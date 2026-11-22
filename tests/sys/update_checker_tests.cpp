#include "sys/updates/update_checker.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
constexpr auto manifest = R"({"schema":1,"channels":{"stable":{"version":"1.0.0-rc5","url":"https://x.test/rc5"}}})";

struct fixture_t {
    notification_center_t center{[] { return notification_time_t{}; }};
    std::atomic<int> fetches{};
    std::string body{manifest};
    bool network_up{true};
    auto make(std::string current = "1.0.0-rc4") {
        return update_checker_t{{.current_version = std::move(current),
            .manifest_url = "https://x.test/latest.json",
            .fetch = [this](std::string_view, std::stop_token) -> result_t<std::string, std::string> {
                ++fetches;
                if (!network_up) return std::unexpected{"offline"};
                return body;
            },
            .notifications = notification_sink_t{center}}};
    }
};
} // namespace

TEST_CASE("an automatic check reports a newer version once", "[sys][updates]") {
    fixture_t fixture;
    auto checker = fixture.make();
    REQUIRE(checker.start(false, 1000));
    checker.join();
    const auto status = checker.status();
    CHECK(status.phase == update_status_t::phase_t::available);
    CHECK(status.latest.version == "1.0.0-rc5");
    fixture.center.drain_inbox(notification_time_t{});
    CHECK(fixture.center.size() == 1);
}

TEST_CASE("an automatic check stays silent when offline or current; a manual one speaks", "[sys][updates]") {
    fixture_t fixture;
    fixture.network_up = false;
    auto checker = fixture.make();
    REQUIRE(checker.start(false, 1000));
    checker.join();
    CHECK(checker.status().phase == update_status_t::phase_t::failed);
    fixture.center.drain_inbox(notification_time_t{});
    CHECK(fixture.center.size() == 0);
    REQUIRE(checker.start(true, 1001));
    checker.join();
    fixture.center.drain_inbox(notification_time_t{});
    CHECK(fixture.center.size() == 1);
}

TEST_CASE("a nightly build is never offered an update", "[sys][updates]") {
    fixture_t fixture;
    auto checker = fixture.make("1.0.0-nightly.20261122.abcd1234");
    REQUIRE(checker.start(false, 1000));
    checker.join();
    CHECK(checker.status().phase == update_status_t::phase_t::up_to_date);
    fixture.center.drain_inbox(notification_time_t{});
    CHECK(fixture.center.size() == 0);
}

TEST_CASE("a hostile manifest is rejected", "[sys][updates]") {
    fixture_t fixture;
    fixture.body = R"j({"schema":1,"channels":{"stable":{"version":"9.0.0","url":"javascript:alert(1)"}}})j";
    auto checker = fixture.make();
    REQUIRE(checker.start(false, 1000));
    checker.join();
    CHECK(checker.status().phase == update_status_t::phase_t::failed);
}
