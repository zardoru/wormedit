#include "../../src/scripting/lua_generator.h"
#include "../../src/scripting/lua_project.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/generator.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/storyboard_preview.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <source_location>

namespace {
using namespace wormedit;
using namespace wormedit::scripting;
template <class Value>
void require_acceptance(const Value& value,
                        std::source_location at = std::source_location::current()) {
    INFO("Acceptance requirement at " << at.line());
    REQUIRE(static_cast<bool>(value));
}
void check_acceptance(bool value,
                      std::source_location at = std::source_location::current()) {
    INFO("Acceptance assertion at " << at.line());
    CHECK(value);
}
const std::filesystem::path example_directory =
    std::filesystem::path(WORMEDIT_LUA_EXAMPLE_DIR) / "storyboard";

auto acceptance_history() -> document_history_t {
    const auto profile = make_osu_mania_profile(4);
    require_acceptance(profile);
    chart_document_t chart{.id = chart_id_t{2}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{5}}},
        .bpms = {{.id = object_id_t{6}, .position = {measure_id_t{3}, 0},
                   .beats_per_minute = {120, 1}}}};
    chart.input_profile = profile->input;
    chart.lane_layouts = {{.id = object_id_t{4}, .position = {measure_id_t{3}, 0},
                           .layout = profile->layout}};
    return document_history_t::from({.id = song_id_t{1}, .charts = {std::move(chart)}});
}

