#include "../../src/scripting/chart_lua.h"
#include "../../src/scripting/lua_project.h"
#include "../support/lua_documentation.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <wormedit/core/chart_clipboard.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
using namespace wormedit;
using namespace wormedit::scripting;
namespace {
auto chart_history() -> document_history_t {
    auto profile = make_osu_mania_profile(4);
    chart_document_t chart{.id = chart_id_t{2},
                           .measures = {{.id = measure_id_t{3}}},
                           .lane_layouts = {{.id = object_id_t{4},
                                             .position = {measure_id_t{3}, 0},
                                             .layout = profile->layout}},
                           .input_profile = profile->input};
    return document_history_t::from({.id = song_id_t{1}, .charts = {std::move(chart)}});
}
auto chart_recipe(std::string source) -> generator_recipe_t {
    const auto hash = native_project_content_checksum(source);
    return {.api_version = "1",
            .source_path = "song.chart.lua",
            .source_hash = hash,
            .source = std::move(source),
            .chart = chart_id_t{2}};
}
auto run_chart(const document_history_t& history, const generator_recipe_t& recipe)
    -> diagnosed_t<generator_output_t> {
    return run_lua_generator(history.document, *history.read_chart(chart_id_t{2}), recipe, {},
                             {.maximum_result_bytes = 8U * 1024U * 1024U});
}
auto prepare_chart(const document_history_t& history, const generator_recipe_t& recipe)
    -> generator_outcome_t {
    auto output = run_chart(history, recipe);
    if (!output.ok())
        return {.diagnostics = std::move(output.diagnostics)};
    return generator_preview_t::propose(history, recipe, std::move(output.value));
}
void check_chart(bool value) {
    CHECK(value);
}
void require_chart(bool value) {
    REQUIRE(value);
}
constexpr std::string_view simple_chart = R"(
difficulty 'Easy'
stable_id 'easy'
columns(4)
measure {id='intro', length=measure_length '0.75', meter=meter(6,8),
  note {key='hit',row=0,lane=1},
  hold {key='hold',row=240,lane=2,end_at=at_measure('outro',0)},
  bgm {key='background',row=0}}
measure {id='outro', note {row=0,lane=3}}
bpm {at=at_measure('intro',0),value=150}
stop {at=at_measure('intro',120),length=rows(60)}
scroll {at=at_measure('outro',0),value=ratio(-1,2)}
difficulty 'Hard'
stable_id 'hard'
columns(4)
mode 'dance-single'
offset(measure_time '-0.125')
measures {{id='a',note {row=0,lane=4}}}
)";
} // namespace
TEST_CASE("chart Lua previews multiple owned difficulties and applies once without replacing "
          "launch chart",
          "[chart-lua]") {
    auto history = chart_history();
    const auto recipe = chart_recipe(std::string(simple_chart));
    const auto proposal = prepare_chart(history, recipe);
    INFO((proposal.diagnostics.empty() ? "success" : proposal.diagnostics.front().message));
    require_chart(proposal.value.has_value());
    check_chart(history.document.charts.size() == 1 && history.undo.empty());
    require_chart(apply_generator(history, *proposal.value).applied);
    check_chart(history.undo.size() == 1 && history.document.charts.size() == 3);
    check_chart(history.document.charts.front().measures.front().notes.empty());
    const auto& binding = history.document.generator_recipes.front().generated_charts.front();
    const auto* chart = history.document.find_chart(binding.chart);
    require_chart(chart != nullptr);
    check_chart(chart->measures.front().definition.length == rational_t{3, 4});
    check_chart(chart->measures.front().definition.meter == meter_t{6, 8});
    check_chart(chart->measures.front().bgm_events.size() == 1);
    check_chart(chart->pauses.front().duration == signed_rational_t{1, 4});
    check_chart(chart->scrolls.front().multiplier == signed_rational_t{-1, 2});
}
TEST_CASE(
    "chart Lua regeneration preserves declaration identities and protects all owned event families",
    "[chart-lua]") {
    auto history = chart_history();
    auto recipe = chart_recipe(std::string(simple_chart));
    auto initial = prepare_chart(history, recipe);
    require_chart(initial.value.has_value());
    require_chart(apply_generator(history, *initial.value).applied);
    recipe = history.document.generator_recipes.front();
    const auto before = recipe.generated_charts.front();
    auto again = prepare_chart(history, recipe);
    require_chart(again.value.has_value());
    check_chart(again.value->payload.recipe.generated_charts.front() == before);
    const auto family = GENERATE(0, 1, 2, 3, 4);
    auto* chart = history.document.find_chart(before.chart);
    require_chart(chart != nullptr);
    if (family == 0)
        chart->measures.front().notes.front().position.row = 1;
    if (family == 1)
        chart->measures.front().bgm_events.front().structural_slot = 3;
    if (family == 2)
        chart->bpms.front().beats_per_minute = {151, 1};
    if (family == 3)
        chart->scrolls.front().multiplier = {2, 1};
    if (family == 4)
        chart->measures.front().definition.meter = meter_t{3, 4};
    check_chart(!prepare_chart(history, recipe).value.has_value());
}
TEST_CASE("chart Lua native round trips retain frozen sources witnesses and declaration keys",
          "[chart-lua]") {
    auto history = chart_history();
    auto recipe = chart_recipe(std::string(simple_chart));
    const auto proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    const auto encoded = write_native_project(history.document);
    require_chart(encoded.value.has_value());
    const auto decoded = parse_native_project(*encoded.value);
    INFO((decoded.diagnostics.empty() ? "success" : decoded.diagnostics.front().message));
    require_chart(decoded.value.has_value());
    check_chart(decoded.value->generator_recipes.front().generated_charts ==
                history.document.generator_recipes.front().generated_charts);
    auto compatible = nlohmann::json::parse(*encoded.value);
    compatible["document"]["generator_recipes"][0].erase("generated_charts");
    const auto optional = parse_native_project(compatible.dump());
    require_chart(optional.value.has_value());
    check_chart(optional.value->generator_recipes.front().generated_charts.empty());
}
TEST_CASE("chart Lua reports bounded numeric rationalization and preserves exact helper meanings",
          "[chart-lua]") {
    const auto history = chart_history();
    const auto result =
        run_chart(history, chart_recipe("difficulty 'A'; offset(measure_time '-0.125'); measure "
                                        "{length=0.1}; resolution(960)"));
    require_chart(result.ok());
    check_chart(result.value.charts.front().content.measures.front().definition.length ==
                rational_t{1, 10});
    check_chart(result.value.charts.front().content.offset == signed_rational_t{-1, 8});
    check_chart(!result.value.rationalizations.empty());
    const auto negative =
        run_chart(history, chart_recipe("difficulty 'A'; offset(-0.1); measure {}; bpm "
                                        "{key='origin',at=at_measure(1,120),value=150}"));
    require_chart(negative.ok());
    check_chart(negative.value.rationalizations.front().find("offset = -1/10") !=
                std::string::npos);
    check_chart(negative.value.charts.front().content.bpms.size() == 2);
}
TEST_CASE("chart Lua rejects invalid exact positions layouts measures and nested timing atomically",
          "[chart-lua]") {
    const auto tail =
        GENERATE("measure {length=0}", "measure {length=-1}", "measure {length=0/0}",
                 "measure {length=measure_length '1/0'}", "measure {length=measure_length '1/7'}",
                 "measure {length=ratio(3,4),meter=meter(4,4)}", "measure {meter=meter(0,4)}",
                 "measure {note {row=960,lane=1}}", "measure {note {row=0,lane=5}}",
                 "measure {hold {row=240,lane=1,end_at=at_measure(1,0)}}",
                 "measure {hold {row=0,lane=1,end_at=at_measure('missing',0)}}",
                 "measure {note {row=0,lane=1},note {row=0,lane=1}}",
                 "measure {bpm {at=at_measure(1,0),value=120}}", "measure {bpms={}}",
                 "columns(0); measure {}", "resolution(0);measure {}",
                 "measure {id='x'};measure {id='x'}", "measure {};difficulty 'A';measure {}",
                 "measure {note {key='same',row=0,lane=1},note {key='same',row=120,lane=2}}",
                 "measure {}; return {notes={ {key='x'} }}");
    auto history = chart_history();
    const auto recipe = chart_recipe(std::string("difficulty 'A';columns(4);") + tail);
    check_chart(!prepare_chart(history, recipe).value.has_value());
    check_chart(history.undo.empty() && history.document.charts.size() == 1);
}
TEST_CASE("chart Lua diagnostics retain user source line numbers", "[chart-lua]") {
    const auto history = chart_history();
    const auto output = run_chart(history, chart_recipe("difficulty 'A'\nerror('line marker')"));
    require_chart(!output.ok());
    check_chart(output.diagnostics.front().message.find("song.chart.lua:2:") != std::string::npos);
}

