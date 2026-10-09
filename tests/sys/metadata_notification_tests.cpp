#include "sys/metadata_actions.h"
#include "ui/frontend.h"
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct metadata_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    document_history_t history;
    workspace_controller_t workspace;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t assets;

    metadata_notification_fixture_t() {
        history = make_document_history(song_document_t{.id = song_id_t{1}, .revision = 4});
        REQUIRE(apply_new_chart_command(history, 4));
        history = make_document_history(std::move(history.document));
        const auto binding = make_workspace_controller(history);
        REQUIRE(binding);
        workspace = *binding.workspace;
    }

    auto dispatch(const WormUiWorkspaceAction& action) -> std::optional<std::string> {
        return dispatch_metadata_action(action, history, workspace, false, drop, assets,
                                        {}, scheduler, false, false, sink);
    }

    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
};

auto song_edit(const metadata_notification_fixture_t& fixture) -> WormUiWorkspaceAction {
    WormUiWorkspaceAction action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_SONG;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_title = "New title";
    action.metadata_artist = "New artist";
    return action;
}

auto redivision_song(std::uint64_t row) -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .charts =
            {
                {
                    .id = chart_id_t{1},
                    .measures = {{.id = measure_id_t{1}}},
                    .lane_layouts =
                        {
                            {
                                .id = object_id_t{1},
                                .position = {measure_id_t{1}, 0},
                                .layout =
                                    {
                                        .profile_id = "metadata-test",
                                        .lanes = {{.id = lane_id_t{1}, .name = "Lane"}},
                                    },
                            },
                        },
                },
            },
    };
    song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{2},
        .position = {measure_id_t{1}, row},
        .lane = lane_id_t{1},
    });
    return song;
}

auto redivision_action(const metadata_notification_fixture_t& fixture, std::uint64_t resolution)
    -> WormUiWorkspaceAction {
    WormUiWorkspaceAction action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_REDIVIDE_CHART;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_chart_id = 1;
    action.metadata_redivision_resolution = resolution;
    return action;
}

auto bms_channel_song() -> song_document_t {
    const auto profile = make_bme_7k_profile();
    return {
        .id = song_id_t{1},
        .revision = 7,
        .charts = {{
            .id = chart_id_t{2},
            .source_family = format_family_t::bms,
            .measures = {{.id = measure_id_t{3}}},
            .lane_layouts = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .layout = profile.layout}},
            .input_profile = profile.input,
        }},
    };
}

// Channel-map payload for lanes 0 and 1 of the first layout segment. Lane 0 and lane 1 get the
// given visible channels; the other channels keep their current text.
struct channel_map_request_t {
    std::array<std::uint64_t, 2> lane_ids{};
    std::array<std::string, 8> texts{};
    std::array<const char*, 8> text_pointers{};
    WormUiWorkspaceAction action{};

    void point() {
        for (std::size_t index = 0; index < texts.size(); ++index)
            text_pointers[index] = texts[index].c_str();
        action.metadata_channel_map_lane_ids = lane_ids.data();
        action.metadata_channel_map_texts = text_pointers.data();
    }
};

void prepare_channel_map_request(channel_map_request_t& request,
                                 const metadata_notification_fixture_t& fixture,
                                 WormUiMetadataEditKind kind,
                                 const std::string& first_visible, const std::string& second_visible) {
    const auto& lanes = fixture.history.document.charts.front().lane_layouts.front().layout.lanes;
    const std::array visible{first_visible, second_visible};
    for (std::size_t lane = 0; lane < 2; ++lane) {
        const auto& channels = lanes[lane].bms_channels;
        request.lane_ids[lane] = lanes[lane].id.value;
        request.texts[lane * 4 + 0] = visible[lane];
        request.texts[lane * 4 + 1] = channels.invisible.value_or("");
        request.texts[lane * 4 + 2] = channels.long_note.value_or("");
        request.texts[lane * 4 + 3] = channels.mine.value_or("");
    }
    request.action.metadata_edit = kind;
    request.action.metadata_document_revision = fixture.history.document.revision;
    request.action.metadata_chart_id = 2;
    request.action.metadata_channel_map_layout_segment_id = 10;
    request.action.metadata_channel_map_lane_count = 2;
    request.point();
}

void use_bms_song(metadata_notification_fixture_t& fixture) {
    fixture.history = make_document_history(bms_channel_song());
    const auto binding = make_workspace_controller(fixture.history);
    REQUIRE(static_cast<bool>(binding));
    fixture.workspace = *binding.workspace;
}

