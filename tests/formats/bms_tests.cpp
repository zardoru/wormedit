#include <wormedit/formats/bms.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto read_binary(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto character : source) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

auto parse(std::string_view source, wormedit::bms_parse_options_t options = {})
    -> wormedit::bms_parse_result_t {
    return wormedit::parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                     options);
}

auto fixture(std::string_view relative) -> wormedit::bms_parse_result_t {
    const auto bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / relative);
    return wormedit::parse_bms_bytes(bytes);
}

auto find_kind(const wormedit::bms_control_node_t& node, wormedit::bms_control_kind_t kind)
    -> std::vector<const wormedit::bms_control_node_t*> {
    std::vector<const wormedit::bms_control_node_t*> matches;
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& current) -> void {
        if (current.kind == kind) {
            matches.push_back(&current);
        }
        for (const auto& child : current.children) {
            self(self, child);
        }
    };
    visit(visit, node);
    return matches;
}

auto source_lines(const wormedit::bms_control_node_t& node) -> std::vector<std::string> {
    std::vector<std::string> lines;
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& current) -> void {
        if (current.kind != wormedit::bms_control_kind_t::root) {
            lines.push_back(current.original_text);
        }
        if (current.close_text.has_value()) {
            lines.push_back(*current.close_text);
        }
        for (const auto& child : current.children) {
            self(self, child);
        }
    };
    visit(visit, node);
    return lines;
}

auto physical_source_spans(const wormedit::bms_control_node_t& node)
    -> std::vector<wormedit::bms_source_span_t> {
    std::vector<wormedit::bms_source_span_t> spans;
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& current) -> void {
        if (current.kind != wormedit::bms_control_kind_t::root &&
            current.kind != wormedit::bms_control_kind_t::if_group) {
            spans.push_back(current.source_span);
        }
        if (current.close_source_span.has_value()) {
            spans.push_back(*current.close_source_span);
        }
        for (const auto& child : current.children) {
            self(self, child);
        }
    };
    visit(visit, node);
    std::ranges::sort(spans, {}, &wormedit::bms_source_span_t::byte_begin);
    return spans;
}

} // namespace

TEST_CASE("BMS reader imports metadata, source bytes, measures and resource declarations",
          "[unit][format][bms]") {
    auto result = fixture("bms/repeated-bgm-ratio-stop-collision.bms");
    REQUIRE(result.song.has_value());
    CHECK_FALSE(result.has_errors());
    CHECK(result.encoding.detected == wormedit::text_encoding_t::ascii);
    CHECK(result.encoding.unchanged_since_import);
    REQUIRE(result.encoding.original_bytes);
    CHECK_FALSE(result.encoding.original_bytes->empty());

    const auto& song = *result.song;
    CHECK(song.metadata.title == "Fixture: layers, ratio, timing collision");
    CHECK(song.metadata.artist == "Wormedit fixture authors");
    REQUIRE(song.metadata.format_groups.size() == 1);
    CHECK(song.metadata.format_groups[0].encoding.original_bytes == result.encoding.original_bytes);
    REQUIRE(song.charts.size() == 1);
    const auto& chart = song.charts.front();
    REQUIRE(chart.metadata.format_groups.size() == 1);
    CHECK(chart.metadata.format_groups[0].encoding.original_bytes == result.encoding.original_bytes);
    REQUIRE(chart.bms_control_tree.has_value());
    REQUIRE(chart.measures.size() == 2);
    CHECK((chart.measures[0].definition.length == wormedit::rational_t{3, 4}));
    CHECK((chart.measures[1].definition.length == wormedit::rational_t{5, 4}));
    CHECK(song.audio_assets.size() == 4);
    CHECK(song.resources.size() == 4);
    CHECK(song.resources[0].name == "bgm-a.wav");
    CHECK(song.audio_assets[0].source_path == std::filesystem::path("bgm-a.wav"));
    CHECK(song.resources[0].target_identities[0].identifier == "01");
    REQUIRE(chart.bgm_source_lanes.size() == 3);
    REQUIRE(chart.measures[0].bgm_events.size() == 10);
    REQUIRE(chart.measures[1].bgm_events.size() == 2);
    CHECK(chart.measures[0].bgm_events[0].sound == song.resources[0].id);
    CHECK_FALSE(chart.measures[0].bgm_events[1].sound.has_value());
    CHECK_FALSE(chart.measures[0].bgm_events[2].sound.has_value());
    CHECK(chart.measures[0].bgm_events[1].structural_slot == 1);
    CHECK(chart.measures[0].bgm_events[5].structural_slot == 0);
    CHECK(chart.measures[0].bgm_events[5].source_lane !=
          chart.measures[0].bgm_events[0].source_lane);
    CHECK(chart.measures[0].bgm_events[5].sound.has_value() == false);
    REQUIRE(chart.bpms.size() == 3);
    CHECK((chart.bpms[0].beats_per_minute == wormedit::signed_rational_t{120, 1}));
    CHECK((chart.bpms[1].beats_per_minute == wormedit::signed_rational_t{120, 1}));
    CHECK((chart.bpms[2].beats_per_minute == wormedit::signed_rational_t{180, 1}));
    REQUIRE(chart.pauses.size() == 1);
    CHECK((chart.pauses[0].duration == wormedit::signed_rational_t{48, 1}));
    CHECK(chart.pauses[0].source_unit == wormedit::pause_unit_t::bms_stop_ticks);
    CHECK(chart.measures[0].notes.empty());

    const auto raw = source_lines(*chart.bms_control_tree);
    CHECK(std::ranges::find(raw, "#00001:0100000200") != raw.end());
    CHECK(std::ranges::find(raw, "#00001:0000030000") != raw.end());
    const auto ratios =
        find_kind(*chart.bms_control_tree, wormedit::bms_control_kind_t::raw_statement);
    const auto ratio_line = std::ranges::find_if(
        ratios, [](const auto* node) { return node->original_text == "#00002:0.75"; });
    REQUIRE(ratio_line != ratios.end());
    CHECK((*ratio_line)->command_token == "00002");
    CHECK((*ratio_line)->raw_value == "0.75");
    CHECK((*ratio_line)->source_order == 10);
    CHECK((*ratio_line)->source_span.byte_end > (*ratio_line)->source_span.byte_begin);
    CHECK((*ratio_line)->line_ending == wormedit::bms_line_ending_t::lf);
    CHECK(wormedit::validate_document(song).empty());
}

