#include "sys/audio/export_cut_preview.h"
#include <catch2/catch_test_macros.hpp>
#include <array>

namespace { using namespace wormedit;
auto preview_cut() -> std::array<export_cut_t, 1> {
    return {export_cut_t{.id = object_id_t{1}, .begin_milliseconds = 1, .end_milliseconds = 2}};
}

void check_split_voices(const preview_schedule_t& schedule,
    const std::vector<audio::preview_object_source_binding_t>& bindings) {
    REQUIRE(schedule.playable.size() == 2);
    REQUIRE(bindings.size() == 2);
    CHECK(schedule.playable[0].sample_frame == 0);
    CHECK(schedule.playable[1].sample_frame == 44);
    CHECK(bindings[0].voices[0].frame_count == 44);
    CHECK(bindings[1].voices[0].source_start_frame == 188);
}
void check_split_music(const preview_schedule_t& schedule) {
    REQUIRE(schedule.music_segments.size() == 2);
    CHECK(schedule.music_segments[1].sample_frame == 44);
    CHECK(schedule.music_segments[1].source_start_frame == 88);
    CHECK(schedule.music_segments[1].frame_count == 353);
    CHECK_FALSE(schedule.music);
}
}
TEST_CASE("Cut preview splits continuous voices and music on the same physical seam", "[sys][audio][export-cut]") {
    using namespace wormedit;
    preview_schedule_t schedule{.sample_rate = 44100,
        .playable = {{.sample_frame = 0, .object = object_id_t{2}, .resource = resource_id_t{3}}},
        .music = preview_schedule_t::music_event_t{.resource = resource_id_t{4}, .frame_count = 441}};
    std::vector<audio::preview_object_source_binding_t> bindings{{.object = object_id_t{2},
        .voices = {{.source_start_frame = 100, .frame_count = 441, .gain = 0.5F, .mix_group = 8}}}};
    const auto prepared = sys::prepare_export_cut_preview(schedule, bindings, preview_cut(), 441);
    REQUIRE(prepared.value);
    check_split_voices(schedule, bindings);
    check_split_music(schedule);
    CHECK(bindings[1].voices[0].frame_count == 353);
    CHECK(bindings[1].voices[0].gain == 0.5F);
    CHECK(bindings[1].voices[0].mix_group == 8);
}
TEST_CASE("A cut covering the complete audition clock keeps its original audio", "[sys][audio][export-cut]") {
    using namespace wormedit;
    preview_schedule_t schedule{.sample_rate = 44100,
        .music = preview_schedule_t::music_event_t{.resource = resource_id_t{4}, .frame_count = 441}};
    std::vector<audio::preview_object_source_binding_t> bindings;
    const std::array cuts{export_cut_t{.id = object_id_t{1}, .end_milliseconds = 10}};
    const auto prepared = sys::prepare_export_cut_preview(schedule, bindings, cuts, 441);
    CHECK_FALSE(prepared.value);
    REQUIRE(prepared.diagnostics.size() == 1);
    CHECK(prepared.diagnostics.front().severity == diagnostic_severity_t::warning);
    REQUIRE(schedule.music);
    CHECK(schedule.music->frame_count == 441);
    CHECK(schedule.music_segments.empty());
}