auto visible_channel(const metadata_notification_fixture_t& fixture, std::size_t lane) -> std::string {
    return fixture.history.document.charts.front().lane_layouts.front().layout.lanes[lane]
        .bms_channels.visible.value_or("");
}

void check_metadata_notification(bool value) { CHECK(value); }
void require_metadata_notification(bool value) { REQUIRE(value); }

void expect_edit_entry(const notification_entry_t& entry, notification_severity_t severity,
                       std::string_view message) {
    CHECK(entry.severity == severity);
    CHECK(entry.source == notification_source_t::edit);
    CHECK(entry.message == message);
}
} // namespace

TEST_CASE("metadata edit notifications preserve success and no-op outcomes", "[unit][sys][metadata-notification]") {
    metadata_notification_fixture_t fixture;
    auto action = song_edit(fixture);
    require_metadata_notification(fixture.dispatch(action) == "Metadata updated.");
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::success, "Metadata updated.");
    const auto revision = fixture.history.document.revision;
    action.metadata_document_revision = revision;
    require_metadata_notification(fixture.dispatch(action) == "No metadata changes.");
    entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::info, "No metadata changes.");
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.size() == 1);
}

TEST_CASE("metadata malformed and stale edits post once without mutation", "[unit][sys][metadata-notification]") {
    metadata_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    auto action = song_edit(fixture);
    action.metadata_title = nullptr;
    const auto invalid = fixture.dispatch(action);
    require_metadata_notification(static_cast<bool>(invalid));
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::warning, *invalid);
    action = song_edit(fixture);
    action.metadata_document_revision = revision - 1;
    const auto stale = fixture.dispatch(action);
    require_metadata_notification(static_cast<bool>(stale));
    entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::error, *stale);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
    check_metadata_notification(!fixture.dispatch(WormUiWorkspaceAction{}));
    check_metadata_notification(fixture.entries().size() == 2);
}

TEST_CASE("metadata redivision action redivides exactly or rejects inexact charts untouched",
          "[unit][sys][metadata-notification]") {
    metadata_notification_fixture_t fixture;
    fixture.history = make_document_history(redivision_song(130));
    const auto binding = make_workspace_controller(fixture.history);
    require_metadata_notification(static_cast<bool>(binding));
    fixture.workspace = *binding.workspace;

    const auto applied = fixture.dispatch(redivision_action(fixture, 192));
    require_metadata_notification(applied == "Redivided 1 positions to 192 divisions per measure.");
    check_metadata_notification(fixture.history.document.charts.front().divisions_per_standard_measure == 192);
    check_metadata_notification(fixture.history.document.charts.front().measures.front().notes.front().position.row == 26);
    check_metadata_notification(fixture.history.undo.size() == 1);
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::success, *applied);

    fixture.history = make_document_history(redivision_song(131));
    const auto inexact_binding = make_workspace_controller(fixture.history);
    require_metadata_notification(static_cast<bool>(inexact_binding));
    fixture.workspace = *inexact_binding.workspace;
    const auto revision = fixture.history.document.revision;
    const auto rejected = fixture.dispatch(redivision_action(fixture, 192));
    require_metadata_notification(static_cast<bool>(rejected));
    check_metadata_notification(rejected->starts_with("Resolution change rejected: "));
    check_metadata_notification(rejected->find("(object 2)") != std::string::npos);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
    check_metadata_notification(fixture.history.document.charts.front().divisions_per_standard_measure == 960);
    check_metadata_notification(fixture.history.document.charts.front().measures.front().notes.front().position.row == 131);
    entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::error, *rejected);
}

TEST_CASE("channel-map preview with a conflict keeps apply rejected and the document untouched",
          "[unit][sys][metadata-notification][channel-map]") {
    metadata_notification_fixture_t fixture;
    use_bms_song(fixture);
    const auto revision = fixture.history.document.revision;
    const auto first = visible_channel(fixture, 0);
    channel_map_request_t preview;
    prepare_channel_map_request(preview, fixture, WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP, first, first);
    const auto previewed = fixture.dispatch(preview.action);
    require_metadata_notification(previewed.has_value());
    require_metadata_notification(fixture.drop.channel_map_preview.has_value());
    check_metadata_notification(!static_cast<bool>(*fixture.drop.channel_map_preview) ||
                                fixture.drop.channel_map_preview->conflict_count > 0);

    WormUiWorkspaceAction apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
    apply.metadata_document_revision = revision;
    apply.metadata_chart_id = 2;
    const auto rejected = fixture.dispatch(apply);
    require_metadata_notification(rejected.has_value());
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
    check_metadata_notification(visible_channel(fixture, 0) == first);
}

