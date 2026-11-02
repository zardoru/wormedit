#include "osbx_lua_link.h"
#include "osbx_lua.h"
#include "lua_curve.h"
#include "../core/text_util.h"
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/rational.h>
#include <wormedit/formats/native_project_storage.h>
#include <algorithm>
#include <map>
#include <set>

namespace wormedit::scripting {
namespace {
auto link_failure(std::string message) -> diagnostic_list_t {
    return {error(diagnostic_code_t::invalid_binding, std::move(message))};
}
auto context(const lua_source_descriptor_t& source) -> lua_sandbox_context_t {
    lua_sandbox_context_t parameters;
    for (const auto& p : source.parameters)
        parameters.push_back(lua_sandbox_node_t::from(p.name, to_double(p.value)));
    return {lua_sandbox_node_t::from("parameters", std::move(parameters)),
        lua_sandbox_node_t::from("seed", std::to_string(source.seed))};
}
// Prefix on the same line keeps source line numbers unchanged. The selected
// entry is a function(t,ctx), not the SV-specific context-to-closure factory.
auto module_function(const lua_source_descriptor_t& source) -> std::string {
    return "local __module=(function() " + source.frozen_source + "\nend)(); local __fn=" +
        (source.entry_point.empty() ? "__module" : "__module['" + source.entry_point + "']") + "; ";
}
auto scalar_recipe(const osbx_lua_track_link_t& link, std::size_t component, std::size_t arity, signed_rational_t tolerance) -> sv_recipe_t {
    auto source = link.link.source;
    source.frozen_source = module_function(source) +
        "return function(p,ctx) local v=__fn(" + std::to_string(link.begin_ms) + "+ctx.absolute_row,ctx); if type(v)=='number' then " +
        (arity != 1 ? "error('Lua track value has the wrong arity')" : "return v") +
        " end; if type(v)~='table' then error('Lua track must return a number or dense numeric array') end; "
        "local n=0; for k in pairs(v) do if math.type(k)~='integer' or k<1 or k>" + std::to_string(arity) +
        " then error('Lua track value has the wrong arity') end; n=n+1 end; if n~=" + std::to_string(arity) +
        " then error('Lua track value has the wrong arity') end; return v[" +
        std::to_string(component + 1) + "] end";
    source.entry_point.clear();
    source.source_hash = native_project_content_checksum(source.frozen_source);
    return {.id = recipe_id_t{1}, .name = "OSBX Lua link", .range = {{measure_id_t{1}, 0}, {measure_id_t{2}, 0}},
        .curve = sv_recipe_curve_t::lua_expression,
        .sampling = {.mode = link.sampling.mode == osbx_lua_sampling_mode_t::adaptive ?
            sv_recipe_sampling_mode_t::adaptive : sv_recipe_sampling_mode_t::fixed,
            .fixed_point_count = link.sampling.mode == osbx_lua_sampling_mode_t::fixed ?
                std::optional<std::uint32_t>{link.sampling.fixed_point_count} : std::nullopt,
            .tolerance = tolerance,
            .max_points = link.sampling.max_points},
        .lua = sv_recipe_lua_t{.source = std::move(source)}};
}
} // namespace

auto osbx_lua_link_backend_t::prepare_track(const osbx_lua_track_link_t& link, std::size_t arity) const
    -> diagnosed_t<std::vector<osbx_key_t<>>> {
    if (auto message = validate_osbx_lua_track_link(link); !message.empty()) return {.diagnostics = link_failure(message)};
    if (!arity || arity > 6) return {.diagnostics = link_failure("Lua track arity must be 1..6")};
    const auto span = link.end_ms - link.begin_ms;
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = static_cast<std::uint64_t>(span),
        .measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}}};
    const auto tolerance = rationalized_value_t::from(link.sampling.tolerance);
    if (!tolerance || tolerance->value.numerator > static_cast<std::uint64_t>(INT64_MAX))
        return {.diagnostics = link_failure("Lua sampling tolerance cannot be represented")};
    const signed_rational_t exact_tolerance{static_cast<std::int64_t>(tolerance->value.numerator), tolerance->value.denominator};
    std::vector<osbx_track_t<>> components;
    std::set<std::int64_t> times;
    auto limits = limits_;
    limits.maximum_instructions /= arity;
    lua_curve_backend_t backend{limits};
    const sv_recipe_evaluation_scope_t scope{backend};
    for (std::size_t dimension = 0; dimension < arity; ++dimension) {
        const auto recipe = scalar_recipe(link, dimension, arity, exact_tolerance);
        auto sampled = sample_sv_recipe(chart, recipe);
        if (!sampled.diagnostics.empty()) return {.diagnostics = std::move(sampled.diagnostics)};
        if (!sampled.tolerance_met) return {.diagnostics = link_failure("Lua sampling tolerance was not met before the point cap")};
        osbx_track_t<> component{.default_value = {0}};
        for (const auto& point : sampled.points) {
            const auto time = link.begin_ms + static_cast<std::int64_t>(point.absolute_row);
            times.insert(time);
            component.keys.push_back({.time_ms = time, .value = {point.value}});
        }
        components.push_back(std::move(component));
    }
    if (times.size() > link.sampling.max_points)
        return {.diagnostics = link_failure("Combined Lua component sampling exceeds the point cap")};
    std::vector<osbx_key_t<>> samples;
    for (const auto time : times) {
        osbx_key_t<> key{.id = link.link.id + "/" + std::to_string(time), .time_ms = time};
        for (const auto& component : components)
            key.value.push_back(sample_osbx_numeric_track(component, time).front());
        samples.push_back(std::move(key));
    }
    return {.value = std::move(samples)};
}
auto osbx_lua_link_backend_t::prepare_children(const osbx_lua_link_t& link) const -> diagnosed_t<osbx_document_t> {
    if (auto message = validate_osbx_lua_link(link); !message.empty()) return {.diagnostics = link_failure(message)};
    auto limits = limits_;
    limits.seed = link.source.seed;
    // OSBX-5 owns declarations, provenance, budgets and deterministic replay.
    const auto source = module_function(link.source) + "storyboard('linked'); __fn(generator)";
    return run_osbx_lua(source, "@" + text_util::path_utf8(*link.source.source_reference), limits, context(link.source));
}
} // namespace wormedit::scripting
