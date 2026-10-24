#include "sys/compose/generator_runtime.h"
#include "core/text_util.h"
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/formats/native_project_storage.h>
#include <nlohmann/json.hpp>
#if defined(WORMEDIT_HAS_LUA)
#include "scripting/lua_generator.h"
#endif
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace wormedit::ui {
auto generator_form_t::from(const generator_recipe_t& recipe) -> generator_form_t {
    generator_form_t form;
    form.refill(recipe);
    return form;
}
void generator_form_t::refill(const generator_recipe_t& recipe) {
    auto parameters_buffer = std::move(parameters);
    auto inputs_buffer = std::move(inputs);
    *this = {};
    parameters = std::move(parameters_buffer); inputs = std::move(inputs_buffer);
    parameters.clear(); inputs.clear();
    auto& form = *this;
    form.recipe = recipe.id;
    std::snprintf(form.path, sizeof(form.path), "%s", text_util::path_utf8(recipe.source_path).c_str());
    std::snprintf(form.seed, sizeof(form.seed), "%llu", static_cast<unsigned long long>(recipe.seed));
    for (const auto& value : recipe.parameters) {
        generator_parameter_form_t parameter;
        std::snprintf(parameter.name, sizeof(parameter.name), "%s", value.name.c_str());
        std::snprintf(parameter.value, sizeof(parameter.value), "%s", value.serialized_value.c_str());
        form.parameters.push_back(std::move(parameter));
    }
    for (const auto& value : recipe.input_assets) {
        generator_input_form_t input;
        std::snprintf(input.asset, sizeof(input.asset), "%llu", static_cast<unsigned long long>(value.asset.value));
        form.inputs.push_back(input);
    }
}
}
namespace wormedit::sys {
namespace {
#if defined(WORMEDIT_HAS_LUA)
auto isolated_generator_document(const song_document_t& source, std::stop_token stop)
    -> attempt_t<song_document_t> {
    std::vector<std::shared_ptr<const bms_program_document_t>> programs;
    programs.reserve(source.prepared_bms_programs.size());
    for (const auto& owner : source.prepared_bms_programs) {
        if (stop.stop_requested()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding,
            "Generator snapshot isolation was cancelled.")}};
        if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_binding,
            "Generator snapshot has no BMS program owner.")}};
        auto isolated = bms_program_document_t::from(owner->difficulty(), owner->prepared(), owner->evaluation());
        if (!isolated.value || !isolated.ok()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding,
            isolated.diagnostics.empty() ? "Cannot isolate the generator's BMS program." : isolated.diagnostics.front().message)}};
        programs.push_back(std::move(*isolated.value));
    }
    auto document = source;
    // Prepared data is immutable; projection caches belong to one reading thread.
    document.prepared_bms_programs = std::move(programs);
    return {.value = std::move(document)};
}
#endif
auto reject_result(generator_job_result_t result, std::string message) -> generator_job_result_t {
    result.status = message;
    result.proposal.value.reset();
    result.proposal.diagnostics = {error(diagnostic_code_t::invalid_binding, std::move(message))};
    return result;
}
auto input_hash(const song_document_t& song, asset_id_t id, std::stop_token stop) -> result_t<std::string, std::string> {
    const auto asset = std::ranges::find(song.midi_assets, id, &midi_asset_t::id);
    if (asset == song.midi_assets.end()) return std::unexpected("Generator inputs must name imported MIDI assets.");
    nlohmann::json events = nlohmann::json::array();
    if (asset->events.size() > 65536) return std::unexpected("Generator MIDI input exceeds 65536 events.");
    std::size_t total_bytes{};
    for (const auto& event : asset->events) {
        total_bytes += event.data.size();
        if (stop.stop_requested() || total_bytes > 8U * 1024U * 1024U)
            return std::unexpected("Generator MIDI hash was cancelled or exceeds 8 MiB.");
        std::vector<unsigned> bytes;
        for (const auto byte : event.data) bytes.push_back(std::to_integer<unsigned>(byte));
        events.push_back({event.tick, event.channel, bytes});
    }
    return native_project_content_checksum(nlohmann::json{{"division", asset->ticks_per_quarter}, {"events", events}}.dump());
}
auto read_source(generator_recipe_t& recipe, const std::filesystem::path& base) -> std::string {
    auto path = recipe.source_path;
    if (path.is_relative()) path = base / path;
    std::ifstream input{path, std::ios::binary};
    if (!input) return "Cannot open generator script: " + text_util::path_utf8(path);
    std::string source(maximum_generator_source_bytes + 1, '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    source.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad() || source.size() > maximum_generator_source_bytes) return "Generator source is unreadable or exceeds 64 KiB.";
    recipe.source = std::move(source);
    recipe.source_hash = native_project_content_checksum(recipe.source);
    if (path.is_absolute() && !base.empty()) recipe.source_path = path.lexically_relative(base);
    return {};
}
auto settings(generator_job_result_t& result, const generator_job_request_t& request, std::stop_token stop) -> std::string {
    const auto& form = request.form;
    const auto old = std::ranges::find(request.document.generator_recipes, form.recipe, &generator_recipe_t::id);
    if (form.recipe && old == request.document.generator_recipes.end()) return "Generator recipe no longer exists.";
    if (old != request.document.generator_recipes.end()) result.recipe = *old;
    if (result.recipe.chart && result.recipe.chart != request.chart) return "Generator belongs to another chart.";
    result.recipe.chart = request.chart;
    result.recipe.api_version = "1";
    const auto seed = parse_integer<std::uint64_t>(form.seed);
    if (!seed) return "Generator seed must be an unsigned integer.";
    result.recipe.seed = *seed;
    result.recipe.source_path = text_util::path_from_utf8(form.path);
    result.recipe.parameters.clear();
    for (const auto& parameter : form.parameters)
        result.recipe.parameters.push_back({parameter.name, parameter.value});
    result.recipe.input_assets.clear();
    for (const auto& input : form.inputs) {
        const auto id = parse_integer<std::uint64_t>(input.asset);
        if (!id || !*id) return "Generator input identities must be nonzero unsigned integers.";
        const auto hash = input_hash(request.document, asset_id_t{*id}, stop);
        if (!hash) return hash.error();
        result.recipe.input_assets.push_back({asset_id_t{*id}, *hash});
    }
    return {};
}
auto hashes_changed(const generator_recipe_t& old, const generator_recipe_t& current) -> bool {
    return (!old.source_hash.empty() && old.source_hash != current.source_hash) ||
        old.input_assets != current.input_assets;
}
} // namespace

