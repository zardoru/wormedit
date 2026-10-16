#include "../support/stale_action_assertions.h"
#include "project_audio_sources_fixture.h"
#include "sys/compose_audition_requests.h"
#include "sys/audio_transport_frame.h"
#include <SDL3/SDL_timer.h>
#include <wormedit/audio/audition_resolver.h>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
using namespace wormedit;
using namespace wormedit::sys;
namespace {
void expect_audition(bool value) { REQUIRE(value); }
struct audition_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    audio::decoded_block_cache_t blocks;
    audio::transport_t transport{blocks};
    audio::audition_transport_bridge_t audition_bridge{transport};
    audio::audition_session_t audition_session{sources, transport, audition_bridge};
    audio::object_audition_controller_t object_audition{audition_bridge};
    audio::preview_transport_bridge_t preview_bridge{transport};
    audio::preview_session_t preview_session{sources, transport, preview_bridge};
    sdl_audition_output_t output{audition_session};
    sdl_preview_output_t preview{preview_session};
    runtime::chart_preview_t preview_state;
    document_history_t history;
    compose_scene_cache_t cache;
    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
    auto inputs() -> compose_audition_inputs_t {
        return {{preview_session, preview, preview_state},
                {retained_sources_for(history.document, project_sources), object_audition, output}, sink};
    }
    void catalog(std::optional<resource_id_t>& request) {
        process_sound_audition_request(request, history, inputs());
    }
    void compose() {
        process_compose_audition_requests(cache, history, inputs());
    }
};
struct audio_lifetime_t { ~audio_lifetime_t() { SDL_QuitSubSystem(SDL_INIT_AUDIO); } };
}
TEST_CASE("Catalog audition rejects missing sources once and idle stays quiet", "[audio][audition-notification]") {
    audition_fixture_t fixture;
    std::optional<resource_id_t> request;
    fixture.catalog(request);
    fixture.compose();
    expect_audition(fixture.entries().empty());
    request = resource_id_t{1};
    fixture.catalog(request);
    auto entries = fixture.entries();
    expect_audition(entries.size() == 1);
    expect_audition(entries[0].severity == notification_severity_t::warning);
    expect_audition(entries[0].source == notification_source_t::audio);
    expect_audition(entries[0].message == "That sound is no longer in the list.");
    expect_audition(!request);
    fixture.catalog(request);
    expect_audition(fixture.entries().size() == 1);
    fixture.history.document.resources = {{.id = resource_id_t{1}, .asset = asset_id_t{1}}};
    request = resource_id_t{1};
    fixture.catalog(request);
    entries = fixture.entries();
    expect_audition(entries.size() == 2);
    expect_audition(entries[0].message == "That sound's audio is not loaded yet.");
    expect_audition(entries[0].severity == notification_severity_t::warning);
    expect_audition(!request);
}
TEST_CASE("Stale object audition feedback is typed and consumes its request once",
          "[audio][audition-notification][stale-object-audition]") {
    audition_fixture_t fixture;
    fixture.cache.frame.audition_request = editor_object_audition_request_t{
        .document_revision = 1, .chart = chart_id_t{2},
        .object = {editor_selection_object_kind_t::note, object_id_t{1}}};
    fixture.compose();
    const auto entries = fixture.entries();
    expect_audition(entries.size() == 1);
    test::check_stale_action_message(entries.front().message, "Audio preview request");
    expect_audition(entries.front().severity == notification_severity_t::warning);
    expect_audition(!fixture.cache.frame.audition_request);
    fixture.compose();
    expect_audition(fixture.center.statistics().posted == 1);
}

