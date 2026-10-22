#include "../support/workspace_result_assertions.h"
#include "../sys/application_dispatch_fixture.h"
#include "sys/metadata/metadata_actions.h"
#include "ui/frontend.h"
#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/format_field_schema.h>
#include <wormedit/core/lane_profiles.h>

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
        history = document_history_t::from(song_document_t{.id = song_id_t{1}, .revision = 4});
        REQUIRE(apply_new_chart_command(history));
        history = document_history_t::from(std::move(history.document));
        const auto binding = workspace_controller_t::from(history);
        wormedit::test::require_workspace_result(binding);
        workspace = *binding.value;
    }

    auto dispatch(const wormedit::ui::workspace_action_t& action, bool timing_locked = false) -> std::optional<std::string> {
        return dispatch_metadata_action(action, history, workspace, timing_locked, drop, assets,
                                        {}, scheduler, false, false, sink);
    }

    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
};

auto song_edit(const metadata_notification_fixture_t& fixture) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
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
    -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_REDIVIDE_CHART;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_chart_id = chart_id_t{1};
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
    wormedit::ui::workspace_action_t action{};

    void point() {
        action.metadata_channel_map_lanes.clear();
        for (std::size_t index = 0; index < lane_ids.size(); ++index) {
            action.metadata_channel_map_lanes.push_back({
                .lane = lane_id_t{lane_ids[index]},
                .visible = texts[index * 4], .invisible = texts[index * 4 + 1],
                .long_note = texts[index * 4 + 2], .mine = texts[index * 4 + 3]});
        }
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
    request.action.metadata_chart_id = chart_id_t{2};
    request.action.metadata_channel_map_layout_segment_id = object_id_t{10};
    request.point();
}

void use_bms_song(metadata_notification_fixture_t& fixture) {
    fixture.history = document_history_t::from(bms_channel_song());
    const auto binding = workspace_controller_t::from(fixture.history);
    wormedit::test::require_workspace_result(binding);
    fixture.workspace = *binding.value;
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
    action.metadata_title = std::nullopt;
    const auto invalid = fixture.dispatch(action);
    require_metadata_notification(static_cast<bool>(invalid));
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::warning, *invalid);
    action = song_edit(fixture);
    action.metadata_document_revision = revision - 1;
    const auto stale = fixture.dispatch(action);
    require_metadata_notification(static_cast<bool>(stale));
    check_metadata_notification(*stale == stale_action_message("Metadata edit"));
    entries = fixture.entries();
    require_metadata_notification(entries.size() == 2);
    expect_edit_entry(entries[0], notification_severity_t::error, *stale);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
    check_metadata_notification(!fixture.dispatch(wormedit::ui::workspace_action_t{}));
    check_metadata_notification(fixture.entries().size() == 2);
}