auto generator_preview_geometry_t::from(const chart_document_t& source, const generator_recipe_t& recipe,
    std::uint64_t revision) -> diagnosed_t<std::shared_ptr<const generator_preview_geometry_t>> {
    auto owner = std::make_shared<generator_preview_geometry_t>();
    auto& chart = owner->chart;
    chart.id = source.id;
    chart.source_family = source.source_family;
    chart.divisions_per_standard_measure = source.divisions_per_standard_measure;
    chart.lane_layouts = source.lane_layouts;
    chart.input_profile = source.input_profile;
    chart.semantic_layers = source.semantic_layers;
    chart.active_branch_selection = source.active_branch_selection;
    chart.virtual_tail_measures = source.virtual_tail_measures;
    for (const auto& measure : source.measures)
        chart.measures.push_back({.id = measure.id, .definition = measure.definition});
    for (const auto& layer : recipe.generated_layers) {
        const auto found = std::ranges::find(chart.semantic_layers, layer.value.id, &semantic_layer_t::id);
        if (found == chart.semantic_layers.end()) chart.semantic_layers.push_back(layer.value);
        else *found = layer.value;
    }
    for (const auto& note : recipe.generated_notes) {
        auto* measure = chart.find_measure(note.value.position.measure);
        if (!measure) return {.diagnostics = {error(diagnostic_code_t::invalid_measure,
            "Generated preview note names an unavailable measure")}};
        measure->notes.push_back(note.value);
    }
    auto indexed = editor_field_scene_index_t::from(chart, revision);
    if (!indexed) return {.diagnostics = indexed.error()};
    owner->index = std::move(*indexed);
    return {.value = std::move(owner)};
}

