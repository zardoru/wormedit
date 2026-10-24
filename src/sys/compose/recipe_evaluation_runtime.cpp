#include "sys/compose/recipe_evaluation_runtime.h"
#include "core/text_util.h"
#include <wormedit/formats/native_project_storage.h>
#if defined(WORMEDIT_HAS_LUA)
#include "scripting/lua_curve.h"
#endif

#include <algorithm>
#include <fstream>

namespace wormedit::sys {
namespace {
void reject(recipe_form_outcome_t& result, std::string message) {
    result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
        .code = diagnostic_code_t::invalid_binding, .message = std::move(message)});
    result.complete(false);
}

auto reload_module(sv_recipe_t& recipe, const std::filesystem::path& base) -> std::string {
    if (!recipe.lua || !recipe.lua->source_reference) return "Choose a Lua module file first.";
    auto path = *recipe.lua->source_reference;
    if (path.is_relative()) path = base / path;
    std::ifstream input{path, std::ios::binary};
    if (!input) return "Cannot open Lua module: " + text_util::path_utf8(path);
    constexpr std::size_t source_limit = 16 * 1024;
    std::string source(source_limit + 1, '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    source.resize(static_cast<std::size_t>(input.gcount()));
    if (source.size() > source_limit) return "Lua module exceeds the 16 KiB source limit.";
    if (input.bad()) return "Cannot read Lua module: " + text_util::path_utf8(path);
    recipe.expression_source = std::move(source);
    recipe.lua->source_hash = native_project_content_checksum(recipe.expression_source);
    if (!base.empty() && path.is_absolute()) recipe.lua->source_reference = path.lexically_relative(base);
    return {};
}

void prepare_chart(chart_document_t& chart, recipe_form_outcome_t& result, std::stop_token stop) {
    auto* report = result.report();
    for (const auto& recipe : chart.sv_recipes) {
        if (stop.stop_requested()) return;
        if (!recipe.enabled && recipe.id != report->candidate.id) continue;
        auto prepared = sv_recipe_prepared_t::from(chart, recipe);
        if (!prepared.diagnostics.empty()) {
            result.diagnostics = std::move(prepared.diagnostics);
            result.complete(false);
            return;
        }
        report->prepared.push_back(std::move(prepared.value));
    }
    std::vector<sv_recipe_prepared_t> family;
    for (const auto& prepared : report->prepared)
        if (prepared.recipe().enabled && prepared.recipe().output == report->candidate.output)
            family.push_back(prepared);
    const auto composed = sv_recipe_compiled_output_t::from(chart, report->candidate.output, family);
    result.diagnostics = composed.diagnostics;
    if (composed.diagnostics.empty()) report->composed_points = composed.value.points;
    result.complete(result.diagnostics.empty());
}
} // namespace

recipe_evaluation_runtime_t::recipe_evaluation_runtime_t(task_scheduler_t& scheduler)
    : job_{scheduler, 2, "Invalid recipe evaluation.", "Recipe evaluation queue is busy.",
        "Recipe evaluation returned no result.", async_job_destruction_policy_t::release} {}

auto recipe_evaluation_runtime_t::request(const song_document_t& document,
    const chart_document_t& chart, const sv_recipe_t& recipe,
    const ui::timing_recipe_form_t& form, const std::filesystem::path& base) -> std::string {
    // Parsing and metadata validation are cheap and never invoke the backend.
    auto proposal = recipe_form_preview_t::propose(document, chart, recipe, form);
    const auto stamp = chart_stamp_t::from(document, chart.id);
    auto* report = proposal.report();
    if (report) {
        report->requested_form = form;
        report->requested_form->lua_reload = false;
        if (report->candidate.lua && report->candidate.lua->source_reference && !base.empty() &&
            report->candidate.lua->source_reference->is_absolute())
            report->candidate.lua->source_reference = report->candidate.lua->source_reference->lexically_relative(base);
    }
    const auto previous_origin = recipe.lua ? recipe.lua->source_reference : std::nullopt;
    const bool origin_changed = report && report->candidate.lua && report->candidate.lua->source_reference &&
        report->candidate.lua->source_reference != previous_origin;
    const bool reload = form.lua_reload || origin_changed;
    requested_ = proposal;
    pending_ = recipe_evaluation_request_t{chart, std::move(proposal), base, reload};
    stamp_ = stamp;
    recipe_ = recipe.id;
    return submit_pending();
}