TEST_CASE(
    "chart Lua timing declarations preserve exact row pauses delays warps and negative scrolls",
    "[chart-lua]") {
    auto history = chart_history();
    const auto recipe = chart_recipe(R"(difficulty 'Timing'; resolution(192); columns(4)
        measure {id='a',meter=meter(6,8)}
        measure {id='b',note{row=0,lane=1}}
        delay {at=at_measure('a',12),length=measure_time '0.125'}
        warp {from=at_measure('a',48),to=at_measure('a',96)}
        stop {at=at_measure('b',24),length=rows(12)}
        scroll {at=at_measure('b',0),value=ratio(-1,2)})");
    const auto proposal = prepare_chart(history, recipe);
    INFO((proposal.diagnostics.empty() ? "success" : proposal.diagnostics.front().message));
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    const auto& content =
        history.document.generator_recipes.front().generated_charts.front().content;
    check_chart(content.warps.size() == 1 && content.pauses.size() == 2);
    check_chart(content.pauses[0].duration == signed_rational_t{1, 8});
    check_chart(content.pauses[0].kind == pause_kind_t::delay &&
                content.pauses[0].source_unit == pause_unit_t::seconds);
    check_chart(content.pauses[1].duration == signed_rational_t{1, 4});
}
TEST_CASE("chart Lua removes omitted owned difficulties and preserves independent metadata",
          "[chart-lua]") {
    auto history = chart_history();
    auto recipe = chart_recipe(std::string(simple_chart));
    auto proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    recipe = history.document.generator_recipes.front();
    const auto easy = recipe.generated_charts.front().chart;
    history.document.find_chart(easy)->metadata.author = "Independent author";
    recipe.source = "difficulty 'Renamed'; stable_id 'easy'; measure {id='intro', note "
                    "{key='hit',row=120,lane=1}}";
    recipe.source_hash = native_project_content_checksum(recipe.source);
    proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    check_chart(history.document.charts.size() == 2);
    check_chart(history.document.find_chart(easy)->metadata.author == "Independent author");
    check_chart(history.document.find_chart(easy)->metadata.name == "Renamed");
}

