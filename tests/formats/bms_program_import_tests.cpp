#include <wormedit/formats/bms.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace wormedit;
namespace {
auto prepare(std::string_view source,
    bms_parse_options_t options = {.lane_profile_id = "bme-7k-outside-scratch"},
    bms_program_limits_t limits = {}) -> bms_program_import_outcome_t {
    return prepare_bms_program(std::as_bytes(std::span{source.data(), source.size()}), std::move(options), limits);
}
auto accepted(std::string_view source) -> bms_program_import_outcome_t {
    auto result = prepare(source);
    REQUIRE(result.ok());
    REQUIRE(result.value.prepared);
    return result;
}
auto fixture(std::string_view path) -> std::string {
    std::ifstream file(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / path, std::ios::binary);
    REQUIRE(file.is_open());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
template<class Value>
auto values(const bms_program_t& program) -> std::vector<const Value*> {
    std::vector<const Value*> result;
    for (const auto& node : program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        for (const auto& item : chart->items)
            if (const auto* value = std::get_if<Value>(&item.value)) result.push_back(value);
    }
    return result;
}
template<class Value>
auto nodes_of(const bms_program_t& program) -> std::vector<const bms_program_node_t*> {
    std::vector<const bms_program_node_t*> result;
    for (const auto& node : program.nodes())
        if (std::holds_alternative<Value>(node.value)) result.push_back(&node);
    return result;
}
constexpr auto scoped_source =
    "; before metadata\r\n#PLAYER 1\r\n#TITLE Root\r\n#BPM 120\r\n#WAV01 common.wav\r\n#BMP01 scene.png\r\n"
    "#00011:01\r\n#00051:01\r\n#RANDOM 2\r\n#IF 1\r\n"
    "#WAV01 a.wav\r\n#BPM01 150\r\n#STOP01 192\r\n#SCROLL01 -0.5\r\n"
    "#00008:01\r\n#00009:01\r\n#000SC:01\r\n#00002:1.5\r\n"
    "#00001:0100\r\n#00001:0001\r\n; retained comment\r\n#TITLE A\r\n#00111:01\r\n#00151:01\r\n"
    "#ELSEIF 2\r\n#WAV01 b.wav\r\n#BPM01 180\r\n#TITLE B\r\n#0010A:01\r\n#00251:01\r\n"
    "#ELSE\r\n#TITLE Otherwise\r\n#ENDIF\r\n#ENDRANDOM\r\n";
}
TEST_CASE("Scoped BMS import prepares an empty Root without activating ownership", "[bms-program-import]") {
    const auto parsed = prepare_bms_program({});
    REQUIRE(parsed.value.prepared);
    CHECK(parsed.value.prepared->program.nodes().size() == 2);
    CHECK(parsed.value.prepared->program.target_chart(parsed.value.prepared->program.root()));
    CHECK(parsed.value.prepared->measures.size() == 1);
}
TEST_CASE("Scoped BMS import decodes inactive definitions without choosing outcomes", "[bms-program-import]") {
    const auto imported = accepted(scoped_source);
    const auto& prepared = *imported.value.prepared;
    CHECK(values<bms_resource_declaration_t>(prepared.program).size() == 4);
    CHECK(values<bms_timing_definition_t>(prepared.program).size() == 5);
    CHECK(values<bms_timing_reference_t>(prepared.program).size() == 3);
    CHECK(prepared.audio_assets.size() == 3);
}
TEST_CASE("Scoped BMS import retains conditional ratios and independent hold endpoints", "[bms-program-import]") {
    const auto imported = accepted(scoped_source);
    const auto& prepared = *imported.value.prepared;
    const auto endpoints = values<bms_hold_endpoint_t>(prepared.program);
    REQUIRE(endpoints.size() == 3);
    CHECK(endpoints[0]->position.measure == prepared.measures[0]);
    CHECK(endpoints[2]->position.measure == prepared.measures[2]);
    CHECK(values<bms_measure_length_t>(prepared.program).front()->length == rational_t{3, 2});
}
TEST_CASE("Scoped BMS import preserves sibling alternatives and all four BGA lanes", "[bms-program-import]") {
    const auto imported = accepted("#BMP01 x.png\n#RANDOM 2\n#IF 1\n#00004:01\n#00006:01\n"
        "#ELSEIF 2\n#00007:01\n#ELSE\n#0000A:01\n#ENDIF\n#ENDRANDOM\n");
    const auto& program = imported.value.prepared->program;
    const auto events = values<bms_bga_content_t>(program);
    const std::array expected{visual_event_kind_t::background, visual_event_kind_t::poor,
        visual_event_kind_t::layer, visual_event_kind_t::layer2};
    CHECK(std::ranges::equal(events, expected, std::ranges::equal_to{}, &bms_bga_content_t::kind));
    CHECK(nodes_of<bms_if_scope_t>(program).size() == 1);
    CHECK(nodes_of<bms_else_if_scope_t>(program).size() == 1);
    CHECK(nodes_of<bms_else_scope_t>(program).size() == 1);
}
TEST_CASE("Scoped BMS import keeps Text between semantic Chart runs in byte order", "[bms-program-import]") {
    const auto imported = accepted("; first\r\n#UNKNOWN keep this\r\n#TITLE A\r\n; middle\r\n#TITLE B\r\n");
    const auto& program = imported.value.prepared->program;
    const auto* root = program.find(program.root());
    REQUIRE(root->children.size() == 4);
    CHECK(std::get<bms_text_t>(program.find(root->children[0])->value).bytes == "; first\r\n#UNKNOWN keep this\r\n");
    CHECK(std::holds_alternative<bms_chart_content_t>(program.find(root->children[1])->value));
    CHECK(std::get<bms_text_t>(program.find(root->children[2])->value).bytes == "; middle\r\n");
    CHECK(program.target_chart(program.root()) == root->children[1]);
}
TEST_CASE("Scoped BMS declarations keep duplicate slots while sharing asset storage", "[bms-program-import]") {
    const auto imported = accepted("#WAV01 same.wav\n#WAV01 same.wav\n#RANDOM 2\n#IF 1\n#WAV01 other.wav\n#ENDIF\n#ENDRANDOM\n");
    const auto& prepared = *imported.value.prepared;
    CHECK(values<bms_resource_declaration_t>(prepared.program).size() == 3);
    CHECK(prepared.resources.size() == 3);
    CHECK(prepared.audio_assets.size() == 2);
    CHECK(prepared.resources[0].asset == prepared.resources[1].asset);
}
TEST_CASE("Scoped BMS source positions remain exact when an outcome is not representable on the legacy grid", "[bms-program-import]") {
    const auto imported = accepted("#WAV01 x.wav\n#RANDOM 2\n#IF 1\n#00011:00010000000000\n#ENDIF\n#ENDRANDOM\n");
    const auto notes = values<bms_note_content_t>(imported.value.prepared->program);
    REQUIRE(notes.size() == 1);
    CHECK(notes.front()->position.fraction == rational_t{1, 7});
    CHECK(imported.value.prepared->measures.size() == 1);
}
TEST_CASE("Scoped BMS repeated BGM lines retain distinct columns and structural padding", "[bms-program-import]") {
    const auto imported = accepted("#WAV01 x.wav\n#RANDOM 2\n#IF 1\n#00001:0100\n#00001:0001\n#ENDIF\n#ENDRANDOM\n");
    const auto& prepared = *imported.value.prepared;
    const auto events = values<bms_bgm_content_t>(prepared.program);
    REQUIRE(events.size() == 4);
    CHECK(prepared.bgm_columns.size() == 2);
    CHECK(events[0]->column != events[2]->column);
    CHECK_FALSE(events[1]->wav);
    CHECK(events[3]->position.fraction == rational_t{1, 2});
}
TEST_CASE("Scoped BMS fixed selectors remain separate settings nodes", "[bms-program-import]") {
    const auto imported = accepted(fixture("bms/control-flow-fixed-values.bms"));
    const auto& program = imported.value.prepared->program;
    const auto fixed = nodes_of<bms_set_random_t>(program);
    REQUIRE(fixed.size() == 1);
    CHECK(fixed.front()->children.empty());
    CHECK(std::get<bms_set_random_t>(fixed.front()->value).value == 2);
    CHECK(nodes_of<bms_set_switch_t>(program).size() == 1);
    CHECK(values<bms_note_content_t>(program).size() == 5);
}
TEST_CASE("Scoped BMS SKIP retains its nested execution anchor on the owning case", "[bms-program-import]") {
    const auto imported = accepted("#WAV01 x.wav\n#SWITCH 2\n#CASE 1\n#RANDOM 2\n#IF 1\n"
        "#00011:01\n#SKIP\n#00111:01\n#ENDIF\n#ENDRANDOM\n#CASE 2\n#DEF\n#ENDSW\n");
    const auto& program = imported.value.prepared->program;
    const auto cases = nodes_of<bms_case_scope_t>(program);
    const auto& skips = std::get<bms_case_scope_t>(cases.front()->value).skips;
    REQUIRE(skips.size() == 1);
    CHECK(std::holds_alternative<bms_if_scope_t>(program.find(skips.front().scope)->value));
    REQUIRE(skips.front().after_child);
    CHECK(std::get<bms_chart_content_t>(program.find(*skips.front().after_child)->value).items.size() == 1);
    CHECK(values<bms_note_content_t>(program).size() == 2);
}
TEST_CASE("Scoped BMS import is independent of requested legacy preview selections", "[bms-program-import]") {
    auto options = bms_parse_options_t{.lane_profile_id = "bme-7k-outside-scratch"};
    const auto first = prepare(scoped_source, options);
    options.branch_choices = {{8, 1}, {999, 2}};
    const auto second = prepare(scoped_source, options);
    REQUIRE(first.value.prepared);
    REQUIRE(second.value.prepared);
    CHECK(first.value.prepared->program == second.value.prepared->program);
    CHECK(first.value.prepared->measures == second.value.prepared->measures);
}
TEST_CASE("Scoped BMS malformed known commands keep backing and reject partial preparation", "[bms-program-import]") {
    std::string source;
    SECTION("invalid control setting") { source = "#RANDOM nope\n#ENDRANDOM\n"; }
    SECTION("unclosed scope") { source = "#RANDOM 2\n#IF 1\n#TITLE A\n"; }
    SECTION("orphan marker") { source = "#ELSE\n#TITLE A\n"; }
    SECTION("empty WAV path") { source = "#WAV01\n"; }
    SECTION("invalid WAV identifier") { source = "#WAV?1 x.wav\n"; }
    SECTION("invalid timing identifier") { source = "#BPM?1 120\n"; }
    SECTION("invalid inactive timing") { source = "#RANDOM 2\n#IF 1\n#BPM01 nope\n#ENDIF\n#ENDRANDOM\n"; }
    SECTION("malformed payload") { source = "#00011:010\n"; }
    SECTION("invalid direct BPM") { source = "#00003:GG\n"; }
    const auto imported = prepare(source);
    CHECK_FALSE(imported.value.prepared);
    REQUIRE(imported.value.encoding.original_bytes);
    CHECK(std::ranges::equal(*imported.value.encoding.original_bytes, std::as_bytes(std::span{source.data(), source.size()})));
    CHECK_FALSE(imported.diagnostics.empty());
}
TEST_CASE("Scoped BMS ambiguous inactive lanes request a profile without converting notes to Text", "[bms-program-import]") {
    const auto imported = prepare("#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#ENDRANDOM\n", {});
    CHECK_FALSE(imported.value.prepared);
    CHECK(std::ranges::any_of(imported.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == bms_parse_diagnostic_code_t::unsupported_channel;
    }));
    CHECK(imported.value.encoding.original_bytes);
}
TEST_CASE("Scoped BMS empty scopes each gain a first Chart without synthetic arms", "[bms-program-import]") {
    const auto imported = accepted("#RANDOM 99\n; inside\n#IF 1\n#ENDIF\n#ENDRANDOM\n");
    const auto& program = imported.value.prepared->program;
    CHECK(nodes_of<bms_random_scope_t>(program).size() == 1);
    CHECK(nodes_of<bms_if_scope_t>(program).size() == 1);
    CHECK(nodes_of<bms_chart_content_t>(program).size() == 3);
    CHECK(program.target_chart(nodes_of<bms_random_scope_t>(program).front()->id));
}
TEST_CASE("Scoped BMS import enforces preparation limits without publishing partial assets", "[bms-program-import]") {
    auto limits = bms_program_limits_t{};
    SECTION("nodes") { limits.maximum_nodes = 1; }
    SECTION("depth") { limits.maximum_depth = 1; }
    SECTION("content") { limits.maximum_content_items = 0; }
    SECTION("Text") { limits.maximum_text_bytes = 1; }
    const auto imported = prepare("; comment\n#WAV01 x.wav\n#TITLE A\n", {.lane_profile_id = "bme-7k-outside-scratch"}, limits);
    CHECK_FALSE(imported.value.prepared);
    CHECK_FALSE(imported.diagnostics.empty());
}
TEST_CASE("Scoped BMS import retains encoded Text bytes and exact metadata values", "[bms-program-import]") {
    const std::string source = "; caf\xe9\r\n#TITLE caf\xe9\r\n#OFFSET -1.25\r\n";
    const auto imported = prepare(source, {.encoding_override = text_encoding_t::windows_1252});
    REQUIRE(imported.value.prepared);
    const auto& program = imported.value.prepared->program;
    CHECK(std::get<bms_text_t>(nodes_of<bms_text_t>(program).front()->value).bytes == "; caf\xe9\r\n");
    CHECK(imported.value.encoding.detected == text_encoding_t::windows_1252);
    CHECK(std::get<std::string>(values<bms_metadata_declaration_t>(program).front()->value) == "caf\xc3\xa9");
    CHECK(std::get<signed_rational_t>(values<bms_metadata_declaration_t>(program).back()->value) == signed_rational_t{-5, 4});
}
TEST_CASE("Scoped BMS encoding and source limits retain diagnostics without a graph", "[bms-program-import]") {
    auto options = bms_parse_options_t{};
    std::string source = "#TITLE A\n";
    auto expected = bms_parse_diagnostic_code_t::malformed_line;
    SECTION("ambiguous encoding") { source = "#TITLE caf\xe9\n"; expected = bms_parse_diagnostic_code_t::invalid_encoding; }
    SECTION("source bound") { options.max_source_bytes = 2; }
    SECTION("line bound") { options.max_line_bytes = 2; }
    SECTION("line count") { options.max_lines = 0; }
    SECTION("nesting bound") {
        options.max_nesting_depth = 0;
        source = "#RANDOM 2\n#ENDRANDOM\n";
        expected = bms_parse_diagnostic_code_t::malformed_control;
    }
    const auto imported = prepare(source, options);
    CHECK_FALSE(imported.value.prepared);
    CHECK(std::ranges::any_of(imported.diagnostics, [expected](const auto& diagnostic) { return diagnostic.code == expected; }));
}
TEST_CASE("Scoped BMS channel and control fixtures prepare all retained alternatives", "[bms-program-import]") {
    std::string path;
    SECTION("nested control flow") { path = "bms/control-flow-mixed.bms"; }
    SECTION("fixed selectors") { path = "bms/control-flow-fixed-values.bms"; }
    SECTION("long note endpoints") { path = "bms/legacy-long-note.bml"; }
    SECTION("repeated columns and timing") { path = "bms/repeated-bgm-ratio-stop-collision.bms"; }
    const auto imported = prepare(fixture(path));
    REQUIRE(imported.value.prepared);
    CHECK(imported.ok());
}

TEST_CASE("Scoped BMS slot provenance retains one source backing without copying whole lines", "[bms-program-import]") {
    std::string source = "#WAV01 x.wav\n#00011:";
    for (int i = 0; i < 128; ++i) source += "01";
    source += '\n';
    const auto imported = accepted(source);
    const auto& program = imported.value.prepared->program;
    std::size_t spelling_bytes{};
    for (const auto& node : program.nodes()) {
        if (const auto* chart = std::get_if<bms_chart_content_t>(&node.value))
            for (const auto& item : chart->items) spelling_bytes += item.provenance.source_spelling.size();
    }
    CHECK(values<bms_note_content_t>(program).size() == 128);
    CHECK(spelling_bytes <= source.size());
    CHECK(imported.value.encoding.original_bytes->size() == source.size());
}
