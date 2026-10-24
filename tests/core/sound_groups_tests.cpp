#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/sound_groups.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace wormedit;

namespace {
auto group_song() -> song_document_t {
    return {.id = song_id_t{1},
            .audio_assets = {{.id = asset_id_t{1}, .source_path = "one.wav"},
                             {.id = asset_id_t{2}, .source_path = "two.wav"},
                             {.id = asset_id_t{3}, .source_path = "three.wav"}},
            .resources = {{.id = resource_id_t{1}, .name = "one", .asset = asset_id_t{1},
                           .roles = {resource_role_t::sample}, .tags = {"kick"}},
                          {.id = resource_id_t{2}, .name = "two", .asset = asset_id_t{2},
                           .roles = {resource_role_t::sample}},
                          {.id = resource_id_t{3}, .name = "three", .asset = asset_id_t{3},
                           .roles = {resource_role_t::sample}}}};
}

auto grouped_history() -> document_history_t {
    auto history = document_history_t::from(group_song());
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{11}, .name = "Drums",
                                    .sounds = {resource_id_t{1}, resource_id_t{2}}}));
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{12}, .name = "Loops",
                                    .sounds = {resource_id_t{3}}}));
    return history;
}

auto single_member_groups_history() -> document_history_t {
    auto history = document_history_t::from(group_song());
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{11}, .name = "Solo",
                                    .sounds = {resource_id_t{1}}}));
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{12}, .name = "Moved",
                                    .sounds = {resource_id_t{2}}}));
    return history;
}

auto read_fixture(std::string_view relative_path) -> std::vector<std::byte> {
    const auto path = std::filesystem::path(WORMEDIT_FIXTURE_DIR) / relative_path;
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> result;
    result.reserve(source.size());
    for (const auto byte : source) result.push_back(static_cast<std::byte>(byte));
    return result;
}

auto first_sample(const song_document_t& song) -> resource_id_t {
    const auto found = std::ranges::find_if(song.resources, [](const auto& resource) {
        return std::ranges::find(resource.roles, resource_role_t::sample) != resource.roles.end();
    });
    return found == song.resources.end() ? resource_id_t{} : found->id;
}

struct sound_group_invariants_t {
    std::vector<std::pair<resource_id_t, std::vector<std::string>>> bms_ids;
    std::vector<std::vector<std::string>> tags;
    std::vector<sound_kind_t> kinds;
    std::vector<std::optional<resource_id_t>> note_sounds;
    std::vector<std::pair<chart_id_t, layer_id_t>> layers;
    auto operator==(const sound_group_invariants_t&) const -> bool = default;
};

auto sound_group_invariants(const song_document_t& song) -> sound_group_invariants_t {
    sound_group_invariants_t state;
    for (const auto& resource : song.resources) {
        state.tags.push_back(resource.tags);
        state.kinds.push_back(resource.sound_kind);
        std::vector<std::string> ids;
        for (const auto& identity : resource.target_identities) {
            if (identity.family == format_family_t::bms) ids.push_back(identity.identifier);
        }
        state.bms_ids.emplace_back(resource.id, std::move(ids));
    }
    for (const auto& chart : song.charts) {
        for (const auto& layer : chart.semantic_layers) state.layers.emplace_back(chart.id, layer.id);
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) state.note_sounds.push_back(note.sound);
            for (const auto& event : measure.bgm_events) state.note_sounds.push_back(event.sound);
        }
    }
    return state;
}

auto add_group_to_one_sample(document_history_t& history) -> void {
    const auto sound = first_sample(history.document);
    REQUIRE(sound);
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{1}, .name = "Group",
                                    .sounds = {sound}}));
}

auto grouped_bmson_outputs() -> std::pair<std::string, std::string> {
    const auto bytes = read_fixture("bmson/slicing-and-mixed-sounds.bmson");
    const auto imported = parse_bmson_bytes(bytes);
    REQUIRE(imported.value);
    const auto before = write_bmson(*imported.value);
    REQUIRE(before.ok());
    REQUIRE(before.value);
    auto history = document_history_t::from(*imported.value);
    const auto state_before = sound_group_invariants(history.document);
    add_group_to_one_sample(history);
    REQUIRE(sound_group_invariants(history.document) == state_before);
    const auto after = write_bmson(history.document);
    REQUIRE(after.ok());
    REQUIRE(after.value);
    return {*before.value, *after.value};
}