TEST_CASE("chart Lua measure iterators are ordered bounded and reject duplicate anchors",
          "[chart-lua]") {
    const auto history = chart_history();
    const auto recipe = chart_recipe("difficulty 'Iterator'; local i=0; measures(function() i=i+1; "
                                     "if i<=3 then return {id=tostring(i)} end end)");
    const auto output = run_chart(history, recipe);
    require_chart(output.ok());
    check_chart(output.value.charts.front().content.measures.size() == 3);
    check_chart(!prepare_chart(history, chart_recipe("difficulty 'Bad'; measures({[2]={}})"))
                     .value.has_value());
}
TEST_CASE("chart Lua retained identities still regenerate when the identity ceiling is exhausted",
          "[chart-lua]") {
    auto history = chart_history();
    auto recipe = chart_recipe("difficulty 'A'; measure {id='a', note {key='x',row=0,lane=1}}");
    auto proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    auto& binding = history.document.generator_recipes.front().generated_charts.front();
    const auto previous = binding.content.measures.front().notes.front().id;
    binding.content.measures.front().notes.front().id = object_id_t{UINT64_MAX};
    for (auto& identity : binding.identities)
        if (identity.value == previous.value)
            identity.value = UINT64_MAX;
    auto* chart = history.document.find_chart(binding.chart);
    chart->measures.front().notes.front().id = object_id_t{UINT64_MAX};
    chart->indices_slot = {};
    recipe = history.document.generator_recipes.front();
    proposal = prepare_chart(history, recipe);
    INFO((proposal.diagnostics.empty() ? "success" : proposal.diagnostics.front().message));
    require_chart(proposal.value.has_value());
}

TEST_CASE("chart Lua creation Undo Redo and stale previews use the shared transaction boundary",
          "[chart-lua]") {
    auto history = chart_history();
    const auto recipe = chart_recipe("difficulty 'A'; measure {note {row=0,lane=1}}");
    const auto proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    const auto chart = history.document.generator_recipes.front().generated_charts.front().chart;
    require_chart(undo_document_command(history));
    check_chart(history.document.charts.size() == 1 && history.document.generator_recipes.empty());
    require_chart(redo_document_command(history));
    check_chart(history.document.find_chart(chart) != nullptr);
    check_chart(!apply_generator(history, *proposal.value).applied);
}

