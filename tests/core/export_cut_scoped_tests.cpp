#include "../support/bms_program_fixture.h"
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/export_cut_transform.h>
#include <wormedit/core/timing.h>

namespace {
using namespace wormedit;
using namespace wormedit::test::bms_program;
auto scoped_cut_song(std::string_view text) -> song_document_t {
    auto input = prepared(text);
    const auto built = bms_program_document_t::from(chart_id_t{9000}, input);
    REQUIRE(built.ok());
    REQUIRE(built.value);
    return {.id = song_id_t{10000}, .audio_assets = input.audio_assets, .visual_assets = input.visual_assets,
        .resources = input.resources, .prepared_bms_programs = {*built.value},
        .export_cuts = {{.id = object_id_t{10001}, .begin_milliseconds = 500, .end_milliseconds = 1000}}};
}
auto authored_note(const chart_document_t& chart, object_id_t id) -> const note_t* {
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) if (note.id == id) return &note;
    return nullptr;
}
auto sample_evidence(const song_document_t& song) -> std::vector<export_cut_source_t> {
    std::vector<export_cut_source_t> result;
    for (const auto& asset : song.audio_assets)
        result.push_back({.asset = asset.id, .native_sample_rate = 44100,
            .native_frame_count = 4410, .output_frame_count = 4410, .source_path = asset.source_path});
    return result;
}
void check_note_time(const chart_document_t& before, const chart_document_t& after,
                     const note_t& note, const export_cut_timeline_t& timeline) {
    const auto* original = authored_note(before, note.id);
    REQUIRE(original);
    const auto first = timing_index_t::from(before);
    const auto second = timing_index_t::from(after);
    const auto a = measure_index_t::from(before);
    const auto b = measure_index_t::from(after);
    REQUIRE((first.value && second.value && a && b));
    const auto source = first.value->query_seconds(*a, original->position);
    const auto output = second.value->query_seconds(*b, note.position);
    REQUIRE((source && output));
    CHECK(timeline.collapsed_seconds(source->value) == output->value);
}
void check_outcome(const bms_program_document_t& before, const bms_program_document_t& after,
                   const bms_program_evaluation_t& choice, const export_cut_timeline_t& timeline) {
    const auto source = bms_program_projection_t::from(before.prepared(), choice);
    const auto output = bms_program_projection_t::from(after.prepared(), choice);
    REQUIRE((source.ok() && source.value && output.ok() && output.value));
    const auto& first = source.value->song().charts.front();
    const auto& second = output.value->song().charts.front();
    for (const auto& measure : second.measures)
        for (const auto& note : measure.notes) check_note_time(first, second, note, timeline);
}
}
TEST_CASE("Scoped export cuts preserve the full selector graph and every note outcome", "[core][export-cut][scoped]") {
    const auto song = scoped_cut_song("#BPM 120\n#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00112:01\n#ENDIF\n#ENDRANDOM\n#00213:01\n");
    const auto evidence = sample_evidence(song);
    const auto result = export_cut_transform_t::from(song, {}, evidence);
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
    REQUIRE((result.ok() && result.value));
    REQUIRE(result.value->document.prepared_bms_programs.size() == 1);
    const auto& before = *song.prepared_bms_programs.front();
    const auto& after = *result.value->document.prepared_bms_programs.front();
    CHECK((after.prepared().program.has_control_flow() &&
        after.prepared().program.nodes().size() == before.prepared().program.nodes().size() &&
        song.prepared_bms_programs.front() != result.value->document.prepared_bms_programs.front() &&
        song.export_cuts.size() == 1));
    const auto choices = before.bounded_evaluations();
    REQUIRE(choices);
    const auto timeline = export_cut_timeline_t::from(song.export_cuts);
    for (const auto& choice : *choices) check_outcome(before, after, choice, timeline.value);
}
TEST_CASE("Conditional timing that cannot share one cut geometry rejects without flattening", "[core][export-cut][scoped]") {
    const auto song = scoped_cut_song("#BPM 120\n#RANDOM 2\n#IF 1\n#BPM 120\n#ELSE\n#BPM 60\n#ENDIF\n#ENDRANDOM\n#00211:00\n");
    const auto before = song.prepared_bms_programs.front();
    const auto result = export_cut_transform_t::from(song, {});
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.value);
    CHECK(song.prepared_bms_programs.front() == before);
    CHECK(before->prepared().program.has_control_flow());
}
TEST_CASE("Scoped rebasing reserves its complete content identity range before building", "[core][export-cut][scoped]") {
    const auto song = scoped_cut_song("#BPM 120\n#00211:00\n");
    const auto& owner = *song.prepared_bms_programs.front();
    const auto* chart = owner.read_chart();
    REQUIRE(chart);
    const auto result = owner.build_rebased_program(*chart, *chart, object_id_t{UINT64_MAX});
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.error().empty());
    CHECK(result.error().front().code == diagnostic_code_t::invalid_id);
}
