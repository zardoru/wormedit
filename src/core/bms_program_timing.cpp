#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/timing_links.h>
#include <wormedit/core/rational.h>
#include "timing_links_internal.h"
#include <algorithm>
#include <array>
#include <map>
#include <ranges>

namespace wormedit {
namespace {
auto native_timing_category(const bms_native_event_t& event) -> std::optional<timing_category_t> {
    return std::visit([](const auto& value) -> std::optional<timing_category_t> {
        using type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<type, bpm_event_t>) return timing_category_t::bpm;
        else if constexpr (std::same_as<type, scroll_event_t>) return timing_category_t::scroll;
        else if constexpr (std::same_as<type, speed_event_t>) return timing_category_t::speed;
        else if constexpr (std::same_as<type, kiai_event_t>) return timing_category_t::kiai;
        else if constexpr (std::same_as<type, pause_event_t>) return value.kind == pause_kind_t::stop ? timing_category_t::stop : timing_category_t::delay;
        else if constexpr (std::same_as<type, warp_event_t>) return timing_category_t::warp;
        else if constexpr (std::same_as<type, timing_anchor_t>) return timing_category_t::anchor;
        else if constexpr (std::same_as<type, sv_recipe_t>) return timing_category_info_t::for_recipe(value.output);
        else return {};
    }, event);
}
auto content_timing_category(const bms_content_value_t& content) -> std::optional<timing_category_t> {
    return std::visit([](const auto& value) -> std::optional<timing_category_t> {
        using type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<type, bms_timing_definition_t> || std::same_as<type, bms_timing_reference_t>) {
            switch (value.kind) {
            case bms_timing_definition_kind_t::bpm: return timing_category_t::bpm;
            case bms_timing_definition_kind_t::stop: return timing_category_t::stop;
            case bms_timing_definition_kind_t::scroll: return timing_category_t::scroll;
            }
        } else if constexpr (std::same_as<type, bms_direct_bpm_t>) return timing_category_t::bpm;
        else if constexpr (std::same_as<type, bms_measure_length_t>) return timing_category_t::measures;
        else if constexpr (std::same_as<type, bms_native_content_t>) return native_timing_category(value.payload());
        else if constexpr (std::same_as<type, bms_metadata_declaration_t>)
            if (value.field == bms_metadata_field_t::offset) return timing_category_t::offset;
        return {};
    }, content);
}
auto has_timing_change(const chart_document_t& before, const chart_document_t& after, timing_category_t category) -> bool {
    if (category == timing_category_t::offset) return before.beat_zero_audio_offset_seconds != after.beat_zero_audio_offset_seconds;
    if (category == timing_category_t::measures) {
        if (before.divisions_per_standard_measure != after.divisions_per_standard_measure || before.measures.size() != after.measures.size()) return true;
        for (std::size_t index = 0; index < before.measures.size(); ++index)
            if (before.measures[index].id != after.measures[index].id || before.measures[index].definition != after.measures[index].definition) return true;
        return false;
    }
    const auto same = timing_link_detail::visit_timing_events(before, after, category,
        [](const auto& source, const auto& target, timing_link_detail::timing_event_filter_t filter) {
            return std::ranges::equal(source | std::views::filter(filter), target | std::views::filter(filter));
        });
    const auto recipes = [category](const sv_recipe_t& recipe) { return timing_category_info_t::for_recipe(recipe.output) == category; };
    return !same || !std::ranges::equal(before.sv_recipes | std::views::filter(recipes), after.sv_recipes | std::views::filter(recipes));
}
struct timing_program_builder_t {
    const chart_document_t& before;
    const chart_document_t& after;
    const timing_position_map_t& mapping;
    std::uint64_t next;
    bool valid{true};
    std::array<bool, timing_category_count> changed{};
    std::vector<bms_program_node_t> nodes;
    std::map<object_id_t, branch_id_t> owners;
    branch_id_t root_chart;

