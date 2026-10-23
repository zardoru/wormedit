#include <wormedit/formats/bms.h>
#include "../support/bms_program_fixture.h"
#include <wormedit/core/rational.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using namespace wormedit;
using namespace wormedit::test::bms_program;
namespace {
struct comparison_t {
    bms_program_projection_t typed;
    song_document_t legacy;
};
auto compare(std::string_view source, std::vector<std::int64_t> values = {},
    std::string profile = "bme-7k-outside-scratch") -> comparison_t {
    auto input = prepared(source, profile);
    auto legacy = parse_bms_bytes(bytes(source), {.lane_profile_id = profile});
    REQUIRE(legacy.value.song);
    std::vector<std::size_t> orders;
    const auto visit = [&](auto&& self, const bms_control_node_t& node) -> void {
        if (node.kind == bms_control_kind_t::random || node.kind == bms_control_kind_t::switch_block) orders.push_back(node.source_order);
        for (const auto& child : node.children) self(self, child);
    };
    visit(visit, *legacy.value.song->charts.front().bms_control_tree);
    const auto controls = selectors(input.program);
    REQUIRE(controls.size() == orders.size());
    bms_program_evaluation_t evaluation;
    bms_parse_options_t options{.lane_profile_id = profile};
    for (std::size_t index = 0; index < values.size(); ++index) if (values[index]) {
        evaluation.selections.push_back({controls[index], values[index]});
        options.branch_choices.push_back({orders[index], values[index]});
    }
    legacy = parse_bms_bytes(bytes(source), options);
    REQUIRE(legacy.value.song);
    return {projected(input, evaluation), std::move(*legacy.value.song)};
}

}