auto recipe_evaluation_runtime_t::submit_pending() -> std::string {
    const auto started = job_.request([&] { return *pending_; }, stamp_, evaluate);
    if (!started) {
        // Preserve the newest draft and retry after the shared queue makes room.
        job_.cancel();
        return "Waiting for recipe evaluation…";
    }
    pending_.reset();
    return "Evaluating recipe…";
}
recipe_evaluation_runtime_t::~recipe_evaluation_runtime_t() { job_.cancel(); }

auto recipe_evaluation_runtime_t::evaluate(const recipe_evaluation_request_t& request,
    std::stop_token stop) -> job_t::work_outcome_t {
    auto result = request.proposal;
    auto* report = result.report();
    if (!report || !report->has_valid_form) return result;
    auto candidate = report->candidate;
    if (request.reload) {
        if (auto error = reload_module(candidate, request.asset_base); !error.empty()) {
            reject(result, std::move(error));
            return result;
        }
        result.report()->candidate = candidate;
    }
    result.diagnostics = validate_sv_recipe(request.chart, candidate);
    result.complete(result.diagnostics.empty());
    if (!result.diagnostics.empty()) return result;
    auto chart = request.chart;
    const auto stored = std::ranges::find(chart.sv_recipes, candidate.id, &sv_recipe_t::id);
    if (stored == chart.sv_recipes.end()) return result;
    *stored = candidate;
#if defined(WORMEDIT_HAS_LUA)
    scripting::lua_sandbox_limits_t limits;
    limits.cancellation = stop;
    scripting::lua_curve_backend_t backend{limits};
    const sv_recipe_evaluation_scope_t scope{backend};
#endif
    prepare_chart(chart, result, stop);
    return result;
}

void recipe_evaluation_runtime_t::poll(const song_document_t& document, chart_id_t chart,
    recipe_id_t selected, std::optional<recipe_form_outcome_t>& preview) {
    if (pending_) {
        if (stamp_ != chart_stamp_t::from(document, chart) || selected != recipe_) {
            cancel();
            return;
        }
        static_cast<void>(submit_pending());
    }
    publish(job_.poll(), document, chart, selected, preview);
}
void recipe_evaluation_runtime_t::wait(const song_document_t& document, chart_id_t chart,
    recipe_id_t selected, std::optional<recipe_form_outcome_t>& preview) {
    if (pending_) static_cast<void>(submit_pending());
    publish(job_.wait(), document, chart, selected, preview);
}
void recipe_evaluation_runtime_t::publish(std::optional<job_t::completion_t> completion,
    const song_document_t& document, chart_id_t chart, recipe_id_t selected,
    std::optional<recipe_form_outcome_t>& preview) {
    if (!completion) return;
    const auto& identity = *completion ? completion->value().identity : completion->error().identity;
    if (identity != chart_stamp_t::from(document, chart) || selected != recipe_) return;
    auto result = *completion ? *completion->value().payload : requested_.value_or(recipe_form_outcome_t{});
    if (!*completion) reject(result, completion->error().cause.message);
    if (!result.diagnostics.empty() && preview && preview->captured_stamp() == identity &&
        preview->report() && preview->report()->candidate.id == recipe_) {
        if (auto* report = result.report(); report && preview->report()) {
            report->prepared = preview->report()->prepared;
            report->composed_points = preview->report()->composed_points;
        }
    }
    preview = std::move(result);
}
void recipe_evaluation_runtime_t::cancel() { job_.cancel(); pending_.reset(); requested_.reset(); stamp_ = {}; recipe_ = {}; }
auto recipe_evaluation_runtime_t::is_busy() const -> bool { return pending_.has_value() || job_.is_busy(); }
} // namespace wormedit::sys