auto old_project_roundtrip_is_stable() -> bool {
    auto song = group_song();
    const auto old_bytes = write_native_project(song);
    REQUIRE(old_bytes.value);
    CHECK(old_bytes.value->find("sound_groups") == std::string::npos);
    CHECK(old_bytes.value->find("sound_group\"") == std::string::npos);
    const auto parsed_old = parse_native_project(*old_bytes.value);
    REQUIRE(parsed_old.value);
    const auto rewritten_old = write_native_project(*parsed_old.value);
    REQUIRE(rewritten_old.value);
    CHECK(*rewritten_old.value == *old_bytes.value);
    return true;
}

auto populated_group_roundtrip_is_stable() -> bool {
    auto song = group_song();
    song.sound_groups = {{.id = sound_group_id_t{11}, .name = "Drums"}};
    song.resources[0].sound_group = sound_group_id_t{11};
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    const auto parsed = parse_native_project(*written.value);
    REQUIRE(parsed.value);
    CHECK(parsed.value->sound_groups == song.sound_groups);
    CHECK(parsed.value->resources[0].sound_group == sound_group_id_t{11});
    return true;
}

auto grouped_bms_outputs() -> std::pair<std::string, std::string> {
    const auto bytes = read_fixture("bms/bgm-columns-per-measure.bms");
    const auto imported = parse_bms_bytes(bytes);
    REQUIRE(imported.value.song);
    const bms_write_options_t options{
        .lane_profile_id = "bme-7k-outside-scratch", .preserve_unchanged_source = false};
    const auto before = write_bms(*imported.value.song, options);
    REQUIRE(before.ok());
    REQUIRE(before.value);
    auto history = document_history_t::from(*imported.value.song);
    const auto state_before = sound_group_invariants(history.document);
    add_group_to_one_sample(history);
    REQUIRE(sound_group_invariants(history.document) == state_before);
    const auto after = write_bms(history.document, options);
    REQUIRE(after.ok());
    REQUIRE(after.value);
    return {*before.value, *after.value};
}

constexpr std::string_view default_bms_source =
    "#PLAYER 1\r\n#TITLE Group passthrough\r\n#BPM 120\r\n#WAV01 one.wav\r\n#00011:01\r\n";

auto default_bms_history() -> document_history_t {
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{default_bms_source.data(),
                                                                 default_bms_source.size()}),
                                        {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(parsed.value.song);
    const auto original = write_bms(*parsed.value.song);
    REQUIRE(original.ok());
    REQUIRE(original.value);
    CHECK(*original.value == default_bms_source);
    return document_history_t::from(*parsed.value.song);
}

auto default_export_matches(const document_history_t& history,
                            const std::optional<std::string>& expected) -> bool {
    const auto written = write_bms(history.document);
    return written.ok() && written.value == expected;
}

auto bms_source_still_unchanged(const song_document_t& song) -> bool {
    if (!song.prepared_bms_programs.empty()) return song.prepared_bms_programs.front()->prepared().source.unchanged_since_import;
    if (song.charts.empty()) return false;
    for (const auto& group : song.charts.front().metadata.format_groups) {
        if (group.family == format_family_t::bms) return group.encoding.unchanged_since_import;
    }
    return false;
}

auto group_document_preserves_bms(document_history_t& history,
                                  const std::optional<std::string>& expected) -> bool {
    add_group_to_one_sample(history);
    return bms_source_still_unchanged(history.document) &&
           default_export_matches(history, expected);
}

auto default_bms_group_undo_redo() -> bool {
    auto history = default_bms_history();
    const auto before = write_bms(history.document);
    REQUIRE(before.value);
    CHECK(group_document_preserves_bms(history, before.value));
    REQUIRE(undo_document_command(history));
    CHECK(default_export_matches(history, before.value));
    REQUIRE(redo_document_command(history));
    CHECK(default_export_matches(history, before.value));
    return true;
}

