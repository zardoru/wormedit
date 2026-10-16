#include <wormedit/core/lane_profile_switch.h>

#include <wormedit/core/lane_layout_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace wormedit {

// Key bindings for a built-in BMS layout whose lane IDs were just assigned, for a chart that has
// none; the built-in lanes and the layout's lanes pair up by display order.
auto input_profile_t::with_defaults(const chart_document_t& chart, const lane_layout_t& layout)
    -> std::optional<input_profile_t> {
    if (!chart.input_profile.bindings.empty())
        return std::nullopt;
    for (const auto& builtin : wormedit::built_in_bms_lane_profiles()) {
        if (builtin.profile.layout.profile_id != layout.profile_id)
            continue;
        lane_layout_t source{.lanes = builtin.profile.layout.lanes};
        auto target = layout.lanes;
        std::ranges::stable_sort(source.lanes, {}, &lane_definition_t::display_order);
        std::ranges::stable_sort(target, {}, &lane_definition_t::display_order);
        if (source.lanes.size() != target.size())
            return std::nullopt;
        auto input = builtin.profile.input;
        for (auto& binding : input.bindings) {
            const auto found = source.find(binding.lane);
            if (found == nullptr)
                return std::nullopt;
            binding.lane = target[static_cast<std::size_t>(found - source.lanes.data())].id;
        }
        return input;
    }
    return std::nullopt;
}

namespace {

constexpr std::array<std::string_view, 3> step_types{"pump-single", "pump-double", "dance-solo"};

auto measure_order(const chart_document_t& chart, measure_id_t id) -> std::optional<std::size_t> {
    const auto found = chart.find_measure(id);
    if (found == nullptr) return std::nullopt;
    return static_cast<std::size_t>(found - chart.measures.data());
}

auto built_in_profile(std::string_view profile_id) -> std::optional<lane_input_profile_t> {
    for (const auto& profile : built_in_bms_lane_profiles())
        if (profile.profile.layout.profile_id == profile_id) return profile.profile;
    for (const auto type : step_types) {
        auto profile = make_stepmania_profile_for(type);
        if (profile && profile->layout.profile_id == profile_id) return profile;
    }
    for (std::uint32_t keys = minimum_osu_mania_keys; keys <= maximum_osu_mania_keys; ++keys) {
        auto profile = make_osu_mania_profile(keys);
        if (profile && profile->layout.profile_id == profile_id) return profile;
    }
    for (const auto keys : {12u, 16u}) {
        auto profile = make_osu_mania_profile(keys, true);
        if (profile && profile->layout.profile_id == profile_id) return profile;
    }
    return std::nullopt;
}

auto materialize_initial_profile_target(document_history_t& scratch, chart_id_t chart)
    -> std::optional<local_position_t> {
    auto found = scratch.document.find_chart(chart);
    if (found == nullptr) return std::nullopt;
    if (found->measures.empty()) {
        if (!has_chart_virtual_tail(*found) || virtual_tail_measure_limit(*found) == 0)
            return std::nullopt;
        const auto materialized =
            apply_measure_edit(scratch, materialize_measures_action_t{chart, 1});
        if (!materialized.applied) return std::nullopt;
        found = scratch.document.find_chart(chart);
        return local_position_t{found->measures.front().id, 0};
    }
    return local_position_t{found->measures.front().id, 0};
}

auto make_initial_profile_layout(const chart_document_t& chart, const lane_layout_t& target)
    -> std::optional<std::pair<lane_layout_t, input_profile_t>> {
    const auto profile = built_in_profile(target.profile_id);
    if (!profile || profile->layout.lanes.size() != target.lanes.size()) return std::nullopt;
    auto layout = target;
    auto input = profile->input;
    auto largest_id = std::uint64_t{};
    for (const auto& existing : chart.lane_layouts)
        for (const auto& lane : existing.layout.lanes)
            largest_id = std::max(largest_id, lane.id.value);
    auto target_lanes = layout.lanes;
    auto profile_lanes = profile->layout.lanes;
    std::ranges::stable_sort(target_lanes, {}, &lane_definition_t::display_order);
    std::ranges::stable_sort(profile_lanes, {}, &lane_definition_t::display_order);
    std::vector<std::pair<lane_id_t, lane_id_t>> remapping;
    remapping.reserve(target_lanes.size());
    for (std::size_t index = 0; index < target_lanes.size(); ++index) {
        if (largest_id == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        const auto prior_id = profile_lanes[index].id;
        const auto new_id = lane_id_t{++largest_id};
        remapping.emplace_back(prior_id, new_id);
    }
    for (auto& lane : layout.lanes) {
        const auto mapped = std::ranges::find(remapping, lane.id,
                                               [](const auto& entry) { return entry.first; });
        if (mapped == remapping.end()) return std::nullopt;
        lane.id = mapped->second;
    }
    for (auto& binding : input.bindings) {
        const auto mapped = std::ranges::find(remapping, binding.lane,
                                               [](const auto& entry) { return entry.first; });
        if (mapped != remapping.end()) binding.lane = mapped->second;
    }
    return std::pair{std::move(layout), std::move(input)};
}

auto insert_initial_lane_profile(document_history_t& history, chart_id_t chart,
                                 const lane_layout_t& target)
    -> document_command_result_t {
    auto result = document_command_result_t{.revision = history.document.revision,
                                             .diagnostics = {}};
    auto scratch = document_history_t::from(history.document);
    const auto target_position = materialize_initial_profile_target(scratch, chart);
    if (!target_position) {
        reject(result, diagnostic_code_t::invalid_id, "The lane profile needs a valid materialized chart measure.");
        return result;
    }
    const auto found = scratch.document.find_chart(chart);
    const auto prepared = make_initial_profile_layout(*found, target);
    if (!prepared) {
        reject(result, diagnostic_code_t::invalid_id, "That lane profile is not a supported built-in profile.");
        return result;
    }
    const auto bindings = found->input_profile.bindings.empty()
        ? std::optional{prepared->second} : std::nullopt;
    const auto inserted = apply_lane_layout_edit(
        scratch,
        wormedit::insert_edit_t<wormedit::lane_layout_edit_value_t>{
            .chart = chart,
            .value = {.position = *target_position, .layout = prepared->first, .input = bindings}});
    if (!inserted.applied) return inserted;
    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(scratch.document));
    return transaction.commit(
                                   "Set initial lane profile");
}

} // namespace

