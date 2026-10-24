#include <wormedit/core/timing_links.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/osu_mania.h>
#include <fstream>
#include <iterator>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <limits>
using namespace wormedit;
namespace {
auto timing_link_song() -> song_document_t {
    auto song = song_document_t{.id = song_id_t{1}, .revision = 7,
        .charts = {{.id = chart_id_t{10}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}},
            .bpms = {{.id = object_id_t{13}, .position = {.measure = measure_id_t{12}, .row = 120}, .beats_per_minute = {120, 1}}}},
            {.id = chart_id_t{20}, .divisions_per_standard_measure = 480,
            .measures = {{.id = measure_id_t{21}}, {.id = measure_id_t{22}}},
            .bpms = {{.id = object_id_t{23}, .position = {.measure = measure_id_t{22}, .row = 60}, .beats_per_minute = {120, 1}}}}}};
    song.charts.back().timing_links.value(timing_category_t::bpm) = timing_category_link_t{
        .source = chart_id_t{10}, .objects = {{.source = object_id_t{13}, .target = object_id_t{23}}}};
    return song;
}
void require_valid_links(const song_document_t& song) {
    const auto diagnostics = validate_timing_links(song);
    for (const auto& diagnostic : diagnostics) INFO(diagnostic.message);
    REQUIRE(diagnostics.empty());
}
void require_link_error(const song_document_t& song, diagnostic_code_t code) {
    const auto diagnostics = validate_timing_links(song);
    REQUIRE(std::ranges::any_of(diagnostics, [code](const auto& diagnostic) { return diagnostic.code == code; }));
}
auto native_link_json(const song_document_t& song) -> nlohmann::json {
    const auto output = write_native_project(song);
    for (const auto& diagnostic : output.diagnostics) INFO(diagnostic.message);
    REQUIRE(output.value);
    return nlohmann::json::parse(*output.value);
}
}
TEST_CASE("Timing links map exact beats across unrelated measure boundaries and resolutions", "[core][timing-links]") {
    auto song = timing_link_song();
    song.charts.back().measures = {{.id = measure_id_t{21}, .definition = {.length = {2, 1}, .meter = std::nullopt}}};
    const auto map = timing_position_map_t::from(song.charts.front(), song.charts.back());
    REQUIRE(map);
    CHECK(map->position({.measure = measure_id_t{12}, .row = 120}) == local_position_t{.measure = measure_id_t{21}, .row = 540});
    CHECK_FALSE(map->position({.measure = measure_id_t{11}, .row = 1}));
    CHECK_FALSE(map->position({.measure = measure_id_t{12}, .row = 960}));
    CHECK(map->position({.measure = measure_id_t{12}, .row = 960}, measure_chart_end_policy_t::allow) ==
        local_position_t{.measure = measure_id_t{21}, .row = 960});
    CHECK(map->rows(240) == 120);
}
TEST_CASE("Timing link spans preserve exclusive chart ends and reject inexact or overflowing grids", "[core][timing-links]") {
    auto song = timing_link_song();
    const auto map = timing_position_map_t::from(song.charts.front(), song.charts.back());
    REQUIRE(map);
    const auto span = map->span({.begin = {.measure = measure_id_t{11}, .row = 0},
        .end = {.measure = measure_id_t{12}, .row = 960}});
    REQUIRE(span);
    CHECK(span->end == local_position_t{.measure = measure_id_t{22}, .row = 480});
    const auto overflow = timing_position_map_t{.source_resolution = 1, .target_resolution = 2};
    CHECK_FALSE(overflow.rows(std::numeric_limits<std::uint64_t>::max()));
    song.charts.back().divisions_per_standard_measure = 0;
    CHECK_FALSE(timing_position_map_t::from(song.charts.front(), song.charts.back()));
}
TEST_CASE("Timing source links validate identity mappings and effective snapshots", "[core][timing-links]") {
    auto song = timing_link_song();
    require_valid_links(song);
    SECTION("wrong effective value") { song.charts.back().bpms.front().beats_per_minute = {180, 1}; }
    SECTION("inexact row") { song.charts.front().bpms.front().position.row = 121; }
    SECTION("wrong target identity") { song.charts.back().bpms.front().id = object_id_t{24}; }
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
TEST_CASE("Timing source graph rejects missing self cyclic and ambiguous mappings", "[core][timing-links]") {
    auto song = timing_link_song();
    auto& link = *song.charts.back().timing_links.value(timing_category_t::bpm);
    SECTION("missing") { link.source = chart_id_t{99}; }
    SECTION("self") { link.source = chart_id_t{20}; }
    SECTION("cycle") {
        song.charts.front().timing_links.value(timing_category_t::bpm) = timing_category_link_t{
            .source = chart_id_t{20}, .objects = {{.source = object_id_t{23}, .target = object_id_t{13}}}};
    }
    SECTION("duplicate mapping") { link.objects.push_back(link.objects.front()); }
    require_link_error(song, diagnostic_code_t::invalid_id);
}
TEST_CASE("Timing local overrides remember a source without following its current snapshot", "[core][timing-links]") {
    auto song = timing_link_song();
    song.charts.back().timing_links.value(timing_category_t::bpm)->is_local_override = true;
    song.charts.back().bpms.front().beats_per_minute = {180, 1};
    require_valid_links(song);
    const auto root = native_link_json(song);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().timing_links == song.charts.back().timing_links);
    CHECK(parsed.value->charts.back().bpms.front() == song.charts.back().bpms.front());
}
TEST_CASE("Native timing links round trip exact source identities and reject inconsistent stored data", "[core][timing-links][native_project]") {
    const auto song = timing_link_song();
    auto root = native_link_json(song);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().timing_links == song.charts.back().timing_links);
    root["document"]["charts"][1]["bpms"][0]["beats_per_minute"]["numerator"] = 180;
    CHECK_FALSE(parse_native_project(root.dump()).ok());
}
TEST_CASE("Native schema twelve migrates every chart to independent timing categories", "[core][timing-links][native_project]") {
    auto song = timing_link_song();
    song.charts.back().timing_links = {};
    auto root = native_link_json(song);
    root["document"]["schema_version"] = 12;
    for (auto& chart : root["document"]["charts"]) chart.erase("timing_links");
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    for (const auto& chart : parsed.value->charts) CHECK(chart.timing_links == timing_links_t{});
    CHECK(parsed.value->charts.back().bpms.front() == song.charts.back().bpms.front());
}
TEST_CASE("Linked scroll recipes map spans and fixed row steps with stable recipe identities", "[core][timing-links]") {
    auto song = timing_link_song();
    auto& source = song.charts.front();
    auto& target = song.charts.back();
    source.sv_recipes.push_back({.id = recipe_id_t{31}, .name = "Scroll", .range = {
        .begin = {.measure = measure_id_t{11}, .row = 0}, .end = {.measure = measure_id_t{12}, .row = 480}},
        .output = sv_recipe_output_t::scrolls, .sampling = {.fixed_point_count = std::nullopt, .fixed_step_rows = 240}});
    auto recipe = source.sv_recipes.front();
    recipe.id = recipe_id_t{32};
    recipe.range = {.begin = {.measure = measure_id_t{21}, .row = 0}, .end = {.measure = measure_id_t{22}, .row = 240}};
    recipe.sampling.fixed_step_rows = 120;
    target.sv_recipes.push_back(recipe);
    target.timing_links.value(timing_category_t::scroll) = timing_category_link_t{.source = source.id,
        .recipes = {{.source = recipe_id_t{31}, .target = recipe_id_t{32}}}};
    require_valid_links(song);
    target.sv_recipes.front().sampling.fixed_step_rows = 240;
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
TEST_CASE("Linked measure layouts preserve independent measure identities and row resolutions", "[core][timing-links]") {
    auto song = timing_link_song();
    song.charts.back().timing_links.value(timing_category_t::measures) = timing_category_link_t{.source = chart_id_t{10},
        .measures = {{.source = measure_id_t{11}, .target = measure_id_t{21}},
            {.source = measure_id_t{12}, .target = measure_id_t{22}}}};
    require_valid_links(song);
    const auto root = native_link_json(song);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().divisions_per_standard_measure == 480);
    CHECK(parsed.value->charts.back().timing_links == song.charts.back().timing_links);
    song.charts.back().measures.back().definition.meter = std::nullopt;
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}
TEST_CASE("Stops delays and offsets resolve independently without rescaling pause units", "[core][timing-links]") {
    auto song = timing_link_song();
    auto& source = song.charts.front();
    auto& target = song.charts.back();
    source.pauses.push_back({.id = object_id_t{14}, .position = {.measure = measure_id_t{11}, .row = 0},
        .kind = pause_kind_t::stop, .duration = {1, 2}, .source_unit = pause_unit_t::beats});
    target.pauses = {{.id = object_id_t{24}, .position = {.measure = measure_id_t{21}, .row = 0},
        .kind = pause_kind_t::stop, .duration = {1, 2}, .source_unit = pause_unit_t::beats},
        {.id = object_id_t{25}, .position = {.measure = measure_id_t{21}, .row = 0},
        .kind = pause_kind_t::delay, .duration = {7, 1}}};
    target.timing_links.value(timing_category_t::stop) = timing_category_link_t{.source = source.id,
        .objects = {{.source = object_id_t{14}, .target = object_id_t{24}}}};
    target.timing_links.value(timing_category_t::offset) = timing_category_link_t{.source = source.id};
    require_valid_links(song);
    target.pauses.back().duration = {8, 1};
    require_valid_links(song);
    target.beat_zero_audio_offset_seconds = {-1, 10};
    require_link_error(song, diagnostic_code_t::unsupported_feature);
}

