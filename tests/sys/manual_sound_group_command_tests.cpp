#include <wormedit/core/sound_groups.h>
#include "sys/compose_sound_actions.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {
using namespace wormedit;

struct dispatch_fixture_t {
    document_history_t history;
    workspace_controller_t workspace;
    sys::application_command_state_t app;
    chart_id_t chart_id{1};

    dispatch_fixture_t() {
        song_document_t song{.id = song_id_t{1}, .charts = {{.id = chart_id,
            .source_family = format_family_t::bms, .measures = {{.id = measure_id_t{1}}},
            .lane_layouts = {{.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
                .layout = {.profile_id = "test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .bpms = {{.id = object_id_t{11}, .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}}}}}};
        for (std::uint64_t id = 1; id <= 3; ++id) {
            song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "test.wav"});
            song.resources.push_back({.id = resource_id_t{id}, .name = "Sound " + std::to_string(id),
                .asset = asset_id_t{id}, .roles = {resource_role_t::sample}});
        }
        history = document_history_t::from(std::move(song));
        const auto binding = workspace_controller_t::from(history);
        REQUIRE(binding.workspace);
        workspace = *binding.workspace;
    }

    auto dispatch(const WormUiSoundGroupAssignment* assignments, std::size_t count,
                  std::uint64_t revision, bool keep) -> std::optional<std::string> {
        WormUiWorkspaceAction action{};
        action.compose_action = WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS;
        action.compose_input_chart_id = chart_id.value;
        action.compose_input_document_revision = revision;
        action.compose_sound_group_assignments = assignments;
        action.compose_sound_group_assignment_count = count;
        action.compose_sound_group_keep_ids_together = keep;
        return sys::dispatch_compose_sound_action(action, history, workspace, chart_id, app);
    }
};

auto base36(std::size_t value) -> std::string {
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return {digits[value / 36], digits[value % 36]};
}

auto grouped_history(bool leave_run) -> document_history_t {
    song_document_t song{.id = song_id_t{1}};
    chart_document_t chart{.id = chart_id_t{1}, .source_family = format_family_t::bms};
    song.charts.push_back(chart);
    constexpr std::array<std::string_view, 5> names{"Kick 5", "Kick 1", "Kick 4", "Kick 2", "Kick 3"};
    constexpr std::array<std::size_t, 5> scattered{1, 3, 5, 7, 9};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto id = resource_id_t{index + 1};
        song.audio_assets.push_back({.id = asset_id_t{index + 1},
                                     .source_path = std::string{names[index]} + ".wav"});
        song.resources.push_back({.id = id, .name = std::string{names[index]},
                                  .asset = asset_id_t{index + 1},
                                  .roles = {resource_role_t::sample},
                                  .target_identities = {{format_family_t::bms,
                                                         base36(scattered[index]), "WAV"}}});
    }
    song.audio_assets.push_back({.id = asset_id_t{6}, .source_path = "other.wav"});
    resource_entry_t other{.id = resource_id_t{6}, .name = "Other", .asset = asset_id_t{6},
                           .roles = {resource_role_t::sample}};
    for (std::size_t value = 1; value < 36 * 36; ++value) {
        if (std::ranges::find(scattered, value) != scattered.end()) continue;
        if (leave_run && value >= 20 && value < 25) continue;
        other.target_identities.push_back({format_family_t::bms, base36(value), "WAV"});
    }
    song.resources.push_back(std::move(other));
    return document_history_t::from(std::move(song));
}

auto two_scattered_groups_history() -> document_history_t {
    song_document_t song{.id = song_id_t{2}, .charts = {{.id = chart_id_t{1},
        .source_family = format_family_t::bms, .measures = {{.id = measure_id_t{1}}}}}};
    for (std::uint64_t id = 1; id <= 6; ++id) {
        song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "group.wav"});
        const auto group = id % 2 == 1 ? sound_group_id_t{70} : sound_group_id_t{80};
        song.resources.push_back({.id = resource_id_t{id}, .name = "Sound " + std::to_string(id),
            .asset = asset_id_t{id}, .roles = {resource_role_t::sample},
            .target_identities = {{format_family_t::bms, base36(id), "WAV"}},
            .sound_group = group});
    }
    song.sound_groups = {{.id = sound_group_id_t{70}, .name = "Odd"},
                         {.id = sound_group_id_t{80}, .name = "Even"}};
    song.audio_assets.push_back({.id = asset_id_t{7}, .source_path = "other.wav"});
    resource_entry_t other{.id = resource_id_t{7}, .name = "Other", .asset = asset_id_t{7},
                           .roles = {resource_role_t::sample}};
    for (std::size_t value = 7; value < 36 * 36; ++value)
        other.target_identities.push_back({format_family_t::bms, base36(value), "WAV"});
    song.resources.push_back(std::move(other));
    return document_history_t::from(std::move(song));
}

auto wav_value(const resource_entry_t& resource) -> std::size_t {
    const auto identity = std::ranges::find_if(resource.target_identities, [](const auto& item) {
        return item.family == format_family_t::bms && item.namespace_name == "WAV";
    });
    REQUIRE(identity != resource.target_identities.end());
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    return digits.find(identity->identifier[0]) * 36 + digits.find(identity->identifier[1]);
}

auto snapshot(const song_document_t& song)
    -> std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>> {
    auto rows = std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>>{};
    for (const auto& resource : song.resources)
        rows.emplace_back(resource.id.value, resource.sound_group ? resource.sound_group->value : 0,
                          resource.target_identities.front().identifier);
    return rows;
}

auto five_member_group_is_contiguous(const document_history_t& history) -> bool {
    std::vector<std::size_t> values;
    for (std::uint64_t id = 1; id <= 5; ++id) {
        const auto resource = std::ranges::find(history.document.resources, resource_id_t{id},
                                                &resource_entry_t::id);
        if (resource == history.document.resources.end() ||
            resource->sound_group != sound_group_id_t{55}) return false;
        values.push_back(wav_value(*resource));
    }
    std::ranges::sort(values);
    for (std::size_t index = 1; index < values.size(); ++index)
        if (values[index] != values[index - 1] + 1) return false;
    return true;
}

auto grouped_command_commits_contiguous_run_and_undoes() -> bool {
    auto history = grouped_history(true);
    const auto before = snapshot(history.document);
    const std::vector<sound_group_edit_action_t> actions{
        create_sound_group_action_t{.group = sound_group_id_t{55}, .name = "Kicks",
                                    .sounds = {resource_id_t{1}, resource_id_t{2},
                                               resource_id_t{3}, resource_id_t{4},
                                               resource_id_t{5}}}};
    if (!apply_sound_group_edits(history, actions, true) ||
        history.undo.size() != 1 || !five_member_group_is_contiguous(history)) return false;
    return undo_document_command(history) && snapshot(history.document) == before;
}

auto grouped_command_off_preserves_ids() -> bool {
    auto history = grouped_history(true);
    std::vector<std::string> before;
    for (const auto& resource : history.document.resources)
        before.push_back(resource.target_identities.front().identifier);
    const std::vector<sound_group_edit_action_t> actions{
        create_sound_group_action_t{.group = sound_group_id_t{55}, .name = "Kicks",
                                    .sounds = {resource_id_t{1}, resource_id_t{2},
                                               resource_id_t{3}, resource_id_t{4},
                                               resource_id_t{5}}}};
    if (!apply_sound_group_edits(history, actions, false)) return false;
    for (std::size_t index = 0; index < history.document.resources.size(); ++index)
        if (history.document.resources[index].target_identities.front().identifier != before[index])
            return false;
    return true;
}

auto group_command_without_run_rejects_cleanly() -> bool {
    auto history = grouped_history(false);
    const auto before = snapshot(history.document);
    const auto before_groups = history.document.sound_groups;
    const auto revision = history.document.revision;
    const std::vector<sound_group_edit_action_t> actions{
        create_sound_group_action_t{.group = sound_group_id_t{55}, .name = "Kicks",
                                    .sounds = {resource_id_t{1}, resource_id_t{2},
                                               resource_id_t{3}, resource_id_t{4},
                                               resource_id_t{5}}}};
    const auto result = apply_sound_group_edits(history, actions, true);
    return !result && history.document.revision == revision &&
           history.document.sound_groups == before_groups && snapshot(history.document) == before &&
           history.undo.empty();
}

auto group_batch_frees_both_interleaved_sets() -> bool {
    auto history = two_scattered_groups_history();
    const std::vector<sound_group_edit_action_t> actions{
        add_sounds_to_group_action_t{.group = sound_group_id_t{70},
            .sounds = {resource_id_t{1}, resource_id_t{3}, resource_id_t{5}}},
        add_sounds_to_group_action_t{.group = sound_group_id_t{80},
            .sounds = {resource_id_t{2}, resource_id_t{4}, resource_id_t{6}}}};
    if (!apply_sound_group_edits(history, actions, true) ||
        history.undo.size() != 1) return false;
    std::array<std::vector<std::size_t>, 2> runs;
    for (std::uint64_t id = 1; id <= 6; ++id)
        runs[id % 2 == 1 ? 0 : 1].push_back(wav_value(history.document.resources[id - 1]));
    for (auto& run : runs) {
        std::ranges::sort(run);
        if (run.size() != 3 || run[1] != run[0] + 1 || run[2] != run[1] + 1) return false;
    }
    return true;
}

auto ungroup_command_is_undoable() -> bool {
    auto history = grouped_history(true);
    if (!apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{55}, .name = "Kicks",
                                    .sounds = {resource_id_t{1}, resource_id_t{2}}}) ||
        !apply_sound_group_edit(history,
        ungroup_sounds_action_t{.sounds = {resource_id_t{1}, resource_id_t{2}}}) ||
        !history.document.sound_groups.empty() || history.document.resources[0].sound_group ||
        !undo_document_command(history)) return false;
    return history.document.sound_groups.size() == 1 &&
           history.document.resources[0].sound_group == sound_group_id_t{55};
}

