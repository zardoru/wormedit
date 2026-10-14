#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/natural_order.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bms.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace wormedit;

// Windows only treats a path with a drive letter as absolute.
#if defined(_WIN32)
#define TEST_ROOT "C:/tmp"
#else
#define TEST_ROOT "/tmp"
#endif

namespace {

auto assignment_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .audio_assets = {{.id = asset_id_t{1}, .source_path = "one.wav"},
                         {.id = asset_id_t{2}, .source_path = "two.wav"},
                         {.id = asset_id_t{3}, .source_path = "missing.wav"}},
        .visual_assets = {{.id = asset_id_t{4}, .source_path = "one.png"},
                          {.id = asset_id_t{5}, .source_path = "two.png"}},
        .resources =
            {{.id = resource_id_t{1},
              .asset = asset_id_t{1},
              .roles = {resource_role_t::sample, resource_role_t::bgm}},
             {.id = resource_id_t{2},
              .asset = asset_id_t{2},
              .roles = {resource_role_t::sample, resource_role_t::bgm}},
             {.id = resource_id_t{3}, .asset = asset_id_t{4}, .roles = {resource_role_t::bga}},
             {.id = resource_id_t{4}, .asset = asset_id_t{5}, .roles = {resource_role_t::bga}},
             {.id = resource_id_t{5},
              .asset = asset_id_t{3},
              .roles = {resource_role_t::sample},
              .missing = true}},
        .charts = {{
            .id = chart_id_t{2},
            .measures = {{
                .id = measure_id_t{3},
                .notes = {{.id = object_id_t{10},
                           .position = {measure_id_t{3}, 120},
                           .lane = lane_id_t{1},
                           .sound = resource_id_t{1}}},
                .bgm_events = {{.id = object_id_t{11},
                                .position = {measure_id_t{3}, 240},
                                .source_lane = object_id_t{20},
                                .sound = resource_id_t{1}}},
            }},
            .lane_layouts =
                {{.id = object_id_t{21},
                  .position = {measure_id_t{3}, 0},
                  .layout = {.profile_id = "one-key",
                             .lanes = {{.id = lane_id_t{1}, .name = "Key", .display_order = 0}}}}},
            .input_profile = {.profile_id = "one-key",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
            .bgm_source_lanes = {{.id = object_id_t{20}, .name = "BGM"}},
            .bpms = {{.id = object_id_t{22},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
            .bga_events = {{.id = object_id_t{12},
                            .position = {measure_id_t{3}, 360},
                            .resource = resource_id_t{3}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto wav_slot_song() -> song_document_t {
    auto song = assignment_song();
    song.audio_assets.front().stream = {48'000, 2, 48'000, true};
    song.resources[0].target_identities = {{format_family_t::bms, "01", "WAV"}};
    song.resources[0].sound_kind = sound_kind_t::keysound;
    song.resources[0].tags = {"kick"};
    song.resources[0].sound_group = sound_group_id_t{7};
    song.resources[1].asset = song.resources[0].asset;
    song.resources[1].target_identities = {{format_family_t::bms, "02", "WAV"}};
    song.sound_groups = {{.id = sound_group_id_t{7}, .name = "Drums"}};
    song.charts.front().semantic_layers = {
        {.id = layer_id_t{8}, .name = "Accent", .color = {255, 128, 0, 255}}};
    song.charts.front().sound_layer_defaults = {
        {.resource = resource_id_t{1}, .layers = {layer_id_t{8}}}};
    REQUIRE(validate_document(song).empty());
    return song;
}

auto wav_slot_import(std::string_view path, std::string name)
    -> import_resource_asset_action_t {
    return {.source_path = std::filesystem::path{TEST_ROOT} / std::filesystem::path{path},
            .role = resource_role_t::sample,
            .name = std::move(name),
            .audio_stream = audio_stream_info_t{48'000, 2, 48'000, true}};
}

auto same_target_identities(const std::vector<target_resource_identity_t>& left,
                            const std::vector<target_resource_identity_t>& right) -> bool {
    return left.size() == right.size() && std::ranges::equal(left, right, [](const auto& a,
                                                                               const auto& b) {
        return a.family == b.family && a.identifier == b.identifier &&
               a.namespace_name == b.namespace_name;
    });
}

void check_wav_slot(bool value) { CHECK(value); }

auto deletion_reference_song() -> song_document_t {
    auto song = assignment_song();
    auto& first = song.charts.front();
    first.measures.front().notes.front().sound_channel_event = object_id_t{51};
    first.sound_channels = {{.id = object_id_t{50},
        .source_clip = {.id = clip_id_t{1}, .source = asset_id_t{1}, .end_frame = 48'000},
        .events = {{.id = object_id_t{51}, .position = {measure_id_t{3}, 120},
                    .lane = lane_id_t{1}}}}};

    chart_document_t second{
        .id = chart_id_t{30},
        .measures = {{.id = measure_id_t{31},
            .notes = {{.id = object_id_t{40}, .position = {measure_id_t{31}, 120},
                       .lane = lane_id_t{2}, .sound = resource_id_t{1}}},
            .bgm_events = {{.id = object_id_t{41}, .position = {measure_id_t{31}, 240},
                            .source_lane = object_id_t{33}, .sound = resource_id_t{1}}}}},
        .lane_layouts = {{.id = object_id_t{32}, .position = {measure_id_t{31}, 0},
            .layout = {.profile_id = "one-key",
                       .lanes = {{.id = lane_id_t{2}, .name = "Key", .display_order = 0}}}}},
        .input_profile = {.profile_id = "one-key",
                          .bindings = {{.key = physical_key_t::digit_2, .lane = lane_id_t{2}}}},
        .bgm_source_lanes = {{.id = object_id_t{33}, .name = "BGM"}},
        .bpms = {{.id = object_id_t{34}, .position = {measure_id_t{31}, 0},
                  .beats_per_minute = {120, 1}}},
        .bga_events = {{.id = object_id_t{35}, .position = {measure_id_t{31}, 360},
                        .resource = resource_id_t{3}}},
    };
    first.bga_events.push_back({.id = object_id_t{36},
        .position = {measure_id_t{3}, 400}, .resource = resource_id_t{3}});
    song.charts.push_back(std::move(second));
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& item) { return item.code == code; });
}

auto make_subset_renumber_history() -> document_history_t {
    auto song = assignment_song();
    song.resources[0].name = "snd10";
    song.resources[1].name = "snd2";
    song.resources[0].target_identities = {{format_family_t::bms, "01", "WAV"}};
    song.resources[1].target_identities = {{format_family_t::bms, "04", "WAV"}};
    song.resources[4].target_identities = {{format_family_t::bms, "05", "WAV"}};
    auto history = document_history_t::from(std::move(song));
    static_cast<void>(apply_bms_identifier_renumber(history,
        {.name_space = "WAV", .resources = {resource_id_t{1}, resource_id_t{2}}}));
    return history;
}

auto subset_identifiers(const document_history_t& history) -> std::vector<std::string> {
    std::vector<std::string> identifiers;
    for (const auto name : {std::string_view{"snd2"}, std::string_view{"snd10"}}) {
        const auto item = std::ranges::find(history.document.resources, name,
                                            &resource_entry_t::name);
        if (item == history.document.resources.end() || item->target_identities.empty()) return {};
        identifiers.push_back(item->target_identities.front().identifier);
    }
    return identifiers;
}

auto find_test_bgm_event(const chart_document_t& chart, object_id_t id) -> const bgm_event_t* {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (found != measure.bgm_events.end()) return &*found;
    }
    return nullptr;
}

void check_renumber_rejection(document_history_t& history,
                              std::vector<resource_id_t> ids,
                              const std::vector<std::string>& names_before,
                              std::uint64_t revision) {
    const auto result = apply_bms_identifier_renumber(history,
        {.name_space = "WAV", .resources = std::move(ids)});
    CHECK_FALSE(result);
    CHECK(history.document.resources[0].name == names_before[0]);
    CHECK(history.document.resources[1].name == names_before[1]);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
}

struct renumbered_bms_fixture_t {
    document_history_t history;
    resource_id_t note_resource;
    object_id_t note_id;
};

auto make_renumbered_bms_fixture() -> std::optional<renumbered_bms_fixture_t> {
    const std::string source =
        "#PLAYER 1\n#TITLE IDs\n#WAV01 snd10.wav\n#WAV02 snd2.wav\n#00011:01\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    if (!parsed.song || parsed.song->charts.empty() || parsed.song->charts.front().measures.empty() ||
        parsed.song->charts.front().measures.front().notes.empty()) return std::nullopt;
    auto history = document_history_t::from(*parsed.song);
    const auto& note = history.document.charts.front().measures.front().notes.front();
    if (!note.sound) return std::nullopt;
    const auto resource = *note.sound;
    const auto object = note.id;
    if (!apply_bms_identifier_renumber(history,
                                       {.name_space = "WAV"})) return std::nullopt;
    return renumbered_bms_fixture_t{std::move(history), resource, object};
}

} // namespace

TEST_CASE("BGA storyboard resource replacement keeps established assignment behavior", "[resource-assignment][bga]") {
    auto song = assignment_song();
    song.charts.front().bga_events.front().kind = visual_event_kind_t::storyboard;
    auto history = document_history_t::from(std::move(song));
    REQUIRE(apply_resource_assignment(history,
        {.chart = chart_id_t{2}, .resource = resource_id_t{4},
         .objects = {{editor_selection_object_kind_t::bga_event, object_id_t{12}}}}));
    CHECK(history.document.charts.front().bga_events.front().kind == visual_event_kind_t::storyboard);
    CHECK(history.document.charts.front().bga_events.front().resource == resource_id_t{4});
    CHECK(history.undo.size() == 1);
}

TEST_CASE("ordinary BGM move preserves identity and advances occupied source lanes",
          "[unit][core][resources][bgm-move]") {
    auto song = assignment_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    chart.measures.push_back({.id = measure_id_t{4}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{23}, .name = "BGM 2",
                                      .source_order = 1});
    chart.measures.back().bgm_events.push_back({.id = object_id_t{24},
                                                .position = {measure_id_t{4}, 120},
                                                .source_lane = object_id_t{20},
                                                .sound = resource_id_t{2}});
    auto history = document_history_t::from(std::move(song));
    const auto revision = history.document.revision;
    const auto moved = apply_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{4}, 120}, .source_lane = object_id_t{20}});
    REQUIRE(moved);
    REQUIRE(history.undo.size() == 1);
    const auto& destination = history.document.charts.front().measures.back();
    REQUIRE(destination.bgm_events.size() == 2);
    const auto found = std::ranges::find(destination.bgm_events, object_id_t{11},
                                         &bgm_event_t::id);
    REQUIRE(found != destination.bgm_events.end());
    CHECK(found->position == (local_position_t{measure_id_t{4}, 120}));
    CHECK(found->source_lane == object_id_t{23});
    CHECK(found->sound == resource_id_t{1});
    CHECK(validate_document(history.document).empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().bgm_events.front().id ==
          object_id_t{11});
}

TEST_CASE("moving a tailed BGM head translates its end by the exact row delta",
          "[unit][core][resources][bgm-move]") {
    auto song = assignment_song();
    auto& chart = song.charts.front();
    chart.measures.push_back({.id = measure_id_t{4}});
    chart.measures.push_back({.id = measure_id_t{5}});
    chart.measures.front().bgm_events.front().end = local_position_t{measure_id_t{4}, 200};
    check_wav_slot(validate_document(song).empty());
    auto history = document_history_t::from(song);
    const auto moved = apply_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{4}, 120}, .source_lane = object_id_t{20}});
    check_wav_slot(static_cast<bool>(moved));
    const auto locate_event = [&] {
        return find_test_bgm_event(history.document.charts.front(), object_id_t{11});
    };
    check_wav_slot(locate_event() != nullptr);
    check_wav_slot(locate_event()->position == local_position_t{measure_id_t{4}, 120});
    check_wav_slot(locate_event()->end == local_position_t{measure_id_t{5}, 80});
    check_wav_slot(static_cast<bool>(apply_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{3}, 120}, .source_lane = object_id_t{20}})));
    check_wav_slot(locate_event() != nullptr);
    check_wav_slot(locate_event()->position == local_position_t{measure_id_t{3}, 120});
    check_wav_slot(locate_event()->end == local_position_t{measure_id_t{4}, 80});
    check_wav_slot(locate_event()->structural_slot == 0);
    check_wav_slot(locate_event()->provenance.source_field.empty());
    check_wav_slot(static_cast<bool>(undo_document_command(history)));
    check_wav_slot(locate_event()->end == local_position_t{measure_id_t{5}, 80});
    check_wav_slot(static_cast<bool>(undo_document_command(history)));
    check_wav_slot(history.document.charts.front().measures.front().bgm_events.front().end ==
                   local_position_t{measure_id_t{4}, 200});
    check_wav_slot(static_cast<bool>(redo_document_command(history)));
    check_wav_slot(static_cast<bool>(redo_document_command(history)));
    check_wav_slot(locate_event()->position == local_position_t{measure_id_t{3}, 120});
    check_wav_slot(locate_event()->end == local_position_t{measure_id_t{4}, 80});
}

