#include <wormedit/core/bms_program_edit.h>
#include <wormedit/formats/bms.h>
#include "../support/stale_action_assertions.h"
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace {

auto bmson_fixture(std::string_view name) -> wormedit::bmson_parse_outcome_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / name,
                         std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    return wormedit::parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
}

auto published_fixture() -> wormedit::bmson_parse_outcome_t {
    return bmson_fixture("published-vox-slicing.bmson");
}

auto cropped_continuation_is_normalized(const wormedit::song_document_t& song,
                                       wormedit::chart_id_t chart,
                                       wormedit::object_id_t note,
                                       std::span<const wormedit::audio::audition_source_binding_t> bindings)
    -> bool {
    const auto resolved = wormedit::audio::audition_resolution_t::from(song, chart, note, bindings);
    return resolved.diagnostics.empty() && resolved.value.clips.size() == 1 &&
           resolved.value.clips.front().source_start_frame == 55'125 &&
           resolved.value.clips.front().frame_count == 33'075;
}

auto runtime_binding(wormedit::asset_id_t asset, std::uint64_t frames = 200'000)
    -> wormedit::audio::audition_source_binding_t {
    return {.asset = asset,
            .source = {"fixture:vox", "decoded:v1"},
            .source_sample_rate = 48'000,
            .source_frame_count = std::nullopt,
            .output_sample_rate = wormedit::audio::output_sample_rate,
            .output_frame_count = frames};
}

auto is_silent_audition(const wormedit::song_document_t& song, wormedit::chart_id_t chart,
                       wormedit::object_id_t object,
                       std::span<const wormedit::audio::audition_source_binding_t> bindings,
                       const wormedit::audio::preview_bindings_resolve_outcome_t& preview) -> bool {
    auto outcome = wormedit::audio::audition_resolution_t::from(song, chart, object, bindings);
    const auto is_silent = outcome.ok() && outcome.value.clips.empty();
    const auto legacy = wormedit::audio::audition_resolve_result_t::from(std::move(outcome));
    return is_silent && !legacy && legacy.diagnostics.empty() && preview.diagnostics.empty();
}

} // namespace