auto dispatcher_creates_one_group_undo() -> bool {
    dispatch_fixture_t fixture;
    const std::array ids{std::uint64_t{1}, std::uint64_t{2}};
    const WormUiSoundGroupAssignment assignment{0, "Kicks", ids.data(), ids.size()};
    const auto result = fixture.dispatch(&assignment, 1, fixture.history.document.revision, false);
    if (!result || fixture.history.document.sound_groups.size() != 1 ||
        fixture.history.document.sound_groups.front().name != "Kicks" ||
        fixture.history.undo.size() != 1) return false;
    return fixture.history.document.resources[0].sound_group ==
               fixture.history.document.sound_groups.front().id &&
           fixture.history.document.resources[1].sound_group ==
               fixture.history.document.sound_groups.front().id;
}

auto dispatcher_rejects_stale_and_duplicate_batch() -> bool {
    dispatch_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    const std::array first_ids{std::uint64_t{1}};
    const std::array second_ids{std::uint64_t{1}, std::uint64_t{2}};
    const std::array assignments{
        WormUiSoundGroupAssignment{0, "One", first_ids.data(), first_ids.size()},
        WormUiSoundGroupAssignment{0, "Two", second_ids.data(), second_ids.size()}};
    const auto duplicate = fixture.dispatch(assignments.data(), assignments.size(), revision, false);
    if (!duplicate || duplicate->find("repeats a sound") == std::string::npos ||
        fixture.history.document.revision != revision || !fixture.history.document.sound_groups.empty() ||
        !fixture.history.undo.empty()) return false;
    const auto stale = fixture.dispatch(assignments.data(), assignments.size(), revision - 1, false);
    return stale && fixture.history.document.revision == revision &&
           fixture.history.document.sound_groups.empty() && fixture.history.undo.empty();
}
} // namespace