TEST_CASE("BGM head movement rejects translated tails outside the chart without history",
          "[unit][core][resources][bgm-move]") {
    auto song = assignment_song();
    auto& chart = song.charts.front();
    chart.measures.push_back({.id = measure_id_t{4}});
    chart.measures.front().bgm_events.front().end = local_position_t{measure_id_t{4}, 900};
    check_wav_slot(validate_document(song).empty());
    auto history = document_history_t::from(song);
    const auto before = history.document;
    const auto result = apply_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{3}, 480}, .source_lane = object_id_t{20}});
    check_wav_slot(!result);
    const auto before_bytes = write_native_project(before);
    const auto after_bytes = write_native_project(history.document);
    check_wav_slot(before_bytes.bytes.has_value());
    check_wav_slot(after_bytes.bytes.has_value());
    check_wav_slot(*after_bytes.bytes == *before_bytes.bytes);
    check_wav_slot(history.undo.empty());
}

TEST_CASE("BMS identifier runs skip undersized holes and capacity counts free ranges",
          "[unit][core][resources][bms-identifiers]") {
    auto song = assignment_song();
    song.resources.front().target_identities = {{format_family_t::bms, "01", "WAV"},
                                                {format_family_t::bms, "02", "WAV"},
                                                {format_family_t::bms, "03", "WAV"},
                                                {format_family_t::bms, "04", "WAV"},
                                                {format_family_t::bms, "05", "WAV"},
                                                {format_family_t::bms, "06", "WAV"},
                                                {format_family_t::bms, "07", "WAV"},
                                                {format_family_t::bms, "08", "WAV"}};
    song.resources[1].target_identities = {{format_family_t::bms, "0C", "WAV"}};
    const auto run = find_bms_identifier_run(song, "WAV", 5);
    REQUIRE(run);
    CHECK(*run == std::vector<std::string>{"0D", "0E", "0F", "0G", "0H"});
    const auto capacity = bms_identifier_capacity(song, "WAV");
    CHECK(capacity.used == 9);
    CHECK(capacity.total == 1'295);
    CHECK(capacity.largest_free_run > 5);
}

TEST_CASE("lowercase BMS base-36 aliases occupy their uppercase namespace ID",
          "[unit][core][resources][bms-identifiers]") {
    auto song = assignment_song();
    for (std::size_t value = 1; value < 10; ++value)
        song.resources.front().target_identities.push_back(
            {format_family_t::bms, std::string{"0"} + "123456789"[value - 1], "WAV"});
    song.resources.front().target_identities.push_back({format_family_t::bms, "0a", "WAV"});
    const auto capacity = bms_identifier_capacity(song, "WAV");
    CHECK(capacity.used == 10);
    CHECK(capacity.total == 1'295);
    CHECK(find_bms_identifier_run(song, "WAV", 1) ==
          std::optional<std::vector<std::string>>{{"0B"}});
}

TEST_CASE("BMS identifier renumber packs a natural-name subset atomically and undoes",
          "[unit][core][resources][bms-identifiers]") {
    const auto history = make_subset_renumber_history();
    CHECK(subset_identifiers(history) == std::vector<std::string>{"01", "02"});
    CHECK(history.document.resources[4].target_identities.front().identifier == "05");
}

TEST_CASE("BMS subset renumber is one undo step",
          "[unit][core][resources][bms-identifiers]") {
    auto history = make_subset_renumber_history();
    REQUIRE(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    const auto first = std::ranges::find(history.document.resources, resource_id_t{1},
                                         &resource_entry_t::id);
    REQUIRE(first != history.document.resources.end());
    CHECK(first->target_identities.front().identifier == "01");
}

TEST_CASE("BMS all-kind renumber includes BGM-only resources and leaves notes intact",
          "[unit][core][resources][bms-identifiers]") {
    auto song = assignment_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    song.resources[0].name = "snd10";
    song.resources[0].target_identities = {{format_family_t::bms, "07", "WAV"}};
    song.resources[1].name = "snd2";
    song.resources[1].roles = {resource_role_t::bgm};
    song.resources[1].target_identities = {{format_family_t::bms, "09", "WAV"}};
    song.resources[4].roles = {resource_role_t::preview};
    auto history = document_history_t::from(std::move(song));
    const auto result = apply_bms_identifier_renumber(history,
        {.name_space = "WAV"});
    REQUIRE(result);
    const auto snd10 = std::ranges::find(history.document.resources, std::string{"snd10"},
                                         &resource_entry_t::name);
    const auto snd2 = std::ranges::find(history.document.resources, std::string{"snd2"},
                                        &resource_entry_t::name);
    REQUIRE(snd10 != history.document.resources.end());
    REQUIRE(snd2 != history.document.resources.end());
    CHECK(snd2->target_identities.front().identifier == "01");
    CHECK(snd10->target_identities.front().identifier == "02");
    CHECK(history.document.charts.front().measures.front().notes.front().id == object_id_t{10});
}

TEST_CASE("BMS export applies renumbered IDs while preserving note resource links",
          "[unit][core][resources][bms-identifiers]") {
    const auto fixture = make_renumbered_bms_fixture();
    REQUIRE(fixture);
    const auto& history = fixture->history;
    CHECK(history.document.charts.front().measures.front().notes.front().sound ==
          std::optional<resource_id_t>{fixture->note_resource});
    CHECK(history.document.charts.front().measures.front().notes.front().id == fixture->note_id);
}

TEST_CASE("BMS export writes the natural renumbered resource IDs",
          "[unit][core][resources][bms-identifiers]") {
    const auto fixture = make_renumbered_bms_fixture();
    REQUIRE(fixture);
    const auto exported = write_bms(fixture->history.document);
    REQUIRE(exported.bytes);
    CHECK(exported.bytes->find("#WAV01 snd2.wav") != std::string::npos);
    CHECK(exported.bytes->find("#WAV02 snd10.wav") != std::string::npos);
    CHECK(exported.bytes->find("#00011:02") != std::string::npos);
}

TEST_CASE("BMS renumber rejects unknown and duplicate selections atomically",
          "[unit][core][resources][bms-identifiers]") {
    auto history = document_history_t::from(assignment_song());
    const auto revision = history.document.revision;
    const auto names_before = std::vector<std::string>{history.document.resources[0].name,
                                                       history.document.resources[1].name};
    check_renumber_rejection(history, {resource_id_t{999}}, names_before, revision);
    check_renumber_rejection(history, {resource_id_t{1}, resource_id_t{1}},
                             names_before, revision);
}

TEST_CASE("BGM move appends a lane only when needed and rejects linked markers",
          "[unit][core][resources][bgm-move]") {
    auto song = assignment_song();
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(apply_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{3}, 240}}).applied);
    CHECK(history.undo.empty());
    history.document.charts.front().measures.front().bgm_events.push_back(
        {.id = object_id_t{23}, .position = {measure_id_t{3}, 360},
         .source_lane = object_id_t{20}, .sound = resource_id_t{2}});
    const auto moved = apply_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{3}, 360}});
    REQUIRE(moved);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    const auto relocated = std::ranges::find(chart.measures.front().bgm_events,
                                             object_id_t{11}, &bgm_event_t::id);
    REQUIRE(relocated != chart.measures.front().bgm_events.end());
    CHECK(relocated->source_lane == chart.bgm_source_lanes.back().id);
    auto linked_song = assignment_song();
    linked_song.charts.front().measures.front().bgm_events.front().sound_channel_event =
        object_id_t{99};
    auto linked_history = document_history_t::from(std::move(linked_song));
    CHECK(has_code(apply_bgm_event_move(linked_history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .position = {measure_id_t{3}, 400}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(linked_history.undo.empty());
}

TEST_CASE("virtual BMS BGM move materializes destination with one undo step",
          "[unit][core][resources][bgm-move][virtual-canvas]") {
    auto song = assignment_song();
    song.charts.front().source_family = format_family_t::bms;
    auto history = document_history_t::from(std::move(song));
    const auto revision = history.document.revision;
    const auto moved = apply_virtual_bms_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11},
         .canvas_row = 959'061, .source_lane = object_id_t{20},
         .snap_denominator = 4});
    REQUIRE(moved);
    CHECK(history.undo.size() == 1);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.measures.size() == 1'000);
    CHECK(chart.measures.front().bgm_events.empty());
    REQUIRE(chart.measures.back().bgm_events.size() == 1);
    const auto& event = chart.measures.back().bgm_events.front();
    CHECK(event.id == object_id_t{11});
    CHECK(event.sound == resource_id_t{1});
    CHECK(event.source_lane == object_id_t{20});
    CHECK(event.position.row == 0);
    CHECK(validate_document(history.document).empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.size() == 1);
    CHECK(history.document.charts.front().measures.front().bgm_events.front().id ==
          object_id_t{11});
}

