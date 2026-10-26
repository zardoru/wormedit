#include "sys/compose/generator_runtime.h"
#include "../support/temporary_directory.h"
#include "../support/native_bms_fixture.h"
#include <wormedit/formats/native_project_storage.h>
#include <latch>
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <cstdio>
#include <source_location>
using namespace wormedit;
using namespace wormedit::sys;
namespace {
void check_runtime(bool value, std::source_location at = std::source_location::current()) {
    INFO("Generator runtime check at " << at.line()); CHECK(value);
}
void require_runtime(bool value, std::source_location at = std::source_location::current()) {
    INFO("Generator runtime requirement at " << at.line()); REQUIRE(value);
}
auto runtime_history() -> document_history_t {
    auto profile = make_bms_7k_profile();
    chart_document_t chart{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}};
    chart.lane_layouts.push_back({.id = object_id_t{4}, .position = {measure_id_t{3}, 0}, .layout = profile.layout});
    chart.input_profile = profile.input;
    return document_history_t::from({.id = song_id_t{1}, .charts = {std::move(chart)}});
}
void write_script(const std::filesystem::path& directory, std::string_view source) {
    std::ofstream output{directory / "pattern.lua", std::ios::binary}; output << source;
}
auto form_for_file() -> ui::generator_form_t {
    ui::generator_form_t form;
    std::snprintf(form.path, sizeof(form.path), "pattern.lua");
    return form;
}
constexpr std::string_view good_script = "return {notes={{key='a',measure='3',row=120,lane='101'}}}";
void run_preview(generator_runtime_t& runtime, const document_history_t& history,
    const ui::generator_form_t& form, const std::filesystem::path& directory, bool confirm = false) {
    runtime.request(history, chart_id_t{2}, form, {}, directory, confirm);
    runtime.wait(history, chart_id_t{2});
    require_runtime(runtime.result() != nullptr);
}
}
TEST_CASE("generator worker Run reports availability and never edits before Apply", "[sys][generator-runtime]") {
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    write_script(directory->path(), good_script);
    auto history = runtime_history();
    task_scheduler_t scheduler{1, 4}; generator_runtime_t runtime{scheduler};
    auto form = form_for_file();
    run_preview(runtime, history, form, directory->path());
    check_runtime(history.undo.empty());
    check_runtime(history.document.charts.front().measures.front().notes.empty());
#if defined(WORMEDIT_HAS_LUA)
    require_runtime(runtime.result()->proposal.value.has_value());
    std::snprintf(form.seed, sizeof(form.seed), "2");
    check_runtime(!runtime.apply(history, form).applied);
    std::snprintf(form.seed, sizeof(form.seed), "0");
    require_runtime(runtime.apply(history, form).applied);
    check_runtime(history.undo.size() == 1 && history.document.generator_recipes.size() == 1);
    check_runtime(history.document.generator_recipes.front().source_path == std::filesystem::path{"pattern.lua"});
#else
    check_runtime(!runtime.result()->proposal.value && !runtime.result()->proposal.diagnostics.empty());
#endif
}
#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("generator hash mismatch warns before execution and acknowledgement uses captured bytes", "[sys][generator-runtime]") {
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    write_script(directory->path(), good_script);
    auto history = runtime_history();
    task_scheduler_t scheduler{1, 4}; generator_runtime_t runtime{scheduler};
    auto form = form_for_file(); run_preview(runtime, history, form, directory->path());
    require_runtime(runtime.apply(history, form).applied);
    form = ui::generator_form_t::from(history.document.generator_recipes.front());
    run_preview(runtime, history, form, directory->path());
    require_runtime(runtime.preview() != nullptr);
    write_script(directory->path(), "while true do end");
    run_preview(runtime, history, form, directory->path());
    check_runtime(runtime.result()->requires_confirmation && !runtime.result()->proposal.value);
    check_runtime(runtime.preview() != nullptr);
    // Change the file again after the warning: acknowledgement must execute the
    // already captured infinite loop, not silently load the new valid bytes.
    write_script(directory->path(), good_script);
    run_preview(runtime, history, form, directory->path(), true);
    check_runtime(!runtime.result()->proposal.value && !runtime.result()->proposal.diagnostics.empty());
    check_runtime(runtime.preview() != nullptr);
    check_runtime(!runtime.apply(history, form).applied && history.undo.size() == 1);
}
TEST_CASE("generator input hash changes require explicit acknowledgement", "[sys][generator-runtime]") {
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    write_script(directory->path(), good_script);
    auto history = runtime_history();
    history.document.midi_assets.push_back({.id = asset_id_t{9}, .ticks_per_quarter = 480});
    task_scheduler_t scheduler{1, 4}; generator_runtime_t runtime{scheduler};
    auto form = form_for_file();
    ui::generator_input_form_t input; std::snprintf(input.asset, sizeof(input.asset), "9"); form.inputs.push_back(input);
    run_preview(runtime, history, form, directory->path()); require_runtime(runtime.apply(history, form).applied);
    form = ui::generator_form_t::from(history.document.generator_recipes.front());
    history.document.midi_assets.front().events.push_back({.tick = 120, .channel = 0,
        .data = {std::byte{0x90}, std::byte{60}, std::byte{100}}});
    ++history.document.revision;
    run_preview(runtime, history, form, directory->path());
    check_runtime(runtime.result()->requires_confirmation && !runtime.result()->proposal.value);
}
TEST_CASE("generator snapshot hides undeclared MIDI inputs", "[sys][generator-runtime]") {
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    write_script(directory->path(), "assert(#wormedit.midi_assets==0); return {notes={}}");
    auto history = runtime_history(); history.document.midi_assets.push_back({.id = asset_id_t{9}, .ticks_per_quarter = 480});
    task_scheduler_t scheduler{1, 4}; generator_runtime_t runtime{scheduler};
    run_preview(runtime, history, form_for_file(), directory->path());
    check_runtime(runtime.result()->proposal.value.has_value());
}