TEST_CASE("BMS channel materialization respects explicit profiles and exact grids",
          "[unit][format][bms][events]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#BPM 120\n#WAV01 key.wav\n#00011:0100\n#00031:0001\n#000D1:0100\n";
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    auto selected = parse(source, options);
    REQUIRE(selected.song.has_value());
    REQUIRE(selected.song->charts.size() == 1);
    const auto& chart = selected.song->charts.front();
    REQUIRE(chart.lane_layouts.size() == 1);
    REQUIRE(chart.measures.front().notes.size() == 3);
    CHECK(chart.measures.front().notes[0].kind == wormedit::note_kind_t::tap);
    CHECK(chart.measures.front().notes[0].sound == selected.song->resources.front().id);
    CHECK(chart.measures.front().notes[1].kind == wormedit::note_kind_t::invisible);
    CHECK(chart.measures.front().notes[1].position.row == 480);
    CHECK(chart.measures.front().notes[2].kind == wormedit::note_kind_t::mine);
    CHECK_FALSE(chart.measures.front().notes[2].sound.has_value());
    CHECK_FALSE(selected.has_errors());

    auto seven_slots = std::string{"#BPM 120\n"};
    for (unsigned int index = 1; index <= 7; ++index) {
        const auto identifier = index < 10 ? "0" + std::to_string(index)
                                           : std::to_string(index);
        seven_slots += "#WAV" + identifier + " sample-" + identifier + ".wav\n";
    }
    seven_slots += "#00001:01020304050607\n";
    auto subdivided = parse(seven_slots);
    REQUIRE(subdivided.song.has_value());
    const auto& subdivided_chart = subdivided.song->charts.front();
    CHECK(subdivided_chart.divisions_per_standard_measure == 6720);
    REQUIRE(subdivided_chart.measures.front().bgm_events.size() == 7);
    for (std::size_t slot = 0; slot < 7; ++slot) {
        CHECK(subdivided_chart.measures.front().bgm_events[slot].position.row == slot * 960);
    }

    auto denominator_only = parse("#00002:1/997\n");
    REQUIRE(denominator_only.song.has_value());
    CHECK(denominator_only.song->charts.front().divisions_per_standard_measure == 957120);
    CHECK((denominator_only.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 997}));

    wormedit::bms_parse_options_t bounded;
    bounded.max_chart_resolution = 960;
    auto capped = parse("#00002:1/997\n", bounded);
    REQUIRE(capped.song.has_value());
    CHECK(capped.song->charts.front().divisions_per_standard_measure == 960);
    CHECK((capped.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 1}));
    CHECK(std::ranges::any_of(capped.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.message.find("remains raw") != std::string::npos;
    }));

    auto capped_subdivision = parse(seven_slots, bounded);
    REQUIRE(capped_subdivision.song.has_value());
    CHECK(capped_subdivision.song->charts.front().divisions_per_standard_measure == 960);
    CHECK(capped_subdivision.song->charts.front().bgm_source_lanes.empty());
    CHECK(capped_subdivision.song->charts.front().measures.front().bgm_events.empty());
    CHECK(std::ranges::any_of(capped_subdivision.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.message.find("channel subdivisions") != std::string::npos;
    }));
}

