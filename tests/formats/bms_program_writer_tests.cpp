#include <wormedit/formats/bms_writer.h>
#include "../support/bms_program_fixture.h"
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using namespace wormedit;
using namespace wormedit::test::bms_program;
namespace {
auto written(const bms_prepared_program_t& input, bms_program_write_options_t options = {}) -> std::string {
    const auto result = write_bms_program(input, std::move(options));
    std::string messages;
    for (const auto& diagnostic : result.diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    REQUIRE(result.ok());
    REQUIRE(result.value);
    return *result.value;
}
auto evaluation(const bms_program_t& program, const std::vector<std::int64_t>& values) -> bms_program_evaluation_t {
    const auto ids = selectors(program);
    bms_program_evaluation_t result;
    for (std::size_t index = 0; index < values.size(); ++index)
        if (values[index]) result.selections.push_back({ids[index], values[index]});
    return result;
}
struct round_trip_t {
    bms_program_projection_t before;
    bms_program_projection_t after;
};
auto round_trip(std::string_view source, const std::vector<std::int64_t>& choices = {},
    std::string profile = "bme-7k-outside-scratch") -> round_trip_t {
    const auto input = prepared(source, profile);
    const auto output = written(input);
    const auto reimported = prepared(output, profile);
    return {projected(input, evaluation(input.program, choices)), projected(reimported, evaluation(reimported.program, choices))};
}
auto nodes_of(bms_prepared_program_t& input) -> std::vector<bms_program_node_t> {
    return {input.program.nodes().begin(), input.program.nodes().end()};
}
auto first_content(std::vector<bms_program_node_t>& nodes) -> bms_chart_content_t& {
    const auto found = std::ranges::find_if(nodes, [](const auto& node) { return std::holds_alternative<bms_chart_content_t>(node.value); });
    REQUIRE(found != nodes.end());
    return std::get<bms_chart_content_t>(found->value);
}
void replace_program(bms_prepared_program_t& input, std::vector<bms_program_node_t> nodes) {
    auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    input.program = std::move(*program);
}
}
TEST_CASE("Scoped BMS output round trips every bounded mixed control outcome", "[bms-program-writer]") {
    const auto random = GENERATE(0, 1, 2);
    const auto selection = GENERATE(0, 1, 2, 3);
    const auto nested = GENERATE(0, 1, 2);
    if (selection != 1 && nested != 0) return;
    const auto result = round_trip(fixture("control-flow-mixed.bms"), {random, selection, nested});
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
    CHECK(event_keys(result.before.song()) == event_keys(result.after.song()));
}
TEST_CASE("Scoped BMS output retains fixed selectors and explicit overrides", "[bms-program-writer]") {
    const auto random = GENERATE(0, 1, 2, 3);
    const auto selection = GENERATE(0, 1, 2, 9);
    const auto result = round_trip(fixture("control-flow-fixed-values.bms"), {random, selection});
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
}
TEST_CASE("Scoped BMS round trips definitions timing geometry and cross-scope endpoints", "[bms-program-writer]") {
    const auto choice = GENERATE(1, 2, 3);
    const auto result = round_trip(scoped_source, {choice});
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
    CHECK(event_keys(result.before.song()) == event_keys(result.after.song()));
    CHECK(bgm_keys(result.before.song()) == bgm_keys(result.after.song()));
    CHECK(length_keys(result.before.song().charts.front()) == length_keys(result.after.song().charts.front()));
    CHECK(result.before.song().metadata.title == result.after.song().metadata.title);
}
TEST_CASE("Ordinary scoped timing exports after typed edits without a flattening policy", "[bms-program-writer]") {
    auto input = prepared("#BPM 120\n#BPM01 180\n#STOP01 192\n#SCROLL01 -0.5\n#00003:78\n#00008:01\n#00009:01\n#000SC:01\n");
    auto nodes = nodes_of(input);
    std::get<bms_timing_definition_t>(first_content(nodes).items.front().value).value = {150, 1};
    replace_program(input, std::move(nodes));
    const auto output = written(input);
    CHECK(output.find("#BPM 150\n") != std::string::npos);
    const auto after = projected(prepared(output));
    CHECK(event_keys(projected(input).song()) == event_keys(after.song()));
}
TEST_CASE("Default scoped output ignores evaluation choices and includes inactive owners", "[bms-program-writer]") {
    const auto input = prepared(scoped_source);
    const auto a = written(input, {.evaluation = evaluation(input.program, {1})});
    const auto b = written(input, {.evaluation = evaluation(input.program, {2})});
    CHECK(a == b);
    CHECK(a.find("#WAV01 b.wav") != std::string::npos);
    CHECK(a.find("#TITLE C") != std::string::npos);
    CHECK(a.find("; split Chart") != std::string::npos);
}
TEST_CASE("Scoped output preserves Text and automatic IF grouping across Chart boundaries", "[bms-program-writer]") {
    const auto input = prepared("; root\r\n#UNKNOWN root\r\n#RANDOM 2\r\n#IF 1\r\n#TITLE A\r\n; between\r\n#TITLE B\r\n#ELSE\r\n#ENDIF\r\n#ENDRANDOM\r\n");
    const auto output = written(input);
    CHECK(output.find("; root\r\n#UNKNOWN root\r\n") != std::string::npos);
    CHECK(output.find("#TITLE A\n; between\r\n#TITLE B\n") != std::string::npos);
    const auto after = prepared(output);
    CHECK(after.program.nodes().size() == input.program.nodes().size());
}
TEST_CASE("Nested SKIP anchors retain execution position on reimport", "[bms-program-writer]") {
    const auto choice = GENERATE(1, 2);
    const auto result = round_trip("#WAV01 x.wav\n#SETSWITCH 1\n#SWITCH 2\n#CASE 1\n#RANDOM 2\n#IF 1\n; before SKIP\n#SKIP\n#00011:01\n#ENDIF\n#ENDRANDOM\n#00111:01\n#CASE 2\n#00211:01\n#ENDSW\n", {0, choice});
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
}
TEST_CASE("BGM padding and repeated line columns survive scoped output", "[bms-program-writer]") {
    const auto result = round_trip("#WAV01 x.wav\n#00001:0000\n#00001:00010000\n#00001:0100\n");
    CHECK(bgm_keys(result.before.song()) == bgm_keys(result.after.song()));
    CHECK(result.after.song().charts.front().bgm_source_lanes.size() == 3);
}
TEST_CASE("Flat BMS output retains inline whitespace and opaque statements after semantic edits",
          "[bms-program-writer][flat-bms]") {
    auto input = prepared("#TITLE Flat\r\n\r\n#BPM 120\r\n#WAV01 a.wav\r\n"
        "#00011:01\r\n\r\n; between notes\r\n#UNKNOWN keep\r\n#00112:01\r\n");
    auto nodes = nodes_of(input);
    auto& items = first_content(nodes).items;
    const auto note = std::ranges::find_if(items, [](const auto& item) {
        return std::holds_alternative<bms_note_content_t>(item.value);
    });
    REQUIRE(note != items.end());
    std::get<bms_note_content_t>(note->value).position.fraction = {1, 2};
    replace_program(input, std::move(nodes));
    const auto output = written(input);
    CHECK(output.find("#00011:0001\n\r\n; between notes\r\n#UNKNOWN keep\r\n#00112:01\n") != std::string::npos);
    CHECK(note_keys(projected(prepared(output)).song()) == note_keys(projected(input).song()));
}
TEST_CASE("Trailing empty measures survive without an invented declaration or autoplay column", "[bms-program-writer]") {
    const auto input = prepared("#00911:00\n");
    const auto output = written(input);
    const auto after = prepared(output);
    CHECK(after.measures.size() == 10);
    CHECK(after.resources.empty());
    CHECK(after.bgm_columns.empty());
    CHECK(projected(after).song().charts.front().bpms.empty());
}
TEST_CASE("Unchanged scoped backing passes through exactly after typed equivalence verification", "[bms-program-writer]") {
    const std::string source = "; comment\r\n#TITLE A\r\n\r\n#BPM 120\r\n; before notes\r\n#00011:010000\r\n";
    const auto imported = prepare_bms_program(bytes(source), {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(imported.value.prepared);
    const auto output = written(*imported.value.prepared, {.source = imported.value.encoding});
    CHECK(output == source);
}
TEST_CASE("A stale unchanged flag cannot hide typed edits or asset relinking", "[bms-program-writer]") {
    const auto edit = GENERATE(0, 1);
    const std::string source = "#TITLE Old\n#WAV01 old.wav\n#00011:01\n";
    auto imported = prepare_bms_program(bytes(source), {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(imported.value.prepared);
    auto& input = *imported.value.prepared;
    if (edit == 0) {
        auto nodes = nodes_of(input);
        std::get<bms_metadata_declaration_t>(first_content(nodes).items.front().value).value = "New";
        first_content(nodes).items.front().provenance.source_spelling = "#TITLE Old";
        replace_program(input, std::move(nodes));
    } else input.audio_assets.front().source_path = "new.wav";
    const auto output = written(input, {.source = imported.value.encoding});
    CHECK(output != source);
    CHECK(output.find(edit == 0 ? "#TITLE New" : "#WAV01 new.wav") != std::string::npos);
}
TEST_CASE("Explicit flattening uses the shared projection and reports its loss policy", "[bms-program-writer]") {
    const auto choice = GENERATE(1, 2, 3);
    const auto input = prepared(scoped_source);
    const auto result = write_bms_program(input, {.format = {.branch_policy = bms_branch_policy_t::flatten_active},
        .evaluation = evaluation(input.program, {choice}), .loss_policy = loss_policy_t::warn});
    REQUIRE(result.value);
    CHECK(result.diagnostics.front().code == bms_write_diagnostic_code_t::lossy_transform);
    const auto after = prepared(*result.value);
    CHECK(selectors(after.program).empty());
    CHECK(note_keys(projected(input, evaluation(input.program, {choice})).song()) == note_keys(projected(after).song()));
}
TEST_CASE("Flattened timing and BGM preserve the accepted scoped evaluation", "[bms-program-writer]") {
    const auto input = prepared(scoped_source);
    const auto choices = evaluation(input.program, {1});
    const auto output = written(input, {.format = {.branch_policy = bms_branch_policy_t::flatten_active}, .evaluation = choices, .loss_policy = loss_policy_t::allow});
    const auto after = projected(prepared(output));
    const auto before = projected(input, choices);
    CHECK(event_keys(before.song()) == event_keys(after.song()));
    CHECK(bgm_keys(before.song()) == bgm_keys(after.song()));
}
TEST_CASE("Flattening refuses implicit loss and unresolved or unreachable choices", "[bms-program-writer]") {
    const auto mode = GENERATE(0, 1, 2);
    const auto input = prepared(fixture("control-flow-mixed.bms"));
    bms_program_write_options_t options{.format = {.branch_policy = bms_branch_policy_t::flatten_active}};
    if (mode) options.loss_policy = loss_policy_t::warn;
    if (mode == 2) options.evaluation = evaluation(input.program, {1, 0, 1});
    const auto result = write_bms_program(input, options);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == (mode == 0 ? bms_write_diagnostic_code_t::lossy_transform : bms_write_diagnostic_code_t::unsupported_branch_policy));
}
TEST_CASE("Classic random flow exports under LR2 while unsupported dialect content refuses", "[bms-program-writer]") {
    const auto input = prepared("#TITLE ASCII\n#WAV01 x.wav\n#SETRANDOM 1\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    const auto output = written(input, {.format = {.profile = bms_export_profile_t::lr2_compatible}});
    CHECK(output.find("#RANDOM 2") != std::string::npos);
    CHECK(note_keys(projected(input).song()) == note_keys(projected(prepared(output)).song()));
}
TEST_CASE("Scoped LR2 refusals include inactive switches scroll and metadata", "[bms-program-writer]") {
    const auto source = GENERATE("#RANDOM 2\n#IF 1\n#SCROLL01 -1\n#ENDIF\n#ENDRANDOM\n", "#SWITCH 2\n#CASE 1\n#ENDSW\n", "#OFFSET 0.25\n");
    const auto result = write_bms_program(prepared(source), {.format = {.profile = bms_export_profile_t::lr2_compatible}});
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::lossy_transform);
}
TEST_CASE("Unrepresentable scoped values refuse without partial output", "[bms-program-writer]") {
    const auto mode = GENERATE(0, 1, 2, 3);
    auto input = prepared("#BPM 120\n#00003:78\n");
    auto nodes = nodes_of(input);
    auto& items = first_content(nodes).items;
    if (mode == 0) std::get<bms_timing_definition_t>(items.front().value).value = {1, 3};
    if (mode == 1) std::get<bms_direct_bpm_t>(items.back().value).value = {256, 1};
    if (mode == 2) std::get<bms_direct_bpm_t>(items.back().value).position.fraction = {1, 0};
    if (mode == 3) items.front().value = bms_measure_length_t{.measure = input.measures.front(), .length = {1, 3}};
    replace_program(input, std::move(nodes));
    const auto snapshot = input.program;
    const auto result = write_bms_program(input);
    CHECK_FALSE(result.value);
    CHECK(input.program == snapshot);
}
TEST_CASE("Scoped output caps prevent partial bytes and preserve authored values", "[bms-program-writer]") {
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    const auto input = prepared("#WAV01 x.wav\n\n; retained\n#00011:000001\n");
    bms_program_write_options_t options;
    switch (mode) {
    case 0: options.maximum_output_bytes = 4; break;
    case 1: options.maximum_lines = 1; break;
    case 2: options.format.max_slots_per_line = 2; break;
    case 3: options.program_limits.maximum_nodes = 1; break;
    case 4: options.program_limits.maximum_content_items = 1; break;
    case 5: options.program_limits.maximum_depth = 0; break;
    case 6: options.maximum_input_objects = 1; break;
    case 7: options.program_limits.maximum_text_bytes = 1; break;
    }
    const auto result = write_bms_program(input, options);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::resource_limit);
}
TEST_CASE("Known commands cannot hide as opaque Text or inject extra lines", "[bms-program-writer]") {
    const auto payload = GENERATE("#TITLE stale\n", "#BPM 120\n", "#00002:1\n", "#SETRANDOM 1\n");
    auto input = prepared("; replace\n");
    auto nodes = nodes_of(input);
    const auto text = std::ranges::find_if(nodes, [](const auto& node) { return std::holds_alternative<bms_text_t>(node.value); });
    std::get<bms_text_t>(text->value).bytes = payload;
    replace_program(input, std::move(nodes));
    const auto result = write_bms_program(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::invalid_document);
}
TEST_CASE("Empty programs and empty sibling scopes serialize without source backing", "[bms-program-writer]") {
    const auto source = GENERATE("", "#RANDOM 2\n#IF 1\n#ELSE\n#ENDIF\n#ENDRANDOM\n");
    const auto input = prepared(source);
    const auto after = prepared(written(input));
    CHECK(selectors(after.program).size() == selectors(input.program).size());
    CHECK(after.measures.size() == 1);
}
TEST_CASE("Scoped output round trips all built-in lane profile fixtures", "[bms-program-writer]") {
    const auto entry = GENERATE_REF(Catch::Generators::from_range(profile_fixtures));
    const auto result = round_trip(fixture(std::string("profiles/") + entry.first), {}, entry.second);
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
    CHECK(result.before.song().charts.front().lane_layouts.front().layout.has_same_shape(result.after.song().charts.front().lane_layouts.front().layout));
}
TEST_CASE("All note channel families retain their authored values", "[bms-program-writer]") {
    const auto result = round_trip("#WAV01 x.wav\n#00011:01\n#00031:01\n#00051:0101\n#000D1:ZZ\n");
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
}
TEST_CASE("Non-gameplay Text resources and repeated BGM retain semantic round trips", "[bms-program-writer]") {
    const auto name = GENERATE("legacy-long-note.bml", "repeated-bgm-ratio-stop-collision.bms", "non-gameplay-roundtrip.bme");
    const auto result = round_trip(fixture(name));
    CHECK(note_keys(result.before.song()) == note_keys(result.after.song()));
    CHECK(event_keys(result.before.song()) == event_keys(result.after.song()));
    CHECK(bgm_keys(result.before.song()) == bgm_keys(result.after.song()));
}
TEST_CASE("Legacy encoded Text and metadata share the established export codec", "[bms-program-writer]") {
    const std::string source = "; \x82\xa0\r\n#TITLE \x82\xa0\r\n#BPM 120\r\n";
    const auto imported = prepare_bms_program(bytes(source), {.encoding_override = text_encoding_t::cp932});
    REQUIRE(imported.value.prepared);
    const auto retained = written(*imported.value.prepared, {.format = {.convert_untouched_source_to_utf8 = false}, .source = imported.value.encoding});
    CHECK(retained == source);
    const auto modern = written(*imported.value.prepared);
    CHECK(modern.find("; \xe3\x81\x82\r\n") != std::string::npos);
    CHECK(modern.find("#TITLE \xe3\x81\x82\n") != std::string::npos);
}
TEST_CASE("Unrepresentable output codecs refuse complete scoped bytes", "[bms-program-writer]") {
    const auto input = prepared("\xef\xbb\xbf#TITLE \xe3\x81\x82\n");
    const auto result = write_bms_program(input, {.format = {.output_encoding = text_encoding_t::ascii}});
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::unrepresentable_text);
}
TEST_CASE("Edited values cannot inject semantic statements through metadata", "[bms-program-writer]") {
    auto input = prepared("#TITLE Original\n");
    auto nodes = nodes_of(input);
    std::get<bms_metadata_declaration_t>(first_content(nodes).items.front().value).value = "Edited\n#BPM 999";
    replace_program(input, std::move(nodes));
    const auto result = write_bms_program(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::unrepresentable_text);
}
TEST_CASE("Inactive unsupported values and missing resource owners refuse preservation", "[bms-program-writer]") {
    auto input = prepared("#RANDOM 2\n#IF 1\n#WAV01 x.wav\n#00011:01\n#ENDIF\n#ENDRANDOM\n");
    input.audio_assets.clear();
    const auto result = write_bms_program(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::unresolved_resource);
}
TEST_CASE("Mutated lane maps cannot change a tap into a timing channel on export", "[bms-program-writer]") {
    auto input = prepared("#00011:01\n");
    auto& lane = *input.lane_profile->layout.find(projected(input).song().charts.front().measures.front().notes.front().lane);
    lane.bms_channels.visible = "03";
    const auto result = write_bms_program(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::missing_profile);
}
TEST_CASE("Unchanged recovered fractional measures retain exact original backing", "[bms-program-writer]") {
    const std::string source = "#00002:0.3333333333333333\n";
    const auto imported = prepare_bms_program(bytes(source));
    REQUIRE(imported.value.prepared);
    const auto output = written(*imported.value.prepared, {.source = imported.value.encoding});
    CHECK(output == source);
    const auto normalized = write_bms_program(*imported.value.prepared);
    CHECK_FALSE(normalized.value);
    CHECK(normalized.diagnostics.back().code == bms_write_diagnostic_code_t::unrepresentable_number);
}
TEST_CASE("Independent channel lines do not require their combined subdivision grid", "[bms-program-writer]") {
    const auto input = prepared("#WAV01 x.wav\n#00011:000100\n#00011:0001000000\n");
    const auto output = written(input, {.format = {.max_slots_per_line = 5}});
    CHECK(note_keys(projected(input).song()) == note_keys(projected(prepared(output)).song()));
}
TEST_CASE("Flattening retains fixed choices without requiring explicit preview overrides", "[bms-program-writer]") {
    const auto input = prepared(fixture("control-flow-fixed-values.bms"));
    const auto output = written(input, {.format = {.branch_policy = bms_branch_policy_t::flatten_active}, .loss_policy = loss_policy_t::warn});
    CHECK(note_keys(projected(input).song()) == note_keys(projected(prepared(output)).song()));
}
TEST_CASE("Ambiguous BGM lines and inconsistent structural slots refuse serialization", "[bms-program-writer]") {
    const auto mode = GENERATE(0, 1, 2);
    auto input = prepared("#00001:01\n#00001:01\n");
    auto nodes = nodes_of(input);
    auto& content = first_content(nodes);
    if (mode == 0) input.bgm_columns.clear();
    if (mode == 1) std::get<bms_bgm_content_t>(content.items.back().value).column = std::get<bms_bgm_content_t>(content.items.front().value).column;
    if (mode == 2) std::get<bms_bgm_content_t>(content.items.front().value).structural_slot = 1;
    replace_program(input, std::move(nodes));
    const auto result = write_bms_program(input);
    CHECK_FALSE(result.value);
    CHECK(result.diagnostics.back().code == (mode == 2 ? bms_write_diagnostic_code_t::invalid_geometry : bms_write_diagnostic_code_t::invalid_document));
}
TEST_CASE("Exact source byte budgets apply to encoded output rather than its verification signature", "[bms-program-writer]") {
    const auto convert = GENERATE(false, true);
    const std::string source = "; \x82\xa0\n#TITLE \x82\xa0\n";
    const auto imported = prepare_bms_program(bytes(source), {.encoding_override = text_encoding_t::cp932});
    REQUIRE(imported.value.prepared);
    const auto result = write_bms_program(*imported.value.prepared,
        {.format = {.convert_untouched_source_to_utf8 = convert}, .source = imported.value.encoding, .maximum_output_bytes = source.size()});
    CHECK(result.value.has_value() == !convert);
    if (convert) CHECK(result.diagnostics.back().code == bms_write_diagnostic_code_t::resource_limit);
    else CHECK(*result.value == source);
}