    auto allocate() -> object_id_t {
        if (!next || next == UINT64_MAX) { valid = false; return {}; }
        return object_id_t{next++};
    }
    void map_position(bms_program_position_t& position) {
        const auto* measure = before.find_measure(position.measure);
        if (!measure) { valid = false; return; }
        const auto capacity = measure_capacity_t::from(before.divisions_per_standard_measure, measure->definition);
        if (!capacity || capacity->row_capacity % position.fraction.denominator) { valid = false; return; }
        const auto mapped = mapping.position({position.measure,
            capacity->row_capacity / position.fraction.denominator * position.fraction.numerator}, measure_chart_end_policy_t::allow);
        const auto exact = mapped ? bms_program_position_t::from(*mapped, after, true) : std::nullopt;
        if (!exact) valid = false;
        else position = *exact;
    }
    void remap_content(bms_content_value_t& value) {
        std::visit([&](auto& content) {
            using type = std::remove_cvref_t<decltype(content)>;
            if constexpr (requires { content.position; }) map_position(content.position);
            if constexpr (std::same_as<type, bms_native_content_t>) {
                auto positions = std::vector<bms_program_position_t>(content.positions().begin(), content.positions().end());
                for (auto& position : positions) map_position(position);
                const auto remapped = bms_native_content_t::from(content.payload(), std::move(positions), content.resource_reference());
                if (!remapped) valid = false;
                else content = *remapped;
            }
        }, value);
    }
    void retain_content() {
        for (auto& node : nodes) {
            auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            std::erase_if(chart->items, [&](const auto& item) {
                const auto category = content_timing_category(item.value);
                if (!category || !changed[static_cast<std::size_t>(*category)]) return false;
                owners.emplace(item.id, node.id);
                return true;
            });
            if (changed[static_cast<std::size_t>(timing_category_t::measures)])
                for (auto& item : chart->items) remap_content(item.value);
        }
    }
    void append(bms_content_item_t item) {
        const auto previous = owners.find(item.id);
        const auto id = previous == owners.end() ? root_chart : previous->second;
        auto node = std::ranges::find(nodes, id, &bms_program_node_t::id);
        std::get<bms_chart_content_t>(node->value).items.push_back(std::move(item));
    }
    template<class Event> void event(const Event& value) {
        const auto native = bms_native_content_t::from(value, after);
        if (!native) { valid = false; return; }
        append({.id = object_id_t{value.id.value}, .value = *native});
    }
    void category(timing_category_t kind) {
        if (!changed[static_cast<std::size_t>(kind)]) return;
        if (kind == timing_category_t::measures) {
            for (const auto& measure : after.measures) append({.id = allocate(), .value = bms_measure_length_t{
                .measure = measure.id, .length = measure.definition.length, .meter = measure.definition.meter}});
        } else if (kind == timing_category_t::offset) append({.id = allocate(), .value = bms_metadata_declaration_t{
            .field = bms_metadata_field_t::offset, .value = after.beat_zero_audio_offset_seconds}});
        else (void)timing_link_detail::visit_timing_events(after, after, kind,
            [&](const auto& events, const auto&, timing_link_detail::timing_event_filter_t filter) {
                for (const auto& value : events | std::views::filter(filter)) event(value);
                return true;
            });
        for (const auto& recipe : after.sv_recipes)
            if (timing_category_info_t::for_recipe(recipe.output) == kind) event(recipe);
    }
};
}
auto bms_program_document_t::with_resolved_timing(const chart_document_t& chart, object_id_t first_id) const
    -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t> {
    const auto* before = read_chart();
    if (!before || chart.id != difficulty_ || prepared_.program.has_control_flow()) return {.diagnostics = {{
        .code = failure_code_t::unavailable, .message = "Timing links require an unconditional scoped difficulty."}}};
    const auto mapping = timing_position_map_t::from(*before, chart);
    if (!mapping) return {.diagnostics = {{.code = failure_code_t::invalid_data,
        .message = "Resolved timing has no exact musical position mapping."}}};
    timing_program_builder_t build{.before = *before, .after = chart, .mapping = *mapping, .next = first_id.value,
        .nodes = {prepared_.program.nodes().begin(), prepared_.program.nodes().end()},
        .root_chart = *prepared_.program.target_chart(prepared_.program.root())};
    for (const auto& category : timing_categories) build.changed[static_cast<std::size_t>(category.kind)] = has_timing_change(*before, chart, category.kind);
    build.retain_content();
    for (const auto& category : timing_categories) build.category(category.kind);
    if (!build.valid) return {.diagnostics = {{.code = failure_code_t::invalid_data,
        .message = "Timing propagation requires rounding or exhausts scoped identities."}}};
    auto program = bms_program_t::from(std::move(build.nodes));
    if (!program) return {.diagnostics = {{.code = program.error().code, .message = program.error().message}}};
    auto prepared = prepared_;
    prepared.program = std::move(*program);
    prepared.measures.clear();
    for (const auto& measure : chart.measures) prepared.measures.push_back(measure.id);
    prepared.settings.divisions_per_standard_measure = chart.divisions_per_standard_measure;
    prepared.settings.timing_links = chart.timing_links;
    return from(difficulty_, std::move(prepared), evaluation_);
}
} // namespace wormedit