TEST_CASE("chart Lua preserves authored positions and rejects grid changes atomically",
          "[chart-lua]") {
    auto history = chart_history();
    auto recipe =
        chart_recipe("difficulty 'A'; stable_id 'a'; measure {id='first'}; measure {id='last'}");
    auto proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    recipe = history.document.generator_recipes.front();
    const auto chart_id = recipe.generated_charts.front().chart;
    auto* chart = history.document.find_chart(chart_id);
    chart->bookmarks.push_back({.id = object_id_t{1000},
                                .label = "Authored",
                                .position = local_position_t{chart->measures.back().id, 1}});
    proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    check_chart(history.document.find_chart(chart_id)->bookmarks.front().position->row == 1);
    const auto before = history.document.revision;
    recipe.source =
        "difficulty 'A'; stable_id 'a'; resolution(480); measure {id='first'}; measure {id='last'}";
    recipe.source_hash = native_project_content_checksum(recipe.source);
    check_chart(!prepare_chart(history, recipe).value.has_value());
    check_chart(history.document.revision == before &&
                history.document.find_chart(chart_id)->divisions_per_standard_measure == 960);
    recipe.source = "difficulty 'A'; stable_id 'a'; measure {id='first'}";
    recipe.source_hash = native_project_content_checksum(recipe.source);
    check_chart(!prepare_chart(history, recipe).value.has_value());
    check_chart(history.document.find_chart(chart_id)->measures.size() == 2);
}

TEST_CASE("Documented chart example materializes an owned difficulty",
          "[chart-lua][documentation]") {
    auto history = chart_history();
    const auto proposal =
        prepare_chart(history, chart_recipe(lua_documentation_example("easy.chart.lua")));
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    check_chart(history.document.charts.size() == 2);
    check_chart(history.document.charts.back().measures.size() == 2);
}

namespace {
constexpr std::string_view shared_sv_chart = "difficulty 'A'; stable_id 'a'; measure {id='first'}; "
                                             "measure {id='middle'}; measure {id='last'}";

auto shared_sv_chart_history() -> document_history_t {
    auto history = chart_history();
    const auto proposal = prepare_chart(history, chart_recipe(std::string(shared_sv_chart)));
    require_chart(proposal.value.has_value());
    require_chart(apply_generator(history, *proposal.value).applied);
    const auto chart_id = history.document.generator_recipes.front().generated_charts.front().chart;
    const auto* chart = history.document.find_chart(chart_id);
    const auto first = chart->measures[0].id;
    const auto middle = chart->measures[1].id;
    const std::string source = "return function(p, ctx) return 1 + p end";
    const sv_recipe_t recipe{
        .id = recipe_id_t{50000},
        .name = "Shared frozen curve",
        .range = {{first, 0}, {middle, 0}},
        .curve = sv_recipe_curve_t::lua_expression,
        .expression_source = source,
        .lua = sv_recipe_lua_t{.source_reference = "curves/shared.lua",
                               .source_hash = native_project_content_checksum(source),
                               .parameters = {{.name = "height", .value = {3, 2}}},
                               .seed = 17}};
    require_chart(apply_sv_recipe_edit(
                      history, insert_edit_t<sv_recipe_t>{.chart = chart_id, .value = recipe})
                      .applied);
    const editor_interaction_state_t selection{.chart = chart_id};
    const auto clipboard = chart_clipboard_t::from(history.document, selection,
                                                   history.document.revision, {}, recipe.id);
    require_chart(clipboard.has_value());
    require_chart(paste_chart_clipboard(history, chart_id, *clipboard, {middle, 0}).applied);
    return history;
}
} // namespace

