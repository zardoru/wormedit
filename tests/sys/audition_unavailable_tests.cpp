#include "sys/compose/actions/compose_audition_requests.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct unavailable_audition_fixture_t {
    document_history_t history;
    notification_center_t center;
    notification_sink_t sink{center};
    compose_auditions_t auditions;
    auto inputs() -> compose_audition_inputs_t {
        return {.history = history, .notifications = sink};
    }
};

void check_unavailable_message(notification_center_t& center) {
    center.drain_inbox(notification_time_t{});
    const auto entries = center.snapshot(notification_capacity);
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().source == notification_source_t::audio);
    CHECK(entries.front().severity == notification_severity_t::warning);
    CHECK(entries.front().message == "Audio preview is unavailable in this build.");
}
}

TEST_CASE("Unavailable audition scopes stay quiet without requests", "[unit][sys][audition-unavailable]") {
    unavailable_audition_fixture_t fixture;
    { auto scope = fixture.auditions.input_scope(fixture.inputs()); }
    { auto scope = fixture.auditions.catalog_scope(fixture.inputs()); }
    CHECK(fixture.center.statistics().posted == 0);
    CHECK_FALSE(fixture.auditions.has_pending_request());
}

TEST_CASE("Unavailable object and draft auditions coalesce before one batch warning",
          "[unit][sys][audition-unavailable]") {
    unavailable_audition_fixture_t fixture;
    const auto first = editor_object_audition_request_t{.document_revision = 1};
    const auto last = editor_object_audition_request_t{.document_revision = 2};
    {
        auto scope = fixture.auditions.input_scope(fixture.inputs());
        fixture.auditions.submit(first);
        fixture.auditions.submit(last);
        fixture.auditions.submit(bmson_draft_audition_request_t{});
        CHECK_FALSE(fixture.auditions.has_pending_request(first));
        CHECK(fixture.auditions.has_pending_request(last));
        CHECK(fixture.center.statistics().posted == 0);
    }
    check_unavailable_message(fixture.center);
    CHECK_FALSE(fixture.auditions.has_pending_request());
    { auto scope = fixture.auditions.input_scope(fixture.inputs()); }
    CHECK(fixture.center.statistics().posted == 1);
}

TEST_CASE("Unavailable catalog audition waits for its next catalog phase and consumes once",
          "[unit][sys][audition-unavailable]") {
    unavailable_audition_fixture_t fixture;
    { auto scope = fixture.auditions.catalog_scope(fixture.inputs()); }
    fixture.auditions.submit(resource_id_t{10});
    fixture.auditions.submit(resource_id_t{20});
    { auto scope = fixture.auditions.input_scope(fixture.inputs()); }
    CHECK(fixture.auditions.has_pending_request(resource_id_t{20}));
    CHECK(fixture.center.statistics().posted == 0);
    { auto scope = fixture.auditions.catalog_scope(fixture.inputs()); }
    check_unavailable_message(fixture.center);
    CHECK_FALSE(fixture.auditions.has_pending_request());
    { auto scope = fixture.auditions.catalog_scope(fixture.inputs()); }
    CHECK(fixture.center.statistics().posted == 1);
}