TEST_CASE("Manual grouping assigns five scattered WAV IDs as one undoable run",
          "[unit][sys][sound-groups]") {
    CHECK(grouped_command_commits_contiguous_run_and_undoes());
}

TEST_CASE("Manual grouping with Keep IDs together off preserves every WAV ID",
          "[unit][sys][sound-groups]") {
    CHECK(grouped_command_off_preserves_ids());
}

TEST_CASE("A group without a contiguous ID run rejects the complete edit",
          "[unit][sys][sound-groups]") {
    CHECK(group_command_without_run_rejects_cleanly());
}

TEST_CASE("Batch grouping frees all affected group IDs before allocating group runs",
          "[unit][sys][sound-groups]") {
    CHECK(group_batch_frees_both_interleaved_sets());
}

TEST_CASE("Ungrouping selected sounds removes empty groups and is undoable",
          "[unit][sys][sound-groups]") {
    CHECK(ungroup_command_is_undoable());
}

TEST_CASE("Manual group action dispatch creates a group in one application undo step",
          "[unit][sys][sound-groups]") {
    CHECK(dispatcher_creates_one_group_undo());
}

TEST_CASE("Manual group dispatch rejects stale revisions and duplicate batch members atomically",
          "[unit][sys][sound-groups]") {
    CHECK(dispatcher_rejects_stale_and_duplicate_batch());
}
