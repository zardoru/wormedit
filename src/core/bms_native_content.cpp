#include <wormedit/core/bms_native_content.h>
#include <wormedit/core/rational.h>
#include "bms_native_content_internal.h"
#include "chart_position_storage.h"

namespace wormedit {
namespace {

auto row_position(bms_program_position_t position, const chart_document_t& chart, bool allows_end)
    -> std::optional<local_position_t> {
    const auto* measure = chart.find_measure(position.measure);
    if (!measure || !position.fraction.denominator) return std::nullopt;
    const auto capacity = measure_capacity_t::from(chart.divisions_per_standard_measure, measure->definition);
    const auto fraction = reduce_fraction(position.fraction);
    const bool is_end = allows_end && position.measure == chart.measures.back().id;
    if (!capacity || fraction.numerator > fraction.denominator ||
        (fraction.numerator == fraction.denominator && !is_end) ||
        capacity->row_capacity % fraction.denominator) return std::nullopt;
    return local_position_t{.measure = position.measure,
        .row = capacity->row_capacity / fraction.denominator * fraction.numerator};
}
auto clear_resolved_reference(bms_native_event_t& event,
    const bms_native_resource_reference_t& reference) -> bool {
    return std::visit([&](auto& value) {
        if constexpr (requires { value.sound; }) {
            if (reference.name_space != bms_resource_namespace_t::wav) return false;
            value.sound.reset();
            return true;
        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, bga_event_t>) {
            if (reference.name_space != bms_resource_namespace_t::bmp) return false;
            value.resource = {};
            return true;
        } else return false;
    }, event);
}

// A single-event scratch container reuses the chart's exhaustive position
// visitor. It is never published or compared with a projected chart.
template<class Value> auto event_container(Value value) -> chart_document_t {
    chart_document_t result;
    result.measures.emplace_back();
    native_events<Value>(result).push_back(std::move(value));
    return result;
}
}

auto bms_program_position_t::from(local_position_t position, const chart_document_t& chart, bool allows_end)
    -> std::optional<bms_program_position_t> {
    const auto* measure = chart.find_measure(position.measure);
    if (!measure) return std::nullopt;
    const auto capacity = measure_capacity_t::from(chart.divisions_per_standard_measure, measure->definition);
    const bool is_end = allows_end && position.measure == chart.measures.back().id;
    if (!capacity || position.row > capacity->row_capacity ||
        (position.row == capacity->row_capacity && !is_end)) return std::nullopt;
    return bms_program_position_t{.measure = position.measure,
        .fraction = reduce_fraction({position.row, capacity->row_capacity})};
}

auto bms_native_content_t::declared_ids() const -> std::vector<object_id_t> {
    std::vector<object_id_t> result;
    if (const auto* channel = std::get_if<sound_channel_t>(&event_)) {
        result.push_back(object_id_t{channel->source_clip.id.value});
        for (const auto& event : channel->events) result.push_back(event.id);
    }
    return result;
}

auto bms_native_content_t::from(bms_native_event_t event, const chart_document_t& geometry,
    std::optional<bms_native_resource_reference_t> reference)
    -> std::optional<bms_native_content_t> {
    if (reference && !clear_resolved_reference(event, *reference)) return std::nullopt;
    std::vector<bms_program_position_t> positions;
    bool valid = true;
    std::visit([&](auto& value) {
        using value_type = std::remove_cvref_t<decltype(value)>;
        value.id = {};
        if constexpr (requires { value.branch; }) value.branch.reset();
        auto container = event_container(value);
        for_each_chart_position(container, [&](local_position_t& position, measure_chart_end_policy_t policy) {
            const auto exact = bms_program_position_t::from(position, geometry, policy == measure_chart_end_policy_t::allow);
            if (!exact) valid = false;
            else positions.push_back(*exact);
            position = {};
        });
        value = std::move(native_events<value_type>(container).front());
    }, event);
    if (!valid) return std::nullopt;
    return bms_native_content_t{std::move(event), std::move(positions), std::move(reference)};
}

auto bms_native_content_t::build_event(object_id_t id, const chart_document_t& geometry) const
    -> std::optional<bms_native_event_t> {
    auto event = event_;
    std::size_t index{};
    bool valid = true;
    std::visit([&](auto& value) {
        using value_type = std::remove_cvref_t<decltype(value)>;
        auto container = event_container(value);
        for_each_chart_position(container, [&](local_position_t& position, measure_chart_end_policy_t policy) {
            if (index >= positions_.size()) { valid = false; return; }
            const auto row = row_position(positions_[index++], geometry, policy == measure_chart_end_policy_t::allow);
            if (!row) valid = false;
            else position = *row;
        });
        value = std::move(native_events<value_type>(container).front());
        value.id = decltype(value.id){id.value};
    }, event);
    if (!valid || index != positions_.size()) return std::nullopt;
    return event;
}
} // namespace wormedit