namespace {
struct queued_generator_gate_t {
    struct request_t { std::latch* started; std::latch* release; };
    using job_t = async_job_t<request_t, int, int>;
    std::latch started{1}, released{1};
    job_t job;
    bool is_released{};
    explicit queued_generator_gate_t(task_scheduler_t& scheduler)
        : job{scheduler, 1, "invalid", "full", "missing"} {
        require_runtime(job.request([&] { return request_t{&started, &released}; }, 1, hold).has_value());
        started.wait();
    }
    ~queued_generator_gate_t() { release(); }
    void release() { if (!is_released) { is_released = true; released.count_down(); } }
    static auto hold(const request_t& request, std::stop_token) -> job_t::work_outcome_t {
        request.started->count_down(); request.release->wait(); return 1;
    }
};
struct scoped_generator_source_t {
    document_history_t history;
    branch_id_t first, second;
    measure_definition_t second_definition;
};
auto scoped_generator_source() -> scoped_generator_source_t {
    auto flat = runtime_history();
    auto& chart = flat.document.charts.front();
    chart.measures.front().notes.push_back({.id = object_id_t{20}, .position = {chart.measures.front().id, 240},
        .lane = chart.lane_layouts.front().layout.lanes.front().id});
    auto history = document_history_t::from(test::native_bms_fixture(flat.document, chart));
    const auto* source = history.document.find_bms_program(chart_id_t{2});
    auto prepared = source->prepared();
    const auto root = prepared.program.root();
    const auto first = prepared.program.target_chart(root); require_runtime(first.has_value());
    const auto* first_chart = source->read_authoring_chart(*first);
    require_runtime(first_chart && !first_chart->measures.empty());
    const auto measure = first_chart->measures.front().id;
    const auto next = generator_identity_floor(history) + 1;
    const branch_id_t second{next};
    auto inserted = prepared.program.with_node(root, prepared.program.find(root)->children.back(),
        {.id = second, .value = bms_chart_content_t{.items = {{.id = object_id_t{next + 1},
            .value = bms_measure_length_t{.measure = measure, .length = {3, 2}}}}}});
    require_runtime(inserted.has_value()); prepared.program = std::move(*inserted);
    auto owner = bms_program_document_t::from(chart_id_t{2}, std::move(prepared));
    require_runtime(owner.value.has_value()); history.document.prepared_bms_programs.front() = *owner.value;
    const auto target = bms_program_edit_target_t::for_authoring(history.document, chart_id_t{2}, *first);
    require_runtime(target.has_value());
    const std::string source_text = "return {notes={}}";
    history.document.generator_recipes.push_back({.id = recipe_id_t{next + 2}, .api_version = "1",
        .source_path = "saved.lua", .source_hash = native_project_content_checksum(source_text),
        .source = source_text, .chart = chart_id_t{2}, .bms_scope = target->scope, .bms_chart = *first});
    const auto* second_chart = history.document.find_bms_program(chart_id_t{2})->read_authoring_chart(second);
    require_runtime(second_chart && !second_chart->measures.empty());
    const auto definition = second_chart->measures.front().definition;
    return {std::move(history), *first, second, definition};
}
}
TEST_CASE("generator workers isolate BMS authoring caches and reacquire validated geometry", "[sys][generator-runtime][bms]") {
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    write_script(directory->path(), "return {notes={}}");
    auto source = scoped_generator_source();
    auto worker_history = document_history_t::from(source.history.document);
    worker_history.bms_edit_target = bms_program_target_key_t{chart_id_t{2}, source.second};
    task_scheduler_t scheduler{1, 4};
    queued_generator_gate_t gate{scheduler};
    generator_runtime_t runtime{scheduler};
    runtime.request(worker_history, chart_id_t{2}, form_for_file(), {}, directory->path());
    // Prime live cache A after request admission; the worker remains queued.
    const auto* live = source.history.document.find_bms_program(chart_id_t{2})->read_catalog(source.first);
    require_runtime(live && !live->charts.empty());
    check_runtime(live->charts.front().bms_authoring_chart == source.first);
    gate.release(); runtime.wait(worker_history, chart_id_t{2});
    require_runtime(runtime.result() != nullptr);
    INFO(runtime.result()->status);
    require_runtime(runtime.result()->proposal.value.has_value());
    check_runtime(live->charts.front().bms_authoring_chart == source.first);
    require_runtime(runtime.result()->geometry != nullptr);
    check_runtime(runtime.result()->geometry->chart.measures.front().definition == source.second_definition);
}
#endif
TEST_CASE("generator stale and cancelled jobs never publish an applicable result", "[sys][generator-runtime]") {
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    write_script(directory->path(), good_script);
    auto history = runtime_history(); task_scheduler_t scheduler{1, 4}; generator_runtime_t runtime{scheduler};
    auto form = form_for_file(); runtime.request(history, chart_id_t{2}, form, {}, directory->path());
    ++history.document.revision; runtime.wait(history, chart_id_t{2});
    check_runtime(runtime.result() == nullptr);
    runtime.request(history, chart_id_t{2}, form, {}, directory->path()); runtime.cancel();
    runtime.wait(history, chart_id_t{2}); check_runtime(runtime.result() == nullptr);
    check_runtime(history.undo.empty());
}