TEST_CASE("invalid virtual BGM move leaves authored extent unchanged",
          "[unit][core][resources][bgm-move][virtual-canvas]") {
    auto song = assignment_song();
    song.charts.front().source_family = format_family_t::bms;
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(apply_virtual_bms_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{999}, .canvas_row = 959'040}));
    CHECK_FALSE(apply_virtual_bms_bgm_event_move(history,
        {.chart = chart_id_t{2}, .event = object_id_t{11}, .canvas_row = 960'000}));
    CHECK(history.document.charts.front().measures.size() == 1);
    CHECK(history.undo.empty());
}

TEST_CASE("virtual BMS BGM and BGA placement commit geometry with the event",
          "[unit][core][resources][virtual-canvas]") {
    for (const auto kind : {resource_placement_kind_t::bgm_event,
                            resource_placement_kind_t::bga_background}) {
        auto song = assignment_song();
        song.charts.front().source_family = format_family_t::bms;
        auto history = document_history_t::from(std::move(song));
        const auto revision = history.document.revision;
        const auto resource = kind == resource_placement_kind_t::bgm_event
                                  ? resource_id_t{1}
                                  : resource_id_t{3};
        const auto placed = apply_virtual_bms_resource_placement(
            history,
            {.chart = chart_id_t{2},
             .resource = resource,
             .canvas_row = 959'050,
             .source_lane = kind == resource_placement_kind_t::bgm_event
                                ? object_id_t{20}
                                : object_id_t{},
             .kind = kind,
             .snap_denominator = 4});
        REQUIRE(placed);
        CHECK(history.undo.size() == 1);
        const auto& chart = history.document.charts.front();
        REQUIRE(chart.measures.size() == 1'000);
        if (kind == resource_placement_kind_t::bgm_event) {
            REQUIRE(chart.measures.back().bgm_events.size() == 1);
            CHECK(chart.measures.back().bgm_events.front().sound == resource);
            CHECK(chart.measures.back().bgm_events.front().position.row == 0);
        } else {
            REQUIRE(chart.bga_events.size() == 2);
            CHECK(chart.bga_events.back().resource == resource);
            CHECK(chart.bga_events.back().position.measure == chart.measures.back().id);
            CHECK(chart.bga_events.back().position.row == 0);
        }
        REQUIRE(undo_document_command(history));
        CHECK(history.document.charts.front().measures.size() == 1);
    }
}

TEST_CASE("invalid virtual BMS resource placement leaves history unchanged",
          "[unit][core][resources][virtual-canvas]") {
    auto song = assignment_song();
    song.charts.front().source_family = format_family_t::bms;
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(apply_virtual_bms_resource_placement(
        history,
        {.chart = chart_id_t{2}, .resource = resource_id_t{3},
         .canvas_row = 959'040, .kind = resource_placement_kind_t::bgm_event}));
    CHECK_FALSE(apply_virtual_bms_resource_placement(
        history,
        {.chart = chart_id_t{2}, .resource = resource_id_t{1},
         .canvas_row = 960'000, .kind = resource_placement_kind_t::bgm_event}));
    CHECK(history.document.charts.front().measures.size() == 1);
    CHECK(history.undo.empty());
}

TEST_CASE("resource placement picker advances in eligible catalog order without wrapping",
          "[unit][core][resources]") {
    auto song = assignment_song();
    CHECK(next_eligible_placement_resource(song, resource_id_t{1}, resource_role_t::sample) ==
          resource_id_t{2});
    CHECK_FALSE(next_eligible_placement_resource(song, resource_id_t{2},
                                                 resource_role_t::sample));
    CHECK(next_eligible_placement_resource(song, resource_id_t{3}, resource_role_t::bga) ==
          resource_id_t{4});
    CHECK_FALSE(next_eligible_placement_resource(song, resource_id_t{4}, resource_role_t::bga));
    CHECK_FALSE(next_eligible_placement_resource(song, resource_id_t{99},
                                                 resource_role_t::sample));
    song.resources.insert(song.resources.begin() + 1,
                          {.id = resource_id_t{6}, .asset = asset_id_t{99},
                           .roles = {resource_role_t::sample}});
    CHECK(next_eligible_placement_resource(song, resource_id_t{1}, resource_role_t::sample) ==
          resource_id_t{2});
}

TEST_CASE("resource creation assigns stable IDs and independent BMS namespaces",
          "[unit][core][resources]") {
    auto song = assignment_song();
    song.resources.front().target_identities = {
        {.family = format_family_t::bms, .identifier = "01", .namespace_name = "WAV"}};
    song.resources[1].target_identities = {
        {.family = format_family_t::bms, .identifier = "02", .namespace_name = "WAV"}};
    auto history = document_history_t::from(std::move(song));
    const auto created = apply_resource_creation(
        history, {.asset = asset_id_t{2}, .role = resource_role_t::sample,
                     .name = "New sample", .allocate_bms_identifier = true});
    REQUIRE(created);
    REQUIRE(history.document.resources.size() == 6);
    const auto& sample = history.document.resources.back();
    CHECK(sample.id == resource_id_t{6});
    CHECK(sample.asset == asset_id_t{2});
    CHECK(sample.target_identities.front().identifier == "03");
    CHECK(sample.target_identities.front().namespace_name == "WAV");
    const auto visual = apply_resource_creation(
        history, {.asset = asset_id_t{4}, .role = resource_role_t::bga,
                     .allocate_bms_identifier = true});
    REQUIRE(visual);
    CHECK(history.document.resources.back().id == resource_id_t{7});
    CHECK(history.document.resources.back().name == "one.png");
    CHECK(history.document.resources.back().target_identities.front().identifier == "01");
    CHECK(history.document.resources.back().target_identities.front().namespace_name == "BMP");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == 6);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == 5);
}

TEST_CASE("resource creation rejects wrong asset types atomically",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    CHECK(has_code(apply_resource_creation(
                       history, {.asset = asset_id_t{4}, .role = resource_role_t::sample}),
                   diagnostic_code_t::dangling_asset));
    CHECK(has_code(apply_resource_creation(
                       history, {.asset = asset_id_t{1}, .role = resource_role_t::bga}),
                   diagnostic_code_t::dangling_asset));
    CHECK(has_code(apply_resource_creation(
                       history, {.asset = asset_id_t{1}, .role = resource_role_t::music}),
                   diagnostic_code_t::unsupported_feature));
    CHECK(history.document.resources.size() == 5);
    CHECK(history.undo.empty());
}

TEST_CASE("resource import registers decoded asset and catalog entry in one history command",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto imported =
        apply_resource_asset_import(history,
                                    {.source_path = TEST_ROOT "/new-audio.wav",
                                     .role = resource_role_t::sample,
                                     .name = "New audio",
                                     .allocate_bms_identifier = true,
                                     .audio_stream = audio_stream_info_t{48'000, 2, 48'000, true}});
    REQUIRE(imported);
    REQUIRE(history.document.audio_assets.size() == 4);
    REQUIRE(history.document.resources.size() == 6);
    CHECK(history.document.resources.back().asset == history.document.audio_assets.back().id);
    CHECK(history.document.resources.back().target_identities.front().identifier == "01");
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.audio_assets.size() == 3);
    CHECK(history.document.resources.size() == 5);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.resources.size() == 6);

    const auto reused =
        apply_resource_asset_import(history,
                                    {.source_path = TEST_ROOT "/new-audio.wav",
                                     .role = resource_role_t::bgm,
                                     .audio_stream = audio_stream_info_t{48'000, 2, 48'000, true}});
    REQUIRE(reused);
    CHECK(history.document.audio_assets.size() == 4);
    CHECK(history.document.resources.back().asset == history.document.audio_assets.back().id);
}

TEST_CASE("explicit BMS WAV assignment creates a resource at the chosen empty slot",
          "[unit][core][resources][bms-wav-slots]") {
    auto history = document_history_t::from(assignment_song());
    const auto old_resources = history.document.resources.size();
    const auto old_assets = history.document.audio_assets.size();
    const auto result = apply_bms_wav_slot_assignment(
        history,
        {.identifier = "0a", .imported_file = wav_slot_import("empty-slot.wav", "Empty slot")});
    check_wav_slot(static_cast<bool>(result));
    check_wav_slot(history.document.resources.size() == old_resources + 1);
    check_wav_slot(history.document.audio_assets.size() == old_assets + 1);
    const auto& added = history.document.resources.back();
    check_wav_slot(added.id == resource_id_t{6});
    check_wav_slot(added.asset == history.document.audio_assets.back().id);
    check_wav_slot(added.target_identities.size() == 1);
    check_wav_slot(added.target_identities.front().identifier == "0A");
    check_wav_slot(added.target_identities.front().namespace_name == "WAV");
    check_wav_slot(history.undo.size() == 1);
    check_wav_slot(static_cast<bool>(undo_document_command(history)));
    check_wav_slot(history.document.resources.size() == old_resources);
    check_wav_slot(history.document.audio_assets.size() == old_assets);
    check_wav_slot(static_cast<bool>(redo_document_command(history)));
    check_wav_slot(history.document.resources.back().target_identities.front().identifier == "0A");
}

TEST_CASE("confirmed BMS WAV replacement preserves resource identity and references",
          "[unit][core][resources][bms-wav-slots]") {
    auto history = document_history_t::from(wav_slot_song());
    const auto original = history.document;
    const auto old_asset = history.document.resources.front().asset;
    const auto result = apply_bms_wav_slot_assignment(
        history,
        {.identifier = "01", .imported_file = wav_slot_import("replacement.wav", "Replacement"),
         .confirm_replacement = true});
    check_wav_slot(static_cast<bool>(result));
    const auto& replaced = history.document.resources.front();
    const auto replacement_asset = replaced.asset;
    check_wav_slot(replaced.id == original.resources.front().id);
    check_wav_slot(replaced.asset != old_asset);
    check_wav_slot(replaced.name == "Replacement");
    check_wav_slot(replaced.roles == original.resources.front().roles);
    check_wav_slot(same_target_identities(replaced.target_identities,
                                          original.resources.front().target_identities));
    check_wav_slot(replaced.sound_kind == original.resources.front().sound_kind);
    check_wav_slot(replaced.tags == original.resources.front().tags);
    check_wav_slot(replaced.sound_group == original.resources.front().sound_group);
    check_wav_slot(history.document.resources[1].asset == old_asset);
    check_wav_slot(same_target_identities(history.document.resources[1].target_identities,
                                          original.resources[1].target_identities));
    check_wav_slot(history.document.audio_assets.front().id == original.audio_assets.front().id);
    check_wav_slot(history.document.audio_assets.front().source_path ==
                   original.audio_assets.front().source_path);
    check_wav_slot(history.document.audio_assets.front().stream.sample_rate ==
                   original.audio_assets.front().stream.sample_rate);
    check_wav_slot(history.document.charts.front().sound_layer_defaults ==
                   original.charts.front().sound_layer_defaults);
    check_wav_slot(history.document.charts.front().measures.front().notes.front().sound ==
                   original.charts.front().measures.front().notes.front().sound);
    check_wav_slot(history.document.charts.front().measures.front().bgm_events.front().sound ==
                   original.charts.front().measures.front().bgm_events.front().sound);
    check_wav_slot(history.undo.size() == 1);
    check_wav_slot(static_cast<bool>(undo_document_command(history)));
    check_wav_slot(history.document.resources.front().id == original.resources.front().id);
    check_wav_slot(history.document.resources.front().asset == original.resources.front().asset);
    check_wav_slot(same_target_identities(history.document.resources.front().target_identities,
                                          original.resources.front().target_identities));
    check_wav_slot(static_cast<bool>(redo_document_command(history)));
    check_wav_slot(history.document.resources.front().asset == replacement_asset);
}

TEST_CASE("BMS WAV assignment rejects reserved, invalid and unconfirmed requests atomically",
          "[unit][core][resources][bms-wav-slots]") {
    auto history = document_history_t::from(wav_slot_song());
    const auto original = history.document;
    auto attempt = [&](std::string id, bool confirm = false) {
        return apply_bms_wav_slot_assignment(history,
            {.identifier = std::move(id), .imported_file = wav_slot_import("reject.wav", "Reject"),
             .confirm_replacement = confirm});
    };
    check_wav_slot(!attempt("00", true));
    check_wav_slot(!attempt("0?", true));
    check_wav_slot(!attempt("100", true));
    check_wav_slot(!attempt("01"));
    auto relative_import = wav_slot_import("relative.wav", "Reject");
    relative_import.source_path = "relative.wav";
    check_wav_slot(!apply_bms_wav_slot_assignment(history,
        {.identifier = "03", .imported_file = relative_import}));
    auto invalid_probe = wav_slot_import("invalid-probe.wav", "Reject");
    invalid_probe.audio_stream->sample_rate = 0;
    check_wav_slot(!apply_bms_wav_slot_assignment(history,
        {.identifier = "03", .imported_file = invalid_probe}));
    check_wav_slot(history.document.resources.size() == original.resources.size());
    check_wav_slot(history.document.resources.front().asset == original.resources.front().asset);
    check_wav_slot(same_target_identities(history.document.resources.front().target_identities,
                                          original.resources.front().target_identities));
    check_wav_slot(history.document.audio_assets.size() == original.audio_assets.size());
    check_wav_slot(history.document.audio_assets.front().source_path ==
                   original.audio_assets.front().source_path);
    check_wav_slot(history.document.revision == original.revision);
    check_wav_slot(history.undo.empty());
}

TEST_CASE("explicit WAV slot assignment matches canonical namespace casing",
          "[unit][core][resources][bms-wav-slots]") {
    auto song = wav_slot_song();
    song.resources.front().target_identities.front().namespace_name = "wav";
    auto history = document_history_t::from(std::move(song));
    const auto result = apply_bms_wav_slot_assignment(
        history,
        {.identifier = "01", .imported_file = wav_slot_import("case-fold.wav", "Case fold")});
    check_wav_slot(!result);
    check_wav_slot(history.document.resources.front().asset == asset_id_t{1});
    check_wav_slot(history.undo.empty());
}

TEST_CASE("BMS WAV slot batches use natural filename order and one undo step",
          "[unit][core][resources][bms-wav-slots]") {
    auto history = document_history_t::from(assignment_song());
    const auto revision = history.document.revision;
    const auto result = apply_bms_wav_slot_batch_assignment(history,
        {.first_identifier = "0Z",
         .imported_files = {wav_slot_import("/b/snd10.wav", "ten"),
                            wav_slot_import("/a/snd2.wav", "two"),
                            wav_slot_import("/z/snd1.wav", "one")}});
    REQUIRE(result);
    REQUIRE(history.undo.size() == 1);
    REQUIRE(history.document.resources.size() == 8);
    const auto find_import = [&](std::string_view name) -> const resource_entry_t* {
        const auto item = std::ranges::find(history.document.resources, name,
                                              &resource_entry_t::name);
        return item == history.document.resources.end() ? nullptr : &*item;
    };
    const auto* one = find_import("one");
    const auto* two = find_import("two");
    const auto* ten = find_import("ten");
    REQUIRE(one);
    REQUIRE(two);
    REQUIRE(ten);
    CHECK(one->target_identities.front().identifier == "0Z");
    CHECK(two->target_identities.front().identifier == "10");
    CHECK(ten->target_identities.front().identifier == "11");
    CHECK(history.document.resources[0].id == resource_id_t{1});
    CHECK(history.document.resources[0].asset == asset_id_t{1});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == 5);
    CHECK(history.document.audio_assets.size() == 3);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.resources.size() == 8);
}

TEST_CASE("BMS WAV slot batches replace occupied resources only when confirmed",
          "[unit][core][resources][bms-wav-slots]") {
    auto history = document_history_t::from(wav_slot_song());
    const auto before = history.document;
    const auto unconfirmed = apply_bms_wav_slot_batch_assignment(history,
        {.first_identifier = "01",
         .imported_files = {wav_slot_import("replace.wav", "Replacement")}});
    CHECK_FALSE(unconfirmed);
    CHECK(history.document.revision == before.revision);
    CHECK(history.document.resources.front().asset == before.resources.front().asset);
    const auto applied = apply_bms_wav_slot_batch_assignment(history,
        {.first_identifier = "01",
         .imported_files = {wav_slot_import("replace.wav", "Replacement")},
         .confirm_replacements = true});
    REQUIRE(applied);
    const auto& replaced = history.document.resources.front();
    CHECK(replaced.id == before.resources.front().id);
    CHECK(replaced.asset != before.resources.front().asset);
    CHECK(replaced.name == "Replacement");
    CHECK(replaced.sound_kind == before.resources.front().sound_kind);
    CHECK(replaced.tags == before.resources.front().tags);
    CHECK(replaced.sound_group == before.resources.front().sound_group);
    CHECK(same_target_identities(replaced.target_identities,
                                 before.resources.front().target_identities));
    CHECK(history.document.charts.front().measures.front().notes.front().sound ==
          before.charts.front().measures.front().notes.front().sound);
    CHECK(history.document.charts.front().sound_layer_defaults ==
          before.charts.front().sound_layer_defaults);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("BMS WAV slot batch rejection leaves document and history untouched",
          "[unit][core][resources][bms-wav-slots]") {
    auto history = document_history_t::from(wav_slot_song());
    const auto before = history.document;
    const auto try_batch = [&](std::string first, std::vector<import_resource_asset_action_t> files,
                               bool confirm = true) {
        return apply_bms_wav_slot_batch_assignment(history,
            {.first_identifier = std::move(first), .imported_files = std::move(files),
             .confirm_replacements = confirm});
    };
    CHECK_FALSE(try_batch("00", {wav_slot_import("x.wav", "X")}));
    CHECK_FALSE(try_batch("ZZ", {wav_slot_import("x.wav", "X"),
                                  wav_slot_import("y.wav", "Y")}));
    CHECK_FALSE(try_batch("03", {wav_slot_import("same.wav", "X"),
                                  wav_slot_import("same.wav", "Y")}));
    auto invalid = wav_slot_import("invalid.wav", "Invalid");
    invalid.audio_stream->sample_rate = 0;
    CHECK_FALSE(try_batch("03", {invalid}));
    CHECK(history.document.revision == before.revision);
    CHECK(history.document.resources.size() == before.resources.size());
    CHECK(history.document.audio_assets.size() == before.audio_assets.size());
    CHECK(history.document.resources.front().asset == before.resources.front().asset);
    CHECK(same_target_identities(history.document.resources.front().target_identities,
                                 before.resources.front().target_identities));
    CHECK(history.undo.empty());
}

TEST_CASE("BMS WAV slot batches accept 1200 files with one-step undo and redo",
          "[unit][core][resources][bms-wav-slots][slow]") {
    auto history = document_history_t::from(assignment_song());
    const auto original_resources = history.document.resources.size();
    const auto original_assets = history.document.audio_assets.size();
    auto files = std::vector<import_resource_asset_action_t>{};
    files.reserve(1200);
    for (std::size_t index = 0; index < 1200; ++index) {
        files.push_back(wav_slot_import("batch" + std::to_string(index) + ".wav",
                                        "Batch " + std::to_string(index)));
    }
    const auto result = apply_bms_wav_slot_batch_assignment(history,
        {.first_identifier = "01", .imported_files = std::move(files)});
    REQUIRE(result);
    CHECK(history.undo.size() == 1);
    CHECK(history.document.resources.size() == original_resources + 1200);
    CHECK(history.document.audio_assets.size() == original_assets + 1200);
    CHECK(history.document.resources.back().target_identities.front().identifier == "XC");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == original_resources);
    CHECK(history.document.audio_assets.size() == original_assets);
    REQUIRE(redo_document_command(history));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.resources.size() == original_resources + 1200);
    CHECK(history.document.audio_assets.size() == original_assets + 1200);
    CHECK(history.document.resources.back().target_identities.front().identifier == "XC");
}

namespace { void check_import_extent(bool value) { CHECK(value); } }

TEST_CASE("BGM import extends native chart measures in the same undo command",
          "[core][resources][audio-extent]") {
    auto song = assignment_song();
    song.charts.front().source_family = format_family_t::native_project;
    auto history = document_history_t::from(song);
    REQUIRE(apply_resource_asset_import(history,
        {.source_path = TEST_ROOT "/track.mp3", .role = resource_role_t::bgm,
         .audio_stream = audio_stream_info_t{44'100, 2, 44'100 * 13, true}}));
    REQUIRE(history.document.charts.front().measures.size() == 7);
    check_import_extent(history.document.charts.front().measures.front().notes.front().position ==
          song.charts.front().measures.front().notes.front().position);
    check_import_extent(history.undo.size() == 1);
    check_import_extent(validate_document(history.document).empty());
    REQUIRE(undo_document_command(history));
    check_import_extent(history.document.charts.front().measures.size() == 1);
    REQUIRE(redo_document_command(history));
    check_import_extent(history.document.charts.front().measures.size() == 7);
}

namespace {
auto music_resources(const song_document_t& song) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count_if(song.resources, [](const auto& r) {
        return std::ranges::find(r.roles, resource_role_t::music) != r.roles.end();
    }));
}
} // namespace

TEST_CASE("setting song audio without a music resource imports one Music+BGM resource",
          "[unit][core][resources][song-audio]") {
    auto history = document_history_t::from(assignment_song());
    REQUIRE(apply_primary_music_set(history,
        {.source_path = TEST_ROOT "/song.mp3", .name = "song.mp3",
         .audio_stream = audio_stream_info_t{44'100, 2, 44'100 * 3, true}}));
    CHECK(music_resources(history.document) == 1);
    const auto& added = history.document.resources.back();
    CHECK(std::ranges::find(added.roles, resource_role_t::bgm) != added.roles.end());
    CHECK(history.document.audio_assets.back().source_path == TEST_ROOT "/song.mp3");
    CHECK(history.undo.size() == 1);
}

TEST_CASE("setting song audio again repoints the music asset instead of adding another",
          "[unit][core][resources][song-audio]") {
    auto song = assignment_song();
    song.charts.front().source_family = format_family_t::native_project;
    song.resources.front().roles.push_back(resource_role_t::music);
    auto history = document_history_t::from(song);
    const auto assets = history.document.audio_assets.size();
    const auto resources = history.document.resources.size();
    REQUIRE(apply_primary_music_set(history,
        {.source_path = TEST_ROOT "/replacement.mp3", .name = "replacement.mp3",
         .audio_stream = audio_stream_info_t{44'100, 2, 44'100 * 13, true}}));
    CHECK(history.document.audio_assets.size() == assets);
    CHECK(history.document.resources.size() == resources);
    CHECK(music_resources(history.document) == 1);
    const auto& music = history.document.resources.front();
    const auto asset = std::ranges::find(history.document.audio_assets, music.asset,
                                         &audio_asset_t::id);
    REQUIRE(asset != history.document.audio_assets.end());
    CHECK(asset->source_path == TEST_ROOT "/replacement.mp3");
    CHECK(asset->stream.frame_count == 44'100U * 13U);
    CHECK(music.name == "replacement.mp3");
    CHECK(history.document.charts.front().measures.size() == 7);
    CHECK(validate_document(history.document).empty());

    CHECK_FALSE(apply_primary_music_set(history,
        {.source_path = TEST_ROOT "/replacement.mp3",
         .audio_stream = audio_stream_info_t{44'100, 2, 44'100 * 13, true}}));
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(std::ranges::find(history.document.audio_assets, music.asset, &audio_asset_t::id)
              ->source_path == "one.wav");
}

TEST_CASE("setting song audio rejects missing stream geometry",
          "[unit][core][resources][song-audio]") {
    auto history = document_history_t::from(assignment_song());
    CHECK(has_code(apply_primary_music_set(history, {.source_path = TEST_ROOT "/song.mp3"}),
                   diagnostic_code_t::invalid_id));
    CHECK(history.undo.empty());
}

TEST_CASE("resource import rejects path and decoded-type errors without partial assets",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    CHECK(has_code(apply_resource_asset_import(history,
                                               {.source_path = "relative.png",
                                                .role = resource_role_t::bga,
                                                .image_geometry = std::pair{640U, 480U}}),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(
        apply_resource_asset_import(history,
                                    {.source_path = TEST_ROOT "/new.png",
                                     .role = resource_role_t::bga,
                                     .audio_stream = audio_stream_info_t{48'000, 2, 100, true}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(history.document.visual_assets.size() == 2);
    CHECK(history.document.resources.size() == 5);
    CHECK(history.undo.empty());
}

TEST_CASE("copied resource import binds a reserved project path in one undo command",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto reserved = next_resource_asset_id(history.document);
    REQUIRE(reserved);
    const auto relative =
        std::filesystem::path{"assets"} / (std::to_string(reserved->value) + "-new-audio.wav");
    const import_resource_asset_action_t action{.source_path = TEST_ROOT "/new-audio.wav",
                                                .copied_project_relative_path = relative,
                                                .expected_new_asset_id = *reserved,
                                                .role = resource_role_t::bgm,
                                                .name = "Copied audio",
                                                .audio_stream =
                                                    audio_stream_info_t{48'000, 2, 48'000, true}};
    const auto imported = apply_resource_asset_import(history, action);
    REQUIRE(imported);
    CHECK(history.document.audio_assets.back().id == *reserved);
    CHECK(history.document.audio_assets.back().source_path == relative);
    CHECK(history.document.resources.back().asset == *reserved);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.audio_assets.size() == 3);
    CHECK(history.document.resources.size() == 5);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.audio_assets.back().source_path == relative);

    auto invalid = action;
    invalid.copied_project_relative_path = "assets/../outside.wav";
    CHECK(has_code(apply_resource_asset_import(history, invalid),
                   diagnostic_code_t::invalid_id));
    invalid = action;
    invalid.expected_new_asset_id = asset_id_t{reserved->value + 8};
    CHECK(has_code(apply_resource_asset_import(history, invalid),
                   diagnostic_code_t::invalid_id));
}

TEST_CASE("export-staged import reserves a new asset while retaining its external source",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto reserved = next_resource_asset_id(history.document);
    REQUIRE(reserved);
    const import_resource_asset_action_t action{.source_path = TEST_ROOT "/export-audio.ogg",
                                                .expected_new_asset_id = *reserved,
                                                .role = resource_role_t::bgm,
                                                .audio_stream =
                                                    audio_stream_info_t{44'100, 2, 22'050, true}};
    REQUIRE(apply_resource_asset_import(history, action));
    CHECK(history.document.audio_assets.back().id == *reserved);
    CHECK(history.document.audio_assets.back().source_path == action.source_path);
    CHECK(history.undo.size() == 1);
    CHECK(has_code(apply_resource_asset_import(history, action),
                   diagnostic_code_t::invalid_id));
}

TEST_CASE("visual resource import records decoded geometry and BMP identity",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto imported = apply_resource_asset_import(
        history,
        {.source_path = TEST_ROOT "/new-background.png",
         .role = resource_role_t::bga,
         .allocate_bms_identifier = true,
         .image_geometry = std::pair{640U, 480U}});
    REQUIRE(imported);
    REQUIRE(history.document.visual_assets.size() == 3);
    REQUIRE(history.document.resources.size() == 6);
    const auto& asset = history.document.visual_assets.back();
    CHECK(asset.kind == asset_kind_t::image);
    CHECK(asset.width == 640);
    CHECK(asset.height == 480);
    CHECK(history.document.resources.back().asset == asset.id);
    CHECK(history.document.resources.back().target_identities.front().namespace_name == "BMP");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.visual_assets.size() == 2);
}

TEST_CASE("resource asset relink preserves IDs and rejects escaping relative paths",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto relinked = apply_resource_asset_relink(
        history, {.asset = asset_id_t{1},
                     .source_path = "assets/drums/one.wav"});
    REQUIRE(relinked);
    CHECK(history.document.audio_assets.front().source_path ==
          std::filesystem::path{"assets/drums/one.wav"});
    CHECK(history.document.resources.front().asset == asset_id_t{1});
    CHECK(history.document.charts.front().measures.front().notes.front().sound ==
          resource_id_t{1});
    CHECK(has_code(apply_resource_asset_relink(
                       history, {.asset = asset_id_t{1},
                                    .source_path = "../outside.wav"}),
                   diagnostic_code_t::invalid_id));
    const auto no_op = apply_resource_asset_relink(
        history, {.asset = asset_id_t{1},
                     .source_path = "assets/drums/one.wav"});
    CHECK_FALSE(no_op);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.audio_assets.front().source_path ==
          std::filesystem::path{"one.wav"});

    const auto visual = apply_resource_asset_relink(
        history,
        {.asset = asset_id_t{4}, .source_path = "assets/background.png"});
    REQUIRE(visual);
    CHECK(history.document.visual_assets.front().source_path ==
          std::filesystem::path{"assets/background.png"});
}

TEST_CASE("resource placement adds typed events with collision and lane allocation",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto sample = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1},
                     .kind = resource_placement_kind_t::sample_note});
    REQUIRE(sample);
    CHECK(history.document.charts.front().measures.front().notes.back().sound ==
          resource_id_t{2});

    const auto bgm = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 240},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(bgm);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    CHECK(chart.measures.front().bgm_events.back().source_lane ==
          chart.bgm_source_lanes.back().id);
    const auto reused = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 480},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(reused);
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 2);
    CHECK(history.document.charts.front().measures.front().bgm_events.back().source_lane ==
          object_id_t{20});

    const auto bga = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{4},
                     .position = {measure_id_t{3}, 240},
                     .kind = resource_placement_kind_t::bga_background});
    REQUIRE(bga);
    CHECK(history.document.charts.front().bga_events.back().resource == resource_id_t{4});
    CHECK(history.undo.size() == 4);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bga_events.size() == 1);
}

