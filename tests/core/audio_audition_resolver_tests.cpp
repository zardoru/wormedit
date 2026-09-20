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

auto bmson_fixture(std::string_view name) -> wormedit::bmson_parse_result_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / name,
                         std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    return wormedit::parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
}

auto published_fixture() -> wormedit::bmson_parse_result_t {
    return bmson_fixture("published-vox-slicing.bmson");
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

} // namespace

TEST_CASE("audition resolver maps published BMSON slices into normalized transport frames",
          "[unit][audio][audition-resolver][bmson]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
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
        wormedit::audio::resolve_object_audition(song, chart.id, note->id, bindings);
    REQUIRE(resolved);
    REQUIRE(resolved.clips.size() == 1);
    CHECK(resolved.document_revision == song.revision);
    CHECK(resolved.clips.front().source_start_frame == 44'100);
    CHECK(resolved.clips.front().frame_count == 11'025);

    const auto retained_shared = event_at(360, true);
    REQUIRE(retained_shared != chart.sound_channels.front().events.end());
    const auto shared_note =
        std::ranges::find_if(chart.measures.front().notes, [&](const auto& candidate) {
            return candidate.sound_channel_event == retained_shared->id;
        });
    REQUIRE(shared_note != chart.measures.front().notes.end());
    const auto shared =
        wormedit::audio::resolve_object_audition(song, chart.id, shared_note->id, bindings);
    REQUIRE(shared);
    REQUIRE(shared.clips.size() == 1);
    CHECK(shared.clips.front().source_start_frame == 11'025);
    CHECK(shared.clips.front().frame_count == 33'075);

    chart.sound_channels.front().source_clip.begin_frame = 48'000;
    chart.sound_channels.front().source_clip.end_frame = 240'000;
    const auto cropped = wormedit::audio::resolve_object_audition(
        song, chart.id, chart.measures.front().notes.front().id, bindings);
    REQUIRE(cropped);
    REQUIRE(cropped.clips.size() == 1);
    CHECK(cropped.clips.front().source_start_frame == 44'100);
    CHECK(cropped.clips.front().frame_count == 11'025);
}

TEST_CASE("audition resolver isolates BGM slices and falls back to whole unlinked resources",
          "[unit][audio][audition-resolver][fallback]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
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
    const auto bgm = wormedit::audio::resolve_object_audition(
        song, chart.id, bgm_measure->bgm_events.front().id, bindings);
    REQUIRE(bgm);
    REQUIRE(bgm.clips.size() == 1);
    CHECK(bgm.clips.front().source_start_frame == 132'300);
    CHECK(bgm.clips.front().frame_count == 67'700);

    auto& note = chart.measures.front().notes.front();
    note.sound_channel_event.reset();
    const auto whole = wormedit::audio::resolve_object_audition(song, chart.id, note.id, bindings);
    REQUIRE(whole);
    REQUIRE(whole.clips.size() == 1);
    CHECK(whole.clips.front().source_start_frame == 0);
    CHECK(whole.clips.front().frame_count == 200'000);

    const std::array duplicate_bindings{bindings.front(), bindings.front()};
    const auto duplicate =
        wormedit::audio::resolve_object_audition(song, chart.id, note.id, duplicate_bindings);
    CHECK_FALSE(duplicate);
    CHECK(duplicate.clips.empty());
    REQUIRE(duplicate.diagnostics.size() == 1);
    CHECK(duplicate.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::duplicate_source_binding);

    auto mismatched = bindings.front();
    mismatched.source_sample_rate = 44'100;
    const std::array mismatched_bindings{mismatched};
    const auto invalid_geometry = wormedit::audio::resolve_object_audition(
        song, chart.id, bgm_measure->bgm_events.front().id, mismatched_bindings);
    CHECK_FALSE(invalid_geometry);
    REQUIRE(invalid_geometry.diagnostics.size() == 1);
    CHECK(invalid_geometry.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::invalid_source_geometry);

    auto wrong_output_rate = bindings.front();
    wrong_output_rate.output_sample_rate = 48'000;
    const std::array wrong_output_bindings{wrong_output_rate};
    const auto invalid_output = wormedit::audio::resolve_object_audition(
        song, chart.id, bgm_measure->bgm_events.front().id, wrong_output_bindings);
    CHECK_FALSE(invalid_output);
    REQUIRE(invalid_output.diagnostics.size() == 1);
    CHECK(invalid_output.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::invalid_source_geometry);

    auto mismatched_length = bindings.front();
    mismatched_length.source_frame_count = 240'000;
    audio_asset->stream.frame_count = 240'001;
    const std::array mismatched_length_bindings{mismatched_length};
    const auto invalid_length = wormedit::audio::resolve_object_audition(
        song, chart.id, bgm_measure->bgm_events.front().id, mismatched_length_bindings);
    CHECK_FALSE(invalid_length);
    REQUIRE(invalid_length.diagnostics.size() == 1);
    CHECK(invalid_length.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::invalid_source_geometry);
}

TEST_CASE("audition resolver layers fused BMSON channels and enforces its layer bound",
          "[unit][audio][audition-resolver][polyphony]") {
    auto parsed = bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
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
        wormedit::audio::resolve_object_audition(song, chart.id, first_note->id, bindings);
    REQUIRE(layered);
    REQUIRE(layered.clips.size() == 2);
    CHECK(layered.clips[0].source != layered.clips[1].source);

    const auto bounded = wormedit::audio::resolve_object_audition(song, chart.id, first_note->id,
                                                                  bindings, {.maximum_layers = 1});
    CHECK_FALSE(bounded);
    CHECK(bounded.clips.empty());
    REQUIRE(bounded.diagnostics.size() == 1);
    CHECK(bounded.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::capacity_exceeded);
}

TEST_CASE("preview binding resolver indexes exact BMSON slices for one schedule batch",
          "[unit][audio][audition-resolver][preview]") {
    auto parsed = published_fixture();
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
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
        wormedit::audio::resolve_preview_bindings(song, chart.id, schedule, bindings);
    REQUIRE(resolved);
    REQUIRE(resolved.bindings.size() == 3);
    const auto voice_for =
        [&](wormedit::object_id_t object) -> const wormedit::audio::preview_voice_source_t& {
        const auto found = std::ranges::find(
            resolved.bindings, object, &wormedit::audio::preview_object_source_binding_t::object);
        REQUIRE(found != resolved.bindings.end());
        REQUIRE(found->voices.size() == 1);
        return found->voices.front();
    };
    CHECK(voice_for(shared.id).source_start_frame == 11'025);
    CHECK(voice_for(shared.id).frame_count == 33'075);
    CHECK(voice_for(restart.id).source_start_frame == 44'100);
    CHECK(voice_for(restart.id).frame_count == 11'025);
    CHECK(voice_for(bgm.id).source_start_frame == 132'300);
    CHECK(voice_for(bgm.id).frame_count == 67'700);

    schedule.playable.front().resource = wormedit::resource_id_t{999'999};
    const auto stale =
        wormedit::audio::resolve_preview_bindings(song, chart.id, schedule, bindings);
    CHECK_FALSE(stale);
    CHECK(stale.bindings.empty());
    REQUIRE(stale.diagnostics.size() == 1);
    CHECK(stale.diagnostics.front().code == wormedit::audio::audition_resolve_code_t::invalid_link);
}

TEST_CASE("preview binding resolver expands one canonical fused object atomically",
          "[unit][audio][audition-resolver][preview][polyphony]") {
    auto parsed = bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
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
        wormedit::audio::resolve_preview_bindings(song, chart.id, schedule, bindings);
    REQUIRE(resolved);
    REQUIRE(resolved.bindings.size() == 1);
    CHECK(resolved.bindings.front().object == first.id);
    REQUIRE(resolved.bindings.front().voices.size() == 2);
    CHECK(resolved.bindings.front().voices[0].source != resolved.bindings.front().voices[1].source);

    const auto bounded = wormedit::audio::resolve_preview_bindings(
        song, chart.id, schedule, bindings, {.maximum_total_clips = 1});
    CHECK_FALSE(bounded);
    CHECK(bounded.bindings.empty());
    REQUIRE(bounded.diagnostics.size() == 1);
    CHECK(bounded.diagnostics.front().code ==
          wormedit::audio::audition_resolve_code_t::capacity_exceeded);
}