#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("project entry point worker previews applies and confirms changed included source", "[sys][generator-runtime]") {
    const auto absolute_path = GENERATE(false, true);
    auto directory = test::temporary_directory_t::from(); require_runtime(directory.has_value());
    { std::ofstream output(directory->path() / "project.lua"); output << "worm_project 'Song'; include 'pattern.lua'"; }
    write_script(directory->path(), good_script);
    auto history = runtime_history();
    task_scheduler_t scheduler{1, 4}; generator_runtime_t runtime{scheduler};
    auto form = form_for_file();
    const auto script_path = absolute_path ? directory->path() / "project.lua" : std::filesystem::path{"project.lua"};
    std::snprintf(form.path, sizeof(form.path), "%s", script_path.string().c_str());
    run_preview(runtime, history, form, directory->path());
    INFO(runtime.result()->status);
    require_runtime(runtime.result()->proposal.value.has_value());
    check_runtime(history.undo.empty());
    require_runtime(runtime.apply(history, form).applied);
    check_runtime(history.undo.size() == 1);
    form = ui::generator_form_t::from(history.document.generator_recipes.front());
    write_script(directory->path(), "return {notes={{key='a',measure='3',row=240,lane='101'}}}");
    run_preview(runtime, history, form, directory->path());
    require_runtime(runtime.result()->requires_confirmation);
    check_runtime(!runtime.result()->proposal.value);
    check_runtime(history.document.charts.front().measures.front().notes.front().position.row == 120);
    // Confirmation uses captured bytes, even if an editor changes the module again meanwhile.
    write_script(directory->path(), "error('must not reread on confirmation')");
    run_preview(runtime, history, form, directory->path(), true);
    INFO(runtime.result()->status);
    require_runtime(runtime.result()->proposal.value.has_value());
    require_runtime(runtime.apply(history, form).applied);
    check_runtime(history.document.charts.front().measures.front().notes.front().position.row == 240);
    std::filesystem::remove(directory->path() / "project.lua");
    std::filesystem::remove(directory->path() / "pattern.lua");
    const auto encoded = write_native_project(history.document);
    require_runtime(encoded.value.has_value());
    const auto decoded = parse_native_project(*encoded.value);
    require_runtime(decoded.value.has_value());
    history = document_history_t::from(std::move(*decoded.value));
    form = ui::generator_form_t::from(history.document.generator_recipes.front());
    form.use_saved_source = true;
    run_preview(runtime, history, form, directory->path());
    require_runtime(runtime.result()->proposal.value.has_value());
}
#endif