auto example_recipe(const document_history_t& history, const std::filesystem::path& root,
                    std::string path) -> generator_recipe_t {
    generator_recipe_t recipe{.api_version = "1", .source_path = std::move(path),
                              .seed = 1, .chart = chart_id_t{2}};
    const auto frozen = lua_project_source_t::from(history.document,
        history.document.charts.front(), recipe, {}, root);
    for (const auto& diagnostic : frozen.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_acceptance(frozen.ok());
    recipe.source = frozen.value.source;
    recipe.source_hash = native_project_content_checksum(recipe.source);
    return recipe;
}

void apply_example(document_history_t& history, const std::filesystem::path& root,
                   std::string path) {
    auto recipe = example_recipe(history, root, std::move(path));
    auto generated = run_lua_generator(history.document, history.document.charts.front(), recipe);
    for (const auto& diagnostic : generated.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_acceptance(generated.ok());
    require_acceptance(generated.value.charts.size() == 1);
    auto proposal = generator_preview_t::propose(history, recipe, std::move(generated.value));
    for (const auto& diagnostic : proposal.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_acceptance(proposal.value);
    require_acceptance(apply_generator(history, *proposal.value).applied);
    check_acceptance(history.undo.size() == 1);
}

auto save_and_load(const song_document_t& song, const std::filesystem::path& path)
    -> song_document_t {
    const auto saved = write_native_project(song);
    for (const auto& diagnostic : saved.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_acceptance(saved.value);
    {
        std::ofstream output(path, std::ios::binary);
        output << *saved.value;
        require_acceptance(output.good());
    }
    std::ifstream input(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    const auto loaded = parse_native_project(bytes);
    require_acceptance(loaded.value);
    check_acceptance(loaded.value->generator_recipes == song.generator_recipes);
    check_acceptance(loaded.value->storyboards == song.storyboards);
    return *loaded.value;
}

auto frame_instances(const osbx_document_t& scene, std::int64_t time)
    -> std::vector<osbx_draw_instance_t> {
    const auto index = osbx_evaluation_index_t::from(scene);
    require_acceptance(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, time, arena);
    require_acceptance(frame.is_success());
    return {frame.instances.begin(), frame.instances.end()};
}

void check_native_frames(const song_document_t& before, const song_document_t& after) {
    require_acceptance(before.storyboards.size() == after.storyboards.size());
    for (std::size_t i = 0; i < before.storyboards.size(); ++i) {
        for (const auto time : {200, 1700, 3300}) {
            const auto expected = frame_instances(*before.storyboards[i].osbx, time);
            const auto actual = frame_instances(*after.storyboards[i].osbx, time);
            require_acceptance(actual.size() == expected.size());
            for (std::size_t j = 0; j < actual.size(); ++j) {
                check_acceptance(actual[j].world == expected[j].world);
                check_acceptance(actual[j] == expected[j]);
            }
        }
    }
}

auto export_request(const song_document_t& song) -> conversion_request_t {
    const auto chart = song.generator_recipes.front().generated_charts.front().chart;
    return {.target = format_family_t::osu_mania, .chart = chart,
        .osbx_image_sizes = {{.image_id = "sky-image", .width = 640, .height = 480},
                            {.image_id = "sun-image", .width = 80, .height = 80},
                            {.image_id = "rays-image", .width = 160, .height = 160},
                            {.image_id = "worm-image", .width = 180, .height = 110},
                            {.image_id = "banner-image", .width = 330, .height = 64}},
        .osbx_playback_span = osbx_osb_span_t{0, 4000}};
}

void check_exported_frame(const osbx_document_t& scene, const storyboard_document_model_t& model,
                          std::int64_t time) {
    const auto expected = frame_instances(scene, time);
    require_acceptance(model.sprites.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto position = evaluate_storyboard_property(model.sprites[i], storyboard_opcode_t::move, time);
        require_acceptance(position.is_success());
        require_acceptance(position.value.present);
        check_acceptance(std::hypot(position.value.values[0] - expected[i].world[4],
                         position.value.values[1] - expected[i].world[5]) <= 0.5);
    }
}

void check_export(const song_document_t& song) {
    const auto converted = convert_document(song, export_request(song));
    for (const auto& diagnostic : converted.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_acceptance(converted);
    require_acceptance(converted.outputs.size() == 2);
    check_acceptance(std::ranges::any_of(converted.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("worm baked") != std::string::npos;
    }));
    const auto output = std::ranges::find_if(converted.outputs, [](const auto& item) {
        return std::filesystem::path(item.suggested_filename).extension() == ".osb";
    });
    require_acceptance(output != converted.outputs.end());
    require_acceptance(output->reimport_validated);
    const auto parsed = parse_osb(output->bytes);
    require_acceptance(parsed.value);
    require_acceptance(parsed.value->sprites.size() == 4);
    for (const auto time : {200, 1700, 3300})
        check_exported_frame(*song.storyboards.front().osbx, *parsed.value, time);
}

void check_banner_refusal(const song_document_t& song, const std::filesystem::path& root) {
    require_acceptance(song.storyboards.size() == 2);
    const auto instances = frame_instances(*song.storyboards.back().osbx, 1700);
    require_acceptance(instances.size() == 1);
    check_acceptance(instances[0].id == "banner");
    check_acceptance(std::abs(instances[0].world[2]) > 0.1);
    const auto converted = convert_document(song, export_request(song));
    require_acceptance(!converted);
    check_acceptance(converted.outputs.empty());
    check_acceptance(converted.storyboard_images.empty());
    check_acceptance(std::ranges::any_of(converted.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("banner/op/2") != std::string::npos;
    }));
    const output_group_t group{.family = format_family_t::osu_mania,
                                .relative_output_path = "refused-export"};
    const auto published = publish_conversion_group_atomic(root, group, export_request(song), converted);
    check_acceptance(!published.published);
    check_acceptance(!published.errors.empty());
    check_acceptance(!std::filesystem::exists(root / "refused-export"));
}

void check_broken_module(document_history_t& history, const std::filesystem::path& root) {
    const auto before = write_native_project(history.document);
    require_acceptance(before.value);
    const auto undo_count = history.undo.size();
    std::ofstream{root / "main.osbx.lua"} << "storyboard 'Sunshine'; group 'partial'; error('broken module')";
    auto recipe = history.document.generator_recipes.front();
    const auto failed = lua_project_source_t::from(history.document,
        history.document.charts.front(), recipe, {}, root);
    require_acceptance(!failed.ok());
    require_acceptance(!failed.diagnostics.empty());
    check_acceptance(failed.diagnostics.front().message.find("broken module") != std::string::npos);
    const auto after = write_native_project(history.document);
    require_acceptance(after.value);
    check_acceptance(*after.value == *before.value);
    check_acceptance(history.undo.size() == undo_count);
}
} // namespace

TEST_CASE("OSBX example survives generator native preview and osu export", "[osbx][acceptance]") {
    const auto directory = test::temporary_directory_t::from();
    require_acceptance(directory);
    std::filesystem::copy(example_directory, directory->path(), std::filesystem::copy_options::recursive);
    auto history = acceptance_history();
    const auto banner = GENERATE(false, true);
    INFO("Banner variant: " << banner);
    apply_example(history, directory->path(), banner ? "banner-project.lua" : "project.lua");
    const auto loaded = save_and_load(history.document, directory->path() / "example.wormedit");
    check_native_frames(history.document, loaded);
    if (banner) check_banner_refusal(loaded, directory->path());
    else check_export(loaded);
    // Optional acceptance artifact for the isolated real-window smoke walkthrough.
    if (const auto destination = std::getenv("WORMEDIT_OSBX_ACCEPTANCE_OUTPUT"); destination && banner)
        std::filesystem::copy(directory->path(), destination,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    check_broken_module(history, directory->path());
}

namespace {
void media_acceptance_files(const std::filesystem::path& root) {
    std::ofstream(root / "project.lua") << "worm_project('media')\nstoryboard('scene')\n"
        "animation('hero',{file='hero.png',frame_count=2,frame_delay_ms=100,loop_type='LoopOnce'})\n"
        "sample('cue',{path='tone.wav',time_ms=100})\n";
    std::ofstream(root / "hero0.png") << "frame0";
    std::ofstream(root / "hero1.png") << "frame1";
    std::ofstream(root / "tone.wav") << "raw audio";
}
void apply_media_output(document_history_t& history, const generator_recipe_t& recipe, generator_output_t output) {
    const auto proposal = generator_preview_t::propose(history, recipe, std::move(output));
    require_acceptance(proposal.value);
    require_acceptance(apply_generator(history, *proposal.value).applied);
}
}

TEST_CASE("Animation and Sample Lua results Apply and persist while failed updates retain last good scene", "[lua][osbx][acceptance][animation][sample]") {
    const auto directory = test::temporary_directory_t::from(); require_acceptance(directory);
    media_acceptance_files(directory->path());
    auto history = acceptance_history();
    const auto recipe = example_recipe(history, directory->path(), "project.lua");
    const auto output = run_lua_generator(history.document, history.document.charts.front(), recipe);
    require_acceptance(output.ok());
    apply_media_output(history, recipe, output.value);
    require_acceptance(history.document.storyboards.size() == 1);
    const auto loaded = save_and_load(history.document, directory->path() / "media.wormedit");
    const auto& scene = *loaded.storyboards.front().osbx;
    const auto first = frame_instances(scene, 0), second = frame_instances(scene, 100);
    require_acceptance(first.size() == 1 && second.size() == 1);
    check_acceptance(first.front().frame_index == 0 && second.front().frame_index == 1);
    auto invalid = output.value;
    invalid.storyboards.front().value.nodes.back().sample->volume = 0;
    const auto before = write_native_project(history.document);
    const auto refused = generator_preview_t::propose(history, recipe, std::move(invalid));
    check_acceptance(!refused.value);
    const auto after = write_native_project(history.document);
    check_acceptance(before.value == after.value);
}