auto can_use_lane_key_profiles(const lane_layout_t& layout) -> bool {
    return !layout.lanes.empty() && layout.lanes.size() <= maximum_osu_mania_keys &&
        std::ranges::all_of(layout.lanes, [](const auto& lane) {
            return lane.role == lane_role_t::playable;
        });
}

auto lane_profile_choices(std::size_t lane_count) -> std::vector<lane_input_profile_t> {
    std::vector<lane_input_profile_t> choices;
    if (const auto osu = make_osu_mania_profile(static_cast<std::uint32_t>(lane_count)))
        choices.push_back(*osu);
    if (lane_count == 12 || lane_count == 16)
        choices.push_back(*make_osu_mania_profile(static_cast<std::uint32_t>(lane_count), true));
    for (const auto step_type : step_types) {
        auto profile = make_stepmania_profile_for(step_type);
        if (profile && profile->layout.lanes.size() == lane_count)
            choices.push_back(std::move(*profile));
    }
    return choices;
}

auto lane_profile_choices() -> std::vector<lane_input_profile_t> {
    std::vector<lane_input_profile_t> choices;
    for (const auto& profile : built_in_bms_lane_profiles())
        choices.push_back(profile.profile);
    for (const auto type : step_types) {
        auto profile = make_stepmania_profile_for(type);
        if (profile) choices.push_back(std::move(*profile));
    }
    for (std::uint32_t keys = minimum_osu_mania_keys; keys <= maximum_osu_mania_keys; ++keys) {
        auto profile = make_osu_mania_profile(keys);
        if (profile) choices.push_back(std::move(*profile));
    }
    for (const auto keys : {12u, 16u}) {
        auto profile = make_osu_mania_profile(keys, true);
        if (profile) choices.push_back(std::move(*profile));
    }
    return choices;
}