TEST_CASE("metadata redivision action redivides exactly or rejects inexact charts untouched",
          "[unit][sys][metadata-notification]") {
    metadata_notification_fixture_t fixture;
    fixture.history = document_history_t::from(redivision_song(130));
    const auto binding = workspace_controller_t::from(fixture.history);
    require_metadata_notification(static_cast<bool>((binding.value && binding.diagnostics.empty())));
    fixture.workspace = *binding.value;

    const auto applied = fixture.dispatch(redivision_action(fixture, 192));
    require_metadata_notification(applied == "Redivided 1 positions to 192 divisions per measure.");
    check_metadata_notification(fixture.history.document.charts.front().divisions_per_standard_measure == 192);
    check_metadata_notification(fixture.history.document.charts.front().measures.front().notes.front().position.row == 26);
    check_metadata_notification(fixture.history.undo.size() == 1);
    auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries[0], notification_severity_t::success, *applied);

    fixture.history = document_history_t::from(redivision_song(131));
    const auto inexact_binding = workspace_controller_t::from(fixture.history);
    require_metadata_notification(static_cast<bool>((inexact_binding.value && inexact_binding.diagnostics.empty())));
    fixture.workspace = *inexact_binding.value;
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
    check_metadata_notification(!fixture.drop.channel_map_preview->value ||
        channel_map_preview_view_t::from(*fixture.drop.channel_map_preview).report->conflict_count > 0);

    wormedit::ui::workspace_action_t apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
    apply.metadata_document_revision = revision;
    apply.metadata_chart_id = chart_id_t{2};
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
    // Emitted payloads survive changes to the frontend staging strings.
    preview.texts.fill("overwritten");
    const auto previewed = fixture.dispatch(preview.action);
    require_metadata_notification(previewed.has_value());
    require_metadata_notification(fixture.drop.channel_map_preview.has_value());
    require_metadata_notification(fixture.drop.channel_map_preview->value.has_value());
    check_metadata_notification(fixture.drop.channel_map_preview->value->payload.report.conflict_count == 0);

    wormedit::ui::workspace_action_t apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
    apply.metadata_document_revision = fixture.history.document.revision;
    apply.metadata_chart_id = chart_id_t{2};
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

    wormedit::ui::workspace_action_t apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
    apply.metadata_document_revision = revision;
    apply.metadata_chart_id = chart_id_t{2};
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
    REQUIRE(apply_new_chart_command(fixture.history));
    REQUIRE(fixture.history.document.charts.size() == 2);
    const std::array<std::uint64_t, 2> ids{fixture.history.document.charts[0].id.value,
                                           fixture.history.document.charts[1].id.value};
    const auto undo_before = fixture.history.undo.size();

    wormedit::ui::workspace_action_t action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_BATCH_CHART;
    for (const auto id : ids) action.metadata_batch_chart_ids.push_back(chart_id_t{id});
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

namespace {
struct typed_family_case_t {
    format_family_t family;
    const char* name;
    const char* value;
    const char* replacement;
    const char* out_of_range;
};

auto song_format_group(const metadata_notification_fixture_t& fixture, format_family_t family)
    -> const format_metadata_t* {
    for (const auto& group : fixture.history.document.metadata.format_groups)
        if (group.family == family) return &group;
    return nullptr;
}

wormedit::ui::workspace_action_t typed_format_action(const metadata_notification_fixture_t& fixture,
                                          WormUiMetadataEditKind kind, format_family_t family,
                                          const char* name, const char* value,
                                          std::size_t index) {
    wormedit::ui::workspace_action_t action{};
    action.metadata_edit = kind;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_format_family = family;
    action.metadata_format_index = index;
    action.metadata_format_name = name;
    action.metadata_format_value = value;
    return action;
}
} // namespace

TEST_CASE("typed per-format fields write the ordered field and out-of-range values never dispatch",
          "[unit][sys][metadata-notification][typed-fields]") {
    const typed_family_case_t cases[]{
        {format_family_t::bms, "PLAYER", "2", "3", "9"},
        {format_family_t::stepmania, "METER", "12", "13", "1000"},
        {format_family_t::osu_mania, "Difficulty.HPDrainRate", "7.5", "8", "11"},
        {format_family_t::bmson, "info.level", "5", "6", "-1"},
    };
    for (const auto& item : cases) {
        CAPTURE(item.name);
        metadata_notification_fixture_t fixture;
        const auto* schema = find_format_field_schema(item.family, item.name);
        REQUIRE(schema != nullptr);
        CHECK_FALSE(validate_format_field_value(*schema, item.value).has_value());
        CHECK_FALSE(validate_format_field_value(*schema, item.replacement).has_value());
        // The editor refuses to build an action from this value, so nothing is dispatched.
        CHECK(validate_format_field_value(*schema, item.out_of_range).has_value());
        const auto revision = fixture.history.document.revision;
        CHECK(fixture.history.undo.empty());

        REQUIRE(fixture.dispatch(typed_format_action(
                    fixture, WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD, item.family, item.name,
                    item.value, 0)) == "Metadata updated.");
        const auto* group = song_format_group(fixture, item.family);
        REQUIRE(group != nullptr);
        REQUIRE(group->ordered_fields.size() == 1);
        CHECK(group->ordered_fields.front() ==
              std::pair<std::string, std::string>{item.name, item.value});

        REQUIRE(fixture.dispatch(typed_format_action(
                    fixture, WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD, item.family, item.name,
                    item.replacement, 0)) == "Metadata updated.");
        group = song_format_group(fixture, item.family);
        REQUIRE(group != nullptr);
        CHECK(group->ordered_fields.front().second == item.replacement);
        CHECK(fixture.history.undo.size() == 2);
        CHECK(fixture.history.document.revision > revision);
    }
}