TEST_CASE("channel-map preview then apply swaps two lanes as one undoable command",
          "[unit][sys][metadata-notification][channel-map]") {
    metadata_notification_fixture_t fixture;
    use_bms_song(fixture);
    const auto first = visible_channel(fixture, 0);
    const auto second = visible_channel(fixture, 1);
    require_metadata_notification(first != second);
    channel_map_request_t preview;
    prepare_channel_map_request(preview, fixture, WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP, second, first);
    const auto previewed = fixture.dispatch(preview.action);
    require_metadata_notification(previewed.has_value());
    require_metadata_notification(fixture.drop.channel_map_preview.has_value());
    require_metadata_notification(static_cast<bool>(*fixture.drop.channel_map_preview));
    check_metadata_notification(fixture.drop.channel_map_preview->conflict_count == 0);

    WormUiWorkspaceAction apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
    apply.metadata_document_revision = fixture.history.document.revision;
    apply.metadata_chart_id = 2;
    const auto applied = fixture.dispatch(apply);
    require_metadata_notification(applied == "Channel maps updated.");
    check_metadata_notification(visible_channel(fixture, 0) == second);
    check_metadata_notification(visible_channel(fixture, 1) == first);
    check_metadata_notification(fixture.history.undo.size() == 1);
    check_metadata_notification(!fixture.drop.channel_map_preview.has_value());
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::success, *applied);
}

TEST_CASE("stale channel-map preview rejects apply without touching the document",
          "[unit][sys][metadata-notification][channel-map]") {
    metadata_notification_fixture_t fixture;
    use_bms_song(fixture);
    const auto first = visible_channel(fixture, 0);
    const auto second = visible_channel(fixture, 1);
    channel_map_request_t preview;
    prepare_channel_map_request(preview, fixture, WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP, second, first);
    require_metadata_notification(fixture.dispatch(preview.action).has_value());

    auto title = song_edit(fixture);
    require_metadata_notification(fixture.dispatch(title) == "Metadata updated.");
    const auto revision = fixture.history.document.revision;

    WormUiWorkspaceAction apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
    apply.metadata_document_revision = revision;
    apply.metadata_chart_id = 2;
    const auto rejected = fixture.dispatch(apply);
    require_metadata_notification(rejected.has_value());
    check_metadata_notification(rejected->find("changed") != std::string::npos);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(visible_channel(fixture, 0) == first);
    check_metadata_notification(fixture.history.undo.size() == 1);
}

TEST_CASE("batch metadata edit writes every selected difficulty in one undo step",
          "[unit][sys][metadata-notification][batch]") {
    metadata_notification_fixture_t fixture;
    REQUIRE(apply_new_chart_command(fixture.history, fixture.history.document.revision));
    REQUIRE(fixture.history.document.charts.size() == 2);
    const std::array<std::uint64_t, 2> ids{fixture.history.document.charts[0].id.value,
                                           fixture.history.document.charts[1].id.value};
    const auto undo_before = fixture.history.undo.size();

    WormUiWorkspaceAction action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_BATCH_CHART;
    action.metadata_batch_chart_ids = ids.data();
    action.metadata_batch_chart_count = ids.size();
    action.metadata_batch_edit_author = true;
    action.metadata_chart_author = "Shared mapper";

    // A stale revision rejects everything.
    action.metadata_document_revision = fixture.history.document.revision - 1;
    const auto stale = fixture.dispatch(action);
    require_metadata_notification(stale.has_value());
    check_metadata_notification(fixture.history.undo.size() == undo_before);
    for (const auto& chart : fixture.history.document.charts)
        check_metadata_notification(chart.metadata.author != "Shared mapper");

    action.metadata_document_revision = fixture.history.document.revision;
    const auto applied = fixture.dispatch(action);
    check_metadata_notification(applied == "Metadata updated for 2 difficulties.");
    check_metadata_notification(fixture.history.undo.size() == undo_before + 1);
    for (const auto& chart : fixture.history.document.charts)
        check_metadata_notification(chart.metadata.author == "Shared mapper");
}