TEST_CASE("resource placement rejects incompatible and occupied targets atomically",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto sample = place_resource_action_t{
        .chart = chart_id_t{2}, .resource = resource_id_t{2},
        .position = {measure_id_t{3}, 120}, .lane = lane_id_t{1},
        .kind = resource_placement_kind_t::sample_note};
    CHECK(has_code(apply_resource_placement(history, sample),
                   diagnostic_code_t::collision));
    auto incompatible = sample;
    incompatible.resource = resource_id_t{4};
    CHECK(has_code(apply_resource_placement(history, incompatible),
                   diagnostic_code_t::unsupported_feature));
    auto missing = sample;
    missing.resource = resource_id_t{5};
    CHECK(has_code(apply_resource_placement(history, missing),
                   diagnostic_code_t::dangling_resource));
    auto visual = place_resource_action_t{
        .chart = chart_id_t{2}, .resource = resource_id_t{4},
        .position = {measure_id_t{3}, 360},
        .kind = resource_placement_kind_t::bga_background};
    CHECK(has_code(apply_resource_placement(history, visual),
                   diagnostic_code_t::collision));
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("BGM placement creates the first source lane when the chart has none",
          "[unit][core][resources]") {
    auto song = assignment_song();
    song.charts.front().measures.front().bgm_events.clear();
    song.charts.front().bgm_source_lanes.clear();
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto applied = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 240},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(applied);
    REQUIRE(history.document.charts.front().bgm_source_lanes.size() == 1);
    REQUIRE(history.document.charts.front().measures.front().bgm_events.size() == 1);
    CHECK(history.document.charts.front().measures.front().bgm_events.front().source_lane ==
          history.document.charts.front().bgm_source_lanes.front().id);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bgm_source_lanes.empty());
}