namespace {
auto change_link_bpm(document_history_t& history, chart_id_t chart, object_id_t event, signed_rational_t value)
    -> document_command_result_t {
    const auto* target = history.document.find_chart(chart);
    const auto found = std::ranges::find(target->bpms, event, &bpm_event_t::id);
    REQUIRE(found != target->bpms.end());
    return apply_bpm_edit(history, update_edit_t<bpm_edit_value_t>{.chart = chart, .id = event,
        .value = bpm_edit_value_t::from(found->position, timing_bpm_value_t{.beats_per_minute = value})});
}
auto edit_timing_link(document_history_t& history, timing_link_operation_t operation, bool confirmed = false)
    -> document_command_result_t {
    return apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::bpm, .operation = operation, .source = chart_id_t{10}, .discard_confirmed = confirmed});
}
void require_timing_command(const document_command_result_t& result) {
    std::string messages;
    for (const auto& diagnostic : result.diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    REQUIRE(result.applied);
}
void check_linked_bpm(const document_history_t& history, signed_rational_t value) {
    const auto& chart = history.document.charts.back();
    REQUIRE(chart.bpms.size() == 1);
    CHECK(chart.bpms.front().beats_per_minute == value);
    CHECK_FALSE(chart.timing_links.value(timing_category_t::bpm)->is_local_override);
}
}
TEST_CASE("Source BPM commands propagate with stable local identity in one Undo record", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    require_timing_command(change_link_bpm(history, chart_id_t{10}, object_id_t{13}, {180, 1}));
    check_linked_bpm(history, {180, 1});
    CHECK(history.document.charts.back().bpms.front().id == object_id_t{23});
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    check_linked_bpm(history, {120, 1});
    REQUIRE(redo_document_command(history));
    check_linked_bpm(history, {180, 1});
}
TEST_CASE("Local timing writes detach only their changed category and Relink requires confirmation", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    song.charts.back().timing_links.value(timing_category_t::offset) = timing_category_link_t{.source = chart_id_t{10}};
    auto history = document_history_t::from(std::move(song));
    require_timing_command(change_link_bpm(history, chart_id_t{20}, object_id_t{23}, {150, 1}));
    CHECK(history.document.charts.back().timing_links.value(timing_category_t::bpm)->is_local_override);
    CHECK_FALSE(history.document.charts.back().timing_links.value(timing_category_t::offset)->is_local_override);
    CHECK_FALSE(edit_timing_link(history, timing_link_operation_t::relink).applied);
    CHECK(history.document.charts.back().bpms.front().beats_per_minute == signed_rational_t{150, 1});
    require_timing_command(edit_timing_link(history, timing_link_operation_t::relink, true));
    check_linked_bpm(history, {120, 1});
}
TEST_CASE("Linking and detaching preserve native source identity and local Undo state", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    song.charts.back().timing_links = {};
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(edit_timing_link(history, timing_link_operation_t::link).applied);
    require_timing_command(edit_timing_link(history, timing_link_operation_t::link, true));
    CHECK(history.document.charts.back().bpms.front().id != object_id_t{13});
    require_timing_command(edit_timing_link(history, timing_link_operation_t::detach));
    CHECK_FALSE(history.document.charts.back().timing_links.value(timing_category_t::bpm));
    REQUIRE(undo_document_command(history));
    check_linked_bpm(history, {120, 1});
}
TEST_CASE("Inexact source updates reject both source and dependent changes atomically", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().bpms.front().position.row = 121;
    const auto result = transaction.commit("Inexact source move");
    CHECK_FALSE(result.applied);
    CHECK(history.document.charts.front().bpms.front().position.row == 120);
    CHECK(history.document.charts.back().bpms.front().position.row == 60);
    CHECK(history.undo.empty());
    require_valid_links(history.document);
}
TEST_CASE("Resolution-only redivision preserves linked categories and musical positions", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    document_command_transaction_t transaction{history};
    auto& target = transaction.document().charts.back();
    target.divisions_per_standard_measure = 960;
    target.bpms.front().position.row = 120;
    require_timing_command(transaction.commit("Redivide linked chart"));
    check_linked_bpm(history, {120, 1});
    CHECK(history.document.charts.back().bpms.front().position.row == 120);
    require_valid_links(history.document);
}
TEST_CASE("Source deletion requires detaching surviving timing dependents", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    CHECK_FALSE(apply_delete_chart_command(history, chart_id_t{10}).applied);
    CHECK(history.document.charts.size() == 2);
    require_timing_command(edit_timing_link(history, timing_link_operation_t::detach));
    require_timing_command(apply_delete_chart_command(history, chart_id_t{10}));
    CHECK(history.document.charts.size() == 1);
    CHECK(history.document.charts.front().id == chart_id_t{20});
}
TEST_CASE("Duplicating linked charts remaps target identities while retaining their source", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    const auto duplicated = apply_duplicate_chart_command(history, chart_id_t{20});
    require_timing_command(duplicated.command);
    const auto& link = *history.document.charts.back().timing_links.value(timing_category_t::bpm);
    CHECK(link.source == chart_id_t{10});
    CHECK(link.objects.front().source == object_id_t{13});
    CHECK(link.objects.front().target == history.document.charts.back().bpms.front().id);
    CHECK(link.objects.front().target != object_id_t{23});
    require_valid_links(history.document);
}
TEST_CASE("Timing materialization budgets fail at a small bound before copying events", "[core][timing-links][transaction]") {
    const auto before = timing_link_song();
    auto proposed = before;
    proposed.charts.front().bpms.front().beats_per_minute = {180, 1};
    const auto diagnostics = reconcile_timing_links(before, proposed, {.maximum_materialized_records = 0});
    CHECK(diagnostic_queries::has_errors(diagnostics));
    CHECK(before.charts.front().bpms.front().beats_per_minute == signed_rational_t{120, 1});
    CHECK(proposed.charts.back().bpms.front().beats_per_minute == signed_rational_t{120, 1});
}
namespace {
void add_link_chain(song_document_t& song) {
    auto chart = chart_document_t{.id = chart_id_t{30},
        .measures = {{.id = measure_id_t{31}}, {.id = measure_id_t{32}}},
        .bpms = {{.id = object_id_t{33}, .position = {.measure = measure_id_t{32}, .row = 120}, .beats_per_minute = {120, 1}}}};
    chart.timing_links.value(timing_category_t::bpm) = timing_category_link_t{.source = chart_id_t{20},
        .objects = {{.source = object_id_t{23}, .target = object_id_t{33}}}};
    song.charts.push_back(std::move(chart));
}
void add_linked_origin_bpm(song_document_t& song) {
    song.charts.front().bpms.push_back({.id = object_id_t{14}, .position = {.measure = measure_id_t{11}, .row = 0}, .beats_per_minute = {120, 1}});
    song.charts.back().bpms.push_back({.id = object_id_t{24}, .position = {.measure = measure_id_t{21}, .row = 0}, .beats_per_minute = {120, 1}});
    song.charts.back().timing_links.value(timing_category_t::bpm)->objects.push_back({.source = object_id_t{14}, .target = object_id_t{24}});
}
}
TEST_CASE("Timing source chains resolve effective values in dependency order", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    add_link_chain(song);
    auto history = document_history_t::from(std::move(song));
    require_timing_command(change_link_bpm(history, chart_id_t{10}, object_id_t{13}, {150, 1}));
    CHECK(history.document.charts[1].bpms.front().beats_per_minute == signed_rational_t{150, 1});
    CHECK(history.document.charts[2].bpms.front().beats_per_minute == signed_rational_t{150, 1});
    CHECK(history.document.charts[2].bpms.front().id == object_id_t{33});
    CHECK(history.undo.size() == 1);
    require_valid_links(history.document);
}
TEST_CASE("Compound source changes propagate multiple categories in one publication", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    add_link_chain(song);
    song.charts[1].timing_links.value(timing_category_t::offset) = timing_category_link_t{.source = chart_id_t{10}};
    song.charts[2].timing_links.value(timing_category_t::offset) = timing_category_link_t{.source = chart_id_t{20}};
    auto history = document_history_t::from(std::move(song));
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().bpms.front().beats_per_minute = {180, 1};
    transaction.document().charts.front().beat_zero_audio_offset_seconds = {-1, 10};
    require_timing_command(transaction.commit("Compound source timing"));
    CHECK(history.document.charts[2].bpms.front().beats_per_minute == signed_rational_t{180, 1});
    CHECK(history.document.charts[2].beat_zero_audio_offset_seconds == signed_rational_t{-1, 10});
    CHECK(history.undo.size() == 1);
    require_valid_links(history.document);
}
TEST_CASE("No-op and failed local edits retain timing links without adding Undo", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    CHECK_FALSE(change_link_bpm(history, chart_id_t{20}, object_id_t{23}, {120, 1}).applied);
    CHECK_FALSE(change_link_bpm(history, chart_id_t{20}, object_id_t{23}, {0, 1}).applied);
    CHECK_FALSE(edit_timing_link(history, timing_link_operation_t::relink).applied);
    CHECK_FALSE(history.document.charts.back().timing_links.value(timing_category_t::bpm)->is_local_override);
    CHECK(history.undo.empty());
}
TEST_CASE("Detached timing previews resolve dependents without publishing live links", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    bool observed{};
    const auto preview = document_command_transaction_t::preview(history.document, [&](document_history_t& detached) {
        auto result = change_link_bpm(detached, chart_id_t{10}, object_id_t{13}, {180, 1});
        observed = detached.document.charts.back().bpms.front().beats_per_minute == signed_rational_t{180, 1};
        return result;
    });
    CHECK(preview.applied);
    CHECK(observed);
    check_linked_bpm(history, {120, 1});
    CHECK(history.undo.empty());
}
namespace {
auto prepare_linked_beat_fit(const song_document_t& song) -> proposal_outcome_t<beat_interval_drag_t> {
    const auto begun = beat_interval_drag_t::propose(song, chart_id_t{10}, object_id_t{13},
        {.measure = measure_id_t{12}, .row = 360}, 48'000);
    REQUIRE(begun.value);
    return begun.value->revise(song, {}, 127'200);
}
}
TEST_CASE("Beat-fit gesture commits propagate through the same timing publication boundary", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    add_linked_origin_bpm(song);
    auto history = document_history_t::from(std::move(song));
    const auto revised = prepare_linked_beat_fit(history.document);
    REQUIRE(revised.value);
    CHECK(history.undo.empty());
    REQUIRE(apply_proposal(history, *revised.value));
    CHECK(history.document.charts.back().bpms.front().beats_per_minute == signed_rational_t{150, 1});
    CHECK_FALSE(history.document.charts.back().timing_links.value(timing_category_t::bpm)->is_local_override);
    CHECK(history.undo.size() == 1);
}
TEST_CASE("Stale link actions cannot detach a newly published timing snapshot", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    const auto stamp = chart_stamp_t::from(history.document, chart_id_t{20});
    require_timing_command(change_link_bpm(history, chart_id_t{10}, object_id_t{13}, {180, 1}));
    const auto stale = apply_timing_link_edit(history, stamp,
        {.category = timing_category_t::bpm, .operation = timing_link_operation_t::detach});
    CHECK_FALSE(stale.applied);
    check_linked_bpm(history, {180, 1});
    CHECK(history.undo.size() == 1);
}
TEST_CASE("Relinking avoids target IDs reused by unrelated local objects", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    document_command_transaction_t transaction{history};
    auto& target = transaction.document().charts.back();
    target.bpms.clear();
    target.scrolls.push_back({.id = object_id_t{23}, .position = {.measure = measure_id_t{21}, .row = 0}});
    require_timing_command(transaction.commit("Replace local timing object"));
    require_timing_command(edit_timing_link(history, timing_link_operation_t::relink, true));
    CHECK(history.document.charts.back().bpms.front().id != object_id_t{23});
    CHECK(history.document.charts.back().scrolls.front().id == object_id_t{23});
    require_valid_links(history.document);
}
namespace {
void add_source_scroll_recipe(song_document_t& song) {
    song.charts.front().sv_recipes.push_back({.id = recipe_id_t{41}, .name = "Linked scroll",
        .range = {.begin = {.measure = measure_id_t{11}, .row = 0}, .end = {.measure = measure_id_t{12}, .row = 480}},
        .output = sv_recipe_output_t::scrolls, .sampling = {.fixed_point_count = std::nullopt, .fixed_step_rows = 240}});
}
void link_scroll_recipe(document_history_t& history) {
    require_timing_command(apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::scroll, .source = chart_id_t{10}}));
}
}
TEST_CASE("Recipe propagation allocates globally fresh IDs and retains them on source updates", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    add_source_scroll_recipe(song);
    auto history = document_history_t::from(std::move(song));
    link_scroll_recipe(history);
    const auto id = history.document.charts.back().sv_recipes.front().id;
    CHECK(id != recipe_id_t{41});
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().sv_recipes.front().parameters.value_to = {2, 1};
    require_timing_command(transaction.commit("Update source recipe"));
    CHECK(history.document.charts.back().sv_recipes.front().id == id);
    CHECK(history.document.charts.back().sv_recipes.front().parameters.value_to == signed_rational_t{2, 1});
    CHECK(history.document.charts.back().sv_recipes.front().sampling.fixed_step_rows == 120);
    require_valid_links(history.document);
}
TEST_CASE("Changing a local recipe output detaches both affected linked categories", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    add_source_scroll_recipe(song);
    song.charts.back().timing_links.value(timing_category_t::speed) = timing_category_link_t{.source = chart_id_t{10}};
    auto history = document_history_t::from(std::move(song));
    link_scroll_recipe(history);
    document_command_transaction_t transaction{history};
    auto& recipe = transaction.document().charts.back().sv_recipes.front();
    recipe.output = sv_recipe_output_t::speeds;
    recipe.transition_duration = rational_t{0, 1};
    recipe.transition_is_seconds = false;
    require_timing_command(transaction.commit("Change local recipe output"));
    CHECK(history.document.charts.back().timing_links.value(timing_category_t::scroll)->is_local_override);
    CHECK(history.document.charts.back().timing_links.value(timing_category_t::speed)->is_local_override);
    CHECK(history.document.charts.front().sv_recipes.front().output == sv_recipe_output_t::scrolls);
    CHECK(history.undo.size() == 2);
    require_valid_links(history.document);
}
namespace {
auto local_layout_song() -> song_document_t {
    auto song = timing_link_song();
    add_linked_origin_bpm(song);
    const auto profile = make_bms_7k_profile();
    auto& target = song.charts.back();
    const auto lane = profile.layout.lanes.front().id;
    target.lane_layouts = {{.id = object_id_t{100}, .position = {.measure = measure_id_t{21}, .row = 0}, .layout = profile.layout},
        {.id = object_id_t{101}, .position = {.measure = measure_id_t{21}, .row = 360}, .layout = profile.layout}};
    target.input_profile = profile.input;
    song.audio_assets.push_back({.id = asset_id_t{500}, .source_path = "sample.wav",
        .stream = {.sample_rate = 48'000, .channels = 2, .frame_count = 192'000, .seekable = true}});
    song.resources.push_back({.id = resource_id_t{502}, .name = "Sample", .asset = asset_id_t{500}, .roles = {resource_role_t::sample}});
    song.visual_assets.push_back({.id = asset_id_t{505}, .source_path = "background.png"});
    song.resources.push_back({.id = resource_id_t{506}, .name = "Background", .asset = asset_id_t{505}, .roles = {resource_role_t::bga}});
    target.measures.front().notes.push_back({.id = object_id_t{1001}, .position = {.measure = measure_id_t{21}, .row = 300},
        .lane = lane, .kind = note_kind_t::hold, .end = local_position_t{.measure = measure_id_t{22}, .row = 60},
        .sound = resource_id_t{502}, .sound_channel_event = object_id_t{503}});
    target.sound_channels.push_back({.id = object_id_t{504}, .name = "Channel", .source_clip = {
        .id = clip_id_t{501}, .source = asset_id_t{500}, .end_frame = 48'000},
        .events = {{.id = object_id_t{503}, .position = {.measure = measure_id_t{21}, .row = 300},
            .end = local_position_t{.measure = measure_id_t{22}, .row = 60}, .lane = lane}}});
    target.bgm_source_lanes.push_back({.id = object_id_t{2001}});
    target.measures.front().bgm_events.push_back({.id = object_id_t{2002},
        .position = {.measure = measure_id_t{21}, .row = 360}, .source_lane = object_id_t{2001}, .sound = resource_id_t{502},
        .end = local_position_t{.measure = measure_id_t{22}, .row = 120}});
    target.bga_events.push_back({.id = object_id_t{2003}, .position = {.measure = measure_id_t{21}, .row = 400}, .resource = resource_id_t{506}});
    target.bookmarks = {{.id = object_id_t{2004}, .position = local_position_t{.measure = measure_id_t{21}, .row = 320}},
        {.id = object_id_t{2005}, .anchor = marker_anchor_t::audio_time, .audio_milliseconds = 1500}};
    target.sections = {{.id = object_id_t{2006}, .beat_span = position_span_t{
        .begin = {.measure = measure_id_t{21}, .row = 330}, .end = {.measure = measure_id_t{22}, .row = 200}}},
        {.id = object_id_t{2007}, .anchor = marker_anchor_t::audio_time, .audio_millisecond_span = std::pair<std::int64_t, std::int64_t>{500, 900}}};
    target.scrolls.push_back({.id = object_id_t{2008}, .position = {.measure = measure_id_t{21}, .row = 390}, .multiplier = {3, 2}});
    target.kiai.push_back({.id = object_id_t{2009}, .position = {.measure = measure_id_t{21}, .row = 410}, .enabled = true});
    target.pauses = {{.id = object_id_t{2010}, .position = {.measure = measure_id_t{21}, .row = 420}, .duration = {1, 2}},
        {.id = object_id_t{2011}, .position = {.measure = measure_id_t{21}, .row = 430}, .kind = pause_kind_t::delay, .duration = {1, 4}}};
    target.warps.push_back({.id = object_id_t{2012}, .span = {
        .begin = {.measure = measure_id_t{22}, .row = 300}, .end = {.measure = measure_id_t{22}, .row = 360}}});
    target.speeds.push_back({.id = object_id_t{2013}, .position = {.measure = measure_id_t{21}, .row = 450},
        .multiplier = {3, 2}, .transition_beats = {1, 1}});
    target.timing_anchors.push_back({.id = object_id_t{2014}, .position = {.measure = measure_id_t{21}, .row = 470}, .source_sample_frame = 100'000});
    target.sv_recipes.push_back({.id = recipe_id_t{6001}, .name = "Local recipe", .range = {
        .begin = {.measure = measure_id_t{21}, .row = 360}, .end = {.measure = measure_id_t{22}, .row = 480}}});
    target.timing_links.value(timing_category_t::measures) = timing_category_link_t{.source = chart_id_t{10},
        .measures = {{.source = measure_id_t{11}, .target = measure_id_t{21}}, {.source = measure_id_t{12}, .target = measure_id_t{22}}}};
    const auto diagnostics = validate_document(song);
    std::string messages;
    for (const auto& diagnostic : diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    REQUIRE(diagnostics.empty());
    return song;
}
auto musical_row(const chart_document_t& chart, local_position_t position, measure_chart_end_policy_t end = measure_chart_end_policy_t::reject)
    -> std::uint64_t {
    const auto index = measure_index_t::from(chart);
    REQUIRE(index);
    const auto row = index->row_of(position, end);
    REQUIRE(row);
    return *row;
}
void split_link_source(document_history_t& history) {
    document_command_transaction_t transaction{history};
    auto& measures = transaction.document().charts.front().measures;
    const auto half = measure_definition_t{.length = {1, 2}, .meter = std::nullopt};
    measures.front().definition = half;
    measures.insert(measures.begin() + 1, {.id = measure_id_t{15}, .definition = half});
    require_timing_command(transaction.commit("Split source layout"));
}
void check_local_sound_positions(const chart_document_t& chart) {
    const auto* note = chart.find_note(object_id_t{1001});
    REQUIRE(note);
    CHECK(std::pair{musical_row(chart, note->position), musical_row(chart, *note->end)} == std::pair{300ULL, 540ULL});
    const auto* bgm = chart.find_bgm_event(object_id_t{2002});
    REQUIRE(bgm);
    CHECK(std::pair{musical_row(chart, bgm->position), musical_row(chart, *bgm->end)} == std::pair{360ULL, 600ULL});
    const auto& channel = chart.sound_channels.front().events.front();
    CHECK(std::pair{musical_row(chart, channel.position), musical_row(chart, *channel.end)} == std::pair{300ULL, 540ULL});
}
void check_local_marker_positions(const chart_document_t& chart) {
    CHECK(musical_row(chart, chart.bookmarks.front().position.value()) == 320);
    CHECK(chart.bookmarks.back().audio_milliseconds == 1500);
    CHECK(std::pair{musical_row(chart, chart.sections.front().beat_span->begin), musical_row(chart, chart.sections.front().beat_span->end)} == std::pair{330ULL, 680ULL});
    CHECK(chart.sections.back().audio_millisecond_span == std::pair<std::int64_t, std::int64_t>{500, 900});
    CHECK(musical_row(chart, chart.bga_events.front().position) == 400);
}
void check_local_timing_positions(const chart_document_t& chart) {
    CHECK(musical_row(chart, chart.scrolls.front().position) == 390);
    CHECK(musical_row(chart, chart.speeds.front().position) == 450);
    CHECK(musical_row(chart, chart.kiai.front().position) == 410);
    CHECK(musical_row(chart, chart.timing_anchors.front().position) == 470);
    CHECK(chart.timing_anchors.front().source_sample_frame == 100'000);
}
void check_local_spans(const chart_document_t& chart) {
    CHECK(std::pair{musical_row(chart, chart.pauses.front().position), musical_row(chart, chart.pauses.back().position)} == std::pair{420ULL, 430ULL});
    CHECK(std::pair{musical_row(chart, chart.warps.front().span.begin), musical_row(chart, chart.warps.front().span.end)} == std::pair{780ULL, 840ULL});
    CHECK(std::pair{musical_row(chart, chart.sv_recipes.front().range.begin), musical_row(chart, chart.sv_recipes.front().range.end, measure_chart_end_policy_t::allow)} == std::pair{360ULL, 960ULL});
    CHECK(musical_row(chart, chart.lane_layouts.back().position) == 360);
    CHECK(chart.sv_recipes.front().id == recipe_id_t{6001});
}
}
TEST_CASE("Linked source splits preserve all local objects and exclusive recipe endpoints", "[core][timing-links][measure-links]") {
    auto history = document_history_t::from(local_layout_song());
    split_link_source(history);
    const auto& target = history.document.charts.back();
    CHECK(target.measures.size() == 3);
    CHECK(target.divisions_per_standard_measure == 480);
    CHECK(target.measures.front().id == measure_id_t{21});
    CHECK(target.measures.back().id == measure_id_t{22});
    check_local_sound_positions(target);
    check_local_marker_positions(target);
    check_local_timing_positions(target);
    check_local_spans(target);
    require_valid_links(history.document);
}
TEST_CASE("Linked layout Undo restores bucket ownership and stable local identities", "[core][timing-links][measure-links]") {
    auto history = document_history_t::from(local_layout_song());
    split_link_source(history);
    CHECK(history.document.charts.back().measures[1].notes.front().id == object_id_t{1001});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.back().measures.size() == 2);
    CHECK(history.document.charts.back().measures.front().notes.front().id == object_id_t{1001});
    check_local_sound_positions(history.document.charts.back());
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.back().measures[1].notes.front().id == object_id_t{1001});
}
TEST_CASE("Short linked layouts reject local content loss without changing either chart", "[core][timing-links][measure-links]") {
    auto history = document_history_t::from(local_layout_song());
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().measures.back().definition = {.length = {1, 2}, .meter = std::nullopt};
    CHECK_FALSE(transaction.commit("Shorten source layout").applied);
    CHECK(history.document.charts.front().measures.back().definition.length == rational_t{1, 1});
    CHECK(history.document.charts.back().measures.size() == 2);
    CHECK(history.undo.empty());
}
namespace {
void register_link_music_clock(song_document_t& song) {
    song.audio_assets.push_back({.id = asset_id_t{50}, .source_path = "music.wav",
        .stream = {.sample_rate = 48'000, .channels = 2, .frame_count = 48'000, .seekable = true}});
    song.resources.push_back({.id = resource_id_t{51}, .name = "Music", .asset = asset_id_t{50}, .roles = {resource_role_t::music}});
}
auto anchor_link_song() -> song_document_t {
    auto song = timing_link_song();
    song.charts.front().timing_anchors.push_back({.id = object_id_t{40}, .position = {.measure = measure_id_t{11}, .row = 0}, .source_sample_frame = 4800});
    return song;
}
auto link_anchors(document_history_t& history) -> document_command_result_t {
    return apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::anchor, .source = chart_id_t{10}});
}
auto make_preview_schedule(const chart_document_t& chart) -> preview_schedule_t {
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto schedule = preview_schedule_t::from(chart, *timing.value, {.sample_rate = 48'000, .include_metronome = false});
    REQUIRE(schedule.value);
    return *schedule.value;
}
}
TEST_CASE("Linked layout growth preserves production clocks recipes and playback event frames", "[core][timing-links][measure-links]") {
    auto song = local_layout_song();
    song.charts.back().sv_recipes.front().parameters.value_to = {2, 1};
    song.charts.back().sv_recipes.front().postprocess = {{.value = {2, 1}}};
    auto history = document_history_t::from(std::move(song));
    const auto before = make_preview_schedule(history.document.charts.back());
    const auto recipe_before = sv_recipe_prepared_t::from(history.document.charts.back(), history.document.charts.back().sv_recipes.front());
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().measures.front().definition = {.length = {2, 1}, .meter = std::nullopt};
    require_timing_command(transaction.commit("Grow source layout"));
    const auto& target = history.document.charts.back();
    const auto after = make_preview_schedule(target);
    CHECK(std::ranges::equal(before.playable, after.playable, {}, &preview_audio_event_t::playable_sort_key, &preview_audio_event_t::playable_sort_key));
    CHECK(std::ranges::equal(before.bgm, after.bgm, {}, &preview_audio_event_t::bgm_sort_key, &preview_audio_event_t::bgm_sort_key));
    CHECK(before.bga == after.bga);
    const auto recipe_after = sv_recipe_prepared_t::from(target, target.sv_recipes.front());
    CHECK(recipe_before.value.value_at(600) == recipe_after.value.value_at(600));
    check_local_sound_positions(target);
    check_local_spans(target);
}
TEST_CASE("Linked layout shortening replaces obsolete derived timing instead of treating it as local content", "[core][timing-links][measure-links]") {
    auto song = timing_link_song();
    add_linked_origin_bpm(song);
    song.charts.back().timing_links.value(timing_category_t::measures) = timing_category_link_t{.source = chart_id_t{10},
        .measures = {{.source = measure_id_t{11}, .target = measure_id_t{21}}, {.source = measure_id_t{12}, .target = measure_id_t{22}}}};
    auto history = document_history_t::from(std::move(song));
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().measures.pop_back();
    transaction.document().charts.front().bpms.erase(transaction.document().charts.front().bpms.begin());
    require_timing_command(transaction.commit("Remove source tail and its derived BPM"));
    CHECK(history.document.charts.back().measures.size() == 1);
    CHECK(history.document.charts.back().bpms.size() == 1);
    CHECK(history.document.charts.back().bpms.front().id == object_id_t{24});
    require_valid_links(history.document);
}
TEST_CASE("Sample anchor links reject unknown and ambiguous project clocks", "[core][timing-links][clock-links]") {
    auto song = anchor_link_song();
    SECTION("unknown") {}
    SECTION("ambiguous") {
        register_link_music_clock(song);
        song.audio_assets.push_back({.id = asset_id_t{52}, .source_path = "other.wav",
            .stream = {.sample_rate = 44'100, .channels = 1, .frame_count = 44'100}});
        song.resources.push_back({.id = resource_id_t{53}, .name = "Other music", .asset = asset_id_t{52}, .roles = {resource_role_t::music}});
    }
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(link_anchors(history).applied);
    CHECK(history.document.charts.back().timing_anchors.empty());
    CHECK(history.undo.empty());
}
TEST_CASE("Sample anchors retain exact source frames and reject updates outside the shared music extent", "[core][timing-links][clock-links]") {
    auto song = anchor_link_song();
    register_link_music_clock(song);
    auto history = document_history_t::from(std::move(song));
    require_timing_command(link_anchors(history));
    CHECK(history.document.charts.back().timing_anchors.front().source_sample_frame == 4800);
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().timing_anchors.front().source_sample_frame = 48'001;
    CHECK_FALSE(transaction.commit("Out-of-clock anchor").applied);
    CHECK(history.document.charts.front().timing_anchors.front().source_sample_frame == 4800);
    CHECK(history.document.charts.back().timing_anchors.front().source_sample_frame == 4800);
    CHECK(history.undo.size() == 1);
}
TEST_CASE("Anchor clocks allow resource aliases of one asset and survive native round trips", "[core][timing-links][clock-links]") {
    auto song = anchor_link_song();
    register_link_music_clock(song);
    song.resources.push_back({.id = resource_id_t{52}, .name = "Music alias", .asset = asset_id_t{50}, .roles = {resource_role_t::music}});
    auto history = document_history_t::from(std::move(song));
    require_timing_command(link_anchors(history));
    const auto root = native_link_json(history.document);
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.back().timing_anchors.front().source_sample_frame == 4800);
    require_valid_links(*parsed.value);
}
TEST_CASE("Adaptive recipe links reject sampled rows that would need target-grid rounding", "[core][timing-links][recipe-links]") {
    auto song = timing_link_song();
    add_source_scroll_recipe(song);
    auto& recipe = song.charts.front().sv_recipes.front();
    recipe.curve = sv_recipe_curve_t::exponential;
    recipe.parameters.value_to = {4, 1};
    recipe.parameters.shape = {2, 1};
    recipe.sampling = {.mode = sv_recipe_sampling_mode_t::adaptive, .fixed_point_count = std::nullopt,
        .tolerance = {1, 10'000'000}, .max_points = 256};
    const auto prepared = sv_recipe_prepared_t::from(song.charts.front(), recipe);
    REQUIRE(prepared.ok());
    REQUIRE(std::ranges::any_of(prepared.value.samples().points, [](const auto& point) { return point.absolute_row % 2 != 0; }));
    auto history = document_history_t::from(std::move(song));
    const auto result = apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::scroll, .source = chart_id_t{10}});
    CHECK_FALSE(result.applied);
    CHECK(history.document.charts.back().sv_recipes.empty());
    CHECK(history.undo.empty());
}
TEST_CASE("Fixed script recipes link across exact grids without evaluating script values in publication", "[core][timing-links][recipe-links]") {
    auto song = timing_link_song();
    add_source_scroll_recipe(song);
    auto& recipe = song.charts.front().sv_recipes.front();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "1";
    auto history = document_history_t::from(std::move(song));
    link_scroll_recipe(history);
    CHECK(history.document.charts.back().sv_recipes.front().curve == sv_recipe_curve_t::lua_expression);
    CHECK(history.document.charts.back().sv_recipes.front().expression_source == "1");
    CHECK(history.document.charts.back().sv_recipes.front().sampling.fixed_step_rows == 120);
    require_valid_links(history.document);
}
TEST_CASE("Chart-end recipe samples reject a coarser grid atomically", "[core][timing-links][recipe-links]") {
    auto song = timing_link_song();
    add_source_scroll_recipe(song);
    song.charts.front().sv_recipes.front().range.end.row = 960;
    auto history = document_history_t::from(std::move(song));
    const auto result = apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::scroll, .source = chart_id_t{10}});
    CHECK_FALSE(result.applied);
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& item) { return item.code == diagnostic_code_t::invalid_resolution; }));
    CHECK(history.document.charts.back().sv_recipes.empty());
    CHECK(history.undo.empty());
}
namespace {
auto imported_bms_link_song(bool conditional) -> song_document_t {
    std::string source = "#PLAYER 1\n#TITLE Base\n#ARTIST Artist\n#BPM 120\n#WAV01 tone.wav\n#00011:01\n";
    if (conditional) source += "#RANDOM 1\n#IF 1\n#BPM01 150\n#00108:01\n#ENDIF\n#ENDRANDOM\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch", .branch_choices = {{.control_source_order = 6, .selected_value = 1}}});
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.song);
    return *parsed.value.song;
}
void add_native_link_target(song_document_t& song) {
    chart_document_t target{.id = chart_id_t{9999},
        .divisions_per_standard_measure = song.read_charts().front()->divisions_per_standard_measure};
    std::uint64_t next = 10000;
    for (const auto& measure : song.read_charts().front()->measures)
        target.measures.push_back({.id = measure_id_t{next++}, .definition = measure.definition});
    song.ensure_chart_order();
    song.chart_order.push_back(target.id);
    song.charts.push_back(std::move(target));
}
auto link_imported_source(document_history_t& history, timing_category_t category) -> document_command_result_t {
    return apply_timing_link_edit(history, chart_stamp_t::from(history.document, history.document.read_charts().back()->id),
        {.category = category, .source = history.document.read_charts().front()->id, .discard_confirmed = true});
}
auto link_fixture_bytes(std::string_view relative) -> std::vector<std::byte> {
    std::ifstream input(std::filesystem::path{WORMEDIT_FIXTURE_DIR} / relative, std::ios::binary);
    REQUIRE(input);
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    const auto view = std::as_bytes(std::span{bytes.data(), bytes.size()});
    return {view.begin(), view.end()};
}
}
TEST_CASE("Flat BMS timing sources can supply normalized target charts", "[core][timing-links][provenance-links]") {
    auto song = imported_bms_link_song(false);
    add_native_link_target(song);
    auto history = document_history_t::from(std::move(song));
    require_timing_command(link_imported_source(history, timing_category_t::bpm));
    CHECK(history.document.charts.back().bpms.front().beats_per_minute == signed_rational_t{120, 1});
    require_valid_links(history.document);
}
TEST_CASE("Conditional BMS timing sources require normalization before linking", "[core][timing-links][provenance-links]") {
    auto song = imported_bms_link_song(true);
    add_native_link_target(song);
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(link_imported_source(history, timing_category_t::bpm).applied);
    CHECK(history.document.charts.back().bpms.empty());
    CHECK_FALSE(history.document.charts.back().timing_links.value(timing_category_t::bpm));
    CHECK(history.undo.empty());
}
TEST_CASE("Unconditional scoped timing targets inherit native sources and propagate", "[core][timing-links][bms-public-cutover]") {
    auto song = imported_bms_link_song(false);
    const auto target = song.read_charts().front()->id;
    add_native_link_target(song);
    auto& source = song.charts.front();
    source.bpms.push_back({.id = object_id_t{20000},
        .position = {.measure = source.measures.front().id}, .beats_per_minute = {180, 1}});
    const auto source_id = source.id;
    auto history = document_history_t::from(std::move(song));
    require_timing_command(apply_timing_link_edit(history, chart_stamp_t::from(history.document, target),
        {.category = timing_category_t::bpm, .source = source_id, .discard_confirmed = true}));
    CHECK(history.document.read_chart(target)->bpms.front().beats_per_minute == signed_rational_t{180, 1});
    require_timing_command(apply_bpm_edit(history, update_edit_t<bpm_edit_value_t>{.chart = source_id,
        .id = object_id_t{20000}, .value = {.position = history.document.read_chart(source_id)->bpms.front().position,
            .settings = {.beats_per_minute = {210, 1}}}}));
    CHECK(history.document.read_chart(target)->bpms.front().beats_per_minute == signed_rational_t{210, 1});
    require_valid_links(history.document);
}