TEST_CASE("Object then draft audition preserve producer diagnostics and consumption order", "[audio][audition-notification]") {
    audition_fixture_t fixture;
    fixture.cache.frame.audition_request = editor_object_audition_request_t{
        .object = {editor_selection_object_kind_t::bpm_event, object_id_t{1}}};
    fixture.cache.frame.draft_audition_request = bmson_draft_audition_request_t{.chart = chart_id_t{2}};
    const auto expected = audio::resolve_bmson_draft_audition(fixture.history.document,
        {.chart = chart_id_t{2}}, {});
    expect_audition(!expected.diagnostics.empty());
    fixture.compose();
    const auto entries = fixture.entries();
    expect_audition(entries.size() == 2);
    expect_audition(entries[0].message == expected.diagnostics.front().message);
    expect_audition(entries[0].severity == notification_severity_t::error);
    expect_audition(entries[1].message == "Only notes and BGM events can be prepared for audition.");
    expect_audition(entries[1].severity == notification_severity_t::warning);
    expect_audition(!fixture.cache.frame.audition_request && !fixture.cache.frame.draft_audition_request);
    fixture.compose();
    expect_audition(fixture.center.statistics().posted == 2);
}
TEST_CASE("Catalog SDL start failure and real dummy success use typed audition feedback", "[audio][audition-notification]") {
    expect_audition(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_audition(SDL_InitSubSystem(SDL_INIT_AUDIO));
    audio_lifetime_t lifetime;
    audition_fixture_t fixture;
    const auto registration = fixture.sources.create_source({.asset = asset_id_t{1},
        .path = std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio/keysound-c4.ogg",
        .source = {"fixture:keysound-c4.ogg", "notification-test"}});
    expect_audition(static_cast<bool>(registration));
    fixture.history.document.resources = {{.id = resource_id_t{1}, .asset = asset_id_t{1}}};
    std::optional<resource_id_t> request{resource_id_t{1}};
    fixture.catalog(request);
    auto entries = fixture.entries();
    expect_audition(entries.size() == 1);
    expect_audition(entries[0].severity == notification_severity_t::error);
    expect_audition(entries[0].message == "default audio output is unavailable");
    std::string error;
    expect_audition(fixture.output.open(&error));
    fixture.sink.post(notification_severity_t::success, notification_source_t::edit, "Edit applied.");
    request = resource_id_t{1};
    fixture.catalog(request);
    entries = fixture.entries();
    expect_audition(entries.size() == 2);
    expect_audition(entries[0].severity == notification_severity_t::success);
    expect_audition(fixture.center.statistics().posted == 2);
    expect_audition(fixture.center.current_line()->message == "Edit applied.");
}

namespace {
auto wait_audition_failure(sdl_audition_output_t& output)
    -> std::optional<sdl_audition_service_result_t> {
    const auto deadline = SDL_GetTicks() + 2000;
    while (SDL_GetTicks() < deadline) {
        auto result = output.service();
        if (result && result->status.has_failure()) return result;
        SDL_Delay(2);
    }
    return std::nullopt;
}
}
TEST_CASE("Deferred audition source failure retains existing notification and is consumed once",
          "[audio][sdl][audition][audition-notification][deferred-failure]") {
    expect_audition(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    expect_audition(SDL_InitSubSystem(SDL_INIT_AUDIO));
    const audio_lifetime_t lifetime;
    audition_fixture_t fixture;
    expect_audition(fixture.output.open());
    const audio::audition_clip_t missing{{"missing-asset", "missing-content"}, 0, 512, 1};
    expect_audition(static_cast<bool>(fixture.output.start(std::span{&missing, 1})));
    const auto failed = wait_audition_failure(fixture.output);
    expect_audition(failed.has_value());
    expect_audition(failed->status.has_failure());
    expect_audition(failed->status.has_failure(wormedit::failure_code_t::pump_failed));
    expect_audition(failed->pump.has_value());
    expect_audition(failed->pump->status.has_failure());
    expect_audition(failed->pump->status == wormedit::failure_code_t::unavailable);
    static_cast<void>(fixture.output.stop());
    post_audition_service_notifications(*failed, fixture.sink);
    const auto entries = fixture.entries();
    expect_audition(entries.size() == 1);
    expect_audition(entries[0].message == "Audio preview stopped because its decoded source is unavailable.");
    expect_audition(entries[0].source == notification_source_t::audio);
    expect_audition(!fixture.output.service());
    expect_audition(!fixture.audition_session.is_active());
}
TEST_CASE("Auditioning a silent note or BGM event is quietly consumed", "[audio][audition-notification]") {
    audition_fixture_t fixture;
    chart_document_t chart;
    chart.id = chart_id_t{2};
    measure_chunk_t measure{.id = measure_id_t{3}};
    measure.notes = {{.id = object_id_t{4}, .position = {measure_id_t{3}, 0}}};
    measure.bgm_events = {{.id = object_id_t{5}, .position = {measure_id_t{3}, 0}}};
    chart.measures = {measure};
    fixture.history.document.charts = {chart};
    for (const auto& object : {editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{4}},
                               editor_selection_object_t{editor_selection_object_kind_t::bgm_event, object_id_t{5}}}) {
        fixture.cache.frame.audition_request = editor_object_audition_request_t{
            .document_revision = fixture.history.document.revision, .chart = chart.id, .object = object};
        fixture.compose();
        expect_audition(!fixture.cache.frame.audition_request);
    }
    expect_audition(fixture.entries().empty());
}