TEST_CASE("pointer resource placement snaps exactly and respects a requested BGM lane",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto& chart = history.document.charts.front();
    const auto snapped = resolve_resource_placement_position(
        chart, {measure_id_t{3}, 190}, 8);
    REQUIRE(snapped);
    CHECK(snapped->row == 240);
    const auto tie = resolve_resource_placement_position(
        chart, {measure_id_t{3}, 180}, 8);
    REQUIRE(tie);
    CHECK(tie->row == 120);
    CHECK_FALSE(resolve_resource_placement_position(
        chart, {measure_id_t{3}, 190}, 7));

    const auto occupied = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 190},
                     .source_lane = object_id_t{20},
                     .kind = resource_placement_kind_t::bgm_event,
                     .snap_denominator = 8});
    CHECK_FALSE(occupied);
    CHECK(has_code(occupied, diagnostic_code_t::collision));
    const auto placed = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 510},
                     .source_lane = object_id_t{20},
                     .kind = resource_placement_kind_t::bgm_event,
                     .snap_denominator = 8});
    REQUIRE(placed);
    CHECK(history.document.charts.front().measures.front().bgm_events.back().position.row == 480);
    CHECK(history.document.charts.front().measures.front().bgm_events.back().source_lane ==
          object_id_t{20});
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 1);
}

TEST_CASE("resource assignment changes typed note and BGM references atomically",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto applied = apply_resource_assignment(
        history,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{10}},
                     {editor_selection_object_kind_t::bgm_event, object_id_t{11}}}});
    REQUIRE(applied);
    const auto& measure = history.document.charts.front().measures.front();
    CHECK(measure.notes.front().sound == resource_id_t{2});
    CHECK(measure.bgm_events.front().sound == resource_id_t{2});
    CHECK(history.document.charts.front().bga_events.front().resource == resource_id_t{3});
    CHECK(history.undo.size() == 1);

    const auto no_op = apply_resource_assignment(
        history,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{10}}}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().notes.front().sound == resource_id_t{1});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().measures.front().bgm_events.front().sound ==
          resource_id_t{2});
}

TEST_CASE("resource assignment changes BGA references through the same command",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto applied = apply_resource_assignment(
        history,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{4},
         .objects = {{editor_selection_object_kind_t::bga_event, object_id_t{12}}}});
    REQUIRE(applied);
    CHECK(history.document.charts.front().bga_events.front().resource == resource_id_t{4});
    CHECK(history.document.charts.front().bga_events.front().kind ==
          visual_event_kind_t::background);
}