auto active_lane_layout_segment(const chart_document_t& chart, local_position_t position)
    -> const lane_layout_segment_t* {
    const auto cursor = measure_order(chart, position.measure);
    const lane_layout_segment_t* best{};
    std::optional<std::size_t> best_order;
    std::uint64_t best_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto order = measure_order(chart, segment.position.measure);
        if (!order || !cursor || *order > *cursor) continue;
        if (*order == *cursor && segment.position.row > position.row) continue;
        if (!best || *order > *best_order ||
            (*order == *best_order && segment.position.row >= best_row)) {
            best = &segment;
            best_order = order;
            best_row = segment.position.row;
        }
    }
    return best;
}

namespace {
auto has_specialstyle_geometry(const lane_layout_t& current, const lane_layout_t& target) -> bool {
    const auto base = "osu-mania-" + std::to_string(target.lanes.size()) + "k";
    return (target.lanes.size() == 12 || target.lanes.size() == 16) &&
        (target.profile_id == base + "-specialstyle" ||
         (current.profile_id == base + "-specialstyle" && target.profile_id == base));
}
} // namespace

auto retarget_lane_layout(const lane_layout_t& current, const lane_layout_t& target)
    -> std::optional<lane_layout_t> {
    if (current.lanes.size() != target.lanes.size()) return std::nullopt;
    auto ordered_current = current.lanes;
    auto ordered_target = target.lanes;
    const auto order_by_visual_position = [](const lane_layout_t& layout,
                                             std::vector<lane_definition_t>& lanes) {
        std::ranges::stable_sort(lanes, [&layout](const auto& left, const auto& right) {
            if (layout.split_stage && left.stage != right.stage) return left.stage < right.stage;
            return std::pair{left.display_order, left.id.value} <
                   std::pair{right.display_order, right.id.value};
        });
    };
    order_by_visual_position(current, ordered_current);
    order_by_visual_position(target, ordered_target);
    auto result = current;
    const auto adopt_geometry = has_specialstyle_geometry(current, target);
    if (adopt_geometry) {
        result.split_stage = target.split_stage;
        result.split_after_column = target.split_after_column;
    }
    result.profile_id = target.profile_id;
    result.display_name = target.display_name;
    for (auto& lane : result.lanes) {
        const auto rank = static_cast<std::size_t>(std::ranges::find(
            ordered_current, lane.id, &lane_definition_t::id) - ordered_current.begin());
        const auto& source = ordered_target[rank];
        if (lane.role != source.role) return std::nullopt;
        lane.name = source.name;
        if (adopt_geometry) lane.stage = source.stage;
        lane.glyph = source.glyph;
        lane.key_color = source.key_color;
    }
    return result;
}

auto apply_lane_profile_switch(document_history_t& history,
                               chart_id_t chart, local_position_t position,
                               const lane_layout_t& target) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto found = history.document.find_chart(chart);
    if (found == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "The selected difficulty is no longer available.");
        return result;
    }
    const auto* segment = active_lane_layout_segment(*found, position);
    if (!segment) return insert_initial_lane_profile(history, chart, target);
    auto layout = retarget_lane_layout(segment->layout, target);
    if (!layout) {
        reject(result, diagnostic_code_t::invalid_id, "That profile has a different number of lanes or incompatible lane roles.");
        return result;
    }
    return apply_lane_layout_edit(
        history, wormedit::update_edit_t<wormedit::lane_layout_edit_value_t>{
                     .chart = chart, .id = segment->id, .value = {.layout = std::move(*layout)}});
}

} // namespace wormedit