TEST_CASE("Absolute-time imported sources require a usable resolved clock", "[core][timing-links][clock-links]") {
    const auto parsed = parse_osu_mania_bytes(link_fixture_bytes("osu/mania-tags-bookmarks-kiai.osu"));
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value);
    auto song = *parsed.value;
    add_native_link_target(song);
    auto history = document_history_t::from(std::move(song));
    require_timing_command(link_imported_source(history, timing_category_t::offset));
    const auto original = history.document.charts.front().bpms;
    document_command_transaction_t transaction{history};
    transaction.document().charts.front().bpms.clear();
    CHECK_FALSE(transaction.commit("Remove absolute source clock").applied);
    CHECK(history.document.charts.front().bpms == original);
    CHECK(history.undo.size() == 1);
}
namespace {
auto all_category_link_song() -> song_document_t {
    auto song = timing_link_song();
    add_linked_origin_bpm(song);
    register_link_music_clock(song);
    add_source_scroll_recipe(song);
    auto& source = song.charts.front();
    source.scrolls.push_back({.id = object_id_t{31}, .position = {.measure = measure_id_t{11}, .row = 120}, .multiplier = {2, 1}});
    source.speeds.push_back({.id = object_id_t{32}, .position = {.measure = measure_id_t{11}, .row = 240},
        .multiplier = {3, 2}, .transition_beats = {1, 1}});
    source.kiai.push_back({.id = object_id_t{33}, .position = {.measure = measure_id_t{11}, .row = 360}, .enabled = true});
    source.pauses = {{.id = object_id_t{34}, .position = {.measure = measure_id_t{11}, .row = 480},
        .kind = pause_kind_t::stop, .duration = {1, 2}, .source_unit = pause_unit_t::beats},
        {.id = object_id_t{35}, .position = {.measure = measure_id_t{11}, .row = 600},
        .kind = pause_kind_t::delay, .duration = {1, 4}}};
    source.warps.push_back({.id = object_id_t{36}, .span = {
        .begin = {.measure = measure_id_t{12}, .row = 240}, .end = {.measure = measure_id_t{12}, .row = 360}}});
    source.timing_anchors.push_back({.id = object_id_t{37}, .position = {.measure = measure_id_t{11}, .row = 0}, .source_sample_frame = 4800});
    source.beat_zero_audio_offset_seconds = {1, 10};
    auto speed = source.sv_recipes.front();
    speed.id = recipe_id_t{42};
    speed.output = sv_recipe_output_t::speeds;
    speed.transition_duration = rational_t{1, 1};
    speed.transition_is_seconds = false;
    source.sv_recipes.push_back(std::move(speed));
    return song;
}
void link_all_categories(document_history_t& history) {
    document_command_transaction_t transaction{history};
    for (const auto& category : timing_categories) {
        const auto staged = stage_timing_link_edit(transaction, chart_id_t{20},
            {.category = category.kind, .source = chart_id_t{10}, .discard_confirmed = true});
        REQUIRE(staged.value);
    }
    require_timing_command(transaction.commit("Link all timing categories"));
}
}
namespace {
void check_all_category_snapshots(const chart_document_t& linked) {
    CHECK(linked.scrolls.front().position.row == 60);
    CHECK(linked.speeds.front().position.row == 120);
    CHECK(linked.pauses.front().source_unit == pause_unit_t::beats);
    CHECK(linked.pauses.back().kind == pause_kind_t::delay);
}
void check_linked_spans_clock_and_recipes(const chart_document_t& linked) {
    CHECK(linked.warps.front().span.end.row == 180);
    CHECK(linked.timing_anchors.front().source_sample_frame == 4800);
    CHECK(linked.beat_zero_audio_offset_seconds == signed_rational_t{1, 10});
    CHECK(linked.sv_recipes.size() == 2);
}
void check_only_kiai_override(const chart_document_t& target) {
    for (const auto& category : timing_categories) {
        const auto& association = target.timing_links.value(category.kind);
        REQUIRE(association);
        CHECK(association->is_local_override == (category.kind == timing_category_t::kiai));
    }
}
}
TEST_CASE("All timing categories follow one source and a local Kiai edit detaches only Kiai", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(all_category_link_song());
    link_all_categories(history);
    check_all_category_snapshots(history.document.charts.back());
    check_linked_spans_clock_and_recipes(history.document.charts.back());
    document_command_transaction_t transaction{history};
    transaction.document().charts.back().kiai.front().enabled = false;
    require_timing_command(transaction.commit("Edit local Kiai"));
    check_only_kiai_override(history.document.charts.back());
    CHECK(history.document.charts.front().kiai.front().enabled);
    require_valid_links(history.document);
}
TEST_CASE("Relink requires confirmation for a deliberate zero offset override", "[core][timing-links][transaction]") {
    auto song = timing_link_song();
    song.charts.front().beat_zero_audio_offset_seconds = {1, 4};
    auto history = document_history_t::from(std::move(song));
    const auto request = timing_link_request_t{.category = timing_category_t::offset, .source = chart_id_t{10}};
    require_timing_command(apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}), request));
    document_command_transaction_t transaction{history};
    transaction.document().charts.back().beat_zero_audio_offset_seconds = {};
    require_timing_command(transaction.commit("Set local offset to zero"));
    const auto relink = apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::offset, .operation = timing_link_operation_t::relink});
    CHECK_FALSE(relink.applied);
    CHECK(history.document.charts.back().timing_links.value(timing_category_t::offset)->is_local_override);
    CHECK(history.document.charts.back().beat_zero_audio_offset_seconds.numerator == 0);
    CHECK(history.undo.size() == 2);
    CHECK(timing_categories[static_cast<std::size_t>(timing_category_t::offset)].requires_link_confirmation(history.document.charts.back()));
    require_timing_command(apply_timing_link_edit(history, chart_stamp_t::from(history.document, chart_id_t{20}),
        {.category = timing_category_t::offset, .operation = timing_link_operation_t::relink, .discard_confirmed = true}));
    CHECK(history.document.charts.back().beat_zero_audio_offset_seconds == signed_rational_t{1, 4});
}
TEST_CASE("Relink requires confirmation after local timing events were deliberately cleared", "[core][timing-links][transaction]") {
    auto history = document_history_t::from(timing_link_song());
    document_command_transaction_t transaction{history};
    transaction.document().charts.back().bpms.clear();
    require_timing_command(transaction.commit("Clear local BPM timing"));
    CHECK_FALSE(edit_timing_link(history, timing_link_operation_t::relink).applied);
    CHECK(history.document.charts.back().bpms.empty());
    CHECK(history.document.charts.back().timing_links.value(timing_category_t::bpm)->is_local_override);
    require_timing_command(edit_timing_link(history, timing_link_operation_t::relink, true));
    CHECK(history.document.charts.back().bpms.front().beats_per_minute == signed_rational_t{120, 1});
}