TEST_CASE("resource assignment rejects incompatible and incomplete selections",
          "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto note =
        editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{10}};
    const auto bgm =
        editor_selection_object_t{editor_selection_object_kind_t::bgm_event, object_id_t{11}};
    CHECK(has_code(
        apply_resource_assignment(
            history, {.chart = chart_id_t{2}, .resource = resource_id_t{4}, .objects = {note}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(has_code(
        apply_resource_assignment(
            history, {.chart = chart_id_t{2}, .resource = resource_id_t{5}, .objects = {note}}),
        diagnostic_code_t::dangling_resource));
    CHECK(has_code(
        apply_resource_assignment(
            history,
            {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .objects = {note, note}}),
        diagnostic_code_t::duplicate_id));
    CHECK(
        has_code(apply_resource_assignment(
                     history,
                     {.chart = chart_id_t{2},
                      .resource = resource_id_t{2},
                      .objects = {note, {editor_selection_object_kind_t::note, object_id_t{99}}}}),
                 diagnostic_code_t::invalid_id));
    CHECK(has_code(
        apply_resource_assignment(
            history,
            {.chart = chart_id_t{2},
             .resource = resource_id_t{2},
             .objects = {note, {editor_selection_object_kind_t::bga_event, object_id_t{12}}}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(has_code(apply_resource_assignment(
                       history,
                       {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .objects = {bgm}},
                       {.maximum_chart_objects = 1}),
                   diagnostic_code_t::invalid_resolution));
    CHECK(history.document.revision == 4);
    CHECK(history.document.charts.front().measures.front().notes.front().sound == resource_id_t{1});
    CHECK(history.document.charts.front().measures.front().bgm_events.front().sound ==
          resource_id_t{1});
    CHECK(history.undo.empty());
}

TEST_CASE("resource assignment protects retained BMSON sound links",
          "[unit][core][resources][bmson]") {
    auto song = assignment_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.front().sound_channel_event = object_id_t{51};
    chart.sound_channels = {
        {.id = object_id_t{50},
         .source_clip = {.id = clip_id_t{1}, .source = asset_id_t{1}, .end_frame = 48'000},
         .events = {
             {.id = object_id_t{51}, .position = {measure_id_t{3}, 120}, .lane = lane_id_t{1}}}}};
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto rejected = apply_resource_assignment(
        history,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{10}}}});
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("sound metadata sets kind and normalized tags in one undoable step",
          "[unit][core][resources][sounds]") {
    auto history = document_history_t::from(assignment_song());
    const auto result = apply_resource_sound_metadata(
        history,
        {.resource = resource_id_t{1},
         .kind = sound_kind_t::stem,
         .tags = {"  Drums ", "drums", "VOX", "vox lead"}});
    REQUIRE(result);
    const auto& resource = history.document.resources.front();
    CHECK(resource.sound_kind == sound_kind_t::stem);
    CHECK(resource.tags == std::vector<std::string>{"Drums", "VOX", "vox lead"});
    REQUIRE(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.front().sound_kind == sound_kind_t::unspecified);
    CHECK(history.document.resources.front().tags.empty());
}

TEST_CASE("sound metadata rejects invalid input without changing the document",
          "[unit][core][resources][sounds]") {
    auto history = document_history_t::from(assignment_song());
    const auto revision = history.document.revision;
    const auto rejected = [&](set_resource_sound_metadata_action_t action) {
        const auto result = apply_resource_sound_metadata(history, action);
        CHECK_FALSE(result);
        CHECK(history.document.revision == revision);
        CHECK(history.undo.empty());
    };
    rejected({.resource = resource_id_t{1}, .tags = {"   "}});
    rejected({.resource = resource_id_t{1}, .tags = {std::string(65, 'a')}});
    rejected({.resource = resource_id_t{1}, .tags = {"bad\ntag"}});
    rejected({.resource = resource_id_t{1}, .tags = {"\xc0\x80"}});
    rejected({.resource = resource_id_t{1}, .name = "\xc0\x80"});
    rejected({.resource = resource_id_t{99}, .tags = {"x"}});
    rejected({.resource = resource_id_t{1}, .kind = static_cast<sound_kind_t>(9)});
    set_resource_sound_metadata_action_t too_many{.resource = resource_id_t{1}};
    for (int index = 0; index < 33; ++index) too_many.tags.push_back("tag" + std::to_string(index));
    rejected(too_many);
}

TEST_CASE("sound metadata that changes nothing creates no history entry",
          "[unit][core][resources][sounds]") {
    auto history = document_history_t::from(assignment_song());
    const auto result = apply_resource_sound_metadata(history, {.resource = resource_id_t{1}});
    CHECK_FALSE(result);
    CHECK(result.diagnostics.empty());
    CHECK(history.undo.empty());
    CHECK(history.document.revision == 4);
}

TEST_CASE("unused resource deletion leaves its asset intact",
          "[unit][core][resources][delete]") {
    auto history = document_history_t::from(assignment_song());
    const auto deleted = apply_resource_deletion(history, resource_id_t{4});
    REQUIRE(deleted);
    CHECK(history.document.resources.size() == 4);
    CHECK((history.document.visual_assets.size() == 2 &&
          history.document.visual_assets.back().source_path == "two.png" &&
          history.document.visual_assets.back().id == asset_id_t{5}));
}

TEST_CASE("undo restores a deleted resource without renumbering BMS identities",
          "[unit][core][resources][delete]") {
    auto song = assignment_song();
    song.resources[2].target_identities = {
        {.family = format_family_t::bms, .identifier = "03", .namespace_name = "BMP"}};
    song.resources[3].target_identities = {
        {.family = format_family_t::bms, .identifier = "04", .namespace_name = "BMP"}};
    auto history = document_history_t::from(std::move(song));
    REQUIRE(apply_resource_deletion(history, resource_id_t{4}));
    REQUIRE(undo_document_command(history));
    CHECK((history.document.resources.size() == 5 &&
          history.document.resources[2].id == resource_id_t{3} &&
          history.document.resources[2].target_identities.front().identifier == "03" &&
          history.document.resources[3].id == resource_id_t{4} &&
          history.document.resources[3].target_identities.front().identifier == "04" &&
          history.document.visual_assets.back().source_path == "two.png"));
}

TEST_CASE("resource deletion counts direct and retained sound references across charts",
          "[unit][core][resources][delete]") {
    auto history = document_history_t::from(deletion_reference_song());
    const auto note_and_bgm = apply_resource_deletion(history, resource_id_t{1});
    CHECK_FALSE(note_and_bgm);
    REQUIRE(note_and_bgm.diagnostics.size() == 1);
    CHECK(note_and_bgm.diagnostics.front().message ==
          "resource is still referenced by 5 stored event(s)");
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("resource deletion counts BGA references across charts",
          "[unit][core][resources][delete]") {
    auto history = document_history_t::from(deletion_reference_song());
    const auto bga = apply_resource_deletion(history, resource_id_t{3});
    CHECK_FALSE(bga);
    REQUIRE(bga.diagnostics.size() == 1);
    CHECK(bga.diagnostics.front().message == "resource is still referenced by 3 stored event(s)");
}

TEST_CASE("retained channel references conservatively protect resource aliases",
          "[unit][core][resources][delete]") {
    auto song = deletion_reference_song();
    song.charts.front().measures.front().notes.front().sound_channel_event.reset();
    song.resources.push_back({.id = resource_id_t{6}, .name = "Alias",
                              .asset = asset_id_t{1}, .roles = {resource_role_t::sample}});
    auto history = document_history_t::from(std::move(song));
    const auto rejected = apply_resource_deletion(history, resource_id_t{6});
    CHECK_FALSE(rejected);
    REQUIRE(rejected.diagnostics.size() == 1);
    CHECK(rejected.diagnostics.front().message ==
          "resource is still referenced by 1 stored event(s)");
    CHECK(history.document.resources.size() == 6);
    CHECK(history.document.audio_assets.front().id == asset_id_t{1});
    CHECK(history.undo.empty());
}

TEST_CASE("resource deletion rejects missing IDs atomically",
          "[unit][core][resources][delete]") {
    auto history = document_history_t::from(assignment_song());
    const auto unknown = apply_resource_deletion(history, resource_id_t{99});
    CHECK_FALSE(unknown);
    CHECK(unknown.diagnostics.front().message ==
          "resource deletion refers to an unknown catalog resource");
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("resource deletion rejects the reserved zero ID atomically",
          "[unit][core][resources][delete]") {
    auto history = document_history_t::from(assignment_song());
    const auto zero = apply_resource_deletion(history, resource_id_t{});
    CHECK_FALSE(zero);
    CHECK(zero.diagnostics.front().message ==
          "resource deletion refers to an unknown catalog resource");
    CHECK(history.document.revision == 4);
    CHECK(history.document.resources.size() == 5);
    CHECK(history.undo.empty());
}

TEST_CASE("resource deletion preserves remaining BMS IDs before undo",
          "[unit][core][resources][delete]") {
    auto song = assignment_song();
    song.resources[0].target_identities = {
        {.family = format_family_t::bms, .identifier = "01", .namespace_name = "WAV"}};
    song.resources[1].target_identities = {
        {.family = format_family_t::bms, .identifier = "02", .namespace_name = "WAV"}};
    auto history = document_history_t::from(std::move(song));
    REQUIRE(apply_resource_deletion(history, resource_id_t{2}));
    CHECK(history.document.resources.front().id == resource_id_t{1});
    CHECK(history.document.resources.front().target_identities.front().identifier == "01");
    CHECK(history.document.audio_assets.size() == 3);
    CHECK(history.document.audio_assets[1].source_path == "two.wav");
}

namespace {

auto layered_sound_song() -> song_document_t {
    auto song = assignment_song();
    song.charts.front().semantic_layers = {
        {.id = layer_id_t{7}, .name = "Vocal", .color = {255, 128, 0, 255}},
        {.id = layer_id_t{8}, .name = "Drums", .color = {0, 128, 255, 255}},
        {.id = layer_id_t{9}, .name = "Frozen", .color = {9, 9, 9, 255}, .locked = true}};
    REQUIRE(validate_document(song).empty());
    return song;
}

auto place_at(document_history_t& history, std::uint32_t division, resource_id_t resource)
    -> document_command_result_t {
    return apply_resource_placement(
        history,
        {.chart = chart_id_t{2}, .resource = resource,
         .position = {measure_id_t{3}, division}, .lane = lane_id_t{1},
         .kind = resource_placement_kind_t::sample_note});
}

} // namespace

TEST_CASE("sound layer defaults group sounds, undo as one step and tag painted notes",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    const auto set =
        apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                             .resources = {resource_id_t{1}, resource_id_t{2}},
                                             .layers = {layer_id_t{7}, layer_id_t{8}}});
    REQUIRE(set);
    REQUIRE(history.document.charts.front().sound_layer_defaults.size() == 2);
    CHECK(history.undo.size() == 1);

    REQUIRE(place_at(history, 480, resource_id_t{2}));
    const auto& painted = history.document.charts.front().measures.front().notes.back();
    CHECK(painted.sound == resource_id_t{2});
    CHECK(painted.semantic_layers == std::vector<layer_id_t>{layer_id_t{7}, layer_id_t{8}});

    const auto same =
        apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                             .resources = {resource_id_t{1}},
                                             .layers = {layer_id_t{7}, layer_id_t{8}}});
    CHECK_FALSE(same);
    CHECK(same.diagnostics.empty());

    REQUIRE(undo_document_command(history));
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sound_layer_defaults.empty());
}

TEST_CASE("placement layers resolve through one rule for every placement kind",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    REQUIRE(apply_sound_layer_defaults(
        history,
        {.chart = chart_id_t{2}, .resources = {resource_id_t{1}}, .layers = {layer_id_t{7}}}));
    const auto& chart = history.document.charts.front();
    const std::optional<std::vector<layer_id_t>> none_armed;
    const std::optional<std::vector<layer_id_t>> no_layer{std::vector<layer_id_t>{}};
    const std::optional<std::vector<layer_id_t>> drums{std::vector{layer_id_t{8}}};

    CHECK(resolve_placement_layers(chart, none_armed, resource_id_t{1}) ==
          std::vector<layer_id_t>{layer_id_t{7}});
    CHECK(resolve_placement_layers(chart, none_armed, std::nullopt).empty());
    CHECK(resolve_placement_layers(chart, no_layer, resource_id_t{1}).empty());
    CHECK(resolve_placement_layers(chart, drums, resource_id_t{1}) ==
          std::vector<layer_id_t>{layer_id_t{8}});

    // A sample note and a BGM event take their layers from the same rule.
    for (const auto kind : {resource_placement_kind_t::sample_note,
                            resource_placement_kind_t::bgm_event}) {
        auto fresh = document_history_t::from(layered_sound_song());
        REQUIRE(apply_sound_layer_defaults(
            fresh,
            {.chart = chart_id_t{2}, .resources = {resource_id_t{1}}, .layers = {layer_id_t{7}}}));
        const auto armed = apply_resource_placement(
            fresh,
            {.chart = chart_id_t{2}, .resource = resource_id_t{1},
             .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1}, .kind = kind,
             .semantic_layers = std::vector{layer_id_t{8}}});
        REQUIRE(armed);
        const auto& measure = fresh.document.charts.front().measures.back();
        const auto layers = kind == resource_placement_kind_t::sample_note
            ? measure.notes.back().semantic_layers : measure.bgm_events.back().semantic_layers;
        CHECK(layers == std::vector<layer_id_t>{layer_id_t{8}});
    }
}

TEST_CASE("a sound without layer defaults paints an untagged note",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    REQUIRE(place_at(history, 480, resource_id_t{2}));
    CHECK(history.document.charts.front().measures.front().notes.back().semantic_layers.empty());
}

TEST_CASE("sound layer defaults can be cleared and skip layers locked after assignment",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    REQUIRE(apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                                 .resources = {resource_id_t{1}},
                                                 .layers = {layer_id_t{7}, layer_id_t{8}}}));
    REQUIRE(apply_semantic_layer_edit(history,
                                      update_semantic_layer_action_t{.chart = chart_id_t{2},
                                                                     .id = layer_id_t{7},
                                                                     .name = "Vocal",
                                                                     .color = {1.0, 0.5, 0.0, 1.0},
                                                                     .visible = true,
                                                                     .locked = true}));
    CHECK(sound_default_layers_for_placement(history.document.charts.front(), resource_id_t{1}) ==
          std::vector<layer_id_t>{layer_id_t{8}});

    REQUIRE(apply_sound_layer_defaults(
        history, {.chart = chart_id_t{2}, .resources = {resource_id_t{1}}, .layers = {}}));
    CHECK(history.document.charts.front().sound_layer_defaults.empty());
}

TEST_CASE("sound layer defaults reject bad batches atomically",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    const auto good_resources = std::vector<resource_id_t>{resource_id_t{1}, resource_id_t{2}};
    CHECK(has_code(apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                                        .resources = {resource_id_t{1}},
                                                        .layers = {layer_id_t{99}}}),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                                        .resources = {resource_id_t{1}},
                                                        .layers = {layer_id_t{7}, layer_id_t{7}}}),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                                        .resources = {resource_id_t{1}},
                                                        .layers = {layer_id_t{9}}}),
                   diagnostic_code_t::unsupported_feature));
    CHECK(has_code(
        apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                             .resources = {resource_id_t{1}, resource_id_t{404}},
                                             .layers = {layer_id_t{7}}}),
        diagnostic_code_t::invalid_id));
    CHECK(
        has_code(apply_sound_layer_defaults(
                     history, {.chart = chart_id_t{2}, .resources = {}, .layers = {layer_id_t{7}}}),
                 diagnostic_code_t::invalid_resolution));
    CHECK(has_code(apply_sound_layer_defaults(history, {.chart = chart_id_t{77},
                                                        .resources = good_resources,
                                                        .layers = {layer_id_t{7}}}),
                   diagnostic_code_t::invalid_id));
    CHECK(history.document.charts.front().sound_layer_defaults.empty());
    CHECK(history.undo.empty());
    CHECK(history.document.revision == 4);
}

TEST_CASE("deleting a layer or a sound removes its sound layer defaults",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    REQUIRE(apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                                 .resources = {resource_id_t{2}, resource_id_t{5}},
                                                 .layers = {layer_id_t{7}, layer_id_t{8}}}));
    REQUIRE(apply_semantic_layer_edit(
        history, delete_semantic_layer_action_t{.chart = chart_id_t{2}, .id = layer_id_t{7}}));
    for (const auto& entry : history.document.charts.front().sound_layer_defaults)
        CHECK(entry.layers == std::vector<layer_id_t>{layer_id_t{8}});
    REQUIRE(validate_document(history.document).empty());

    REQUIRE(apply_resource_deletion(history, resource_id_t{5}));
    REQUIRE(history.document.charts.front().sound_layer_defaults.size() == 1);
    CHECK(history.document.charts.front().sound_layer_defaults.front().resource ==
          resource_id_t{2});
    REQUIRE(validate_document(history.document).empty());

    REQUIRE(apply_semantic_layer_edit(
        history, delete_semantic_layer_action_t{.chart = chart_id_t{2}, .id = layer_id_t{8}}));
    CHECK(history.document.charts.front().sound_layer_defaults.empty());
}

TEST_CASE("validation reports dangling sound layer defaults",
          "[unit][core][resources][semantic-layer-editor]") {
    auto song = layered_sound_song();
    song.charts.front().sound_layer_defaults = {
        {.resource = resource_id_t{404}, .layers = {layer_id_t{7}}},
        {.resource = resource_id_t{1}, .layers = {layer_id_t{99}}},
        {.resource = resource_id_t{1}, .layers = {}}};
    const auto diagnostics = validate_document(song);
    CHECK(std::ranges::any_of(diagnostics, [](const auto& d) {
        return d.code == diagnostic_code_t::dangling_resource;
    }));
    CHECK(std::ranges::any_of(diagnostics, [](const auto& d) {
        return d.code == diagnostic_code_t::duplicate_id;
    }));
    CHECK(diagnostics.size() >= 4);
}

TEST_CASE("batch resource deletion removes unused sounds in one undo step",
          "[unit][core][resources][delete]") {
    auto history = document_history_t::from(layered_sound_song());
    REQUIRE(apply_sound_layer_defaults(history, {.chart = chart_id_t{2},
                                                 .resources = {resource_id_t{2}, resource_id_t{5}},
                                                 .layers = {layer_id_t{7}}}));
    const auto doomed = std::vector<resource_id_t>{resource_id_t{2}, resource_id_t{5}};
    const auto deleted = apply_resource_deletions(history, doomed);
    REQUIRE(deleted);
    CHECK(history.document.resources.size() == 3);
    CHECK(history.document.charts.front().sound_layer_defaults.empty());
    CHECK(history.undo.size() == 2);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == 5);
    CHECK(history.document.charts.front().sound_layer_defaults.size() == 2);
}