generator_runtime_t::generator_runtime_t(task_scheduler_t& scheduler)
    : job_{scheduler, 2, "Invalid generator request.", "Generator queue is busy.", "Generator returned no result.",
        async_job_destruction_policy_t::release} {}
generator_runtime_t::~generator_runtime_t() { job_.cancel(); }
auto generator_runtime_t::request(const document_history_t& history, chart_id_t chart,
    const ui::generator_form_t& form, std::vector<editor_selection_object_t> selection,
    const std::filesystem::path& base, bool confirm) -> std::string {
    std::optional<generator_recipe_t> acknowledged;
    if (confirm) {
        if (!result_ || !result_->requires_confirmation || result_->form != form ||
            stamp_ != chart_stamp_t::from(history, chart)) return "Generator warning is stale; Run again.";
        acknowledged = result_->recipe;
    }
    pending_ = generator_job_request_t{history.document, history.bms_edit_target, chart, form,
        std::move(selection), base, std::move(acknowledged), generator_identity_floor(history)};
    stamp_ = chart_stamp_t::from(history, chart);
    return submit();
}
auto generator_runtime_t::submit() -> std::string {
    const auto started = job_.request([&] { return *pending_; }, stamp_, evaluate);
    if (!started) { job_.cancel(); return "Waiting for generator worker…"; }
    pending_.reset();
    return "Running generator…";
}
auto generator_runtime_t::evaluate(const generator_job_request_t& request, std::stop_token stop)
    -> job_t::work_outcome_t {
    generator_job_result_t result{.form = request.form};
    if (auto error = settings(result, request, stop); !error.empty()) return reject_result(std::move(result), error);
    const auto previous = std::ranges::find(request.document.generator_recipes, request.form.recipe, &generator_recipe_t::id);
    if (request.confirmed) result.recipe = *request.confirmed;
    else if (!request.form.use_saved_source) {
        if (auto error = read_source(result.recipe, request.base); !error.empty()) return reject_result(std::move(result), error);
    }
    if (request.form.use_saved_source && result.recipe.source_hash != native_project_content_checksum(result.recipe.source))
        return reject_result(std::move(result), "Saved generator source does not match its content hash.");
    if (auto errors = validate_generator_recipe(result.recipe); !errors.empty())
        return reject_result(std::move(result), errors.front().message);
    if (!request.confirmed && previous != request.document.generator_recipes.end() && hashes_changed(*previous, result.recipe)) {
        result.requires_confirmation = true;
        result.status = "Script or input content changed. Review settings, then Run changed script to continue.";
        return result;
    }
#if defined(WORMEDIT_HAS_LUA)
    auto isolated = isolated_generator_document(request.document, stop);
    if (!isolated.value || !isolated.ok()) return reject_result(std::move(result), isolated.diagnostics.front().message);
    return document_history_t::inspect_snapshot(std::move(*isolated.value), request.target,
        [&](const document_history_t& history) -> generator_job_result_t {
            const auto* chart = history.read_chart(request.chart);
            if (!chart) return reject_result(std::move(result), "Generator chart no longer exists.");
            scripting::lua_sandbox_limits_t limits;
            limits.cancellation = stop;
            auto evaluated = scripting::run_lua_generator(history.document, *chart, result.recipe, request.selection, limits);
            if (!evaluated.diagnostics.empty()) return reject_result(std::move(result), evaluated.diagnostics.front().message);
            result.proposal = generator_preview_t::propose(history, result.recipe, std::move(evaluated.value), request.form.keep_recipe, request.identity_floor);
            if (!result.proposal.diagnostics.empty()) return reject_result(std::move(result), result.proposal.diagnostics.front().message);
            if (!result.proposal.value) return reject_result(std::move(result), "Generator returned no preview proposal.");
            // Proposal validation can refresh an authoring projection, even on this worker.
            const auto* prepared_chart = history.read_chart(request.chart);
            if (!prepared_chart) return reject_result(std::move(result), "Generator chart disappeared after validation.");
            auto geometry = generator_preview_geometry_t::from(*prepared_chart, result.proposal.value->payload.recipe, history.document.revision);
            if (!geometry.diagnostics.empty()) return reject_result(std::move(result), geometry.diagnostics.front().message);
            result.geometry = std::move(geometry.value);
            result.status = "Generator preview ready. Apply commits one command; Cancel discards it.";
            return result;
        });
#else
    static_cast<void>(stop);
    return reject_result(std::move(result), "Lua generators are unavailable in this build.");
#endif
    return result;
}
void generator_runtime_t::publish(std::optional<job_t::completion_t> completion,
    const document_history_t& history, chart_id_t chart) {
    if (!completion) return;
    const auto& identity = *completion ? completion->value().identity : completion->error().identity;
    if (identity != chart_stamp_t::from(history, chart)) return;
    if (*completion) result_ = completion->value().payload;
    else result_ = std::make_shared<const generator_job_result_t>(reject_result({}, completion->error().cause.message));
    if (result_->proposal.value) last_good_ = result_;
}
void generator_runtime_t::poll(const document_history_t& history, chart_id_t chart) {
    if (stamp_ != chart_stamp_t::from(history, chart)) { cancel(); return; }
    if (pending_) static_cast<void>(submit());
    publish(job_.poll(), history, chart);
}
void generator_runtime_t::wait(const document_history_t& history, chart_id_t chart) {
    if (stamp_ != chart_stamp_t::from(history, chart)) { cancel(); return; }
    if (pending_) static_cast<void>(submit());
    publish(job_.wait(), history, chart);
}
void generator_runtime_t::cancel() {
    job_.cancel(); pending_.reset(); result_.reset(); last_good_.reset(); stamp_ = {}; projection_ = {};
}
auto generator_runtime_t::is_busy() const -> bool { return pending_.has_value() || job_.is_busy(); }
auto generator_runtime_t::preview() const -> const generator_preview_t* {
    return last_good_ && last_good_->proposal.value ? &last_good_->proposal.value->payload : nullptr;
}
auto generator_runtime_t::preview_scene(const editor_interaction_view_model_t& interaction,
    const editor_field_scene_request_t& request) const -> const editor_field_scene_view_t* {
    const auto source = last_good_ ? last_good_->geometry : nullptr;
    if (!source || !source->index) return nullptr;
    auto& cache = projection_;
    if (cache.source != source) {
        cache = {};
        cache.source = source;
    }
    const auto viewport = std::tuple{interaction.viewport_scroll_pixels, interaction.viewport_height_pixels};
    if (cache.viewport != viewport || !cache.scene.value ||
        !can_reuse_editor_field_scene_window(source->chart, *source->index, interaction, request, cache.window)) {
        cache.scene = editor_field_scene_view_result_t::from(source->chart, *source->index,
            interaction, request, cache.window);
        cache.viewport = viewport;
    }
    return cache.scene.value && cache.scene.ok() ? &*cache.scene.value : nullptr;
}

auto generator_runtime_t::apply(document_history_t& history, const ui::generator_form_t& form) -> document_command_result_t {
    if (is_busy() || !result_ || !result_->proposal.value || result_->form != form)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_binding,
            "Run these generator settings successfully before Apply.");
    const auto applied = apply_generator(history, *result_->proposal.value);
    if (applied.applied) cancel();
    return applied;
}
} // namespace wormedit::sys