namespace {
auto lane_profile_index_for_other_profile(const metadata_notification_fixture_t& fixture) -> std::size_t {
    const auto& chart = fixture.history.document.charts.front();
    const auto targets = lane_profile_family_targets(chart);
    const auto& current = chart.lane_layouts.front().layout.profile_id;
    for (std::size_t index = 0; index < targets.size(); ++index)
        if (targets[index].layout.profile_id != current) return index;
    return targets.size();
}

auto lane_profile_preview_action(const metadata_notification_fixture_t& fixture, std::size_t index)
    -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_PREVIEW_LANE_PROFILE;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_chart_id = chart_id_t{2};
    action.metadata_lane_profile_index = index;
    return action;
}
}

TEST_CASE("key-count preview then apply changes the family profile as one undoable command",
          "[unit][sys][metadata-notification][lane-profile]") {
    metadata_notification_fixture_t fixture;
    use_bms_song(fixture);
    const auto index = lane_profile_index_for_other_profile(fixture);
    require_metadata_notification(index < lane_profile_family_targets(fixture.history.document.charts.front()).size());
    const auto target = lane_profile_family_targets(fixture.history.document.charts.front())[index];
    require_metadata_notification(fixture.dispatch(lane_profile_preview_action(fixture, index)).has_value());
    require_metadata_notification(fixture.drop.lane_profile_preview.has_value());
    require_metadata_notification(fixture.drop.lane_profile_preview->value.has_value());

    wormedit::ui::workspace_action_t apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE;
    apply.metadata_document_revision = fixture.history.document.revision;
    apply.metadata_chart_id = chart_id_t{2};
    const auto applied = fixture.dispatch(apply);
    require_metadata_notification(applied == "Key-count profile changed.");
    check_metadata_notification(fixture.history.document.charts.front().lane_layouts.front().layout.profile_id ==
                                target.layout.profile_id);
    check_metadata_notification(fixture.history.undo.size() == 1);
    check_metadata_notification(!fixture.drop.lane_profile_preview.has_value());
}

TEST_CASE("stale key-count preview rejects apply without touching the document",
          "[unit][sys][metadata-notification][lane-profile]") {
    metadata_notification_fixture_t fixture;
    use_bms_song(fixture);
    const auto index = lane_profile_index_for_other_profile(fixture);
    require_metadata_notification(index < lane_profile_family_targets(fixture.history.document.charts.front()).size());
    const auto before = fixture.history.document.charts.front().lane_layouts.front().layout.profile_id;
    require_metadata_notification(fixture.dispatch(lane_profile_preview_action(fixture, index)).has_value());

    auto title = song_edit(fixture);
    require_metadata_notification(fixture.dispatch(title) == "Metadata updated.");
    const auto revision = fixture.history.document.revision;

    wormedit::ui::workspace_action_t apply{};
    apply.metadata_edit = WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE;
    apply.metadata_document_revision = revision;
    apply.metadata_chart_id = chart_id_t{2};
    const auto rejected = fixture.dispatch(apply);
    require_metadata_notification(rejected.has_value());
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.document.charts.front().lane_layouts.front().layout.profile_id == before);
    check_metadata_notification(fixture.history.undo.size() == 1);
}

TEST_CASE("key-count preview refuses Custom and indices outside the chart's family",
          "[unit][sys][metadata-notification][lane-profile]") {
    metadata_notification_fixture_t fixture;
    use_bms_song(fixture);
    const auto targets = lane_profile_family_targets(fixture.history.document.charts.front());
    require_metadata_notification(!targets.empty());
    const auto refused = fixture.dispatch(lane_profile_preview_action(fixture, targets.size()));
    require_metadata_notification(refused.has_value());
    check_metadata_notification(!fixture.drop.lane_profile_preview.has_value());
    check_metadata_notification(fixture.history.undo.empty());
}