TEST_CASE("batch resource deletion is all-or-nothing and names the referenced sound",
          "[unit][core][resources][delete]") {
    auto song = assignment_song();
    song.resources[0].name = "kick";
    auto history = document_history_t::from(std::move(song));
    // Resource 1 is used by a note and a BGM event; resource 2 is free.
    const auto mixed = std::vector<resource_id_t>{resource_id_t{2}, resource_id_t{1}};
    const auto rejected = apply_resource_deletions(history, mixed);
    CHECK_FALSE(rejected);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.front().message.find("kick") != std::string::npos);
    CHECK(history.document.resources.size() == 5);
    CHECK(history.undo.empty());

    const auto repeated = std::vector<resource_id_t>{resource_id_t{2}, resource_id_t{2}};
    CHECK(has_code(apply_resource_deletions(history, repeated), diagnostic_code_t::invalid_id));
    CHECK(has_code(apply_resource_deletions(history, {}), diagnostic_code_t::invalid_resolution));
    CHECK(history.document.resources.size() == 5);
}

TEST_CASE("sound layer membership adds and removes one layer across a group",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    REQUIRE(apply_sound_layer_defaults(
        history,
        {.chart = chart_id_t{2}, .resources = {resource_id_t{1}}, .layers = {layer_id_t{7}}}));
    const auto group = std::vector<resource_id_t>{resource_id_t{1}, resource_id_t{2}};
    REQUIRE(apply_sound_layer_membership(
        history,
        {.chart = chart_id_t{2}, .resources = group, .layer = layer_id_t{8}, .member = true}));
    const auto& defaults = history.document.charts.front().sound_layer_defaults;
    REQUIRE(defaults.size() == 2);
    CHECK(defaults[0].layers == std::vector<layer_id_t>{layer_id_t{7}, layer_id_t{8}});
    CHECK(defaults[1].layers == std::vector<layer_id_t>{layer_id_t{8}});

    const auto same = apply_sound_layer_membership(
        history,
        {.chart = chart_id_t{2}, .resources = group, .layer = layer_id_t{8}, .member = true});
    CHECK_FALSE(same);
    CHECK(same.diagnostics.empty());

    REQUIRE(apply_sound_layer_membership(
        history,
        {.chart = chart_id_t{2}, .resources = group, .layer = layer_id_t{8}, .member = false}));
    REQUIRE(history.document.charts.front().sound_layer_defaults.size() == 1);
    CHECK(history.document.charts.front().sound_layer_defaults.front().layers ==
          std::vector<layer_id_t>{layer_id_t{7}});
    CHECK(validate_document(history.document).empty());
}

TEST_CASE("sound layer membership rejects locked additions and unknown targets",
          "[unit][core][resources][semantic-layer-editor]") {
    auto history = document_history_t::from(layered_sound_song());
    const auto group = std::vector<resource_id_t>{resource_id_t{1}};
    CHECK(has_code(
        apply_sound_layer_membership(
            history,
            {.chart = chart_id_t{2}, .resources = group, .layer = layer_id_t{9}, .member = true}),
        diagnostic_code_t::unsupported_feature));
    CHECK(has_code(
        apply_sound_layer_membership(
            history,
            {.chart = chart_id_t{2}, .resources = group, .layer = layer_id_t{99}, .member = true}),
        diagnostic_code_t::invalid_id));
    CHECK(has_code(
        apply_sound_layer_membership(
            history,
            {.chart = chart_id_t{2}, .resources = group, .layer = layer_id_t{99}, .member = false}),
        diagnostic_code_t::invalid_id));
    CHECK(history.undo.empty());
    CHECK(history.document.charts.front().sound_layer_defaults.empty());
}

namespace {
auto sound_note_layers_history() -> document_history_t {
    auto song = layered_sound_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes.push_back({.id = object_id_t{30}, .position = {measure_id_t{3}, 480},
        .lane = lane_id_t{1}, .sound = resource_id_t{1}, .semantic_layers = {layer_id_t{8}}});
    notes.push_back({.id = object_id_t{31}, .position = {measure_id_t{3}, 720},
        .lane = lane_id_t{1}, .sound = resource_id_t{2}});
    return document_history_t::from(std::move(song));
}

void check_sound_note_assignment(bool overwrite) {
    auto history = sound_note_layers_history();
    REQUIRE(apply_sound_layer_membership(history, {.chart = chart_id_t{2},
                                                   .resources = {resource_id_t{1}},
                                                   .layer = layer_id_t{7},
                                                   .overwrite_note_layers = overwrite}));
    const auto& notes = history.document.charts.front().measures.front().notes;
    const auto expected = std::vector{layer_id_t{overwrite ? 7U : 8U}};
    CHECK(notes[0].semantic_layers == std::vector{layer_id_t{7}});
    CHECK(notes[1].semantic_layers == expected);
    CHECK(notes[2].semantic_layers.empty());
    CHECK(history.undo.size() == 1);
}

void check_sound_note_assignment_undo_and_paint() {
    auto history = sound_note_layers_history();
    const auto original = write_native_project(history.document).bytes;
    REQUIRE(apply_sound_layer_membership(
        history,
        {.chart = chart_id_t{2}, .resources = {resource_id_t{1}}, .layer = layer_id_t{7}}));
    REQUIRE(place_at(history, 240, resource_id_t{1}));
    CHECK(history.document.charts.front().measures.front().notes.back().semantic_layers ==
          std::vector{layer_id_t{7}});
    REQUIRE(undo_document_command(history));
    REQUIRE(undo_document_command(history));
    CHECK(write_native_project(history.document).bytes == original);
}
} // namespace

TEST_CASE("sound layers tag matching notes and optionally overwrite existing layers",
          "[unit][core][resources][sound-note-layers]") {
    check_sound_note_assignment(false);
    check_sound_note_assignment(true);
    check_sound_note_assignment_undo_and_paint();
}

TEST_CASE("sound layer overwrite rejects locked note memberships atomically",
          "[unit][core][resources][sound-note-layers]") {
    auto history = sound_note_layers_history();
    history.document.charts.front().measures.front().notes[1].semantic_layers = {layer_id_t{9}};
    const auto original = write_native_project(history.document).bytes;
    CHECK_FALSE(apply_sound_layer_membership(history, {.chart = chart_id_t{2},
                                                       .resources = {resource_id_t{1}},
                                                       .layer = layer_id_t{7},
                                                       .overwrite_note_layers = true}));
    CHECK(write_native_project(history.document).bytes == original);
    CHECK(history.undo.empty());
}

namespace {
void check_bga_stroke(bool condition) { CHECK(condition); }
void require_bga_stroke(bool condition) { REQUIRE(condition); }

auto same_bga_events(std::span<const bga_event_t> left,
                     std::span<const bga_event_t> right) -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (left[index].id != right[index].id || left[index].position != right[index].position ||
            left[index].resource != right[index].resource || left[index].kind != right[index].kind)
            return false;
    return true;
}
}

TEST_CASE("BGA draw stroke preserves occupied cells and commits deduplicated targets once",
          "[unit][core][resources][bga][stroke]") {
    auto history = document_history_t::from(assignment_song());
    const std::array targets{
        bga_stroke_target_t{{measure_id_t{3}, 360}, visual_event_kind_t::background},
        bga_stroke_target_t{{measure_id_t{3}, 240}, visual_event_kind_t::layer},
        bga_stroke_target_t{{measure_id_t{3}, 240}, visual_event_kind_t::layer},
        bga_stroke_target_t{{measure_id_t{3}, 480}, visual_event_kind_t::layer2},
    };
    const auto applied = apply_bga_stroke(history, chart_id_t{2},
                                          resource_id_t{3}, targets);
    require_bga_stroke(static_cast<bool>(applied));
    check_bga_stroke(history.document.charts.front().bga_events.size() == 3);
    const auto& events = history.document.charts.front().bga_events;
    check_bga_stroke(events.front().id == object_id_t{12} &&
                     events.front().resource == resource_id_t{3} &&
                     events.front().kind == visual_event_kind_t::background);
    check_bga_stroke(events[1].position.row == 240 &&
                     events[1].kind == visual_event_kind_t::layer);
    check_bga_stroke(events[2].position.row == 480 &&
                     events[2].kind == visual_event_kind_t::layer2);
    check_bga_stroke(history.undo.size() == 1);
    require_bga_stroke(undo_document_command(history));
    check_bga_stroke(history.document.charts.front().bga_events.size() == 1);
    check_bga_stroke(history.document.charts.front().bga_events.front().id == object_id_t{12});
}

TEST_CASE("BGA erase stroke removes video base lane without touching other layers",
          "[unit][core][resources][bga][stroke]") {
    auto song = assignment_song();
    song.visual_assets.front().kind = asset_kind_t::video;
    auto& events = song.charts.front().bga_events;
    events.front().kind = visual_event_kind_t::video;
    events.push_back({.id = object_id_t{13}, .position = {measure_id_t{3}, 360},
                      .resource = resource_id_t{4}, .kind = visual_event_kind_t::layer});
    events.push_back({.id = object_id_t{14}, .position = {measure_id_t{3}, 480},
                      .resource = resource_id_t{3}, .kind = visual_event_kind_t::video});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const std::array targets{
        bga_stroke_target_t{{measure_id_t{3}, 360}, visual_event_kind_t::background},
    };
    const auto erased = apply_bga_stroke(history, chart_id_t{2},
                                         std::nullopt, targets, true);
    require_bga_stroke(static_cast<bool>(erased));
    const auto& after = history.document.charts.front().bga_events;
    check_bga_stroke(after.size() == 2);
    check_bga_stroke(std::ranges::any_of(after, [](const auto& event) {
        return event.id == object_id_t{13} && event.kind == visual_event_kind_t::layer;
    }));
    check_bga_stroke(std::ranges::any_of(after, [](const auto& event) {
        return event.id == object_id_t{14} && event.position.row == 480;
    }));
    check_bga_stroke(history.undo.size() == 1);
    require_bga_stroke(undo_document_command(history));
    check_bga_stroke(history.document.charts.front().bga_events.size() == 3);
}

TEST_CASE("BGA draw on an occupied cell and erase on an empty cell are no-ops",
          "[unit][core][resources][bga][stroke]") {
    auto history = document_history_t::from(assignment_song());
    const auto revision = history.document.revision;
    const std::array occupied{
        bga_stroke_target_t{{measure_id_t{3}, 360}, visual_event_kind_t::background}};
    const auto draw = apply_bga_stroke(history, chart_id_t{2}, resource_id_t{3}, occupied);
    check_bga_stroke(!draw.applied && draw.diagnostics.empty());
    const std::array empty{
        bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::layer2}};
    const auto erase = apply_bga_stroke(history, chart_id_t{2}, std::nullopt, empty, true);
    check_bga_stroke(!erase.applied && erase.diagnostics.empty());
    check_bga_stroke(history.document.revision == revision && history.undo.empty());
    check_bga_stroke(history.document.charts.front().bga_events.front().id == object_id_t{12});
}

TEST_CASE("BGA strokes reject invalid video and over-cap batches atomically",
          "[unit][core][resources][bga][stroke]") {
    auto history = document_history_t::from(assignment_song());
    const auto original = history.document.charts.front().bga_events;
    const auto revision = history.document.revision;
    const std::array valid_target{
        bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::layer},
    };
    check_bga_stroke(has_code(apply_bga_stroke(history, chart_id_t{2},
        resource_id_t{99}, valid_target), diagnostic_code_t::invalid_id));
    check_bga_stroke(apply_bga_stroke(history, chart_id_t{2}, resource_id_t{3},
        std::span<const bga_stroke_target_t>{}).diagnostics.empty());
    auto video_song = assignment_song();
    video_song.visual_assets.front().kind = asset_kind_t::video;
    auto video_history = document_history_t::from(std::move(video_song));
    const auto video_layer = std::array{
        bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::layer}};
    check_bga_stroke(has_code(apply_bga_stroke(video_history,
        chart_id_t{2}, resource_id_t{3}, video_layer),
        diagnostic_code_t::unsupported_feature));
    const auto storyboard = std::array{
        bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::storyboard}};
    check_bga_stroke(has_code(apply_bga_stroke(history, chart_id_t{2},
        resource_id_t{3}, storyboard), diagnostic_code_t::unsupported_feature));
    const std::array invalid_row{
        bga_stroke_target_t{{measure_id_t{999}, 0}, visual_event_kind_t::background}};
    check_bga_stroke(has_code(apply_bga_stroke(history, chart_id_t{2},
        resource_id_t{3}, invalid_row), diagnostic_code_t::row_out_of_range));
    std::vector<bga_stroke_target_t> capped(50'001,
        bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::background});
    check_bga_stroke(has_code(apply_bga_stroke(history, chart_id_t{2},
        std::nullopt, capped, true), diagnostic_code_t::invalid_resolution));
    check_bga_stroke(history.document.revision == revision && history.undo.empty());
    check_bga_stroke(same_bga_events(history.document.charts.front().bga_events, original));
    check_bga_stroke(video_history.document.revision == revision && video_history.undo.empty());
}

