#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

auto fixture() -> wormedit::song_document_t {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                            "published-vox-slicing.bmson", std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    const auto parsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    return *parsed.song;
}

auto first_measure_position(const wormedit::song_document_t& song, std::uint64_t row)
    -> wormedit::local_position_t {
    return {song.charts.front().measures.front().id, row};
}

void clear_channel_for_empty_stem_test(wormedit::chart_document_t& chart) {
    const auto& events = chart.sound_channels.front().events;
    const auto linked = [&](std::optional<wormedit::object_id_t> source) {
        return source && std::ranges::any_of(events, [&](const auto& event) {
            return event.id == *source;
        });
    };
    for (auto& measure : chart.measures) {
        std::erase_if(measure.bgm_events, [&](const auto& event) {
            return linked(event.sound_channel_event);
        });
        std::erase_if(measure.notes, [&](const auto& event) {
            return linked(event.sound_channel_event);
        });
    }
    chart.sound_channels.front().events.clear();
}

void require_t463_applied(const wormedit::document_command_result_t& result) {
    INFO((result.diagnostics.empty() ? "No diagnostic" : result.diagnostics.front().message));
    REQUIRE(result.applied);
}

auto stem_point_matches(wormedit::song_document_t song, std::uint64_t row, bool prefer_restart,
                        std::optional<wormedit::bmson_source_geometry_t> geometry,
                        bool expected_restart, bool empty_channel = false) -> bool {
    for (auto& resource : song.resources) resource.missing = false;
    auto& chart = song.charts.front();
    const auto channel_id = chart.sound_channels.front().id;
    if (empty_channel) clear_channel_for_empty_stem_test(chart);
    auto history = wormedit::make_document_history(std::move(song));
    const auto before_revision = history.document.revision;
    const auto before_undo = history.undo.size();
    const auto result = wormedit::insert_bmson_stem_point(
        history, before_revision, history.document.charts.front().id, channel_id,
        first_measure_position(history.document, row), prefer_restart, geometry);
    require_t463_applied(result);
    if (!result || history.undo.size() != before_undo + 1) return false;
    const auto& changed = history.document.charts.front();
    const auto& event = changed.sound_channels.front().events.back();
    if (event.continuation != !expected_restart || event.playable) return false;
    const auto linked = std::ranges::any_of(changed.measures, [&](const auto& measure) {
        return std::ranges::any_of(measure.bgm_events, [&](const auto& bgm) {
            return bgm.sound_channel_event == event.id && bgm.source_lane ==
                   changed.bgm_source_lanes.front().id;
        });
    });
    return linked && wormedit::validate_document(history.document).empty() &&
           wormedit::undo_document_command(history);
}