TEST_CASE("Malformed metadata payload keeps precedence over its stale stamp",
          "[unit][sys][metadata-notification][metadata-stamp-order]") {
    metadata_notification_fixture_t fixture;
    auto action = song_edit(fixture);
    action.metadata_title = std::nullopt;
    --action.metadata_document_revision;
    const auto revision = fixture.history.document.revision;
    const auto response = fixture.dispatch(action);
    require_metadata_notification(response.has_value());
    check_metadata_notification(*response ==
        "Metadata edit payload is incomplete or exceeds the tag-count cap.");
    const auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries.front(), notification_severity_t::warning, *response);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
}

namespace {
void check_metadata_timing_boundary(WormUiMetadataEditKind kind, bool timing_locked) {
    metadata_notification_fixture_t fixture;
    auto action = redivision_action(fixture, 192);
    action.metadata_edit = kind;
    action.metadata_audio_offset_numerator_text = "1";
    action.metadata_audio_offset_denominator_text = "4";
    --action.metadata_document_revision;
    const auto revision = fixture.history.document.revision;
    const auto response = fixture.dispatch(action, timing_locked);
    require_metadata_notification(response.has_value());
    const auto expected = timing_locked
        ? std::string{"audio offset editing is disabled while timing is locked"}
        : kind == WORM_UI_METADATA_EDIT_REDIVIDE_CHART
            ? "Resolution change rejected: " + stale_action_message("Resolution change")
            : stale_action_message("Audio offset edit");
    check_metadata_notification(*response == expected);
    const auto entries = fixture.entries();
    require_metadata_notification(entries.size() == 1);
    expect_edit_entry(entries.front(), notification_severity_t::error, *response);
    check_metadata_notification(fixture.history.document.revision == revision);
    check_metadata_notification(fixture.history.undo.empty());
}
} // namespace

TEST_CASE("Metadata audio offset stale rejection uses the shared formatter",
          "[unit][sys][metadata-notification][metadata-timing-stamp]") {
    check_metadata_timing_boundary(WORM_UI_METADATA_EDIT_AUDIO_OFFSET, false);
}
TEST_CASE("Metadata resolution stale rejection uses the shared formatter",
          "[unit][sys][metadata-notification][metadata-timing-stamp]") {
    check_metadata_timing_boundary(WORM_UI_METADATA_EDIT_REDIVIDE_CHART, false);
}
TEST_CASE("Metadata audio offset keeps lock priority over a stale stamp",
          "[unit][sys][metadata-notification][metadata-timing-stamp]") {
    check_metadata_timing_boundary(WORM_UI_METADATA_EDIT_AUDIO_OFFSET, true);
}

namespace {
void check_fresh_inheritance_dispatch(metadata_notification_fixture_t& fixture, const wormedit::ui::workspace_action_t& action) {
    REQUIRE(fixture.dispatch(action));
    CHECK(fixture.history.undo.size() == 1);
    CHECK(fixture.history.document.charts.front().metadata.overrides == action.metadata_overrides);
    CHECK(undo_document_command(fixture.history));
    CHECK_FALSE(fixture.history.document.charts.front().metadata.overrides.has_values());
}
void check_stale_inheritance_dispatch(metadata_notification_fixture_t& fixture, wormedit::ui::workspace_action_t action) {
    --action.metadata_document_revision;
    const auto message = fixture.dispatch(action);
    REQUIRE(message);
    CHECK(*message == stale_action_message("Metadata edit"));
    CHECK(fixture.history.undo.empty());
    CHECK_FALSE(fixture.history.document.charts.front().metadata.overrides.has_values());
}
}
TEST_CASE("Metadata inheritance dispatch is stamped atomic and undoable", "[sys][metadata-inheritance]") {
    metadata_notification_fixture_t fixture;
    wormedit::ui::workspace_action_t action{};
    action.metadata_edit = WORM_UI_METADATA_EDIT_INHERITANCE;
    action.metadata_document_revision = fixture.history.document.revision;
    action.metadata_chart_id = fixture.history.document.charts.front().id;
    action.metadata_overrides.value(inherited_metadata_field_t::title) = "Local title";
    action.metadata_overrides.value(inherited_metadata_field_t::artist) = "";
    SECTION("fresh") { check_fresh_inheritance_dispatch(fixture, action); }
    SECTION("stale") { check_stale_inheritance_dispatch(fixture, action); }
}