#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("project chart modules create owned difficulties through captured source Run Apply and saved regeneration", "[sys][generator-runtime][chart-lua]") {
    auto directory=test::temporary_directory_t::from(); require_runtime(directory.has_value());
    { std::ofstream file(directory->path()/"project.lua"); file<<"worm_project 'Difficulties'; include 'easy.chart.lua'"; }
    { std::ofstream file(directory->path()/"easy.chart.lua"); file<<"difficulty 'Easy'; stable_id 'easy'; measure {id='a',length=0.75,note{key='hit',row=0,lane=1}}"; }
    auto history=runtime_history();
    task_scheduler_t scheduler{1,4}; generator_runtime_t runtime{scheduler};
    auto form=form_for_file(); std::snprintf(form.path,sizeof(form.path),"project.lua");
    run_preview(runtime,history,form,directory->path());
    INFO(runtime.result()->status);
    require_runtime(runtime.result()->proposal.value.has_value());
    check_runtime(history.document.charts.size()==1 && history.undo.empty());
    check_runtime(runtime.result()->proposal.value->payload.charts.added==1);
    check_runtime(!runtime.result()->proposal.value->payload.rationalizations.empty());
    require_runtime(runtime.apply(history,form).applied);
    check_runtime(history.document.charts.size()==2 && history.undo.size()==1);
    const auto generated=history.document.generator_recipes.front().generated_charts.front().chart;
    std::filesystem::remove(directory->path()/"project.lua");
    std::filesystem::remove(directory->path()/"easy.chart.lua");
    form=ui::generator_form_t::from(history.document.generator_recipes.front()); form.use_saved_source=true;
    run_preview(runtime,history,form,directory->path());
    require_runtime(runtime.result()->proposal.value.has_value());
    check_runtime(runtime.result()->proposal.value->payload.recipe.generated_charts.front().chart==generated);
}
#endif