TEST_CASE("audition resolver maps published BMSON slices into normalized transport frames",
          "[unit][audio][audition-resolver][bmson]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    REQUIRE(chart.sound_channels.size() == 1);
    const auto asset = chart.sound_channels.front().source_clip.source;
    const auto audio_asset =
        std::ranges::find(song.audio_assets, asset, &wormedit::audio_asset_t::id);
    REQUIRE(audio_asset != song.audio_assets.end());
    audio_asset->stream.sample_rate = 48'000;
    audio_asset->stream.frame_count.reset();
    const std::array bindings{runtime_binding(asset)};

    const auto event_at = [&](std::uint64_t local_row, bool playable) {
        return std::ranges::find_if(chart.sound_channels.front().events, [&](const auto& event) {
            return event.position.row == local_row && event.playable == playable;
        });
    };
    const auto retained_restart = event_at(720, true);
    REQUIRE(retained_restart != chart.sound_channels.front().events.end());
    const auto note =
        std::ranges::find_if(chart.measures.front().notes, [&](const auto& candidate) {
            return candidate.sound_channel_event == retained_restart->id;
        });
    REQUIRE(note != chart.measures.front().notes.end());

    const auto resolved =
        wormedit::audio::audition_resolution_t::from(song, chart.id, note->id, bindings);
    REQUIRE(resolved.diagnostics.empty());
    REQUIRE(resolved.value.clips.size() == 1);
    CHECK(resolved.value.document_revision == song.revision);
    CHECK(resolved.value.clips.front().source_start_frame == 0);
    CHECK(resolved.value.clips.front().frame_count == 11'025);

    const auto retained_shared = event_at(360, true);
    REQUIRE(retained_shared != chart.sound_channels.front().events.end());
    const auto shared_note =
        std::ranges::find_if(chart.measures.front().notes, [&](const auto& candidate) {
            return candidate.sound_channel_event == retained_shared->id;
        });
    REQUIRE(shared_note != chart.measures.front().notes.end());
    const auto shared =
        wormedit::audio::audition_resolution_t::from(song, chart.id, shared_note->id, bindings);
    REQUIRE(shared.diagnostics.empty());
    REQUIRE(shared.value.clips.size() == 1);
    CHECK(shared.value.clips.front().source_start_frame == 11'025);
    CHECK(shared.value.clips.front().frame_count == 33'075);

    chart.sound_channels.front().source_clip.begin_frame = 48'000;
    chart.sound_channels.front().source_clip.end_frame = 240'000;
    const auto cropped = wormedit::audio::audition_resolution_t::from(
        song, chart.id, chart.measures.front().notes.front().id, bindings);
    REQUIRE(cropped.diagnostics.empty());
    REQUIRE(cropped.value.clips.size() == 1);
    CHECK(cropped.value.clips.front().source_start_frame == 44'100);
    CHECK(cropped.value.clips.front().frame_count == 11'025);
    CHECK(cropped_continuation_is_normalized(song, chart.id, shared_note->id, bindings));
}

TEST_CASE("audition resolver isolates BGM slices and falls back to whole unlinked resources",
          "[unit][audio][audition-resolver][fallback]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    const auto asset = chart.sound_channels.front().source_clip.source;
    const auto audio_asset =
        std::ranges::find(song.audio_assets, asset, &wormedit::audio_asset_t::id);
    REQUIRE(audio_asset != song.audio_assets.end());
    audio_asset->stream.sample_rate = 48'000;
    audio_asset->stream.frame_count.reset();
    const std::array bindings{runtime_binding(asset)};

    const auto bgm_measure = std::ranges::find_if(
        chart.measures, [](const auto& measure) { return !measure.bgm_events.empty(); });
    REQUIRE(bgm_measure != chart.measures.end());
    const auto bgm = wormedit::audio::audition_resolution_t::from(
        song, chart.id, bgm_measure->bgm_events.front().id, bindings);
    REQUIRE(bgm.diagnostics.empty());
    REQUIRE(bgm.value.clips.size() == 1);
    // Pulse 1680 is two source seconds after the restart at pulse 720.
    CHECK(bgm.value.clips.front().source_start_frame == 88'200);
    CHECK(bgm.value.clips.front().frame_count == 111'800);

    auto& note = chart.measures.front().notes.front();
    note.sound_channel_event.reset();
    const auto whole = wormedit::audio::audition_resolution_t::from(song, chart.id, note.id, bindings);
    REQUIRE(whole.diagnostics.empty());
    REQUIRE(whole.value.clips.size() == 1);
    CHECK(whole.value.clips.front().source_start_frame == 0);
    CHECK(whole.value.clips.front().frame_count == 200'000);

    const std::array duplicate_bindings{bindings.front(), bindings.front()};
    const auto duplicate =
        wormedit::audio::audition_resolution_t::from(song, chart.id, note.id, duplicate_bindings);
    CHECK_FALSE(duplicate.diagnostics.empty());
    CHECK(duplicate.value.clips.empty());
    REQUIRE(duplicate.diagnostics.size() == 1);
    CHECK(duplicate.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::duplicate_source_binding);

    auto mismatched = bindings.front();
    mismatched.source_sample_rate = 44'100;
    const std::array mismatched_bindings{mismatched};
    const auto invalid_geometry = wormedit::audio::audition_resolution_t::from(
        song, chart.id, bgm_measure->bgm_events.front().id, mismatched_bindings);
    CHECK_FALSE(invalid_geometry.diagnostics.empty());
    REQUIRE(invalid_geometry.diagnostics.size() == 1);
    CHECK(invalid_geometry.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::invalid_source_geometry);

    auto wrong_output_rate = bindings.front();
    wrong_output_rate.output_sample_rate = 48'000;
    const std::array wrong_output_bindings{wrong_output_rate};
    const auto invalid_output = wormedit::audio::audition_resolution_t::from(
        song, chart.id, bgm_measure->bgm_events.front().id, wrong_output_bindings);
    CHECK_FALSE(invalid_output.diagnostics.empty());
    REQUIRE(invalid_output.diagnostics.size() == 1);
    CHECK(invalid_output.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::invalid_source_geometry);

    auto mismatched_length = bindings.front();
    mismatched_length.source_frame_count = 240'000;
    audio_asset->stream.frame_count = 240'001;
    const std::array mismatched_length_bindings{mismatched_length};
    const auto invalid_length = wormedit::audio::audition_resolution_t::from(
        song, chart.id, bgm_measure->bgm_events.front().id, mismatched_length_bindings);
    CHECK_FALSE(invalid_length.diagnostics.empty());
    REQUIRE(invalid_length.diagnostics.size() == 1);
    CHECK(invalid_length.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::invalid_source_geometry);
}

TEST_CASE("BMSON draft audition separates stale revision from unknown marker rejection",
          "[unit][audio][audition-resolver][draft-stamp]") {
    const auto missing_marker = GENERATE(false, true);
    const wormedit::song_document_t song{.revision = 10};
    const std::array markers{wormedit::bmson_sound_marker_insertion_t{}};
    const auto result = wormedit::audio::resolve_bmson_draft_audition(song,
        {.expected_revision = 9, .chart = wormedit::chart_id_t{2},
         .markers = markers, .marker_index = missing_marker ? 1u : 0u}, {});
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    if (missing_marker)
        CHECK(result.diagnostics.front().message == "BMSON draft audition has a stale revision or unknown marker");
    else
        wormedit::test::check_stale_action_message(result.diagnostics.front().message, "BMSON draft audition");
    CHECK(song.revision == 10);
}

TEST_CASE("BMSON draft audition resolves a staged region without editing the song",
          "[unit][audio][audition-resolver][bmson]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    const auto& chart = song.charts.front();
    const auto& channel = chart.sound_channels.front();
    const auto asset = channel.source_clip.source;
    const auto audio_asset = std::ranges::find(song.audio_assets, asset,
                                               &wormedit::audio_asset_t::id);
    REQUIRE(audio_asset != song.audio_assets.end());
    audio_asset->stream.sample_rate = 48'000;
    audio_asset->stream.frame_count.reset();
    const auto bgm_measure = std::ranges::find_if(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(bgm_measure != chart.measures.end());
    const auto reference = bgm_measure->bgm_events.front();
    const auto original_source_count = channel.events.size();
    const auto original_bgm_count = bgm_measure->bgm_events.size();
    const auto revision = song.revision;
    const std::array markers{
        wormedit::bmson_sound_marker_insertion_t{{reference.position.measure, 600}, false},
        wormedit::bmson_sound_marker_insertion_t{{reference.position.measure, 840}, true}};
    const std::array bindings{runtime_binding(asset)};

    const auto first = wormedit::audio::resolve_bmson_draft_audition(
        song,
        {.expected_revision = revision,
         .chart = chart.id,
         .reference_bgm_event = reference.id,
         .markers = markers,
         .marker_index = 0},
        bindings);
    for (const auto& diagnostic : first.diagnostics) {
        UNSCOPED_INFO(diagnostic.message);
    }
    REQUIRE(first);
    REQUIRE(first.clips.size() == 1);
    CHECK(first.clips.front().source_start_frame == 77'175);
    CHECK(first.clips.front().frame_count == 11'025);
    const auto second = wormedit::audio::resolve_bmson_draft_audition(
        song,
        {.expected_revision = revision,
         .chart = chart.id,
         .reference_bgm_event = reference.id,
         .markers = markers,
         .marker_index = 1},
        bindings);
    REQUIRE(second);
    REQUIRE(second.clips.size() == 1);
    // The second draft marker requests another restart, so it plays from zero.
    CHECK(second.clips.front().source_start_frame == 0);
    CHECK(second.clips.front().frame_count == 200'000);
    auto mixed_markers = markers;
    mixed_markers[0].playable_lane = chart.lane_layouts.front().layout.lanes.front().id;
    const auto playable = wormedit::audio::resolve_bmson_draft_audition(
        song,
        {.expected_revision = revision,
         .chart = chart.id,
         .reference_bgm_event = reference.id,
         .markers = mixed_markers,
         .marker_index = 0},
        bindings);
    REQUIRE(playable);
    REQUIRE(playable.clips.size() == 1);
    CHECK(playable.clips.front().source_start_frame == first.clips.front().source_start_frame);
    CHECK(playable.clips.front().frame_count == first.clips.front().frame_count);
    CHECK(song.revision == revision);
    CHECK(song.charts.front().sound_channels.front().events.size() == original_source_count);
    CHECK(bgm_measure->bgm_events.size() == original_bgm_count);
    CHECK_FALSE(wormedit::audio::resolve_bmson_draft_audition(
        song,
        {.expected_revision = revision + 1,
         .chart = chart.id,
         .reference_bgm_event = reference.id,
         .markers = markers,
         .marker_index = 0},
        bindings));
}

TEST_CASE("audition resolver layers fused BMSON channels and enforces its layer bound",
          "[unit][audio][audition-resolver][polyphony]") {
    auto parsed = bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    REQUIRE(chart.sound_channels.size() == 2);
    for (auto& asset : song.audio_assets) {
        asset.stream.sample_rate = 48'000;
        asset.stream.frame_count.reset();
    }
    std::vector<wormedit::audio::audition_source_binding_t> bindings;
    for (std::size_t index = 0; index < song.audio_assets.size(); ++index) {
        bindings.push_back({.asset = song.audio_assets[index].id,
                            .source = {"asset:" + std::to_string(index), "decoded:v1"},
                            .source_sample_rate = 48'000,
                            .source_frame_count = std::nullopt,
                            .output_sample_rate = wormedit::audio::output_sample_rate,
                            .output_frame_count = 200'000});
    }

    auto* first_note = &chart.measures.front().notes.front();
    auto second_note = std::ranges::find_if(chart.measures.front().notes, [&](const auto& note) {
        return note.sound_channel_event.has_value() &&
               std::ranges::any_of(chart.sound_channels[1].events, [&](const auto& event) {
                   return event.id == *note.sound_channel_event;
               });
    });
    REQUIRE(second_note != chart.measures.front().notes.end());
    second_note->position = first_note->position;
    second_note->lane = first_note->lane;
    second_note->end = first_note->end;
    second_note->kind = first_note->kind;

    const auto layered =
        wormedit::audio::audition_resolution_t::from(song, chart.id, first_note->id, bindings);
    REQUIRE(layered.diagnostics.empty());
    REQUIRE(layered.value.clips.size() == 2);
    CHECK(layered.value.clips[0].source != layered.value.clips[1].source);

    const auto bounded = wormedit::audio::audition_resolution_t::from(song, chart.id, first_note->id,
                                                                  bindings, {.maximum_layers = 1});
    CHECK_FALSE(bounded.diagnostics.empty());
    CHECK(bounded.value.clips.empty());
    REQUIRE(bounded.diagnostics.size() == 1);
    CHECK(bounded.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::capacity_exceeded);
}

TEST_CASE("preview binding resolver indexes exact BMSON slices for one schedule batch",
          "[unit][audio][audition-resolver][preview]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    const auto asset = chart.sound_channels.front().source_clip.source;
    auto audio_asset = std::ranges::find(song.audio_assets, asset, &wormedit::audio_asset_t::id);
    REQUIRE(audio_asset != song.audio_assets.end());
    audio_asset->stream.sample_rate = 48'000;
    audio_asset->stream.frame_count.reset();
    const std::array bindings{runtime_binding(asset)};

    const auto note_for_row = [&](std::uint64_t row) -> const wormedit::note_t& {
        const auto retained =
            std::ranges::find_if(chart.sound_channels.front().events, [&](const auto& event) {
                return event.playable && event.position.row == row;
            });
        REQUIRE(retained != chart.sound_channels.front().events.end());
        const auto note = std::ranges::find_if(chart.measures.front().notes, [&](const auto& item) {
            return item.sound_channel_event == retained->id;
        });
        REQUIRE(note != chart.measures.front().notes.end());
        return *note;
    };
    const auto& shared = note_for_row(360);
    const auto& restart = note_for_row(720);
    const auto bgm_measure = std::ranges::find_if(
        chart.measures, [](const auto& measure) { return !measure.bgm_events.empty(); });
    REQUIRE(bgm_measure != chart.measures.end());
    const auto& bgm = bgm_measure->bgm_events.front();
    REQUIRE(shared.sound.has_value());
    REQUIRE(restart.sound.has_value());
    REQUIRE(bgm.sound.has_value());

    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    schedule.playable = {{.sample_frame = 10, .object = shared.id, .resource = *shared.sound},
                         {.sample_frame = 20, .object = restart.id, .resource = *restart.sound}};
    schedule.bgm = {{.sample_frame = 30, .object = bgm.id, .resource = *bgm.sound}};

    const auto resolved =
        wormedit::audio::preview_bindings_resolution_t::from(song, chart.id, schedule, bindings);
    REQUIRE(resolved.diagnostics.empty());
    REQUIRE(resolved.value.bindings.size() == 3);
    const auto voice_for =
        [&](wormedit::object_id_t object) -> const wormedit::audio::preview_voice_source_t& {
        const auto found = std::ranges::find(
            resolved.value.bindings, object, &wormedit::audio::preview_object_source_binding_t::object);
        REQUIRE(found != resolved.value.bindings.end());
        REQUIRE(found->voices.size() == 1);
        return found->voices.front();
    };
    CHECK(voice_for(shared.id).source_start_frame == 11'025);
    CHECK(voice_for(shared.id).frame_count == 33'075);
    CHECK(voice_for(restart.id).source_start_frame == 0);
    CHECK(voice_for(restart.id).frame_count == 11'025);
    CHECK(voice_for(bgm.id).source_start_frame == 88'200);
    CHECK(voice_for(bgm.id).frame_count == 111'800);

    schedule.playable.front().resource = wormedit::resource_id_t{999'999};
    const auto stale =
        wormedit::audio::preview_bindings_resolution_t::from(song, chart.id, schedule, bindings);
    CHECK_FALSE(stale.diagnostics.empty());
    CHECK(stale.value.bindings.empty());
    REQUIRE(stale.diagnostics.size() == 1);
    CHECK(stale.diagnostics.front().code == wormedit::audio::audition_resolve_code_t::invalid_link);
}

TEST_CASE("preview points past the end of a short sample stay silent while the rest still plays",
          "[unit][audio][audition-resolver][preview]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    const auto asset = chart.sound_channels.front().source_clip.source;
    const auto audio_asset = std::ranges::find(song.audio_assets, asset, &wormedit::audio_asset_t::id);
    REQUIRE(audio_asset != song.audio_assets.end());
    audio_asset->stream.sample_rate = 48'000;
    audio_asset->stream.frame_count.reset();
    // A 40,000 frame sample: the BGM point's continuation starts at about 96,000, well past its end.
    auto binding = runtime_binding(asset, 36'750);
    binding.source_frame_count = 40'000;
    const std::array bindings{binding};
    const auto restart_event = std::ranges::find_if(chart.sound_channels.front().events, [](const auto& event) {
        return event.position.row == 720 && event.playable;
    });
    REQUIRE(restart_event != chart.sound_channels.front().events.end());
    const auto& restart = *std::ranges::find_if(chart.measures.front().notes, [&](const auto& note) {
        return note.sound_channel_event == restart_event->id;
    });
    const auto bgm_measure = std::ranges::find_if(chart.measures, [](const auto& m) { return !m.bgm_events.empty(); });
    REQUIRE(bgm_measure != chart.measures.end());
    const auto& bgm = bgm_measure->bgm_events.front();
    REQUIRE(restart.sound.has_value());
    REQUIRE(bgm.sound.has_value());

    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    schedule.playable = {{.sample_frame = 20, .object = restart.id, .resource = *restart.sound}};
    schedule.bgm = {{.sample_frame = 30, .object = bgm.id, .resource = *bgm.sound}};
    const auto resolved = wormedit::audio::preview_bindings_resolution_t::from(song, chart.id, schedule, bindings);
    CHECK(is_silent_audition(song, chart.id, bgm.id, bindings, resolved));
    const auto has = [&](wormedit::object_id_t object) {
        return std::ranges::any_of(resolved.value.bindings, [&](const auto& item) { return item.object == object; });
    };
    CHECK(has(restart.id));
    CHECK_FALSE(has(bgm.id));
}

TEST_CASE("preview binding resolver can leave unavailable audio silent without losing the chart",
          "[unit][audio][audition-resolver][preview]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    const auto& chart = song.charts.front();
    const auto& note = chart.measures.front().notes.front();
    REQUIRE(note.sound.has_value());

    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    schedule.playable = {{.sample_frame = 24'000,
                          .object = note.id,
                          .resource = *note.sound}};
    const auto strict = wormedit::audio::preview_bindings_resolution_t::from(song, chart.id, schedule, {});
    REQUIRE(strict.diagnostics.size() == 1);

    const auto permissive = wormedit::audio::preview_bindings_resolution_t::from(
        song, chart.id, schedule, {}, {.skip_unavailable_preview_sources = true});
    const std::array checks{
        !strict.diagnostics.empty(),
        strict.diagnostics.front().code ==
            wormedit::audio::audition_resolve_code_t::missing_source_binding,
        permissive.diagnostics.empty(), permissive.value.bindings.empty(),
        permissive.value.skipped_unavailable_objects == 1, permissive.diagnostics.empty(),
        schedule.playable.front().sample_frame == 24'000,
    };
    CHECK(std::ranges::all_of(checks, [](bool value) { return value; }));
}

TEST_CASE("preview binding resolver keeps available audio alongside silent chart objects",
          "[unit][audio][audition-resolver][preview]") {
    auto parsed = bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    REQUIRE(song.audio_assets.size() >= 2);
    for (auto& asset : song.audio_assets) {
        asset.stream.sample_rate = 48'000;
        asset.stream.frame_count.reset();
    }
    const auto available_asset = chart.sound_channels.front().source_clip.source;
    const std::array bindings{runtime_binding(available_asset)};
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.sound && schedule.playable.size() < 8) {
                schedule.playable.push_back({.sample_frame = 24'000,
                                             .object = note.id,
                                             .resource = *note.sound});
            }
        }
    }
    REQUIRE(schedule.playable.size() >= 2);

    const auto resolved = wormedit::audio::preview_bindings_resolution_t::from(
        song, chart.id, schedule, bindings, {.skip_unavailable_preview_sources = true});
    const std::array checks{
        resolved.diagnostics.empty(), resolved.value.skipped_unavailable_objects > 0,
        !resolved.value.bindings.empty(),
        resolved.value.bindings.size() + resolved.value.skipped_unavailable_objects ==
            schedule.playable.size(),
    };
    CHECK(std::ranges::all_of(checks, [](bool value) { return value; }));
}

TEST_CASE("preview binding resolver expands one canonical fused object atomically",
          "[unit][audio][audition-resolver][preview][polyphony]") {
    auto parsed = bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    std::vector<wormedit::audio::audition_source_binding_t> bindings;
    for (std::size_t index = 0; index < song.audio_assets.size(); ++index) {
        song.audio_assets[index].stream.sample_rate = 48'000;
        song.audio_assets[index].stream.frame_count.reset();
        bindings.push_back({.asset = song.audio_assets[index].id,
                            .source = {"asset:" + std::to_string(index), "decoded:v1"},
                            .source_sample_rate = 48'000,
                            .source_frame_count = std::nullopt,
                            .output_sample_rate = wormedit::audio::output_sample_rate,
                            .output_frame_count = 200'000});
    }

    auto& first = chart.measures.front().notes.front();
    const auto second = std::ranges::find_if(chart.measures.front().notes, [&](const auto& note) {
        return note.sound_channel_event.has_value() &&
               std::ranges::any_of(chart.sound_channels[1].events, [&](const auto& event) {
                   return event.id == *note.sound_channel_event;
               });
    });
    REQUIRE(second != chart.measures.front().notes.end());
    second->position = first.position;
    second->lane = first.lane;
    second->end = first.end;
    second->kind = first.kind;
    REQUIRE(first.sound.has_value());
    wormedit::preview_schedule_t schedule{.sample_rate = wormedit::audio::output_sample_rate};
    schedule.playable = {{.object = first.id, .resource = *first.sound}};

    const auto resolved =
        wormedit::audio::preview_bindings_resolution_t::from(song, chart.id, schedule, bindings);
    REQUIRE(resolved.diagnostics.empty());
    REQUIRE(resolved.value.bindings.size() == 1);
    CHECK(resolved.value.bindings.front().object == first.id);
    REQUIRE(resolved.value.bindings.front().voices.size() == 2);
    CHECK(resolved.value.bindings.front().voices[0].source != resolved.value.bindings.front().voices[1].source);

    const auto bounded = wormedit::audio::preview_bindings_resolution_t::from(
        song, chart.id, schedule, bindings, {.maximum_total_clips = 1});
    CHECK_FALSE(bounded.diagnostics.empty());
    CHECK(bounded.value.bindings.empty());
    REQUIRE(bounded.diagnostics.size() == 1);
    CHECK(bounded.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::capacity_exceeded);
}

namespace {
void require_scoped_audition(bool value) { REQUIRE(value); }
}
TEST_CASE("Object audition can resolve an inactive scoped authoring Chart",
          "[audio][audition-resolver][bms-scoped]") {
    using namespace wormedit;
    const std::string source = "#WAV01 a.wav\n#RANDOM 1\n#IF 2\n#00011:01\n#ENDIF\n#ENDRANDOM\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    require_scoped_audition(parsed.value.song.has_value());
    const auto& song = *parsed.value.song;
    require_scoped_audition(song.prepared_bms_programs.size() == 1);
    const auto& owner = *song.prepared_bms_programs.front();
    for (const auto& node : owner.prepared().program.nodes()) {
        if (!std::holds_alternative<bms_if_scope_t>(node.value)) continue;
        const auto selected = owner.prepared().program.target_chart(node.id);
        require_scoped_audition(selected.has_value());
        const auto* chart = owner.read_authoring_chart(*selected);
        require_scoped_audition(chart);
        require_scoped_audition(!chart->measures.front().notes.empty());
        const auto& note = chart->measures.front().notes.front();
        const std::array bindings{runtime_binding(song.resources.front().asset)};
        const auto resolved = audio::audition_resolution_t::from(song, owner.difficulty(), note.id,
            bindings, {.authoring_chart = *selected});
        require_scoped_audition(resolved.ok());
        CHECK(resolved.value.clips.size() == 1);
        return;
    }
    FAIL("missing inactive branch fixture");
}