TEST_CASE("BMS notes need a compatible selected profile and conditional payloads stay raw",
          "[unit][format][bms][events]") {
    constexpr std::string_view playable = "#WAV01 key.wav\n#00011:0100\n";
    auto unselected = parse(playable);
    REQUIRE(unselected.song.has_value());
    CHECK(unselected.song->charts.front().measures.front().notes.empty());
    CHECK(std::ranges::any_of(unselected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unsupported_channel;
    }));

    wormedit::bms_parse_options_t incompatible_options;
    incompatible_options.lane_profile_id = "pms-standard-5button";
    auto incompatible = parse(playable, incompatible_options);
    REQUIRE(incompatible.song.has_value());
    CHECK(incompatible.song->charts.front().measures.front().notes.empty());
    CHECK(std::ranges::any_of(incompatible.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unsupported_channel;
    }));

    auto conditional = parse("#RANDOM 1\n#00001:01\n#ENDRANDOM\n");
    REQUIRE(conditional.song.has_value());
    CHECK(conditional.song->charts.front().bgm_source_lanes.empty());
    CHECK(conditional.song->charts.front().measures.front().bgm_events.empty());
    const auto& tree = *conditional.song->charts.front().bms_control_tree;
    const auto statements = find_kind(tree, wormedit::bms_control_kind_t::raw_statement);
    CHECK(std::ranges::any_of(statements, [](const auto* node) {
        return node->original_text == "#00001:01";
    }));
}

TEST_CASE("BMS unresolved channel references retain unbound source slots",
          "[unit][format][bms][events]") {
    auto result = parse("#BPM 120\n#00001:01\n#00003:7Z\n#00008:02\n#00009:03\n");
    REQUIRE(result.song.has_value());
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.front().bgm_events.size() == 1);
    CHECK_FALSE(chart.measures.front().bgm_events.front().sound.has_value());
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unresolved_reference;
    }));
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_number;
    }));
    CHECK(chart.bpms.size() == 1);
    CHECK(chart.pauses.empty());
}

TEST_CASE("BMS reader grows measures through the largest channel index", "[unit][format][bms]") {
    auto result = fixture("bms/custom-channel.bme");
    REQUIRE(result.song.has_value());
    REQUIRE(result.song->charts.size() == 1);
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.size() == 1);
    CHECK((chart.measures[0].definition.length == wormedit::rational_t{1, 1}));

    auto high_measure = parse("#TITLE high\n#99911:01\n");
    REQUIRE(high_measure.song.has_value());
    REQUIRE(high_measure.song->charts[0].measures.size() == 1000);
    CHECK((high_measure.song->charts[0].measures[999].definition.length ==
           wormedit::rational_t{1, 1}));
}