TEST_CASE("chart Lua regeneration preserves shared SV definitions and frozen source metadata",
          "[chart-lua][sv-recipe][integration]") {
    auto history = shared_sv_chart_history();
    auto recipe = history.document.generator_recipes.front();
    const auto chart_id = recipe.generated_charts.front().chart;
    const auto placements = history.document.find_chart(chart_id)->sv_recipes;
    const auto definitions = history.document.sv_recipe_definitions;
    recipe.source += "; measure {id='extra'}";
    recipe.source_hash = native_project_content_checksum(recipe.source);
    const auto proposal = prepare_chart(history, recipe);
    INFO((proposal.diagnostics.empty() ? "success" : proposal.diagnostics.front().message));
    require_chart(proposal.value.has_value());
    const auto& generated = proposal.value->payload.recipe.generated_charts.front();
    check_chart(generated.content.measures.back().id.value > definitions.front().id.value);
    require_chart(apply_generator(history, *proposal.value).applied);
    check_chart(history.document.find_chart(chart_id)->sv_recipes == placements);
    check_chart(history.document.sv_recipe_definitions == definitions);
    const auto written = write_native_project(history.document);
    require_chart(written.value.has_value());
    const auto loaded = parse_native_project(*written.value);
    require_chart(loaded.value.has_value());
    check_chart(loaded.value->find_chart(chart_id)->sv_recipes == placements);
    check_chart(loaded.value->sv_recipe_definitions == definitions);
    check_chart(loaded.value->generator_recipes.front() ==
                history.document.generator_recipes.front());
    require_chart(undo_document_command(history));
    check_chart(history.document.find_chart(chart_id)->measures.size() == 3);
    check_chart(history.document.find_chart(chart_id)->sv_recipes == placements);
    require_chart(redo_document_command(history));
    check_chart(history.document.find_chart(chart_id)->measures.size() == 4);
    check_chart(history.document.sv_recipe_definitions == definitions);
    const auto stale = prepare_chart(history, history.document.generator_recipes.front());
    require_chart(stale.value.has_value());
    auto edited = history.document.find_chart(chart_id)->sv_recipes.front();
    edited.lua->seed = 99;
    require_chart(apply_sv_recipe_edit(history, update_edit_t<sv_recipe_t>{.chart = chart_id,
                                                                           .id = edited.id,
                                                                           .value = edited})
                      .applied);
    check_chart(!apply_generator(history, *stale.value).applied);
    check_chart(history.document.find_chart(chart_id)->sv_recipes.back().lua == edited.lua);
}

TEST_CASE("chart Lua rejects incompatible regeneration with shared SV ranges atomically",
          "[chart-lua][sv-recipe][integration]") {
    auto history = shared_sv_chart_history();
    auto recipe = history.document.generator_recipes.front();
    const auto source =
        GENERATE("resolution(480); difficulty 'A'; stable_id 'a'; measure {id='first'}; "
                 "measure {id='middle'}; measure {id='last'}",
                 "difficulty 'A'; stable_id 'a'; measure {id='first'}; measure {id='middle'}",
                 "difficulty 'Other'; stable_id 'other'; measure {};");
    recipe.source = source;
    recipe.source_hash = native_project_content_checksum(recipe.source);
    const auto before = write_native_project(history.document);
    require_chart(before.value.has_value());
    const auto undo_count = history.undo.size();
    const auto proposal = prepare_chart(history, recipe);
    check_chart(!proposal.value.has_value());
    check_chart(diagnostic_queries::has_errors(proposal.diagnostics));
    const auto after = write_native_project(history.document);
    require_chart(after.value.has_value());
    check_chart(*after.value == *before.value);
    check_chart(history.undo.size() == undo_count);
}

TEST_CASE("chart Lua allocates above retained SV definition identities without live placements",
          "[chart-lua][sv-recipe][integration]") {
    auto history = shared_sv_chart_history();
    auto recipe = history.document.generator_recipes.front();
    const auto chart_id = recipe.generated_charts.front().chart;
    const auto definition = history.document.sv_recipe_definitions.front().id;
    const auto placements = history.document.find_chart(chart_id)->sv_recipes;
    for (const auto& placement : placements)
        require_chart(apply_sv_recipe_edit(history, delete_edit_t<sv_recipe_t>{.chart = chart_id,
                                                                               .id = placement.id})
                          .applied);
    // Reopen the saved snapshot so Undo history and live placement IDs cannot
    // conceal a missing reservation of the retained definition itself.
    const auto written = write_native_project(history.document);
    require_chart(written.value.has_value());
    auto loaded = parse_native_project(*written.value);
    require_chart(loaded.value.has_value());
    history = document_history_t::from(std::move(*loaded.value));
    recipe.source += "; measure {id='extra'}";
    recipe.source_hash = native_project_content_checksum(recipe.source);
    const auto proposal = prepare_chart(history, recipe);
    require_chart(proposal.value.has_value());
    check_chart(
        proposal.value->payload.recipe.generated_charts.front().content.measures.back().id.value >
        definition.value);
    require_chart(apply_generator(history, *proposal.value).applied);
    check_chart(history.document.sv_recipe_definitions.front().id == definition);
    check_chart(history.document.find_chart(chart_id)->sv_recipes.empty());
}
