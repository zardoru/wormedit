#include <wormedit/core/timing_links.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/formats/native_project.h>
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
        .begin = {.measure = measure_id_t{11}, .row = 0}, .end = {.measure = measure_id_t{12}, .row = 960}},
        .output = sv_recipe_output_t::scrolls, .sampling = {.fixed_point_count = std::nullopt, .fixed_step_rows = 240}});
    auto recipe = source.sv_recipes.front();
    recipe.id = recipe_id_t{32};
    recipe.range = {.begin = {.measure = measure_id_t{21}, .row = 0}, .end = {.measure = measure_id_t{22}, .row = 480}};
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
        .range = {.begin = {.measure = measure_id_t{11}, .row = 0}, .end = {.measure = measure_id_t{12}, .row = 960}},
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