TEST_CASE("BMS control flow is retained as nested source nodes with closing provenance",
          "[unit][format][bms][control]") {
    auto result = fixture("bms/control-flow-mixed.bms");
    REQUIRE(result.song.has_value());
    REQUIRE(result.song->charts[0].bms_control_tree.has_value());
    const auto& root = *result.song->charts[0].bms_control_tree;
    const auto randoms = find_kind(root, wormedit::bms_control_kind_t::random);
    REQUIRE(randoms.size() == 2);
    CHECK(randoms[0]->value == 2);
    CHECK(randoms[0]->close_text == "#ENDRANDOM");
    CHECK(randoms[0]->close_source_order.has_value());
    CHECK(randoms[0]->close_line_ending == wormedit::bms_line_ending_t::lf);

    const auto groups = find_kind(root, wormedit::bms_control_kind_t::if_group);
    REQUIRE(groups.size() == 2);
    const auto branches = find_kind(root, wormedit::bms_control_kind_t::if_branch);
    REQUIRE(branches.size() == 2);
    CHECK(branches[0]->value == 1);
    const auto else_if = find_kind(root, wormedit::bms_control_kind_t::else_if_branch);
    REQUIRE(else_if.size() == 1);
    CHECK(else_if[0]->value == 2);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::else_branch).size() == 1);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::switch_block).size() == 1);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::case_branch).size() == 2);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::default_branch).size() == 1);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::skip).size() == 1);

    auto fixed = fixture("bms/control-flow-fixed-values.bms");
    REQUIRE(fixed.song.has_value());
    const auto& fixed_root = *fixed.song->charts[0].bms_control_tree;
    REQUIRE(find_kind(fixed_root, wormedit::bms_control_kind_t::set_random).size() == 1);
    REQUIRE(find_kind(fixed_root, wormedit::bms_control_kind_t::set_switch).size() == 1);

    const auto source_bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) /
                                          "bms/control-flow-mixed.bms");
    const auto spans = physical_source_spans(root);
    REQUIRE_FALSE(spans.empty());
    std::size_t cursor{};
    for (const auto span : spans) {
        CHECK(span.byte_begin == cursor);
        CHECK(span.byte_end > span.byte_begin);
        cursor = span.byte_end;
    }
    CHECK(cursor == source_bytes.size());
}

TEST_CASE("BMS reader preserves malformed and orphan source with located diagnostics",
          "[unit][format][bms][recovery]") {
    auto malformed = fixture("bms/negative-orphan-control.bms");
    REQUIRE(malformed.song.has_value());
    CHECK(std::ranges::any_of(malformed.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::malformed_control &&
               diagnostic.line > 0 && diagnostic.source_span.has_value();
    }));
    const auto& malformed_root = *malformed.song->charts[0].bms_control_tree;
    const auto malformed_source = source_lines(malformed_root);
    CHECK(std::ranges::find(malformed_source, "#ENDRANDOM") != malformed_source.end());

    constexpr std::string_view source = "#TITLE recover\r\nnot a command\n#00002:bad\r";
    auto recovered = parse(source);
    REQUIRE(recovered.song.has_value());
    CHECK(recovered.has_errors());
    CHECK_FALSE(std::ranges::any_of(recovered.diagnostics, [](const auto& diagnostic) {
        return diagnostic.line == 2;
    }));
    CHECK(std::ranges::any_of(recovered.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.line == 3;
    }));
    const auto& root = *recovered.song->charts[0].bms_control_tree;
    const auto raw = find_kind(root, wormedit::bms_control_kind_t::raw_statement);
    const auto retained = std::ranges::find_if(
        raw, [](const auto* node) { return node->original_text == "not a command"; });
    REQUIRE(retained != raw.end());
    CHECK((*retained)->line_ending == wormedit::bms_line_ending_t::lf);
    CHECK((*retained)->source_span.byte_end - (*retained)->source_span.byte_begin == 14);
    const auto endings = find_kind(root, wormedit::bms_control_kind_t::raw_statement);
    CHECK(std::ranges::any_of(endings, [](const auto* node) {
        return node->original_text == "#TITLE recover" &&
               node->line_ending == wormedit::bms_line_ending_t::crlf;
    }));

    auto odd_pairs = fixture("bms/negative-malformed-pairs.bms");
    REQUIRE(odd_pairs.song.has_value());
    CHECK(std::ranges::any_of(odd_pairs.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::malformed_line &&
               diagnostic.message.find("odd number") != std::string::npos;
    }));
}

TEST_CASE("BMS conditional declarations stay raw and are not flattened",
          "[unit][format][bms][control]") {
    constexpr std::string_view source =
        "#TITLE root\n#RANDOM 2\n#IF 1\n#TITLE branch\n#WAV01 branch.wav\n"
        "#00002:0.75\n#ENDIF\n#ENDRANDOM\n#00011:01\n";
    auto result = parse(source);
    REQUIRE(result.song.has_value());
    CHECK(result.song->metadata.title == "root");
    CHECK(result.song->resources.empty());
    REQUIRE(result.song->charts[0].measures.size() == 1);
    CHECK((result.song->charts[0].measures[0].definition.length == wormedit::rational_t{1, 1}));
    CHECK(std::ranges::count_if(result.diagnostics, [](const auto& diagnostic) {
              return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
                     diagnostic.message.find("remains raw") != std::string::npos;
          }) == 4);
}