auto normal_bms_edit_does_not_reenable_passthrough() -> bool {
    auto history = default_bms_history();
    add_group_to_one_sample(history);
    REQUIRE(apply_resource_sound_metadata(history,
        {.resource = first_sample(history.document), .kind = sound_kind_t::stem, .tags = {"kick"}}));
    CHECK_FALSE(bms_source_still_unchanged(history.document));
    REQUIRE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{1}, .name = "Renamed"}));
    CHECK_FALSE(bms_source_still_unchanged(history.document));
    return true;
}

auto invalid_group_inputs_are_rejected() -> bool {
    auto song = group_song();
    song.visual_assets = {{.id = asset_id_t{4}, .source_path = "image.png"}};
    song.resources.push_back({.id = resource_id_t{4}, .name = "image", .asset = asset_id_t{4},
                              .roles = {resource_role_t::bga}});
    auto history = document_history_t::from(song);
    const auto revision = history.document.revision;
    CHECK_FALSE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{1}, .name = "Empty", .sounds = {}}));
    CHECK_FALSE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{1}, .name = "Unknown",
                                    .sounds = {resource_id_t{999}}}));
    CHECK_FALSE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{1}, .name = "Repeated",
                                    .sounds = {resource_id_t{1}, resource_id_t{1}}}));
    CHECK_FALSE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{1}, .name = "Visual",
                                    .sounds = {resource_id_t{4}}}));
    CHECK_FALSE(apply_sound_group_edit(history,
        add_sounds_to_group_action_t{.group = sound_group_id_t{900},
                                    .sounds = {resource_id_t{1}}}));
    return history.document.sound_groups.empty();
}
}

TEST_CASE("moving a sound retains a populated source group", "[unit][core][sound-groups]") {
    auto history = grouped_history();
    REQUIRE(apply_sound_group_edit(history,
        add_sounds_to_group_action_t{.group = sound_group_id_t{12},
                                    .sounds = {resource_id_t{1}}}));
    CHECK(history.document.resources[0].sound_group == sound_group_id_t{12});
    CHECK(history.document.resources[1].sound_group == sound_group_id_t{11});
    CHECK(history.document.sound_groups.size() == 2);
}

TEST_CASE("moving and ungrouping the final member cleans up and undo restores it",
          "[unit][core][sound-groups]") {
    auto history = single_member_groups_history();
    REQUIRE(apply_sound_group_edit(history,
        add_sounds_to_group_action_t{.group = sound_group_id_t{12},
                                    .sounds = {resource_id_t{1}}}));
    CHECK(history.document.sound_groups.size() == 1);
    CHECK(history.document.sound_groups.front().id == sound_group_id_t{12});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.sound_groups.size() == 2);
    CHECK(history.document.resources[0].sound_group == sound_group_id_t{11});
}

TEST_CASE("ungrouping the final member removes its group", "[unit][core][sound-groups]") {
    auto history = single_member_groups_history();
    REQUIRE(apply_sound_group_edit(history,
        ungroup_sounds_action_t{.sounds = {resource_id_t{1}}}));
    CHECK(history.document.sound_groups.size() == 1);
    CHECK(history.document.resources[0].sound_group == std::nullopt);
}

TEST_CASE("group rename, collision, delete and undo are atomic", "[unit][core][sound-groups]") {
    auto history = grouped_history();
    REQUIRE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{12}, .name = "  Loop Pack  "}));
    CHECK(history.document.sound_groups[1].name == "Loop Pack");
    const auto before_reject = history.document.sound_groups;
    CHECK_FALSE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{12}, .name = " drums "}));
    CHECK(history.document.sound_groups == before_reject);
}

TEST_CASE("group delete and undo restore membership", "[unit][core][sound-groups]") {
    auto history = grouped_history();
    REQUIRE(apply_sound_group_edit(history,
        delete_sound_group_action_t{.group = sound_group_id_t{12}}));
    CHECK(history.document.sound_groups.size() == 1);
    CHECK(history.document.resources[2].sound_group == std::nullopt);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.sound_groups.size() == 2);
    CHECK(history.document.resources[2].sound_group == sound_group_id_t{12});
}