namespace {
auto bgm_stroke_events(const document_history_t& history) -> const std::vector<bgm_event_t>& {
    return history.document.charts.front().measures.front().bgm_events;
}
}

TEST_CASE("BGM draw stroke places deduplicated cells on one lane as one undo step",
          "[unit][core][resources][bgm][stroke]") {
    auto history = document_history_t::from(assignment_song());
    const std::array targets{local_position_t{measure_id_t{3}, 240}, local_position_t{measure_id_t{3}, 360},
                             local_position_t{measure_id_t{3}, 360}, local_position_t{measure_id_t{3}, 480}};
    const auto applied = apply_bgm_stroke(history, chart_id_t{2},
                                          object_id_t{20}, resource_id_t{1}, targets);
    require_bga_stroke(static_cast<bool>(applied));
    const auto& events = bgm_stroke_events(history);
    check_bga_stroke(events.size() == 3);
    check_bga_stroke(events[1].position.row == 360 && events[1].source_lane == object_id_t{20} &&
                     events[1].sound == resource_id_t{1});
    check_bga_stroke(events[2].position.row == 480 && events[2].id.value > events[1].id.value);
    check_bga_stroke(history.undo.size() == 1);
    require_bga_stroke(undo_document_command(history));
    check_bga_stroke(bgm_stroke_events(history).size() == 1);
}

TEST_CASE("BGM draw stroke over only same-sound cells is a no-op and a different sound rejects",
          "[unit][core][resources][bgm][stroke]") {
    auto history = document_history_t::from(assignment_song());
    const auto revision = history.document.revision;
    const std::array same{local_position_t{measure_id_t{3}, 240}};
    const auto draw = apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
                                       resource_id_t{1}, same);
    check_bga_stroke(!draw.applied && draw.diagnostics.empty());
    const std::array mixed{local_position_t{measure_id_t{3}, 360},
                           local_position_t{measure_id_t{3}, 240}};
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
        resource_id_t{2}, mixed), diagnostic_code_t::collision));
    check_bga_stroke(history.document.revision == revision && history.undo.empty() &&
                     bgm_stroke_events(history).size() == 1);
}

TEST_CASE("BGM erase stroke removes only the pressed lane's events in one step",
          "[unit][core][resources][bgm][stroke]") {
    auto song = assignment_song();
    song.charts.front().bgm_source_lanes.push_back({.id = object_id_t{23}, .name = "BGM 2",
                                                    .source_order = 1});
    song.charts.front().measures.front().bgm_events.push_back(
        {.id = object_id_t{13}, .position = {measure_id_t{3}, 240},
         .source_lane = object_id_t{23}, .sound = resource_id_t{1}});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const std::array targets{local_position_t{measure_id_t{3}, 240},
                             local_position_t{measure_id_t{3}, 600}};
    const auto erased = apply_bgm_stroke(history, chart_id_t{2},
                                         object_id_t{20}, std::nullopt, targets, true);
    require_bga_stroke(static_cast<bool>(erased));
    check_bga_stroke(bgm_stroke_events(history).size() == 1 &&
                     bgm_stroke_events(history).front().id == object_id_t{13});
    check_bga_stroke(history.undo.size() == 1);
    const auto empty = apply_bgm_stroke(history, chart_id_t{2},
                                        object_id_t{20}, std::nullopt, targets, true);
    check_bga_stroke(!empty.applied && empty.diagnostics.empty());
}

TEST_CASE("BGM strokes reject invalid input atomically",
          "[unit][core][resources][bgm][stroke]") {
    auto history = document_history_t::from(assignment_song());
    const auto revision = history.document.revision;
    const std::array target{local_position_t{measure_id_t{3}, 600}};
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{99},
        resource_id_t{1}, target), diagnostic_code_t::invalid_id));
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
        std::nullopt, target), diagnostic_code_t::invalid_id));
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
        resource_id_t{3}, target), diagnostic_code_t::unsupported_feature));
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
        resource_id_t{5}, target), diagnostic_code_t::dangling_resource));
    const std::array bad_row{local_position_t{measure_id_t{999}, 0}};
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
        resource_id_t{1}, bad_row), diagnostic_code_t::row_out_of_range));
    const std::vector<local_position_t> capped(50'001, target.front());
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2}, object_id_t{20},
        std::nullopt, capped, true), diagnostic_code_t::invalid_resolution));
    check_bga_stroke(history.document.revision == revision && history.undo.empty() &&
                     bgm_stroke_events(history).size() == 1);
}

TEST_CASE("BGM draw stroke rejects a sound that cannot get a BMS ID when IDs are exhausted",
          "[unit][core][resources][bgm][stroke]") {
    auto song = assignment_song();
    song.charts.front().source_family = format_family_t::bms;
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (std::size_t value = 1; value < 36 * 36; ++value)
        song.resources.push_back({.id = resource_id_t{100 + value}, .asset = asset_id_t{1},
            .roles = {resource_role_t::sample},
            .target_identities = {{format_family_t::bms,
                std::string{digits[value / 36], digits[value % 36]}, "WAV"}}});
    auto history = document_history_t::from(std::move(song));
    const std::array target{local_position_t{measure_id_t{3}, 600}};
    check_bga_stroke(has_code(apply_bgm_stroke(history, chart_id_t{2},
        object_id_t{20}, resource_id_t{2}, target), diagnostic_code_t::invalid_id));
    check_bga_stroke(history.undo.empty() && bgm_stroke_events(history).size() == 1);
}

namespace {
void check_sound_workflow(bool value) { CHECK(value); }
void require_sound_workflow(bool value) { REQUIRE(value); }
}

TEST_CASE("Shared resource usage matches protected deletion across charts and retained aliases",
          "[core][resources][sound-workflow]") {
    auto song = deletion_reference_song();
    song.resources.push_back({.id = resource_id_t{6}, .name = "Alias",
                              .asset = asset_id_t{1}, .roles = {resource_role_t::sample}});
    const auto counts = count_resource_references(song);
    for (const auto [id, expected] : std::array<std::pair<std::uint64_t, std::uint64_t>, 3>{{{1, 5}, {3, 3}, {6, 1}}}) {
        const auto found = counts.find(id);
        require_sound_workflow(found != counts.end());
        check_sound_workflow(found->second == expected);
        auto history = document_history_t::from(song);
        const auto result = apply_resource_deletion(history, resource_id_t{id});
        require_sound_workflow(!result.diagnostics.empty());
        check_sound_workflow(result.diagnostics.front().message ==
            "resource is still referenced by " + std::to_string(expected) + " stored event(s)");
        check_sound_workflow(history.undo.empty());
    }
    check_sound_workflow(!counts.contains(2));
}

TEST_CASE("Sound name and kind edit atomically preserve supplied tags and undo once",
          "[core][resources][sound-workflow]") {
    auto song = assignment_song();
    song.resources[0].name = "Old";
    song.resources[0].tags = {"Keep"};
    auto history = document_history_t::from(song);
    const set_resource_sound_metadata_action_t action{.resource = resource_id_t{1},
        .kind = sound_kind_t::sample, .tags = {"Keep"}, .name = "  新 kick  "};
    require_sound_workflow(static_cast<bool>(apply_resource_sound_metadata(history, action)));
    check_sound_workflow(history.document.resources[0].name == "新 kick");
    check_sound_workflow(history.document.resources[0].sound_kind == sound_kind_t::sample);
    check_sound_workflow(history.document.resources[0].tags == std::vector<std::string>{"Keep"});
    check_sound_workflow(history.undo.size() == 1);
    const auto no_op = apply_resource_sound_metadata(history, action);
    check_sound_workflow(!no_op.applied && no_op.diagnostics.empty());
    check_sound_workflow(history.undo.size() == 1);
    require_sound_workflow(static_cast<bool>(undo_document_command(history)));
    check_sound_workflow(history.document.resources[0].name == "Old");
    check_sound_workflow(history.document.resources[0].sound_kind == sound_kind_t::unspecified);
    check_sound_workflow(history.document.resources[0].tags == std::vector<std::string>{"Keep"});
}

TEST_CASE("Invalid sound names reject every field without history",
          "[core][resources][sound-workflow]") {
    for (const auto& name : {std::string{}, std::string{" \t "}, std::string{"bad\nname"}, std::string(257, 'x')}) {
        auto history = document_history_t::from(assignment_song());
        const auto result = apply_resource_sound_metadata(history,
            {.resource = resource_id_t{1}, .kind = sound_kind_t::stem, .tags = {"New"}, .name = name});
        check_sound_workflow(!result.applied && !result.diagnostics.empty());
        check_sound_workflow(history.document.resources[0].name.empty());
        check_sound_workflow(history.document.resources[0].sound_kind == sound_kind_t::unspecified);
        check_sound_workflow(history.document.resources[0].tags.empty());
        check_sound_workflow(history.document.revision == 4 && history.undo.empty());
    }
}

namespace {
void require_resource_role(bool value) { REQUIRE(value); }
}

TEST_CASE("Resource role validation agrees for placement and assignment",
          "[unit][core][resources][resource-role]") {
    auto song = assignment_song();
    song.resources[1].roles = {resource_role_t::sample};
    auto assigned = document_history_t::from(song);
    const auto bgm = apply_resource_assignment(assigned,
        {.chart = chart_id_t{2}, .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{11}}}});
    require_resource_role(bgm.applied);
    auto placed = document_history_t::from(song);
    require_resource_role(apply_resource_placement(placed,
        {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .position = {measure_id_t{3}, 480},
         .kind = resource_placement_kind_t::bgm_event}).applied);
    song.resources[1].roles = {resource_role_t::bgm};
    auto rejected = document_history_t::from(song);
    const auto revision = rejected.document.revision;
    require_resource_role(has_code(apply_resource_assignment(rejected,
        {.chart = chart_id_t{2}, .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{11}},
                     {editor_selection_object_kind_t::note, object_id_t{10}}}}),
        diagnostic_code_t::unsupported_feature));
    require_resource_role(has_code(apply_resource_placement(rejected,
        {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .position = {measure_id_t{3}, 480},
         .lane = lane_id_t{1}, .kind = resource_placement_kind_t::sample_note}),
        diagnostic_code_t::unsupported_feature));
    require_resource_role(rejected.document.revision == revision);
    require_resource_role(rejected.undo.empty());
    require_resource_role(rejected.document.charts.front().measures.front().bgm_events.front().sound == resource_id_t{1});
}

TEST_CASE("resource creation rejects malformed UTF-8 names atomically", "[unit][core][resources]") {
    auto history = document_history_t::from(assignment_song());
    const auto result = apply_resource_creation(history,
        {.asset = asset_id_t{1}, .role = resource_role_t::sample, .name = "\xc0\x80"});
    CHECK_FALSE(result);
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("BGA stroke reuses a captured transaction and publishes one undo entry",
          "[core][resources][bga][stroke][transaction]") {
    auto history = document_history_t::from(assignment_song());
    const auto stamp = chart_stamp_t::from(history.document, chart_id_t{2});
    auto transaction = document_command_transaction_t::from(history, stamp, "BGA Draw stroke");
    const std::array targets{bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::layer}};
    const auto result = apply_bga_stroke(transaction, chart_id_t{2}, resource_id_t{3}, targets);
    check_bga_stroke(result.applied);
    check_bga_stroke(history.undo.size() == 1);
    check_bga_stroke(!transaction.is_ready());
    check_bga_stroke(history.document.charts.front().bga_events.size() == 2);
    REQUIRE(undo_document_command(history));
    check_bga_stroke(history.document.charts.front().bga_events.size() == 1);
}

TEST_CASE("rejected BGA stroke closes its supplied transaction and discards a working copy",
          "[core][resources][bga][stroke][transaction]") {
    auto history = document_history_t::from(assignment_song());
    auto transaction = document_command_transaction_t::from(history);
    const auto original = history.document.metadata.title;
    transaction.copy_document().metadata.title = "Unpublished";
    const std::array targets{bga_stroke_target_t{{measure_id_t{3}, 600}, visual_event_kind_t::layer}};
    const auto rejected = apply_bga_stroke(transaction, chart_id_t{99}, resource_id_t{3}, targets);
    check_bga_stroke(!rejected.applied);
    check_bga_stroke(!rejected.diagnostics.empty());
    check_bga_stroke(!transaction.is_ready());
    check_bga_stroke(!transaction.has_copy());
    check_bga_stroke(!transaction.commit("Must stay closed").applied);
    check_bga_stroke(history.document.metadata.title == original);
    check_bga_stroke(history.undo.empty());
}