TEST_CASE("BMS parser source, line and nesting limits are configurable",
          "[unit][format][bms][limits]") {
    auto source_limited = parse("#TITLE a\n", {.max_source_bytes = 3});
    CHECK_FALSE(source_limited.song.has_value());
    CHECK_FALSE(source_limited.encoding.original_bytes);

    auto line_limited = parse("#TITLE too long\n", {.max_line_bytes = 4});
    CHECK_FALSE(line_limited.song.has_value());
    CHECK(line_limited.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::malformed_line);

    auto nesting_limited = parse("#RANDOM 1\n#RANDOM 2\n#ENDRANDOM\n#ENDRANDOM\n",
                                 {.max_nesting_depth = 1});
    REQUIRE(nesting_limited.song.has_value());
    CHECK(std::ranges::any_of(nesting_limited.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("depth limit") != std::string::npos;
    }));

    auto lines_limited = parse("\n\n", {.max_lines = 1});
    CHECK_FALSE(lines_limited.song.has_value());
    REQUIRE_FALSE(lines_limited.diagnostics.empty());
    CHECK(lines_limited.diagnostics[0].message.find("physical line limit") != std::string::npos);
}

TEST_CASE("BMS parser retains malformed terminal branch ordering as raw source",
          "[unit][format][bms][control][recovery]") {
    constexpr std::string_view source =
        "#IF 1\n#ELSE\n#ELSE\n#ELIF 2\n#ENDIF\n"
        "#SWITCH 2\n#DEF\n#DEF\n#CASE 1\n#ENDSW\n";
    auto result = parse(source);
    REQUIRE(result.song.has_value());
    const auto& root = *result.song->charts[0].bms_control_tree;
    const auto raw = source_lines(root);
    CHECK(std::ranges::count(raw, "#ELSE") == 2);
    CHECK(std::ranges::find(raw, "#ELIF 2") != raw.end());
    CHECK(std::ranges::count(raw, "#DEF") == 2);
    CHECK(std::ranges::find(raw, "#CASE 1") != raw.end());
    CHECK(std::ranges::count_if(result.diagnostics, [](const auto& diagnostic) {
              return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::malformed_control;
          }) == 4);
}

TEST_CASE("BMS reader handles ASCII, BOM UTF-8, and caller-selected legacy codecs",
          "[unit][format][bms][encoding]") {
    auto ascii = fixture("bms/encoding/ascii-ambiguous.bms");
    REQUIRE(ascii.song.has_value());
    CHECK(ascii.encoding.detected == wormedit::text_encoding_t::ascii);
    CHECK_FALSE(ascii.encoding.user_override.has_value());

    auto utf8 = fixture("bms/encoding/utf8-bom.bms");
    REQUIRE(utf8.song.has_value());
    CHECK(utf8.encoding.detected == wormedit::text_encoding_t::utf8);
    CHECK(utf8.song->metadata.title == "UTF-8 BOM / 雪");
    REQUIRE(utf8.encoding.original_bytes);
    CHECK((*utf8.encoding.original_bytes)[0] == std::byte{0xef});

    const auto parse_with = [](std::string_view name, wormedit::text_encoding_t encoding) {
        const auto bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / name);
        return wormedit::parse_bms_bytes(bytes, {.encoding_override = encoding});
    };
    auto cp932 = parse_with("bms/encoding/cp932-shift-jis.bms", wormedit::text_encoding_t::cp932);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::cp932)) {
        REQUIRE(cp932.song.has_value());
        CHECK(cp932.song->metadata.title == "日本語譜面");
        CHECK(cp932.song->metadata.artist == "音楽家");
        CHECK(cp932.encoding.detected == wormedit::text_encoding_t::cp932);
        REQUIRE(cp932.encoding.original_bytes);
        CHECK_FALSE(cp932.encoding.original_bytes->empty());
    } else {
        CHECK_FALSE(cp932.song.has_value());
        REQUIRE_FALSE(cp932.diagnostics.empty());
        CHECK(cp932.diagnostics[0].code ==
              wormedit::bms_parse_diagnostic_code_t::unsupported_encoding);
    }

    auto euc_kr = parse_with("bms/encoding/euc-kr.bms", wormedit::text_encoding_t::euc_kr);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::euc_kr)) {
        REQUIRE(euc_kr.song.has_value());
        CHECK(euc_kr.song->metadata.title == "한국어");
    } else {
        CHECK_FALSE(euc_kr.song.has_value());
    }
    auto cp949 = parse_with("bms/encoding/cp949-extension.bms", wormedit::text_encoding_t::cp949);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::cp949)) {
        REQUIRE(cp949.song.has_value());
        CHECK(cp949.song->metadata.title == "뷁");
    } else {
        CHECK_FALSE(cp949.song.has_value());
    }
    auto windows =
        parse_with("bms/encoding/windows-1252.bms", wormedit::text_encoding_t::windows_1252);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::windows_1252)) {
        REQUIRE(windows.song.has_value());
        CHECK(windows.song->metadata.title == "café déjà");
        CHECK(windows.song->metadata.artist == "Björk");
    } else {
        CHECK_FALSE(windows.song.has_value());
    }

    const auto ambiguous = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) /
                                       "bms/encoding/cp932-shift-jis.bms");
    auto no_override = wormedit::parse_bms_bytes(ambiguous);
    CHECK_FALSE(no_override.song.has_value());
    REQUIRE(no_override.encoding.original_bytes);
    CHECK(*no_override.encoding.original_bytes == ambiguous);
    CHECK(no_override.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::invalid_encoding);
}