TEST_CASE("Typed scoped projection matches legacy notes for every mixed control outcome", "[bms-program-projection]") {
    const auto random = GENERATE(0, 1, 2);
    const auto selection = GENERATE(0, 1, 2, 3);
    const auto nested = GENERATE(0, 1, 2);
    if (selection != 1 && nested != 0) return; // A nested choice is valid only when reachable.
    const auto result = compare(fixture("control-flow-mixed.bms"), {random, selection, nested});
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(event_keys(result.typed.song()) == event_keys(result.legacy));
}
TEST_CASE("Scoped projections validate without a second legacy control tree", "[bms-program-projection]") {
    const auto choice = GENERATE(1, 2, 3);
    auto input = prepared(scoped_source);
    const auto projection = projected(input, {.selections = {{selectors(input.program).front(), choice}}});
    CHECK_FALSE(diagnostic_queries::has_errors(validate_document(projection.song())));
    CHECK(projection.song().charts.front().active_branch_selection.empty());
    CHECK(projection.selections().size() == 1);
}
TEST_CASE("Fixed selector settings project identically to legacy including explicit overrides", "[bms-program-projection]") {
    const auto random = GENERATE(0, 1, 2, 3);
    const auto selection = GENERATE(0, 1, 2, 9);
    const auto result = compare(fixture("control-flow-fixed-values.bms"), {random, selection});
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
}
TEST_CASE("Typed scoped declarations geometry and cross-scope holds match legacy", "[bms-program-projection]") {
    const auto choice = GENERATE(1, 2, 3);
    const auto result = compare(scoped_source, {choice});
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(event_keys(result.typed.song()) == event_keys(result.legacy));
    CHECK(bgm_keys(result.typed.song()) == bgm_keys(result.legacy));
    CHECK(length_keys(result.typed.song().charts.front()) == length_keys(result.legacy.charts.front()));
    CHECK(result.typed.song().metadata.title == result.legacy.metadata.title);
}
TEST_CASE("Existing bounded non-control fixtures retain all playable semantics", "[bms-program-projection]") {
    const auto name = GENERATE("legacy-long-note.bml", "repeated-bgm-ratio-stop-collision.bms", "non-gameplay-roundtrip.bme");
    const auto result = compare(fixture(name));
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(event_keys(result.typed.song()) == event_keys(result.legacy));
    CHECK(bgm_keys(result.typed.song()) == bgm_keys(result.legacy));
}
TEST_CASE("SKIP inside selected nested scopes stops later cases but retains the current case remainder", "[bms-program-projection]") {
    const auto selected = GENERATE(1, 2);
    const auto result = compare("#WAV01 x.wav\n#SETSWITCH 1\n#SWITCH 2\n#CASE 1\n#RANDOM 2\n#IF 1\n#SKIP\n#00011:01\n#ENDIF\n#ENDRANDOM\n#00111:01\n#CASE 2\n#00211:01\n#ENDSW\n", {0, selected});
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
}
TEST_CASE("Active BGM column ranks exclude inactive earlier lines", "[bms-program-projection]") {
    const auto selected = GENERATE(1, 2);
    const auto result = compare("#WAV01 x.wav\n#RANDOM 2\n#IF 1\n#00001:0100\n#ELSE\n#00001:0001\n#ENDIF\n#ENDRANDOM\n#00001:01\n", {selected});
    CHECK(bgm_keys(result.typed.song()) == bgm_keys(result.legacy));
    CHECK(result.typed.song().charts.front().bgm_source_lanes.size() == 2);
}
TEST_CASE("Projection reports missing references and shares a missing BMP placeholder", "[bms-program-projection]") {
    const auto result = compare("#00011:01\n#00004:01\n#00107:01\n#00008:01\n");
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(event_keys(result.typed.song()) == event_keys(result.legacy));
    CHECK(result.typed.song().resources.size() == 1);
}
TEST_CASE("New Chart program order controls declarations without concatenating musical time", "[bms-program-projection]") {
    auto input = prepared("#TITLE First\n#00011:01\n; split\n#TITLE Last\n#00012:01\n");
    auto nodes = std::vector(input.program.nodes().begin(), input.program.nodes().end());
    std::ranges::reverse(nodes.front().children);
    input.program = *bms_program_t::from(std::move(nodes));
    const auto result = projected(input);
    CHECK(result.song().metadata.title == "First");
    CHECK(result.song().charts.front().measures.size() == 1);
    CHECK(result.song().charts.front().measures.front().notes.size() == 2);
    CHECK(result.song().charts.front().measures.front().notes.front().position.row == 0);
}
TEST_CASE("Projection is detached from original bytes and resolves typed edits", "[bms-program-projection]") {
    auto input = prepared("#BPM 120\n#00003:78\n");
    auto nodes = std::vector(input.program.nodes().begin(), input.program.nodes().end());
    auto& content = std::get<bms_chart_content_t>(nodes.back().value);
    std::get<bms_direct_bpm_t>(content.items.back().value).value = {200, 1};
    input.program = *bms_program_t::from(std::move(nodes));
    const auto result = projected(input);
    CHECK(result.song().charts.front().bpms.back().beats_per_minute == signed_rational_t{200, 1});
}
TEST_CASE("Visible objects retain stable owners across outcomes and hide stale selections", "[bms-program-projection]") {
    const auto input = prepared("#WAV01 x.wav\n#00011:01\n#RANDOM 2\n#IF 1\n#00111:01\n#ELSE\n#00211:01\n#ENDIF\n#ENDRANDOM\n");
    const auto control = selectors(input.program).front();
    const auto a = projected(input, {{{control, 1}}}), b = projected(input, {{{control, 2}}});
    const auto root = a.song().charts.front().measures.front().notes.front().id;
    const auto hidden = a.song().charts.front().measures[1].notes.front().id;
    CHECK(std::ranges::equal(a.owners(root.value), b.owners(root.value)));
    CHECK(b.owners(hidden.value).empty());
    CHECK_FALSE(b.resolve_owner(hidden.value, a.owners(hidden.value).front()));
    CHECK(a.resolve_owner(hidden.value, a.owners(hidden.value).front()));
}
TEST_CASE("Holds retain both scoped endpoint owners and the winning resource contributor", "[bms-program-projection]") {
    const auto input = prepared(scoped_source);
    const auto result = projected(input, {{{selectors(input.program).front(), 1}}});
    const auto& notes = result.song().charts.front().measures.front().notes;
    const auto hold = std::ranges::find(notes, note_kind_t::hold, &note_t::kind);
    REQUIRE(hold != notes.end());
    const auto owners = result.owners(hold->id.value);
    REQUIRE(owners.size() == 3);
    CHECK(owners[0].scope != owners[1].scope);
    CHECK(owners[0].item == hold->id);
}
TEST_CASE("Invalid duplicate and unreachable evaluation choices refuse the entire candidate", "[bms-program-projection]") {
    const auto input = prepared("#RANDOM 2\n#IF 1\n#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#ENDRANDOM\n#ENDIF\n#ENDRANDOM\n");
    const auto ids = selectors(input.program);
    const auto selections = GENERATE(0, 1, 2, 3);
    bms_program_evaluation_t evaluation;
    switch (selections) {
    case 0: evaluation.selections = {{branch_id_t{999999}, 1}}; break;
    case 1: evaluation.selections = {{ids[0], 3}}; break;
    case 2: evaluation.selections = {{ids[0], 1}, {ids[0], 2}}; break;
    case 3: evaluation.selections = {{ids[0], 2}, {ids[1], 1}}; break;
    }
    const auto result = bms_program_projection_t::from(input, evaluation);
    CHECK_FALSE(result.value);
    CHECK_FALSE(result.ok());
}
TEST_CASE("Projection limits refuse capped candidates without changing accepted output", "[bms-program-projection]") {
    const auto input = prepared("#BPM 120\n#WAV01 x.wav\n#00011:0101\n");
    const auto accepted = projected(input);
    const auto kind = GENERATE(0, 1, 2, 3, 4);
    bms_program_projection_limits_t limits;
    switch (kind) {
    case 0: limits.maximum_active_items = 1; break;
    case 1: limits.maximum_input_items = 1; break;
    case 2: limits.maximum_nodes = 1; break;
    case 3: limits.maximum_depth = 0; break;
    case 4: limits.maximum_resolution = 1; break;
    }
    const auto result = bms_program_projection_t::from(input, {}, limits);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.front().code == failure_code_t::capacity_exceeded);
    CHECK(accepted.song().charts.front().measures.front().notes.size() == 2);
}
TEST_CASE("Projection diagnostic bounds refuse an otherwise unbounded warning stream", "[bms-program-projection]") {
    const auto input = prepared("#00011:01020304\n");
    const auto result = bms_program_projection_t::from(input, {}, {.maximum_diagnostics = 2});
    CHECK_FALSE(result.value);
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics.back().code == failure_code_t::capacity_exceeded);
}
TEST_CASE("Invalid exact positions and unpaired endpoints refuse the projection", "[bms-program-projection]") {
    const auto invalid = GENERATE(0, 1, 2);
    auto input = prepared("#00051:0101\n");
    auto nodes = std::vector(input.program.nodes().begin(), input.program.nodes().end());
    auto& items = std::get<bms_chart_content_t>(nodes.back().value).items;
    if (invalid == 0) items.pop_back();
    if (invalid == 1) std::get<bms_hold_endpoint_t>(items.front().value).position.measure = measure_id_t{999999};
    if (invalid == 2) std::get<bms_hold_endpoint_t>(items.front().value).position.fraction = {1, 0};
    input.program = *bms_program_t::from(std::move(nodes));
    const auto result = bms_program_projection_t::from(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.front().code == failure_code_t::invalid_geometry);
}
TEST_CASE("All BMS-family lane profile fixtures project identically to legacy", "[bms-program-projection]") {
    const auto entry = GENERATE_REF(Catch::Generators::from_range(profile_fixtures));
    const auto result = compare(fixture(std::string("profiles/") + entry.first), {}, entry.second);
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(result.typed.song().charts.front().lane_layouts.front().layout.has_same_shape(result.legacy.charts.front().lane_layouts.front().layout));
}
TEST_CASE("Later scoped resources bind earlier channels and retain the chosen declaration owner", "[bms-program-projection]") {
    const auto choice = GENERATE(1, 2);
    const auto result = compare("#00011:01\n#00004:01\n#RANDOM 2\n#IF 1\n#WAV01 a.wav\n#BMP01 a.png\n#ELSE\n#WAV01 b.wav\n#BMP01 b.png\n#ENDIF\n#ENDRANDOM\n", {choice});
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(event_keys(result.typed.song()) == event_keys(result.legacy));
    const auto& note = result.typed.song().charts.front().measures.front().notes.front();
    CHECK(result.typed.owners(note.id.value).size() == 2);
}
TEST_CASE("Initial BPM declaration retains its program order against same-row channel BPM", "[bms-program-projection]") {
    const auto result = compare("#00003:78\n#BPM 180\n");
    const auto& typed = result.typed.song().charts.front().bpms;
    const auto& legacy = result.legacy.charts.front().bpms;
    REQUIRE(typed.size() == legacy.size());
    CHECK(typed.front().beats_per_minute == legacy.front().beats_per_minute);
    CHECK(typed.back().beats_per_minute == legacy.back().beats_per_minute);
}
TEST_CASE("Empty scoped bodies and common RANDOM content preserve legacy evaluation", "[bms-program-projection]") {
    const auto selected = GENERATE(0, 1, 2);
    const auto result = compare("#TITLE Empty\n#RANDOM 2\n#00011:01\n#IF 1\n#ELSE\n#ENDIF\n#ENDRANDOM\n", {selected});
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
    CHECK(result.typed.song().charts.front().measures.size() == 1);
}
TEST_CASE("Fixed selector settings follow global execution order across scopes", "[bms-program-projection]") {
    const auto result = compare("#SETRANDOM 1\n#RANDOM 2\n#IF 1\n#SETRANDOM 2\n#SETSWITCH 3\n#ENDIF\n#ENDRANDOM\n"
        "#RANDOM 2\n#IF 2\n#00011:01\n#ENDIF\n#ENDRANDOM\n#SWITCH 4\n#CASE 1\n#00111:01\n#DEF\n#00211:01\n#ENDSW\n");
    CHECK(note_keys(result.typed.song()) == note_keys(result.legacy));
}
TEST_CASE("Derived identities remain stable when inactive assets and events change the active subset", "[bms-program-projection]") {
    const auto input = prepared("#RANDOM 2\n#IF 1\n#00004:02\n#ELSE\n#00004:03\n#ENDIF\n#ENDRANDOM\n#00104:01\n");
    const auto control = selectors(input.program).front();
    const auto a = projected(input, {{{control, 1}}}), b = projected(input, {{{control, 2}}});
    const auto& left = a.song().charts.front();
    const auto& right = b.song().charts.front();
    CHECK(a.song().id == b.song().id);
    CHECK(left.id == right.id);
    CHECK(left.lane_layouts.front().id == right.lane_layouts.front().id);
    CHECK(left.bga_events.back().resource == right.bga_events.back().resource);
    CHECK(left.bga_events.back().id == right.bga_events.back().id);
}
TEST_CASE("Ambiguous catalog identities and missing or wrong assets refuse projection", "[bms-program-projection]") {
    auto input = prepared("#WAV01 x.wav\n#00011:01\n");
    const auto invalid = GENERATE(0, 1, 2, 3);
    if (invalid == 0) input.audio_assets.clear();
    if (invalid == 1) input.resources.push_back(input.resources.front());
    if (invalid == 2) input.resources.front().id = resource_id_t{input.measures.front().value};
    if (invalid == 3) {
        input.visual_assets.push_back({.id = input.audio_assets.front().id, .source_path = "wrong.png"});
        input.audio_assets.clear();
    }
    const auto result = bms_program_projection_t::from(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.front().code == failure_code_t::invalid_data);
}