auto packed_song() -> wormedit::song_document_t {
    auto song = fixture();
    for (auto& resource : song.resources) resource.missing = false;
    auto& chart = song.charts.front();
    const auto lane_id = wormedit::object_id_t{900'001};
    chart.bgm_source_lanes.push_back({.id = lane_id, .name = "Packed lane"});
    auto asset = song.audio_assets.front();
    asset.id = wormedit::asset_id_t{900'002};
    asset.stream = {48'000, 2, 144'000, true};
    song.audio_assets.push_back(asset);
    auto resource = song.resources.front();
    resource.id = wormedit::resource_id_t{900'003};
    resource.name = "packed.wav";
    resource.asset = asset.id;
    resource.roles = {wormedit::resource_role_t::sample};
    resource.target_identities.clear();
    resource.sound_kind = wormedit::sound_kind_t::keysound;
    resource.missing = false;
    song.resources.push_back(std::move(resource));
    return song;
}

auto packed_insert_matches(wormedit::song_document_t song, std::uint64_t row,
                           std::size_t expected_channels) -> bool {
    auto history = wormedit::make_document_history(std::move(song));
    const auto chart_id = history.document.charts.front().id;
    const auto lane = history.document.charts.front().bgm_source_lanes.back().id;
    const auto resource = history.document.resources.back().id;
    const auto position = first_measure_position(history.document, row);
    const auto result = wormedit::insert_bmson_packed_one_shot(
        history, history.document.revision, chart_id, lane, resource, position);
    require_t463_applied(result);
    if (!result || history.document.charts.front().sound_channels.size() != expected_channels)
        return false;
    const auto& changed = history.document.charts.front();
    const auto& source = changed.sound_channels.back().events.back();
    const auto linked = std::ranges::any_of(changed.measures, [&](const auto& measure) {
        return std::ranges::any_of(measure.bgm_events, [&](const auto& bgm) {
            return bgm.sound_channel_event == source.id && bgm.source_lane == lane &&
                   bgm.sound == resource;
        });
    });
    return !source.playable && !source.continuation && linked &&
           wormedit::validate_document(history.document).empty();
}

auto packed_reuse_preserves_clip(wormedit::song_document_t song) -> bool {
    auto history = wormedit::make_document_history(std::move(song));
    const auto chart_id = history.document.charts.front().id;
    const auto lane = history.document.charts.front().bgm_source_lanes.back().id;
    const auto resource = history.document.resources.back().id;
    const auto count = history.document.charts.front().sound_channels.size();
    const auto first = wormedit::insert_bmson_packed_one_shot(
        history, history.document.revision, chart_id, lane, resource,
        first_measure_position(history.document, 90));
    require_t463_applied(first);
    if (!first) return false;
    const auto channel_id = history.document.charts.front().sound_channels.back().id;
    const auto clip = history.document.charts.front().sound_channels.back().source_clip;
    const auto second = wormedit::insert_bmson_packed_one_shot(
        history, history.document.revision, chart_id, lane, resource,
        first_measure_position(history.document, 120));
    if (!second) return false;
    const auto& channel = history.document.charts.front().sound_channels.back();
    if (history.document.charts.front().sound_channels.size() != count + 1 ||
        channel.id != channel_id || channel.source_clip.id != clip.id ||
        channel.source_clip.source != clip.source ||
        channel.source_clip.begin_frame != clip.begin_frame ||
        channel.source_clip.end_frame != clip.end_frame ||
        channel.events.size() != 2 || !wormedit::validate_document(history.document).empty())
        return false;
    return wormedit::undo_document_command(history) &&
           history.document.charts.front().sound_channels.back().events.size() == 1;
}

auto packed_rejections_are_atomic() -> bool {
    auto history = wormedit::make_document_history(packed_song());
    const auto chart = history.document.charts.front().id;
    const auto lane = history.document.charts.front().bgm_source_lanes.back().id;
    history.document.resources.front().sound_kind = wormedit::sound_kind_t::stem;
    const auto stem_resource = history.document.resources.front().id;
    const auto revision = history.document.revision;
    const auto stem_rejected = wormedit::insert_bmson_packed_one_shot(
        history, revision, chart, lane, stem_resource,
        first_measure_position(history.document, 100));
    if (stem_rejected || history.document.revision != revision ||
        stem_rejected.diagnostics.empty() ||
        stem_rejected.diagnostics.front().message.find("resource") == std::string::npos)
        return false;
    const auto resource = history.document.resources.back().id;
    const auto position = first_measure_position(history.document, 90);
    if (!wormedit::insert_bmson_packed_one_shot(history, revision, chart, lane, resource, position))
        return false;
    const auto changed_revision = history.document.revision;
    const auto collision = wormedit::insert_bmson_packed_one_shot(
        history, changed_revision, chart, lane, resource, position);
    return !collision && !collision.diagnostics.empty() &&
           collision.diagnostics.front().code == wormedit::diagnostic_code_t::collision &&
           history.document.revision == changed_revision && wormedit::undo_document_command(history);
}

auto duplicate_asset_binding_song() -> wormedit::song_document_t {
    auto song = fixture();
    auto& chart = song.charts.front();
    for (auto& measure : chart.measures) {
        measure.notes.clear();
        measure.bgm_events.clear();
    }
    chart.sound_channels.clear();
    chart.bgm_source_lanes.clear();
    song.resources.clear();
    auto& asset = song.audio_assets.front();
    asset.stream = {48'000, 2, 144'000, true};
    const std::array lane_ids{wormedit::object_id_t{930'001}, wormedit::object_id_t{930'002},
                              wormedit::object_id_t{930'003}};
    const std::array channel_ids{wormedit::object_id_t{950'001}, wormedit::object_id_t{950'002},
                                 wormedit::object_id_t{950'003}};
    const std::array resource_ids{wormedit::resource_id_t{940'001},
                                  wormedit::resource_id_t{940'002},
                                  wormedit::resource_id_t{940'003}};
    const std::array names{"Stem A", "Stem B", "Packed"};
    for (std::size_t index = 0; index < lane_ids.size(); ++index) {
        const bool stem = index < 2;
        chart.bgm_source_lanes.push_back({.id = lane_ids[index], .name = names[index],
            .stem_channel = stem ? std::optional{channel_ids[index]} : std::nullopt});
        chart.sound_channels.push_back({.id = channel_ids[index], .name = names[index],
            .source_clip = {.id = wormedit::clip_id_t{960'001 + index}, .source = asset.id,
                            .begin_frame = 0, .end_frame = asset.stream.frame_count},
            .events = {{.id = wormedit::object_id_t{910'001 + index},
                        .position = {chart.measures.front().id, 100}, .playable = false}}});
        song.resources.push_back({.id = resource_ids[index], .name = names[index],
            .asset = asset.id, .roles = {wormedit::resource_role_t::bgm},
            .missing = false, .sound_kind = stem ? wormedit::sound_kind_t::stem
                                                : wormedit::sound_kind_t::keysound});
        chart.measures.front().bgm_events.push_back({
            .id = wormedit::object_id_t{920'001 + index},
            .position = {chart.measures.front().id, 100}, .source_lane = lane_ids[index],
            .sound = resource_ids[index],
            .sound_channel_event = wormedit::object_id_t{910'001 + index}});
    }
    return song;
}

auto has_bgm_binding(const wormedit::chart_document_t& chart, wormedit::object_id_t source,
                     wormedit::resource_id_t resource, std::optional<std::uint64_t> row = {})
    -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& bgm : measure.bgm_events) {
            if (bgm.sound_channel_event == source && bgm.sound == resource &&
                (!row || bgm.position.row == *row)) return true;
        }
    }
    return false;
}

auto duplicate_asset_insertions_match_exact_resources() -> bool {
    auto history = wormedit::make_document_history(duplicate_asset_binding_song());
    const auto chart_id = history.document.charts.front().id;
    const std::array channel_ids{wormedit::object_id_t{950'001},
                                 wormedit::object_id_t{950'002},
                                 wormedit::object_id_t{950'003}};
    const std::array resource_ids{wormedit::resource_id_t{940'001},
                                  wormedit::resource_id_t{940'002},
                                  wormedit::resource_id_t{940'003}};
    const auto bindings = wormedit::bmson_channel_resource_bindings(history.document, chart_id);
    if (bindings.size() != 3) return false;
    for (std::size_t index = 0; index < 3; ++index) {
        if (bindings[index].ambiguous || bindings[index].channel != channel_ids[index])
            return false;
    }
    const auto geometry = wormedit::bmson_source_geometry_t{
        .asset = history.document.audio_assets.front().id,
        .sample_rate = 48'000, .frame_count = 144'000};
    for (std::size_t index = 0; index < 2; ++index) {
        const auto result = wormedit::insert_bmson_stem_point(history,
            history.document.revision, chart_id, channel_ids[index],
            first_measure_position(history.document, 500), true, geometry);
        if (!result) return false;
        const auto& events = history.document.charts.front().sound_channels[index].events;
        const auto source = events.back().id;
        const auto expected_resource = resource_ids[index];
        if (!has_bgm_binding(history.document.charts.front(), source, expected_resource))
            return false;
    }
    const auto packed = wormedit::insert_bmson_packed_one_shot(history,
        history.document.revision, chart_id, wormedit::object_id_t{930'003},
        resource_ids[2], first_measure_position(history.document, 600));
    if (!packed || history.document.charts.front().sound_channels.size() != 3) return false;
    const auto& packed_channel = history.document.charts.front().sound_channels[2];
    const auto packed_source = packed_channel.events.back().id;
    const auto linked = has_bgm_binding(history.document.charts.front(), packed_source,
                                        resource_ids[2], 600);
    return packed_channel.id == channel_ids[2] && packed_channel.events.size() == 2 && linked &&
           wormedit::validate_document(history.document).empty();
}

auto ambiguous_empty_stem_binding_rejects() -> bool {
    auto song = duplicate_asset_binding_song();
    auto& chart = song.charts.front();
    for (auto& channel : chart.sound_channels) channel.events.clear();
    for (auto& measure : chart.measures) measure.bgm_events.clear();
    chart.sound_channels.resize(2);
    chart.sound_channels[1].name = chart.sound_channels[0].name;
    chart.bgm_source_lanes.resize(2);
    chart.bgm_source_lanes[0].name = chart.sound_channels[0].name;
    chart.bgm_source_lanes[1].name = chart.sound_channels[0].name;
    chart.bgm_source_lanes[0].stem_channel = chart.sound_channels[0].id;
    chart.bgm_source_lanes[1].stem_channel = chart.sound_channels[1].id;
    song.resources.resize(1);
    song.resources.front().name = chart.sound_channels[0].name;
    const auto bindings = wormedit::bmson_channel_resource_bindings(song, chart.id);
    const auto chart_id = chart.id;
    const auto channel_id = chart.sound_channels[0].id;
    auto history = wormedit::make_document_history(std::move(song));
    const auto revision = history.document.revision;
    const auto result = wormedit::insert_bmson_stem_point(history, revision, chart_id,
        channel_id, first_measure_position(history.document, 500), true);
    return bindings.size() == 1 && bindings.front().ambiguous && !bindings.front().channel &&
           !result && history.document.revision == revision && history.undo.empty();
}

void expect_bmson_core(bool condition) {
    REQUIRE(condition);
}

} // namespace

TEST_CASE("BMSON linked sound can move between BGM and a playable lane",
          "[unit][core][bmson][assignment]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto chart_id = history.document.charts.front().id;
    const auto& chart = history.document.charts.front();
    const auto measure_it = std::ranges::find_if(
        chart.measures, [](const auto& candidate) { return !candidate.bgm_events.empty(); });
    REQUIRE(measure_it != chart.measures.end());
    const auto measure_index = static_cast<std::size_t>(measure_it - chart.measures.begin());
    const auto& measure = *measure_it;
    REQUIRE(measure.bgm_events.size() == 1);
    const auto bgm = measure.bgm_events.front();
    REQUIRE(bgm.sound_channel_event);
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    const auto revision = history.document.revision;
    REQUIRE(wormedit::assign_bmson_sound_event(history, revision, chart_id, bgm.id, lane));
    const auto& assigned = history.document.charts.front();
    const auto converted =
        std::ranges::find(assigned.measures[measure_index].notes, bgm.id, &wormedit::note_t::id);
    REQUIRE(converted != assigned.measures[measure_index].notes.end());
    CHECK(converted->lane == lane);
    CHECK(converted->sound_channel_event == bgm.sound_channel_event);
    CHECK(assigned.measures[measure_index].bgm_events.empty());
    const auto source =
        std::ranges::find(assigned.sound_channels.front().events, *bgm.sound_channel_event,
                          &wormedit::sound_channel_event_t::id);
    REQUIRE(source != assigned.sound_channels.front().events.end());
    CHECK(source->playable);
    CHECK(source->lane == lane);
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json);
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song);
    CHECK(reparsed.song->charts.front().measures[measure_index].bgm_events.empty());
    CHECK_FALSE(
        wormedit::assign_bmson_sound_event(history, revision, chart_id, bgm.id, std::nullopt));

    REQUIRE(wormedit::assign_bmson_sound_event(history, history.document.revision, chart_id, bgm.id,
                                               std::nullopt));
    const auto& restored = history.document.charts.front();
    REQUIRE(restored.measures[measure_index].bgm_events.size() == 1);
    CHECK(restored.measures[measure_index].bgm_events.front().id == bgm.id);
    CHECK(restored.measures[measure_index].bgm_events.front().sound_channel_event ==
          bgm.sound_channel_event);
    CHECK(std::ranges::find(restored.measures[measure_index].notes, bgm.id,
                            &wormedit::note_t::id) == restored.measures[measure_index].notes.end());
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(std::ranges::find(history.document.charts.front().measures[measure_index].notes, bgm.id,
                            &wormedit::note_t::id) !=
          history.document.charts.front().measures[measure_index].notes.end());
}

TEST_CASE("BMSON assignment rejects occupied lanes without changing the document",
          "[unit][core][bmson][assignment]") {
    auto song = fixture();
    auto& chart = song.charts.front();
    const auto chart_id = chart.id;
    const auto measure_it = std::ranges::find_if(
        chart.measures, [](const auto& candidate) { return !candidate.bgm_events.empty(); });
    REQUIRE(measure_it != chart.measures.end());
    const auto measure_index = static_cast<std::size_t>(measure_it - chart.measures.begin());
    const auto bgm = measure_it->bgm_events.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    chart.measures[measure_index].notes.push_back(
        {.id = wormedit::object_id_t{999'999}, .position = bgm.position, .lane = lane});
    auto history = wormedit::make_document_history(std::move(song));
    const auto revision = history.document.revision;
    const auto rejected =
        wormedit::assign_bmson_sound_event(history, revision, chart_id, bgm.id, lane);
    CHECK_FALSE(rejected);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.front().code == wormedit::diagnostic_code_t::collision);
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().measures[measure_index].bgm_events.size() == 1);
}

TEST_CASE("BMSON tap assignment creates a source BGM lane when needed",
          "[unit][core][bmson][assignment]") {
    auto song = fixture();
    auto& chart = song.charts.front();
    const auto chart_id = chart.id;
    const auto bgm = std::ranges::find_if(
        chart.measures, [](const auto& measure) { return !measure.bgm_events.empty(); });
    REQUIRE(bgm != chart.measures.end());
    const auto removed_source = *bgm->bgm_events.front().sound_channel_event;
    bgm->bgm_events.clear();
    chart.bgm_source_lanes.clear();
    std::erase_if(chart.sound_channels.front().events,
                  [&](const auto& event) { return event.id == removed_source; });
    const auto note = chart.measures.front().notes.front();
    REQUIRE(note.sound_channel_event);
    auto history = wormedit::make_document_history(std::move(song));
    REQUIRE(wormedit::assign_bmson_sound_event(history, history.document.revision, chart_id,
                                               note.id, std::nullopt));
    const auto& changed = history.document.charts.front();
    REQUIRE(changed.bgm_source_lanes.size() == 1);
    REQUIRE(changed.measures.front().bgm_events.size() == 1);
    CHECK(changed.measures.front().bgm_events.front().id == note.id);
    CHECK(changed.measures.front().bgm_events.front().source_lane ==
          changed.bgm_source_lanes.front().id);
    CHECK(changed.measures.front().bgm_events.front().sound_channel_event ==
          note.sound_channel_event);
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json);
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song);
    CHECK(reparsed.song->charts.front().measures.front().bgm_events.size() == 1);
}

TEST_CASE("BMSON restart toggle updates one channel pulse and survives export and undo",
          "[unit][core][bmson][restart]") {
    auto history = wormedit::make_document_history(fixture());
    const auto chart_id = history.document.charts.front().id;
    const auto& initial_chart = history.document.charts.front();
    REQUIRE(initial_chart.sound_channels.size() == 1);
    const auto event_id = initial_chart.sound_channels.front().events.back().id;
    REQUIRE_FALSE(initial_chart.sound_channels.front().events.back().playable);
    REQUIRE(initial_chart.sound_channels.front().events.back().continuation);
    const auto revision = history.document.revision;
    const auto before_scene = wormedit::build_editor_field_scene_index(initial_chart, revision);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(before_scene));
    REQUIRE(std::get<wormedit::editor_field_scene_index_t>(before_scene).bgm_events.size() == 1);
    CHECK(std::get<wormedit::editor_field_scene_index_t>(before_scene)
              .bgm_events.front().bmson_restart == false);

    const auto applied = wormedit::toggle_bmson_sound_channel_restart(
        history, revision, chart_id, event_id);
    REQUIRE(applied);
    CHECK_FALSE(history.document.charts.front().sound_channels.front().events.back().continuation);
    const auto after_scene = wormedit::build_editor_field_scene_index(
        history.document.charts.front(), history.document.revision);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(after_scene));
    CHECK(std::get<wormedit::editor_field_scene_index_t>(after_scene)
              .bgm_events.front().bmson_restart == true);
    const auto slices = wormedit::derive_sound_channel_slices(
        history.document.charts.front(), history.document.charts.front().sound_channels.front(),
        48'000, 200'000);
    REQUIRE(slices);
    CHECK(slices.slices.back().restart);
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.song->charts.front().sound_channels.front().events.back().continuation);

    CHECK_FALSE(wormedit::toggle_bmson_sound_channel_restart(
        history, revision, chart_id, event_id));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.back().continuation);
}

TEST_CASE("BMSON BGM marker move preserves source link and rejects channel collisions",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto source_measure = std::ranges::find_if(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(source_measure != chart.measures.end());
    const auto bgm = source_measure->bgm_events.front();
    REQUIRE(bgm.sound_channel_event.has_value());
    REQUIRE(chart.measures.back().id != bgm.position.measure);
    const auto target = wormedit::local_position_t{chart.measures.back().id, 0};
    const auto revision = history.document.revision;
    const auto applied = wormedit::move_bmson_bgm_marker(
        history, revision, chart_id, bgm.id, target);
    REQUIRE(applied);
    const auto& moved_chart = history.document.charts.front();
    const auto moved_measure = std::ranges::find(moved_chart.measures, target.measure,
                                                 &wormedit::measure_chunk_t::id);
    REQUIRE(moved_measure != moved_chart.measures.end());
    const auto moved_item = std::ranges::find(moved_measure->bgm_events, bgm.id,
                                               &wormedit::bgm_event_t::id);
    REQUIRE(moved_item != moved_measure->bgm_events.end());
    const auto& moved = *moved_item;
    CHECK(moved.id == bgm.id);
    CHECK(moved.position == target);
    CHECK(moved.sound_channel_event == bgm.sound_channel_event);
    CHECK(moved_chart.sound_channels.front().events.back().position == target);
    CHECK(wormedit::validate_document(history.document).empty());
    const auto slices = wormedit::derive_sound_channel_slices(
        moved_chart, moved_chart.sound_channels.front(), 48'000, 200'000);
    REQUIRE(slices);
    CHECK(slices.slices.back().absolute_row > 0);

    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().sound_channels.front().events.back().position == target);

    CHECK_FALSE(wormedit::move_bmson_bgm_marker(
        history, history.document.revision, chart_id, bgm.id,
        moved_chart.sound_channels.front().events.front().position));
    CHECK_FALSE(wormedit::move_bmson_bgm_marker(
        history, history.document.revision, chart_id, bgm.id, target));
    REQUIRE(wormedit::undo_document_command(history));
    const auto& restored = history.document.charts.front();
    const auto restored_measure = std::ranges::find(restored.measures, bgm.position.measure,
                                                    &wormedit::measure_chunk_t::id);
    REQUIRE(restored_measure != restored.measures.end());
    CHECK(std::ranges::any_of(restored_measure->bgm_events, [&](const auto& event) {
        return event.id == bgm.id && event.position == bgm.position;
    }));
}

TEST_CASE("BMSON BGM marker deletion removes its source event and is undoable",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto initial_sound_event_count = chart.sound_channels.front().events.size();
    const auto measure = std::ranges::find_if(chart.measures, [](const auto& item) {
        return !item.bgm_events.empty();
    });
    REQUIRE(measure != chart.measures.end());
    const auto bgm = measure->bgm_events.front();
    REQUIRE(bgm.sound_channel_event.has_value());
    const auto revision = history.document.revision;
    REQUIRE(wormedit::delete_bmson_bgm_marker(history, revision, chart_id, bgm.id));
    const auto& changed = history.document.charts.front();
    CHECK(std::ranges::none_of(changed.measures, [&](const auto& item) {
        return std::ranges::any_of(item.bgm_events, [&](const auto& event) {
            return event.id == bgm.id;
        });
    }));
    CHECK(std::ranges::none_of(changed.sound_channels.front().events, [&](const auto& event) {
        return event.id == *bgm.sound_channel_event;
    }));
    CHECK(wormedit::validate_document(history.document).empty());
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().sound_channels.front().events.size() ==
          initial_sound_event_count - 1);
    CHECK_FALSE(wormedit::delete_bmson_bgm_marker(history, revision, chart_id, bgm.id));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(std::ranges::any_of(history.document.charts.front().sound_channels.front().events,
                              [&](const auto& event) {
                                  return event.id == *bgm.sound_channel_event;
                              }));
}

TEST_CASE("BMSON marker insertion links a new source pulse and BGM in one undoable edit",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& original = history.document.charts.front();
    const auto chart_id = original.id;
    const auto source_count = original.sound_channels.front().events.size();
    const auto source_measure = std::ranges::find_if(original.measures, [](const auto& item) {
        return !item.bgm_events.empty();
    });
    REQUIRE(source_measure != original.measures.end());
    const auto reference = source_measure->bgm_events.front();
    const wormedit::local_position_t target{reference.position.measure,
                                             reference.position.row - 120};
    const auto revision = history.document.revision;
    REQUIRE(wormedit::insert_bmson_bgm_marker(
        history, revision, chart_id, reference.id, target, true));
    const auto& changed = history.document.charts.front();
    const auto& source = changed.sound_channels.front().events.back();
    CHECK(source.position == target);
    CHECK_FALSE(source.playable);
    CHECK_FALSE(source.continuation);
    CHECK(source.id != *reference.sound_channel_event);
    CHECK(changed.sound_channels.front().events.size() == source_count + 1);
    const auto changed_measure = std::ranges::find(changed.measures, target.measure,
                                                   &wormedit::measure_chunk_t::id);
    REQUIRE(changed_measure != changed.measures.end());
    const auto semantic = std::ranges::find_if(changed_measure->bgm_events,
        [&](const auto& event) { return event.sound_channel_event == source.id; });
    REQUIRE(semantic != changed_measure->bgm_events.end());
    CHECK(semantic->id != reference.id);
    CHECK(semantic->position == target);
    CHECK(semantic->source_lane == reference.source_lane);
    CHECK(semantic->sound == reference.sound);
    CHECK(wormedit::validate_document(history.document).empty());
    const auto slices = wormedit::derive_sound_channel_slices(
        changed, changed.sound_channels.front(), 48'000, 200'000);
    REQUIRE(slices);
    CHECK(std::ranges::any_of(slices.slices, [&](const auto& slice) {
        return slice.restart && std::ranges::find(slice.events, source.id) != slice.events.end();
    }));
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(std::ranges::any_of(reparsed.song->charts.front().sound_channels.front().events,
                              [&](const auto& event) {
                                  return event.position == target && !event.playable &&
                                         !event.continuation;
                              }));

    CHECK_FALSE(wormedit::insert_bmson_bgm_marker(
        history, history.document.revision, chart_id, reference.id, target, false));
    CHECK_FALSE(wormedit::insert_bmson_bgm_marker(
        history, history.document.revision, chart_id, reference.id,
        changed.sound_channels.front().events.front().position, false));
    CHECK_FALSE(wormedit::insert_bmson_bgm_marker(
        history, revision, chart_id, reference.id,
        {target.measure, target.row + 1}, false));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.size() == source_count);
    REQUIRE(wormedit::insert_bmson_bgm_marker(
        history, history.document.revision, chart_id, reference.id, target, false));
    CHECK(history.document.charts.front().sound_channels.front().events.back().continuation);
}

TEST_CASE("BMSON marker batch validates every pulse and commits one history step",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto reference_measure = std::ranges::find_if(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(reference_measure != chart.measures.end());
    const auto reference = reference_measure->bgm_events.front().id;
    const auto reference_measure_id = reference_measure->id;
    const auto source_count = chart.sound_channels.front().events.size();
    const auto bgm_count = chart.measures.front().bgm_events.size() +
                           reference_measure->bgm_events.size();
    const std::vector<wormedit::bmson_sound_marker_insertion_t> markers{
        {{chart.measures.front().id, 480}, false},
        {{reference_measure_id, 600}, true},
        {{reference_measure_id, 840}, false},
    };
    const auto revision = history.document.revision;
    const std::vector<wormedit::bmson_sound_marker_insertion_t> too_many(257, markers.front());
    CHECK_FALSE(
        wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, too_many));
    auto duplicates = markers;
    duplicates.push_back(markers.front());
    CHECK_FALSE(
        wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, duplicates));
    CHECK(history.document.revision == revision);
    auto outside = markers;
    outside[2].position.row = 999'999;
    CHECK_FALSE(
        wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, outside));
    CHECK(history.document.revision == revision);
    auto occupied = markers;
    occupied[2].position = chart.sound_channels.front().events.front().position;
    CHECK_FALSE(
        wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, occupied));
    CHECK(history.document.revision == revision);
    REQUIRE(wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, markers));
    const auto& changed = history.document.charts.front();
    CHECK(changed.sound_channels.front().events.size() == source_count + markers.size());
    const auto changed_reference = std::ranges::find(changed.measures,
        reference_measure_id, &wormedit::measure_chunk_t::id);
    REQUIRE(changed_reference != changed.measures.end());
    CHECK(changed.measures.front().bgm_events.size() +
          changed_reference->bgm_events.size() == bgm_count + markers.size());
    for (std::size_t index = 0; index < markers.size(); ++index) {
        const auto& source = changed.sound_channels.front().events[source_count + index];
        CHECK(source.position == markers[index].position);
        CHECK(source.continuation == !markers[index].restart);
        CHECK(std::ranges::any_of(changed.measures, [&](const auto& measure) {
            return std::ranges::any_of(measure.bgm_events, [&](const auto& event) {
                return event.position == source.position &&
                       event.sound_channel_event == source.id;
            });
        }));
    }
    CHECK(wormedit::validate_document(history.document).empty());
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().sound_channels.front().events.size() ==
          source_count + markers.size());
    CHECK_FALSE(
        wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, markers));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.size() == source_count);
    CHECK(history.undo.empty());
}

TEST_CASE("BMSON marker batch assigns playable and BGM slices atomically",
          "[unit][core][bmson][marker][assignment]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    const auto& original = song.charts.front();
    const auto chart_id = original.id;
    const auto lane = original.lane_layouts.front().layout.lanes.front().id;
    const auto reference_measure = std::ranges::find_if(
        original.measures, [](const auto& measure) { return !measure.bgm_events.empty(); });
    REQUIRE(reference_measure != original.measures.end());
    const auto reference = reference_measure->bgm_events.front().id;
    const auto reference_sound = reference_measure->bgm_events.front().sound;
    const auto source_count = original.sound_channels.front().events.size();
    const std::array markers{
        wormedit::bmson_sound_marker_insertion_t{{original.measures.front().id, 480}, false, lane},
        wormedit::bmson_sound_marker_insertion_t{{reference_measure->id, 600}, true, std::nullopt}};
    auto blocked_song = song;
    blocked_song.charts.front().measures.front().notes.push_back(
        {.id = wormedit::object_id_t{999'999}, .position = markers.front().position, .lane = lane});
    auto blocked = wormedit::make_document_history(std::move(blocked_song));
    const auto blocked_revision = blocked.document.revision;
    CHECK_FALSE(wormedit::insert_bmson_sound_markers(blocked, blocked_revision, chart_id, reference,
                                                     markers));
    CHECK(blocked.document.revision == blocked_revision);
    CHECK(blocked.document.charts.front().sound_channels.front().events.size() == source_count);

    auto invalid_lane = markers;
    invalid_lane.front().playable_lane = wormedit::lane_id_t{999'999};
    auto invalid_history = wormedit::make_document_history(song);
    CHECK_FALSE(wormedit::insert_bmson_sound_markers(
        invalid_history, invalid_history.document.revision, chart_id, reference, invalid_lane));
    CHECK(invalid_history.document.charts.front().sound_channels.front().events.size() ==
          source_count);

    auto history = wormedit::make_document_history(std::move(song));
    const auto revision = history.document.revision;
    REQUIRE(wormedit::insert_bmson_sound_markers(history, revision, chart_id, reference, markers));
    const auto& changed = history.document.charts.front();
    REQUIRE(changed.sound_channels.front().events.size() == source_count + 2);
    const auto& playable_source = changed.sound_channels.front().events[source_count];
    const auto& bgm_source = changed.sound_channels.front().events[source_count + 1];
    CHECK(playable_source.playable);
    CHECK(playable_source.lane == lane);
    CHECK_FALSE(bgm_source.playable);
    const auto playable =
        std::ranges::find_if(changed.measures.front().notes, [&](const auto& note) {
            return note.sound_channel_event == playable_source.id;
        });
    REQUIRE(playable != changed.measures.front().notes.end());
    CHECK(playable->position == markers.front().position);
    CHECK(playable->sound == reference_sound);
    const auto bgm_measure = std::ranges::find(changed.measures, markers.back().position.measure,
                                               &wormedit::measure_chunk_t::id);
    REQUIRE(bgm_measure != changed.measures.end());
    CHECK(std::ranges::any_of(bgm_measure->bgm_events, [&](const auto& event) {
        return event.sound_channel_event == bgm_source.id;
    }));
    CHECK(wormedit::validate_document(history.document).empty());
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json);
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song);
    const auto& round_trip = reparsed.song->charts.front();
    CHECK(std::ranges::any_of(round_trip.measures.front().notes, [&](const auto& note) {
        return note.position == markers.front().position;
    }));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.size() == source_count);
}

TEST_CASE("BMSON razor path fills exact grid cells across measures in pointer order",
          "[unit][core][bmson][razor]") {
    const auto chart = fixture().charts.front();
    REQUIRE(chart.measures.size() >= 2);
    const wormedit::local_position_t begin{chart.measures[0].id, 720};
    const wormedit::local_position_t end{chart.measures[1].id, 240};
    const auto forward = wormedit::interpolate_bmson_razor_path(chart, begin, end, 8);
    REQUIRE(forward);
    const std::vector<wormedit::local_position_t> expected{
        {chart.measures[0].id, 840}, {chart.measures[1].id, 0},
        {chart.measures[1].id, 120}, {chart.measures[1].id, 240}};
    CHECK(forward.positions == expected);
    const auto backward = wormedit::interpolate_bmson_razor_path(chart, end, begin, 8);
    REQUIRE(backward);
    CHECK(backward.positions == (std::vector<wormedit::local_position_t>{
        {chart.measures[1].id, 120}, {chart.measures[1].id, 0},
        {chart.measures[0].id, 840}, {chart.measures[0].id, 720}}));
    const auto still = wormedit::interpolate_bmson_razor_path(chart, begin, begin, 8);
    REQUIRE(still);
    CHECK(still.positions.empty());
    CHECK_FALSE(wormedit::interpolate_bmson_razor_path(chart, begin, end, 8, 3));
    CHECK_FALSE(wormedit::interpolate_bmson_razor_path(
        chart, {chart.measures[0].id, 721}, end, 8));
}

TEST_CASE("BMSON stem points classify span, Shift, EOF, before-first and empty-channel rows",
          "[unit][core][bmson][stem]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    const auto geometry = wormedit::bmson_source_geometry_t{
        .asset = song.charts.front().sound_channels.front().source_clip.source,
        .sample_rate = 48'000, .frame_count = 200'000};
    expect_bmson_core(stem_point_matches(song, 300, false, geometry, false));
    expect_bmson_core(stem_point_matches(song, 300, true, geometry, true));
    auto short_geometry = geometry;
    short_geometry.frame_count = 15'000;
    expect_bmson_core(stem_point_matches(song, 400, false, short_geometry, true));
    expect_bmson_core(stem_point_matches(song, 100, false, geometry, true));
    expect_bmson_core(stem_point_matches(song, 300, false, geometry, true, true));
}

TEST_CASE("BMSON stem point uses supplied native geometry and rejects unknown ambiguous length",
          "[unit][core][bmson][stem]") {
    auto known = fixture();
    known.audio_assets.front().stream = {};
    const auto geometry = wormedit::bmson_source_geometry_t{
        .asset = known.charts.front().sound_channels.front().source_clip.source,
        .sample_rate = 48'000, .frame_count = 200'000};
    known.charts.front().sound_channels.front().source_clip.begin_frame = 10'000;
    known.charts.front().sound_channels.front().source_clip.end_frame = 50'000;
    expect_bmson_core(stem_point_matches(known, 300, false, geometry, false));

    auto unknown = fixture();
    unknown.audio_assets.front().stream = {48'000, 2, std::nullopt, true};
    unknown.charts.front().sound_channels.front().source_clip.end_frame.reset();
    auto history = wormedit::make_document_history(std::move(unknown));
    const auto revision = history.document.revision;
    const auto chart = history.document.charts.front().id;
    const auto channel = history.document.charts.front().sound_channels.front().id;
    const auto target = first_measure_position(history.document, 300);
    const auto result = wormedit::insert_bmson_stem_point(
        history, revision, chart, channel, target, false);
    expect_bmson_core(!result && history.document.revision == revision && history.undo.empty());
}

TEST_CASE("BMSON stem insertion rejects stale chart and occupied rows atomically",
          "[unit][core][bmson][stem]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto revision = history.document.revision;
    const auto chart = history.document.charts.front().id;
    const auto channel = history.document.charts.front().sound_channels.front().id;
    const auto occupied = history.document.charts.front().sound_channels.front().events.front().position;
    const auto stale = wormedit::insert_bmson_stem_point(
        history, revision + 1, chart, channel, first_measure_position(history.document, 300), false);
    const auto unknown_chart = wormedit::insert_bmson_stem_point(
        history, revision, wormedit::chart_id_t{999'999}, channel,
        first_measure_position(history.document, 300), false);
    const auto collision = wormedit::insert_bmson_stem_point(
        history, revision, chart, channel, occupied, false);
    expect_bmson_core(!stale && !unknown_chart && !collision &&
        collision.diagnostics.front().code == wormedit::diagnostic_code_t::collision &&
        history.document.revision == revision && history.undo.empty());
}

TEST_CASE("Packed BMSON one-shot creates, reuses, and atomically links an unstemmed channel",
          "[unit][core][bmson][packed]") {
    expect_bmson_core(packed_insert_matches(packed_song(), 90, 2));
    expect_bmson_core(packed_reuse_preserves_clip(packed_song()));
}

TEST_CASE("Packed BMSON insertion rejects stem resources and collisions without mutation",
          "[unit][core][bmson][packed]") {
    expect_bmson_core(packed_rejections_are_atomic());
}

TEST_CASE("BMSON semantic binding keeps duplicate asset stems and packed keysounds distinct",
          "[unit][core][bmson][stem][packed]") {
    expect_bmson_core(duplicate_asset_insertions_match_exact_resources());
}

TEST_CASE("BMSON empty stem binding rejects duplicate same-name candidates",
          "[unit][core][bmson][stem]") {
    expect_bmson_core(ambiguous_empty_stem_binding_rejects());
}

namespace {

auto total_channel_events(const wormedit::chart_document_t& chart) -> std::size_t {
    std::size_t total{};
    for (const auto& channel : chart.sound_channels) total += channel.events.size();
    return total;
}

auto packed_lane_bgm_count(const wormedit::chart_document_t& chart, wormedit::object_id_t lane)
    -> std::size_t {
    std::size_t total{};
    for (const auto& measure : chart.measures)
        total += static_cast<std::size_t>(std::ranges::count(measure.bgm_events, lane,
                                                             &wormedit::bgm_event_t::source_lane));
    return total;
}

struct packed_stroke_fixture_t {
    wormedit::document_history_t history = wormedit::make_document_history(packed_song());
    wormedit::chart_id_t chart = history.document.charts.front().id;
    wormedit::object_id_t lane = history.document.charts.front().bgm_source_lanes.back().id;
    wormedit::resource_id_t resource = history.document.resources.back().id;
    std::vector<wormedit::local_position_t> cells;
    explicit packed_stroke_fixture_t(std::initializer_list<std::uint64_t> rows) {
        for (const auto row : rows) cells.push_back(first_measure_position(history.document, row));
    }
    auto draw(bool erase = false) {
        return wormedit::apply_bmson_packed_stroke(history, history.document.revision, chart, lane,
                                                   erase ? std::nullopt : std::optional{resource},
                                                   cells, erase);
    }
};

} // namespace

TEST_CASE("Packed BMSON stroke places three cells in one undo step and exports",
          "[unit][core][bmson][packed-stroke]") {
    packed_stroke_fixture_t f{90, 120, 150, 120};
    const auto channels = f.history.document.charts.front().sound_channels.size();
    const auto events = total_channel_events(f.history.document.charts.front());
    const auto undo = f.history.undo.size();
    REQUIRE(f.draw());
    const auto& chart = f.history.document.charts.front();
    CHECK(f.history.undo.size() == undo + 1);
    CHECK(chart.sound_channels.size() == channels + 1);
    CHECK(total_channel_events(chart) == events + 3);
    CHECK(packed_lane_bgm_count(chart, f.lane) == 3);
    CHECK(wormedit::validate_document(f.history.document).empty());
    const auto written = wormedit::write_bmson(f.history.document);
    INFO((written.diagnostics.empty() ? "no diagnostic" : written.diagnostics.front().message));
    REQUIRE(written.json);
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song);
    CHECK(total_channel_events(reparsed.song->charts.front()) == total_channel_events(chart));
    REQUIRE(wormedit::undo_document_command(f.history));
    CHECK(f.history.document.charts.front().sound_channels.size() == channels);
    CHECK(total_channel_events(f.history.document.charts.front()) == events);
}

TEST_CASE("Packed BMSON stroke skips rows already showing the sound and reuses the channel",
          "[unit][core][bmson][packed-stroke]") {
    packed_stroke_fixture_t f{90, 120};
    REQUIRE(f.draw());
    const auto revision = f.history.document.revision;
    const auto again = f.draw();
    CHECK_FALSE(again.applied);
    CHECK(again.diagnostics.empty());
    CHECK(f.history.document.revision == revision);
    f.cells = {first_measure_position(f.history.document, 120),
               first_measure_position(f.history.document, 150)};
    const auto channels = f.history.document.charts.front().sound_channels.size();
    REQUIRE(f.draw());
    const auto& chart = f.history.document.charts.front();
    CHECK(chart.sound_channels.size() == channels);
    CHECK(chart.sound_channels.back().events.size() == 3);
    CHECK(packed_lane_bgm_count(chart, f.lane) == 3);
}

TEST_CASE("Packed BMSON right-drag erase removes the column's events in one step",
          "[unit][core][bmson][packed-stroke]") {
    packed_stroke_fixture_t f{90, 120, 150};
    REQUIRE(f.draw());
    const auto undo = f.history.undo.size();
    f.cells = {first_measure_position(f.history.document, 90),
               first_measure_position(f.history.document, 120),
               first_measure_position(f.history.document, 180)};
    REQUIRE(f.draw(true));
    const auto& chart = f.history.document.charts.front();
    CHECK(f.history.undo.size() == undo + 1);
    CHECK(packed_lane_bgm_count(chart, f.lane) == 1);
    CHECK(chart.sound_channels.back().events.size() == 1);
    CHECK(wormedit::validate_document(f.history.document).empty());
    const auto empty = f.draw(true);
    CHECK(empty.diagnostics.empty());
    REQUIRE(wormedit::undo_document_command(f.history));
    CHECK(packed_lane_bgm_count(f.history.document.charts.front(), f.lane) == 3);
}

TEST_CASE("Packed BMSON stroke rejects stem lanes and stale input atomically",
          "[unit][core][bmson][packed-stroke]") {
    packed_stroke_fixture_t f{90, 120};
    const auto revision = f.history.document.revision;
    const auto stem_lane = f.history.document.charts.front().bgm_source_lanes.front().id;
    REQUIRE(f.history.document.charts.front().bgm_source_lanes.front().stem_channel);
    const auto stem = wormedit::apply_bmson_packed_stroke(f.history, revision, f.chart, stem_lane,
                                                          f.resource, f.cells);
    REQUIRE_FALSE(stem.diagnostics.empty());
    CHECK(stem.diagnostics.front().message.find("stem lanes are not drawable") != std::string::npos);
    CHECK_FALSE(wormedit::apply_bmson_packed_stroke(f.history, revision + 1, f.chart, f.lane,
                                                    f.resource, f.cells).diagnostics.empty());
    CHECK_FALSE(wormedit::apply_bmson_packed_stroke(f.history, revision, f.chart, f.lane,
                                                    wormedit::resource_id_t{1}, f.cells)
                    .diagnostics.empty());
    CHECK_FALSE(wormedit::apply_bmson_packed_stroke(f.history, revision, f.chart, f.lane,
                                                    std::nullopt, f.cells).diagnostics.empty());
    f.history.document.resources.front().sound_kind = wormedit::sound_kind_t::stem;
    CHECK_FALSE(wormedit::apply_bmson_packed_stroke(f.history, revision, f.chart, f.lane,
                                                    f.history.document.resources.front().id,
                                                    f.cells).diagnostics.empty());
    CHECK(f.history.document.revision == revision);
    CHECK(f.history.undo.empty());
    const std::vector<wormedit::local_position_t> capped(50'001, f.cells.front());
    CHECK_FALSE(wormedit::apply_bmson_packed_stroke(f.history, revision, f.chart, f.lane,
                                                    f.resource, capped).diagnostics.empty());
}