TEST_CASE("Malformed BOM UTF-8 and explicit legacy sequences fail without fallback",
          "[unit][format][bms][encoding]") {
    auto invalid_utf8 = fixture("bms/encoding/invalid-utf8-after-bom.bms");
    CHECK_FALSE(invalid_utf8.song.has_value());
    REQUIRE_FALSE(invalid_utf8.diagnostics.empty());
    CHECK(invalid_utf8.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::invalid_encoding);
    REQUIRE(invalid_utf8.encoding.original_bytes);
    CHECK(invalid_utf8.encoding.original_bytes->size() > 3);

    const auto cp932_bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) /
                                         "bms/encoding/invalid-cp932-truncated-lead.bms");
    auto invalid_cp932 = wormedit::parse_bms_bytes(
        cp932_bytes, {.encoding_override = wormedit::text_encoding_t::cp932});
    CHECK_FALSE(invalid_cp932.song.has_value());
    REQUIRE_FALSE(invalid_cp932.diagnostics.empty());
    CHECK(invalid_cp932.diagnostics[0].code ==
          (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::cp932)
               ? wormedit::bms_parse_diagnostic_code_t::invalid_encoding
               : wormedit::bms_parse_diagnostic_code_t::unsupported_encoding));

    constexpr std::array bom_bytes{std::byte{0xef}, std::byte{0xbb}, std::byte{0xbf},
                                   std::byte{'#'},  std::byte{'T'},  std::byte{'I'},
                                   std::byte{'T'},  std::byte{'L'},  std::byte{'E'},
                                   std::byte{' '},  std::byte{0xe9}};
    auto conflicting_override = wormedit::parse_bms_bytes(
        bom_bytes, {.encoding_override = wormedit::text_encoding_t::windows_1252});
    CHECK_FALSE(conflicting_override.song.has_value());
    CHECK(conflicting_override.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::invalid_encoding);
}

TEST_CASE("WAV and BMP declarations retain unresolved relative asset paths",
          "[unit][format][bms][resource]") {
    auto result = parse("#TITLE art\n#WAV01 clips/voice.ogg\n#BMP01 bg/picture.png\n#00011:01\n");
    REQUIRE(result.song.has_value());
    REQUIRE(result.song->audio_assets.size() == 1);
    REQUIRE(result.song->visual_assets.size() == 1);
    REQUIRE(result.song->resources.size() == 2);
    CHECK(result.song->audio_assets[0].source_path == std::filesystem::path("clips/voice.ogg"));
    CHECK(result.song->visual_assets[0].source_path == std::filesystem::path("bg/picture.png"));
    CHECK_FALSE(result.song->resources[0].missing);
    CHECK(result.song->resources[1].roles == std::vector{wormedit::resource_role_t::bga});
    REQUIRE(result.song->resources[0].target_identities.size() == 1);
    REQUIRE(result.song->resources[1].target_identities.size() == 1);
    CHECK(result.song->resources[0].target_identities[0].namespace_name == "WAV");
    CHECK(result.song->resources[1].target_identities[0].namespace_name == "BMP");

    auto unicode = parse("#WAV01 caf\xc3\xa9.ogg\n",
                         {.encoding_override = wormedit::text_encoding_t::utf8});
    REQUIRE(unicode.song.has_value());
    REQUIRE(unicode.song->audio_assets.size() == 1);
    CHECK(unicode.song->audio_assets[0].source_path ==
          std::filesystem::path(std::u8string{u8"café.ogg"}));
}