TEST_CASE("deleting the last grouped sound removes its group", "[unit][core][sound-groups]") {
    auto history = document_history_t::from(group_song());
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{11}, .name = "Solo",
                                    .sounds = {resource_id_t{1}}}));
    REQUIRE(apply_resource_deletion(history, resource_id_t{1}));
    CHECK(history.document.sound_groups.empty());
    CHECK(validate_document(history.document).empty());
}

TEST_CASE("group commands reject unknown, duplicate, visual and empty inputs atomically",
          "[unit][core][sound-groups]") {
    CHECK(invalid_group_inputs_are_rejected());
}

TEST_CASE("group names trim whitespace and reject case-insensitive collisions",
          "[unit][core][sound-groups]") {
    auto history = grouped_history();
    REQUIRE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{12}, .name = "  Loop Pack  "}));
    const auto before = history.document.sound_groups;
    CHECK_FALSE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{12}, .name = " drums "}));
    CHECK_FALSE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{12}, .name = "   "}));
    CHECK(history.document.sound_groups == before);
}

TEST_CASE("group name byte limits accept one and sixty-four bytes and reject sixty-five",
          "[unit][core][sound-groups]") {
    auto history = document_history_t::from(group_song());
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{11}, .name = "x",
                                    .sounds = {resource_id_t{1}}}));
    std::string maximum(64, 'a');
    REQUIRE(apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{12}, .name = maximum,
                                    .sounds = {resource_id_t{2}}}));
    const auto before = history.document.sound_groups;
    maximum.push_back('a');
    CHECK_FALSE(apply_sound_group_edit(history,
        rename_sound_group_action_t{.group = sound_group_id_t{12}, .name = maximum}));
    CHECK(history.document.sound_groups == before);
}

TEST_CASE("sound group metadata does not change normalized BMS export",
          "[unit][core][sound-groups][bms]") {
    const auto [before, after] = grouped_bms_outputs();
    CHECK(after == before);
}

TEST_CASE("default BMS passthrough survives group undo and redo but regular edits invalidate it",
          "[unit][core][sound-groups][bms]") {
    CHECK(default_bms_group_undo_redo());
    CHECK(normal_bms_edit_does_not_reenable_passthrough());
}

TEST_CASE("sound group metadata does not change BMSON export",
          "[unit][core][sound-groups][bmson]") {
    const auto [before, after] = grouped_bmson_outputs();
    CHECK(after == before);
}

TEST_CASE("native groups preserve old files and round-trip populated state",
          "[unit][core][sound-groups][native-project]") {
    CHECK(old_project_roundtrip_is_stable());
    CHECK(populated_group_roundtrip_is_stable());
}

TEST_CASE("song validation rejects dangling and empty sound groups", "[unit][core][sound-groups]") {
    auto song = group_song();
    song.sound_groups = {{.id = sound_group_id_t{11}, .name = "Group"}};
    song.resources[0].sound_group = sound_group_id_t{11};
    REQUIRE(validate_document(song).empty());
    song.resources[0].sound_group = sound_group_id_t{999};
    CHECK_FALSE(validate_document(song).empty());
    song.resources[0].sound_group = std::nullopt;
    CHECK_FALSE(validate_document(song).empty());
}

TEST_CASE("song validation rejects duplicate IDs and names for groups", "[unit][core][sound-groups]") {
    auto song = group_song();
    song.sound_groups = {{.id = sound_group_id_t{11}, .name = "First"},
                         {.id = sound_group_id_t{12}, .name = "Second"}};
    song.resources[0].sound_group = sound_group_id_t{11};
    song.resources[1].sound_group = sound_group_id_t{12};
    REQUIRE(validate_document(song).empty());
    song.sound_groups[1].name = "first";
    CHECK_FALSE(validate_document(song).empty());
    song.sound_groups[1].name = "Second";
    song.sound_groups[1].id = sound_group_id_t{11};
    CHECK_FALSE(validate_document(song).empty());
}

TEST_CASE("sound group names reject malformed UTF-8", "[unit][core][helpers][sound-groups]") {
    auto history = document_history_t::from(group_song());
    const auto result = apply_sound_group_edit(history,
        create_sound_group_action_t{.group = sound_group_id_t{11}, .name = "bad\xc0\x80",
                                    .sounds = {resource_id_t{1}}});
    CHECK_FALSE(result);
    CHECK(history.document.sound_groups.empty());
}
