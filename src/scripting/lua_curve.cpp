#include "lua_curve.h"
#include "../core/text_util.h"
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/core/rational.h>

#include <cmath>
#include <limits>
#include <utility>

namespace wormedit::scripting {
namespace {
auto recipe_failure(const sv_recipe_t& recipe, std::string message) -> diagnostic_list_t {
    return {{.severity = diagnostic_severity_t::error, .code = diagnostic_code_t::unsupported_feature,
        .message = "Lua SV recipe: " + std::move(message), .object = object_id_t{recipe.id.value}}};
}
auto parameters(const sv_recipe_lua_t& lua) -> lua_sandbox_context_t {
    lua_sandbox_context_t result;
    for (const auto& parameter : lua.parameters)
        result.push_back(lua_sandbox_node_t::from(parameter.name, to_double(parameter.value)));
    return result;
}
auto inputs(const sv_recipe_lua_t& lua) -> lua_sandbox_context_t {
    lua_sandbox_context_t result;
    for (const auto& input : lua.inputs) result.push_back({static_cast<std::int64_t>(result.size() + 1),
        lua_sandbox_context_t{lua_sandbox_node_t::from_unsigned("asset_id", input.asset.value), lua_sandbox_node_t::from("content_hash", input.content_hash)}});
    return result;
}
auto measures(const chart_document_t& chart, const measure_index_t& index) -> lua_sandbox_context_t {
    lua_sandbox_context_t result;
    for (std::size_t i = 0; i < chart.measures.size(); ++i) {
        const auto& measure = chart.measures[i];
        const auto& entry = index.entries[i];
        lua_sandbox_context_t data{lua_sandbox_node_t::from_unsigned("id", measure.id.value), lua_sandbox_node_t::from_unsigned("start_row", entry.absolute_begin_row),
            lua_sandbox_node_t::from_unsigned("row_count", entry.row_capacity), lua_sandbox_node_t::from("length", to_double(measure.definition.length))};
        if (measure.definition.meter) data.push_back(lua_sandbox_node_t::from("meter", {
            lua_sandbox_node_t::from_unsigned("numerator", measure.definition.meter->numerator), lua_sandbox_node_t::from_unsigned("denominator", measure.definition.meter->denominator)}));
        result.push_back({static_cast<std::int64_t>(i + 1), std::move(data)});
    }
    return result;
}
template<class Event, class Fields>
auto timing_events(const std::vector<Event>& events, const measure_index_t& index, Fields fields)
    -> lua_sandbox_context_t {
    lua_sandbox_context_t result;
    for (const auto& event : events) {
        const auto row = index.row_of(event.position);
        if (!row) continue;
        auto data = fields(event);
        data.push_back(lua_sandbox_node_t::from_unsigned("id", event.id.value));
        data.push_back(lua_sandbox_node_t::from_unsigned("row", *row));
        result.push_back({static_cast<std::int64_t>(result.size() + 1), std::move(data)});
    }
    return result;
}
auto warps(const chart_document_t& chart, const measure_index_t& index) -> lua_sandbox_context_t {
    lua_sandbox_context_t result;
    for (const auto& warp : chart.warps) {
        const auto begin = index.row_of(warp.span.begin);
        const auto end = index.row_of(warp.span.end, measure_chart_end_policy_t::allow);
        if (!begin || !end) continue;
        result.push_back({static_cast<std::int64_t>(result.size() + 1), lua_sandbox_context_t{
            lua_sandbox_node_t::from_unsigned("id", warp.id.value), lua_sandbox_node_t::from_unsigned("start_row", *begin), lua_sandbox_node_t::from_unsigned("end_row", *end)}});
    }
    return result;
}
auto timing(const chart_document_t& chart, const measure_index_t& index) -> lua_sandbox_context_t {
    return {lua_sandbox_node_t::from("warps", warps(chart, index)),
        lua_sandbox_node_t::from("anchors", timing_events(chart.timing_anchors, index, [](const timing_anchor_t& event) {
            return lua_sandbox_context_t{lua_sandbox_node_t::from_unsigned("source_sample_frame", event.source_sample_frame), lua_sandbox_node_t::from("locked", event.locked)}; })),
        lua_sandbox_node_t::from("bpms", timing_events(chart.bpms, index, [](const bpm_event_t& event) {
            return lua_sandbox_context_t{lua_sandbox_node_t::from("value", to_double(event.beats_per_minute))}; })),
        lua_sandbox_node_t::from("scrolls", timing_events(chart.scrolls, index, [](const scroll_event_t& event) {
            return lua_sandbox_context_t{lua_sandbox_node_t::from("value", to_double(event.multiplier))}; })),
        lua_sandbox_node_t::from("speeds", timing_events(chart.speeds, index, [](const speed_event_t& event) {
            return lua_sandbox_context_t{lua_sandbox_node_t::from("value", to_double(event.multiplier)),
                lua_sandbox_node_t::from("transition", to_double(event.transition_beats)), lua_sandbox_node_t::from("transition_is_seconds", event.transition_is_seconds)}; })),
        lua_sandbox_node_t::from("pauses", timing_events(chart.pauses, index, [](const pause_event_t& event) {
            return lua_sandbox_context_t{lua_sandbox_node_t::from("duration", to_double(event.duration)),
                lua_sandbox_node_t::from("kind", std::string(event.kind == pause_kind_t::stop ? "stop" : "delay")),
                lua_sandbox_node_t::from_unsigned("unit", static_cast<std::uint64_t>(event.source_unit))}; })),
        lua_sandbox_node_t::from("kiai", timing_events(chart.kiai, index, [](const kiai_event_t& event) {
            return lua_sandbox_context_t{lua_sandbox_node_t::from("enabled", event.enabled)}; }))};
}
auto static_context(const chart_document_t& chart, const measure_index_t& index,
    const sv_recipe_t& recipe, const sv_recipe_lua_t& lua) -> lua_sandbox_context_t {
    const auto begin = index.row_of(recipe.range.begin).value_or(0);
    const auto end = index.row_of(recipe.range.end, measure_chart_end_policy_t::allow).value_or(0);
    return {lua_sandbox_node_t::from("interval", {lua_sandbox_node_t::from_unsigned("start_row", begin), lua_sandbox_node_t::from_unsigned("end_row", end)}),
        lua_sandbox_node_t::from("parameters", parameters(lua)), lua_sandbox_node_t::from("inputs", inputs(lua)), lua_sandbox_node_t::from("seed", std::to_string(lua.seed)),
        lua_sandbox_node_t::from("measures", measures(chart, index)), lua_sandbox_node_t::from("timing", timing(chart, index))};
}
auto query_context(const lua_sandbox_context_t& base, const sv_recipe_script_query_t& query)
    -> lua_sandbox_context_t {
    auto result = base;
    result.push_back(lua_sandbox_node_t::from("baseline", static_cast<double>(query.baseline)));
    result.push_back(lua_sandbox_node_t::from_unsigned("absolute_row", query.absolute_row));
    result.push_back(lua_sandbox_node_t::from_unsigned("cycle_index", query.cycle_index));
    result.push_back(lua_sandbox_node_t::from_unsigned("emission_index", query.emission_index));
    result.push_back(lua_sandbox_node_t::from_unsigned("cycle_emission_index", query.cycle_emission_index));
    result.push_back(lua_sandbox_node_t::from_unsigned("cycle_count", query.cycle_count));
    result.push_back(lua_sandbox_node_t::from_unsigned("emission_count", query.emission_count));
    return result;
}
auto numeric(const lua_sandbox_value_t& value) -> std::optional<long double> {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
    if (const auto* number = std::get_if<double>(&value); number && std::isfinite(*number)) return *number;
    return std::nullopt;
}
class lua_curve_session_t final : public sv_recipe_script_session_t {
  public:
    lua_curve_session_t(sv_recipe_t recipe, lua_sandbox_limits_t limits, lua_sandbox_context_t context,
        lua_sandbox_t sandbox, std::string source, std::string name)
        : recipe_(std::move(recipe)), limits_(std::move(limits)), context_(std::move(context)),
          sandbox_(std::move(sandbox)), source_(std::move(source)), name_(std::move(name)) {}
    auto evaluate(const sv_recipe_script_query_t& query) -> diagnosed_t<long double> override {
        if (finished_) return {0, recipe_failure(recipe_, "session is already finished")};
        if (queries_.size() >= 65536) return {0, recipe_failure(recipe_, "query count exceeds 65536")};
        const auto value = sandbox_.call_function(static_cast<double>(query.p), query_context(context_, query));
        if (!value) return {0, recipe_failure(recipe_, value.error().message)};
        const auto number = numeric(*value);
        if (!number) return {0, recipe_failure(recipe_, "curve must return one finite number")};
        queries_.push_back(query); results_.push_back(*number);
        return {*number, {}};
    }
    auto finish() -> diagnostic_list_t override {
        if (finished_) return recipe_failure(recipe_, "session is already finished");
        finished_ = true;
        if (!sandbox_.is_open()) return recipe_failure(recipe_, "cannot finish a failed Lua session");
        auto replay_limits = limits_;
        replay_limits.maximum_instructions = sandbox_.usage().remaining_instructions;
        { auto completed = std::move(sandbox_); }
        if (!replay_limits.maximum_instructions) return recipe_failure(recipe_, "instruction budget exhausted before replay");
        auto replay = lua_sandbox_t::from(replay_limits);
        if (!replay) return recipe_failure(recipe_, replay.error().message);
        const auto lua = recipe_.lua.value_or(sv_recipe_lua_t{});
        const auto installed = replay->install_function(source_, query_context(context_, {}), lua.entry_point,
            name_, lua.mode == sv_recipe_lua_mode_t::pure);
        if (!installed) return recipe_failure(recipe_, installed.error().message);
        const auto pure = lua.mode == sv_recipe_lua_mode_t::pure;
        for (std::size_t i = 0; i < queries_.size(); ++i) {
            const auto position = pure ? queries_.size() - i - 1 : i;
            const auto diagnostic = verify(*replay, position);
            if (!diagnostic.empty()) return diagnostic;
            if (pure) {
                const auto repeated = verify(*replay, position);
                if (!repeated.empty()) return repeated;
            }
        }
        return {};
    }
  private:
    auto verify(lua_sandbox_t& replay, std::size_t position) -> diagnostic_list_t {
        const auto& query = queries_[position];
        const auto value = replay.call_function(static_cast<double>(query.p), query_context(context_, query));
        if (!value) return recipe_failure(recipe_, "replay failed: " + value.error().message);
        const auto number = numeric(*value);
        if (!number || *number != results_[position]) return recipe_failure(recipe_,
            "curve is not reproducible for the declared execution mode");
        return {};
    }
    sv_recipe_t recipe_;
    lua_sandbox_limits_t limits_;
    lua_sandbox_context_t context_;
    lua_sandbox_t sandbox_;
    std::string source_, name_;
    std::vector<sv_recipe_script_query_t> queries_;
    std::vector<long double> results_;
    bool finished_{};
};
} // namespace
lua_curve_backend_t::lua_curve_backend_t(lua_sandbox_limits_t limits) : limits_(std::move(limits)) {}
auto lua_curve_backend_t::start(const chart_document_t& chart, const sv_recipe_t& recipe) const
    -> diagnosed_t<std::unique_ptr<sv_recipe_script_session_t>> {
    const auto index = measure_index_t::from(chart);
    if (!index) return {nullptr, index.error()};
    if (chart.measures.size() + chart.bpms.size() + chart.scrolls.size() + chart.speeds.size() +
        chart.pauses.size() + chart.kiai.size() + chart.warps.size() + chart.timing_anchors.size() > 65536)
        return {nullptr, recipe_failure(recipe, "snapshot exceeds 65536 entries")};
    const auto lua = recipe.lua.value_or(sv_recipe_lua_t{});
    if (!lua.source_hash.empty() && !text_util::ascii_iequal(lua.source_hash,
        native_project_content_checksum(recipe.expression_source)))
        return {nullptr, recipe_failure(recipe, "frozen source does not match its content hash")};
    if (lua.api_version != 1) return {nullptr, recipe_failure(recipe, "unsupported script API version")};
    auto limits = limits_; limits.seed = lua.seed;
    auto sandbox = lua_sandbox_t::from(limits);
    if (!sandbox) return {nullptr, recipe_failure(recipe, sandbox.error().message)};
    auto context = static_context(chart, *index, recipe, lua);
    if (lua.seed <= static_cast<std::uint64_t>(INT64_MAX)) context.push_back(lua_sandbox_node_t::from_unsigned("seed_integer", lua.seed));
    auto name = lua.source_reference ? "@" + text_util::path_utf8(*lua.source_reference) : "SV recipe " + std::to_string(recipe.id.value);
    auto source = lua.entry_point.empty()
        ? "local function values(fn) return fn end return " + recipe.expression_source : recipe.expression_source;
    auto installed = sandbox->install_function(source, query_context(context, {}), lua.entry_point,
        name, lua.mode == sv_recipe_lua_mode_t::pure);
    // Let Lua's parser distinguish function expressions from returning chunks.
    if (!installed && installed.error().code == failure_code_t::invalid_data && lua.entry_point.empty()) {
        limits.maximum_instructions = sandbox->usage().remaining_instructions;
        if (!limits.maximum_instructions) return {nullptr, recipe_failure(recipe, "instruction budget exhausted before source retry")};
        sandbox = lua_sandbox_t::from(limits);
        if (!sandbox) return {nullptr, recipe_failure(recipe, sandbox.error().message)};
        source = recipe.expression_source;
        installed = sandbox->install_function(source, query_context(context, {}), {}, name,
            lua.mode == sv_recipe_lua_mode_t::pure);
    }
    if (!installed) return {nullptr, recipe_failure(recipe, installed.error().message)};
    return {std::make_unique<lua_curve_session_t>(recipe, std::move(limits), std::move(context),
        std::move(*sandbox), std::move(source), std::move(name)), {}};
}
} // namespace wormedit::scripting
