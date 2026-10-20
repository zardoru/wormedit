#include "text_util.h"
#include "checked_math.h"
#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
auto workspace_document_cache_key_t::from(const workspace_document_cache_query_t& query)
    -> workspace_document_cache_key_t {
    workspace_document_cache_key_t key;
    key.assign(query);
    return key;
}

void workspace_document_cache_key_t::assign(const workspace_document_cache_query_t& query) {
    document_identity = &query.document;
    stamp = document_stamp_t::from(query.document);
    limits = query.limits;
}

auto workspace_document_cache_key_t::operator==(const workspace_document_cache_query_t& query) const
    -> bool {
    return document_identity == &query.document && stamp.is_current(query.document) &&
           limits == query.limits;
}

auto workspace_bms_branch_outline_cache_key_t::from(
    const workspace_bms_branch_outline_cache_query_t& query)
    -> workspace_bms_branch_outline_cache_key_t {
    workspace_bms_branch_outline_cache_key_t key;
    key.assign(query);
    return key;
}

void workspace_bms_branch_outline_cache_key_t::assign(
    const workspace_bms_branch_outline_cache_query_t& query) {
    document.assign(query.document);
    chart_identity = &query.chart;
    chart = query.chart.id;
    choices.assign(query.chart.active_branch_selection.begin(),
                   query.chart.active_branch_selection.end());
}

auto workspace_bms_branch_outline_cache_key_t::operator==(
    const workspace_bms_branch_outline_cache_query_t& query) const -> bool {
    return document == query.document && chart_identity == &query.chart && chart == query.chart.id &&
           std::ranges::equal(choices, query.chart.active_branch_selection);
}

auto workspace_chart_option_t::from(const chart_document_t& chart) -> workspace_chart_option_t {
    return {
        .id = chart.id,
        .difficulty_name = chart.metadata.name,
        .rating = chart.metadata.rating,
    };
}

namespace {

using diagnostic_queries::has_errors;

auto index_of(workspace_tab_t tab) noexcept -> std::optional<std::size_t> {
    const auto index = static_cast<std::size_t>(tab);
    if (index >= workspace_tab_count) {
        return std::nullopt;
    }
    return index;
}

auto add_error(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message)
    -> void {
    diagnostics.push_back(error(code, std::move(message)));
}

template <typename id_t, typename projection_t>
auto validate_ids(const auto& values, projection_t projection, std::string_view label,
                  diagnostic_list_t& diagnostics) -> void {
    std::unordered_set<std::uint64_t> seen;
    for (const auto& value : values) {
        const id_t id = projection(value);
        if (!id) {
            add_error(diagnostics, diagnostic_code_t::invalid_id,
                      std::string(label) + " has the reserved zero ID");
        } else if (!seen.insert(id.value).second) {
            add_error(diagnostics, diagnostic_code_t::duplicate_id,
                      std::string(label) + " ID is duplicated");
        }
    }
}

auto validate_binding_bounds(const song_document_t& document, const workspace_limits_t& limits)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (limits.maximum_comparison_charts < 2) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "workspace comparison chart cap must be at least two");
    }
    if (!document.id) {
        add_error(diagnostics, diagnostic_code_t::invalid_id,
                  "workspace cannot bind to a song with the reserved zero ID");
    }
    if (document.charts.size() > limits.maximum_charts) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace chart cap");
    }
    if (document.audio_assets.size() > limits.maximum_audio_assets) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace audio asset cap");
    }
    if (document.visual_assets.size() > limits.maximum_visual_assets) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace visual asset cap");
    }
    if (document.midi_assets.size() > limits.maximum_midi_assets) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace MIDI asset cap");
    }
    if (document.resources.size() > limits.maximum_resources) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace resource cap");
    }
    if (document.storyboards.size() > limits.maximum_storyboards) {
        add_error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "song exceeds the workspace storyboard cap");
    }
    validate_ids<chart_id_t>(
        document.charts, [](const auto& chart) { return chart.id; }, "chart", diagnostics);
    validate_ids<asset_id_t>(
        document.audio_assets, [](const auto& asset) { return asset.id; }, "audio asset",
        diagnostics);
    validate_ids<asset_id_t>(
        document.visual_assets, [](const auto& asset) { return asset.id; }, "visual asset",
        diagnostics);
    validate_ids<asset_id_t>(
        document.midi_assets, [](const auto& asset) { return asset.id; }, "MIDI asset",
        diagnostics);
    validate_ids<resource_id_t>(
        document.resources, [](const auto& resource) { return resource.id; }, "resource",
        diagnostics);
    std::unordered_set<std::uint64_t> asset_ids;
    for (const auto& asset : document.audio_assets) {
        if (asset.id) {
            asset_ids.insert(asset.id.value);
        }
    }
    for (const auto& asset : document.visual_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_error(diagnostics, diagnostic_code_t::duplicate_id,
                      "audio and visual assets share a duplicate ID");
        }
    }
    for (const auto& asset : document.midi_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_error(diagnostics, diagnostic_code_t::duplicate_id,
                      "audio, visual, and MIDI assets share a duplicate ID");
        }
    }
    validate_ids<storyboard_id_t>(
        document.storyboards, [](const auto& storyboard) { return storyboard.id; }, "storyboard",
        diagnostics);
    return diagnostics;
}

auto binding_diagnostics(const song_document_t& document, const workspace_limits_t& limits,
                         workspace_binding_validation_cache_t* cache) -> diagnostic_list_t {
    if (!cache) return validate_binding_bounds(document, limits);
    const workspace_document_cache_query_t key{.document = document, .limits = limits};
    return cache->get_or_build(key, std::bind_front(validate_binding_bounds,
        std::cref(key.document), std::cref(key.limits)));
}



auto default_comparison_charts(const song_document_t& document, std::size_t maximum)
    -> std::vector<chart_id_t> {
    std::vector<chart_id_t> result;
    result.reserve(std::min({document.charts.size(), maximum, std::size_t{2}}));
    for (const auto& chart : document.charts) {
        if (result.size() == maximum || result.size() == 2) {
            break;
        }
        result.push_back(chart.id);
    }
    return result;
}

auto is_comparison_selection_valid(const song_document_t& document,
                                const std::vector<chart_id_t>& charts, std::size_t maximum)
    -> bool {
    if (charts.size() > maximum ||
        (document.charts.size() >= 2 ? charts.size() < 2
                                     : charts.size() > document.charts.size())) {
        return false;
    }
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(charts.size());
    return std::ranges::all_of(charts, [&](const auto chart) {
        return chart && seen.insert(chart.value).second && document.find_chart(chart) != nullptr;
    });
}

struct clip_location_t {
    const audio_clip_t* clip{};
    const sound_channel_t* channel{};
    bool duplicate{};
};

auto find_clip(const song_document_t& document, clip_id_t id) -> clip_location_t {
    clip_location_t result;
    for (const auto& chart : document.charts) {
        for (const auto& channel : chart.sound_channels) {
            if (channel.source_clip.id == id) {
                if (result.clip != nullptr) {
                    result.duplicate = true;
                } else {
                    result = {&channel.source_clip, &channel, false};
                }
            }
        }
    }
    return result;
}

auto has_object_id(const song_document_t& document, object_id_t id) -> bool {
    if (!id) {
        return false;
    }
    for (const auto& chart : document.charts) {
        const auto in = [id](const auto& values) {
            return std::ranges::any_of(values, [id](const auto& value) { return value.id == id; });
        };
        if (in(chart.lane_layouts) || in(chart.bgm_source_lanes) || in(chart.bpms) ||
            in(chart.pauses) || in(chart.warps) || in(chart.scrolls) || in(chart.speeds) ||
            in(chart.kiai) || in(chart.timing_anchors) || in(chart.bookmarks) ||
            in(chart.sections) || in(chart.bga_events) || in(chart.sound_channels)) {
            return true;
        }
        for (const auto& measure : chart.measures) {
            if (in(measure.notes) || in(measure.bgm_events)) {
                return true;
            }
        }
        for (const auto& channel : chart.sound_channels) {
            if (in(channel.events)) {
                return true;
            }
        }
    }
    return false;
}

auto has_focus_target(const song_document_t& document, const workspace_focus_target_t& target)
    -> bool {
    return std::visit(
        [&document](const auto& id) -> bool {
            using id_t = std::remove_cvref_t<decltype(id)>;
            if constexpr (std::is_same_v<id_t, std::monostate>) {
                return true;
            } else if constexpr (std::is_same_v<id_t, chart_id_t>) {
                return static_cast<bool>(id) && document.find_chart(id) != nullptr;
            } else if constexpr (std::is_same_v<id_t, object_id_t>) {
                return has_object_id(document, id);
            } else if constexpr (std::is_same_v<id_t, asset_id_t>) {
                return static_cast<bool>(id) &&
                       (document.find_audio_asset(id) != nullptr ||
                        std::ranges::any_of(document.visual_assets,
                                            [id](const auto& asset) { return asset.id == id; }) ||
                        std::ranges::any_of(document.midi_assets,
                                            [id](const auto& asset) { return asset.id == id; }));
            } else if constexpr (std::is_same_v<id_t, clip_id_t>) {
                const auto clip = find_clip(document, id);
                return static_cast<bool>(id) && clip.clip != nullptr && !clip.duplicate;
            } else if constexpr (std::is_same_v<id_t, resource_id_t>) {
                return static_cast<bool>(id) &&
                       std::ranges::any_of(document.resources, [id](const auto& resource) {
                           return resource.id == id;
                       });
            } else if constexpr (std::is_same_v<id_t, storyboard_id_t>) {
                return static_cast<bool>(id) &&
                       std::ranges::any_of(document.storyboards, [id](const auto& storyboard) {
                           return storyboard.id == id;
                       });
            }
            return false;
        },
        target);
}

[[maybe_unused]] auto make_chart_option(const chart_document_t& chart) -> workspace_chart_option_t {
    return workspace_chart_option_t::from(chart);
}

auto checked_add(std::size_t& total, std::size_t value) -> bool {
    return checked_math::account_size(total, value);
}

auto enforce_summary_cap(std::size_t value, const workspace_controller_t& workspace,
                         std::string_view description, diagnostic_list_t& diagnostics) -> bool {
    if (value <= workspace.limits.maximum_summary_items) {
        return true;
    }
    add_error(diagnostics, diagnostic_code_t::invalid_resolution,
              std::string(description) + " exceeds the workspace summary cap");
    return false;
}

auto is_audio_clip_valid_for_asset(const audio_clip_t& clip, const audio_asset_t& asset) -> bool {
    if (clip.begin_frame >
        asset.stream.frame_count.value_or(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    if (!clip.end_frame.has_value()) {
        return true;
    }
    return *clip.end_frame >= clip.begin_frame &&
           *clip.end_frame <=
               asset.stream.frame_count.value_or(std::numeric_limits<std::uint64_t>::max());
}





auto branch_kind_name(bms_control_kind_t kind) -> std::string_view {
    switch (kind) {
    case bms_control_kind_t::root:
        return "BMS program";
    case bms_control_kind_t::raw_statement:
        return "Statement";
    case bms_control_kind_t::random:
        return "RANDOM";
    case bms_control_kind_t::set_random:
        return "SETRANDOM";
    case bms_control_kind_t::if_group:
        return "IF group";
    case bms_control_kind_t::if_branch:
        return "IF";
    case bms_control_kind_t::else_if_branch:
        return "ELSEIF";
    case bms_control_kind_t::else_branch:
        return "ELSE";
    case bms_control_kind_t::switch_block:
        return "SWITCH";
    case bms_control_kind_t::set_switch:
        return "SETSWITCH";
    case bms_control_kind_t::case_branch:
        return "CASE";
    case bms_control_kind_t::default_branch:
        return "DEFAULT";
    case bms_control_kind_t::skip:
        return "SKIP";
    }
    return "BMS node";
}

auto bounded_utf8_prefix(std::string_view text, std::size_t maximum_bytes) -> std::string {
    return std::string(text_util::truncate_utf8(text, maximum_bytes));
}

struct branch_outline_context_t {
    const chart_document_t& chart;
    const workspace_limits_t& limits;
    workspace_bms_branch_outline_view_t& outline;
    std::size_t maximum_diagnostics{};
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> selections_by_id;
    std::vector<bool> matched_selections;
    std::vector<bool> active_selections;
    std::unordered_set<std::uint64_t> diagnosed_selection_ids;
    std::unordered_set<std::uint64_t> node_ids;
    std::size_t stored_text_bytes{};
    bool diagnostic_cap_reported{};
    bool row_cap_reported{};
    bool depth_cap_reported{};
    bool text_cap_reported{};

    branch_outline_context_t(const chart_document_t& chart_value,
                             const workspace_limits_t& limits_value,
                             workspace_bms_branch_outline_view_t& outline_value,
                             std::size_t diagnostic_limit)
        : chart(chart_value), limits(limits_value), outline(outline_value),
          maximum_diagnostics(diagnostic_limit) {}
};

struct branch_row_info_t {
    std::optional<std::size_t> source_order;
    std::optional<std::int64_t> stored_choice_value;
    std::optional<std::int64_t> choice_value;
    bool choice_identity_unambiguous{true};
};

auto add_branch_diagnostic(branch_outline_context_t& context, diagnostic_code_t code,
                           std::string message) -> void {
    auto& diagnostics = context.outline.diagnostics;
    if (diagnostics.size() < context.maximum_diagnostics) {
        diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                               .code = code,
                               .message = std::move(message),
                               .object = {}});
        return;
    }
    if (context.diagnostic_cap_reported)
        return;
    context.diagnostic_cap_reported = true;
    context.outline.diagnostics_truncated = true;
    if (!diagnostics.empty())
        diagnostics.back() = {
            .severity = diagnostic_severity_t::warning,
            .code = diagnostic_code_t::invalid_resolution,
            .message = "additional BMS branch outline diagnostics were capped",
            .object = {},
        };
}

auto add_branch_diagnostic_once(branch_outline_context_t& context, bool& reported,
                                diagnostic_code_t code, std::string message) -> void {
    if (reported)
        return;
    reported = true;
    add_branch_diagnostic(context, code, std::move(message));
}

auto index_branch_selections(branch_outline_context_t& context) -> void {
    const auto limit = std::max<std::size_t>(context.limits.maximum_bms_branch_rows, 1);
    const auto count = std::min(context.chart.active_branch_selection.size(), limit);
    context.selections_by_id.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& selection = context.chart.active_branch_selection[index];
        context.selections_by_id[selection.branch.value].push_back(index);
    }
    if (context.chart.active_branch_selection.size() > count)
        add_branch_diagnostic(context, diagnostic_code_t::invalid_resolution,
                              "BMS branch choice metadata exceeds the outline inspection cap");
    context.matched_selections.resize(count);
    context.active_selections.resize(count);
}

auto is_branch_row_allowed(branch_outline_context_t& context, std::size_t depth) -> bool {
    if (context.outline.rows_truncated)
        return false;
    if (depth > context.limits.maximum_bms_branch_depth) {
        context.outline.rows_truncated = true;
        context.outline.choices_truncated = true;
        add_branch_diagnostic_once(context, context.depth_cap_reported,
                                   diagnostic_code_t::invalid_resolution,
                                   "BMS branch outline was truncated at the configured depth cap");
        return false;
    }
    if (context.outline.rows.size() >= context.limits.maximum_bms_branch_rows) {
        context.outline.rows_truncated = true;
        context.outline.choices_truncated = true;
        add_branch_diagnostic_once(context, context.row_cap_reported,
                                   diagnostic_code_t::invalid_resolution,
                                   "BMS branch outline was truncated at the configured row cap");
        return false;
    }
    return true;
}

auto prepare_branch_row(branch_outline_context_t& context, const bms_control_node_t& node,
                        std::size_t depth, bool active, bool selected)
    -> std::optional<branch_row_info_t> {
    auto& outline = context.outline;
    if (!is_branch_row_allowed(context, depth))
        return std::nullopt;
    if (!node.id || !context.node_ids.insert(node.id.value).second)
        add_branch_diagnostic(context, diagnostic_code_t::invalid_id,
                              "BMS branch outline contains a zero or duplicate node ID");

    auto display_source = std::string_view(node.original_text);
    std::string fallback_display;
    if (display_source.empty()) {
        fallback_display = branch_kind_name(node.kind);
        if (!node.raw_value.empty()) {
            fallback_display += " ";
            fallback_display += node.raw_value;
        }
        display_source = fallback_display;
    }
    const auto available_text_bytes = context.limits.maximum_bms_branch_display_bytes >
                                              context.stored_text_bytes
        ? context.limits.maximum_bms_branch_display_bytes - context.stored_text_bytes
        : 0;
    const auto text_budget = std::min(context.limits.maximum_bms_branch_text_bytes,
                                      available_text_bytes);
    auto display_text = bounded_utf8_prefix(display_source, text_budget);
    const bool display_text_truncated = display_text.size() < display_source.size();
    if (display_text_truncated) {
        outline.display_text_truncated = true;
        add_branch_diagnostic_once(context, context.text_cap_reported,
                                   diagnostic_code_t::invalid_resolution,
                                   "BMS branch display text was truncated at the configured byte cap");
    }
    context.stored_text_bytes += display_text.size();
    auto source_order = std::optional<std::size_t>{node.source_order};
    if (node.kind == bms_control_kind_t::root)
        source_order.reset();
    else if (node.kind == bms_control_kind_t::if_group && !node.children.empty())
        source_order = node.children.front().source_order;

    branch_row_info_t info{.source_order = source_order,
                           .stored_choice_value = {},
                           .choice_value = {},
                           .choice_identity_unambiguous = true};
    const auto choice_indices = context.selections_by_id.find(node.id.value);
    if (is_choice_control(node.kind) && choice_indices != context.selections_by_id.end()) {
        for (const auto index : choice_indices->second) {
            context.matched_selections[index] = true;
            context.active_selections[index] = context.active_selections[index] || active;
        }
        if (choice_indices->second.size() > 1) {
            info.choice_identity_unambiguous = false;
            if (context.diagnosed_selection_ids.insert(node.id.value).second)
                add_branch_diagnostic(context, diagnostic_code_t::invalid_id,
                                      "BMS branch control has duplicate selected values");
        } else {
            info.stored_choice_value = context.chart.active_branch_selection[
                choice_indices->second.front()].selected_value;
            if (active)
                info.choice_value = info.stored_choice_value;
        }
    }
    outline.rows.push_back({.kind = node.kind,
                            .id = node.id,
                            .depth = depth,
                            .source_order = source_order,
                            .active = active,
                            .selected = selected,
                            .display_text = std::move(display_text),
                            .display_text_truncated = display_text_truncated});
    return info;
}

void visit_branch_node(branch_outline_context_t& context, const bms_control_node_t& node,
                       std::size_t depth, bool active,
                       std::optional<std::int64_t> random_value, bool selected,
                       bool* switch_stopped);

auto visit_branch_children(branch_outline_context_t& context,
                           const bms_control_node_t& node, std::size_t depth,
                           bool active, std::optional<std::int64_t> random_value,
                           bool* switch_stopped) -> void {
    for (const auto& child : node.children) {
        if (context.outline.rows_truncated)
            break;
        const bool effective_active = active &&
            (switch_stopped == nullptr || !*switch_stopped);
        visit_branch_node(context, child, depth + 1, effective_active,
                          random_value, false, switch_stopped);
    }
}

auto append_random_choice(branch_outline_context_t& context, const bms_control_node_t& node,
                          const branch_row_info_t& info, std::size_t depth, bool active,
                          bool* switch_stopped) -> void {
    const bool valid_choice = active && info.choice_value && node.value &&
        *info.choice_value >= 1 && *info.choice_value <= *node.value;
    workspace_bms_branch_choice_t choice{
        .id = node.id,
        .kind = node.kind,
        .source_order = info.source_order,
        .active = active,
        .editable = active && info.choice_identity_unambiguous && node.value &&
                    *node.value > 0 && (!info.stored_choice_value || valid_choice),
        .selected_value = info.stored_choice_value,
        .previous_value = {},
        .next_value = {},
        .can_clear = active && info.choice_identity_unambiguous && info.stored_choice_value,
    };
    if (choice.editable) {
        if (!info.stored_choice_value) {
            choice.next_value = 1;
        } else {
            if (*info.stored_choice_value > 1)
                choice.previous_value = *info.stored_choice_value - 1;
            if (*info.stored_choice_value < *node.value)
                choice.next_value = *info.stored_choice_value + 1;
        }
    }
    context.outline.choices.push_back(std::move(choice));
    context.outline.rows.back().selected = valid_choice;
    if (active && info.choice_value && !valid_choice)
        add_branch_diagnostic(context, diagnostic_code_t::invalid_resolution,
                              "BMS RANDOM selection is outside the declared range");
    if (valid_choice) {
        const auto indices = context.selections_by_id.find(node.id.value);
        if (indices != context.selections_by_id.end() && indices->second.size() == 1)
            context.active_selections[indices->second.front()] = true;
    }
    visit_branch_children(context, node, depth, valid_choice,
                          valid_choice ? info.choice_value : std::nullopt, switch_stopped);
}

auto selected_if_branch(const bms_control_node_t& node, bool active,
                        std::optional<std::int64_t> random_value)
    -> const bms_control_node_t* {
    const bms_control_node_t* selected{};
    if (!active || !random_value)
        return selected;
    for (const auto& child : node.children) {
        if ((child.kind == bms_control_kind_t::if_branch ||
             child.kind == bms_control_kind_t::else_if_branch) && child.value == random_value) {
            selected = &child;
            break;
        }
        if (child.kind == bms_control_kind_t::else_branch)
            selected = &child;
    }
    return selected;
}

auto append_if_branches(branch_outline_context_t& context, const bms_control_node_t& node,
                        std::size_t depth, bool active,
                        std::optional<std::int64_t> random_value,
                        bool* switch_stopped) -> void {
    const auto selected = selected_if_branch(node, active, random_value);
    context.outline.rows.back().selected = selected != nullptr;
    for (const auto& child : node.children) {
        if (context.outline.rows_truncated)
            break;
        const bool is_selected = &child == selected;
        visit_branch_node(context, child, depth + 1, active && is_selected,
                          random_value, is_selected, switch_stopped);
    }
}

auto selected_switch_branch(branch_outline_context_t& context,
                            const bms_control_node_t& node, bool active,
                            std::optional<std::int64_t> choice_value)
    -> const bms_control_node_t* {
    const bms_control_node_t* selected{};
    const bms_control_node_t* fallback{};
    if (!active || !choice_value)
        return selected;
    for (const auto& child : node.children) {
        if (child.kind == bms_control_kind_t::case_branch && child.value == choice_value) {
            selected = &child;
            break;
        }
        if (child.kind == bms_control_kind_t::default_branch)
            fallback = &child;
    }
    if (!selected)
        selected = fallback;
    if (!selected)
        add_branch_diagnostic(context, diagnostic_code_t::invalid_resolution,
                              "BMS SWITCH selection has no matching CASE or DEFAULT");
    return selected;
}

auto switch_case_values(const bms_control_node_t& node, bool& has_default)
    -> std::vector<std::int64_t> {
    std::vector<std::int64_t> values;
    for (const auto& child : node.children) {
        if (child.kind == bms_control_kind_t::case_branch && child.value)
            values.push_back(*child.value);
        else if (child.kind == bms_control_kind_t::default_branch)
            has_default = true;
    }
    std::ranges::sort(values);
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

auto make_switch_choice(const bms_control_node_t& node, const branch_row_info_t& info,
                        bool active, const std::vector<std::int64_t>& case_values,
                        bool has_default) -> workspace_bms_branch_choice_t {
    const bool stored_matches_case = info.stored_choice_value &&
        std::ranges::find(case_values, *info.stored_choice_value) != case_values.end();
    const bool stored_is_valid = !info.stored_choice_value || stored_matches_case || has_default;
    workspace_bms_branch_choice_t choice{
        .id = node.id,
        .kind = node.kind,
        .source_order = info.source_order,
        .active = active,
        .editable = active && info.choice_identity_unambiguous && stored_is_valid &&
                    (!case_values.empty() || has_default),
        .selected_value = info.stored_choice_value,
        .previous_value = {},
        .next_value = {},
        .can_clear = active && info.choice_identity_unambiguous && info.stored_choice_value,
    };
    if (!choice.editable)
        return choice;
    if (!info.stored_choice_value) {
        choice.next_value = case_values.empty() ? std::optional<std::int64_t>{0}
                                               : std::optional{case_values.front()};
        return choice;
    }
    const auto lower = std::ranges::lower_bound(case_values, *info.stored_choice_value);
    if (lower != case_values.begin())
        choice.previous_value = *std::prev(lower);
    const auto upper = std::ranges::upper_bound(case_values, *info.stored_choice_value);
    if (upper != case_values.end())
        choice.next_value = *upper;
    return choice;
}

auto append_switch_branches(branch_outline_context_t& context, const bms_control_node_t& node,
                            const branch_row_info_t& info, std::size_t depth, bool active,
                            std::optional<std::int64_t> random_value,
                            bool* switch_stopped) -> void {
    const auto selected = selected_switch_branch(context, node, active, info.choice_value);
    bool has_default{};
    const auto cases = switch_case_values(node, has_default);
    context.outline.choices.push_back(
        make_switch_choice(node, info, active, cases, has_default));
    context.outline.rows.back().selected = selected != nullptr;
    bool entered{};
    bool stopped{};
    for (const auto& child : node.children) {
        if (context.outline.rows_truncated)
            break;
        if (is_switch_branch(child.kind))
            entered = entered || &child == selected;
        const bool child_active = active && entered && !stopped;
        visit_branch_node(context, child, depth + 1, child_active, random_value,
                          &child == selected, &stopped);
    }
    (void)switch_stopped;
}

auto visit_branch_node(branch_outline_context_t& context, const bms_control_node_t& node,
                       std::size_t depth, bool active,
                       std::optional<std::int64_t> random_value, bool selected,
                       bool* switch_stopped) -> void {
    auto info = prepare_branch_row(context, node, depth, active, selected);
    if (!info)
        return;
    if (node.kind == bms_control_kind_t::skip) {
        if (active && switch_stopped)
            *switch_stopped = true;
        return;
    }
    if (node.kind == bms_control_kind_t::random) {
        append_random_choice(context, node, *info, depth, active, switch_stopped);
        return;
    }
    if (node.kind == bms_control_kind_t::if_group) {
        append_if_branches(context, node, depth, active, random_value, switch_stopped);
        return;
    }
    if (node.kind == bms_control_kind_t::switch_block) {
        append_switch_branches(context, node, *info, depth, active, random_value,
                               switch_stopped);
        return;
    }
    visit_branch_children(context, node, depth, active, random_value, switch_stopped);
}

auto report_unmatched_branch_selections(branch_outline_context_t& context) -> void {
    if (context.outline.rows_truncated)
        return;
    for (std::size_t index = 0; index < context.matched_selections.size(); ++index) {
        if (!context.matched_selections[index])
            add_branch_diagnostic(context, diagnostic_code_t::invalid_id,
                                  "BMS branch choice refers to a missing control node");
        else if (!context.active_selections[index])
            add_branch_diagnostic(context, diagnostic_code_t::invalid_id,
                                  "BMS branch choice is stored beneath an inactive branch");
    }
}

} // namespace

auto workspace_bms_branch_outline_view_t::from(const song_document_t& document,
                                               const chart_document_t& chart,
                                               const workspace_limits_t& limits,
                                               workspace_bms_branch_outline_cache_t* cache)
    -> workspace_shared_bms_branch_outline_t {
    // Retained ordinary statements are visible too; only an absent tree is empty.
    if (!chart.bms_control_tree) return {};
    const auto build = [&] {
        return std::make_shared<const workspace_bms_branch_outline_view_t>(
            workspace_bms_branch_outline_view_t::from(chart, limits));
    };
    if (!cache) return build();
    const workspace_bms_branch_outline_cache_query_t key{
        .document = {.document = document, .limits = limits}, .chart = chart};
    return cache->get_or_build(key, build);
}

auto workspace_compose_view_t::bms_branch_outline() const
    -> const workspace_bms_branch_outline_view_t& {
    static const workspace_bms_branch_outline_view_t empty;
    return shared_bms_branch_outline ? *shared_bms_branch_outline : empty;
}

auto workspace_bms_branch_outline_view_t::from(const chart_document_t& chart,
                                               const workspace_limits_t& limits)
    -> workspace_bms_branch_outline_view_t {
    workspace_bms_branch_outline_view_t outline;
    if (!chart.bms_control_tree)
        return outline;
    outline.available = true;
    branch_outline_context_t context{chart, limits, outline,
                                     limits.maximum_validation_diagnostics};
    index_branch_selections(context);
    visit_branch_node(context, *chart.bms_control_tree, 0, true, std::nullopt, false, nullptr);
    report_unmatched_branch_selections(context);
    return outline;
}

auto is_workspace_tab_enabled(workspace_tab_t tab) noexcept -> bool {
    // The Timing screen is retired: Compose's timing focus mode carries its functions.
    return index_of(tab).has_value() && tab != workspace_tab_t::storyboard &&
           tab != workspace_tab_t::timing;
}

auto workspace_controller_t::from(const song_document_t& document, const workspace_limits_t& limits)
    -> workspace_bind_outcome_t {
    workspace_bind_outcome_t result{};
    result.diagnostics = validate_binding_bounds(document, limits);
    if (has_errors(result.diagnostics)) {
        return result;
    }

    workspace_controller_t workspace{
        .song = document.id,
        .document_revision = document.revision,
        .limits = limits,
    };
    if (!document.charts.empty()) {
        workspace.selected_chart = document.charts.front().id;
    }
    const auto music = std::ranges::find_if(document.resources,
        std::bind_back(&resource_entry_t::has_role, resource_role_t::music));
    if (music != document.resources.end() && document.find_audio_asset(music->asset))
        workspace.transport.audio_asset = music->asset;
    workspace.comparison_charts =
        default_comparison_charts(document, limits.maximum_comparison_charts);
    result.value = std::move(workspace);
    return result;
}

auto workspace_controller_t::from(const document_history_t& history,
                                  const workspace_limits_t& limits) -> workspace_bind_outcome_t {
    auto result = workspace_controller_t::from(history.document, limits);
    if (result.value.has_value()) {
        result.value->history = {.available = true,
                                     .undo_depth = history.undo.size(),
                                     .redo_depth = history.redo.size(),
                                     .dirty = is_document_dirty(history)};
    }
    return result;
}

auto rebind_transport(workspace_transport_t& transport, const song_document_t& document) -> void {
    if (!transport.audio_asset.has_value()) {
        return;
    }
    const auto* asset = document.find_audio_asset(*transport.audio_asset);
    if (asset == nullptr) {
        transport = {};
        return;
    }
    const auto frame_count = asset->stream.frame_count;
    if (frame_count.has_value()) {
        transport.position_frame = std::min(transport.position_frame, *frame_count);
    }
    if (transport.selected_clip.has_value()) {
        const auto clip = find_clip(document, *transport.selected_clip);
        if (clip.clip == nullptr || clip.duplicate || clip.clip->source != asset->id ||
            !is_audio_clip_valid_for_asset(*clip.clip, *asset)) {
            transport.selected_clip.reset();
        }
    }
    if (transport.loop_frames.has_value()) {
        const auto [begin, end] = *transport.loop_frames;
        if (begin >= end || (frame_count.has_value() && end > *frame_count)) {
            transport.loop_frames.reset();
        }
    }
}

auto rebind_selection(workspace_controller_t& workspace, const song_document_t& document) -> void {
    if (!index_of(workspace.active_tab).has_value() ||
        !is_workspace_tab_enabled(workspace.active_tab)) {
        workspace.active_tab = workspace_tab_t::setup;
    }
    if (!workspace.selected_chart.has_value() ||
        document.find_chart(*workspace.selected_chart) == nullptr) {
        workspace.selected_chart =
            document.charts.empty() ? std::nullopt : std::optional{document.charts.front().id};
    }
    if (!is_comparison_selection_valid(document, workspace.comparison_charts,
                                    workspace.limits.maximum_comparison_charts)) {
        workspace.comparison_charts =
            default_comparison_charts(document, workspace.limits.maximum_comparison_charts);
    }
    for (auto& focus : workspace.tab_focus) {
        if (focus.region.size() > workspace.limits.maximum_focus_region_bytes ||
            !has_focus_target(document, focus.target)) {
            focus = {};
        }
    }
    if (workspace.selected_visual_asset &&
        std::ranges::none_of(document.visual_assets, [&](const auto& asset) {
            return asset.id == *workspace.selected_visual_asset &&
                   asset.kind == asset_kind_t::image;
        })) {
        workspace.selected_visual_asset.reset();
    }
}

auto rebind_workspace(workspace_controller_t& workspace, const song_document_t& document)
    -> dispatch_outcome_t {
    dispatch_outcome_t result{};
    if (document.id != workspace.song) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace cannot be rebound to a different song identity");
        return result;
    }
    result.diagnostics = validate_binding_bounds(document, workspace.limits);
    if (has_errors(result.diagnostics)) {
        return result;
    }

    rebind_selection(workspace, document);
    rebind_transport(workspace.transport, document);

    workspace.document_revision = document.revision;
    result.value = true;
    return result;
}

auto rebind_workspace(workspace_controller_t& workspace, const document_history_t& history)
    -> dispatch_outcome_t {
    auto result = rebind_workspace(workspace, history.document);
    if (result.is_accepted(diagnostic_acceptance_policy_t::require_empty)) {
        workspace.history = {.available = true,
                             .undo_depth = history.undo.size(),
                             .redo_depth = history.redo.size(),
                             .dirty = is_document_dirty(history)};
    }
    return result;
}

struct workspace_action_context_t {
    workspace_controller_t& proposal;
    const song_document_t& document;
    diagnostic_list_t& diagnostics;

    auto reject(diagnostic_code_t code, std::string message) -> void {
        add_error(diagnostics, code, std::move(message));
    }
};

auto apply_action(workspace_action_context_t& context, const workspace_switch_tab_action_t& action)
    -> void {
    if (!index_of(action.tab).has_value()) {
        context.reject(diagnostic_code_t::invalid_id, "workspace tab value is invalid");
    } else if (!is_workspace_tab_enabled(action.tab)) {
        context.reject(diagnostic_code_t::unsupported_feature,
                       "storyboard editing is not available yet");
    } else {
        context.proposal.active_tab = action.tab;
    }
}

auto apply_action(workspace_action_context_t& context,
                  const workspace_select_chart_action_t& action) -> void {
    if (!action.chart) {
        context.reject(diagnostic_code_t::invalid_id, "selected chart uses the reserved zero ID");
    } else if (context.document.find_chart(action.chart) == nullptr) {
        context.reject(diagnostic_code_t::invalid_id,
                       "selected chart does not exist in the bound document");
    } else {
        context.proposal.selected_chart = action.chart;
    }
}

auto apply_action(workspace_action_context_t& context,
                  const workspace_set_comparison_charts_action_t& action) -> void {
    if (action.charts.size() < 2 ||
        action.charts.size() > context.proposal.limits.maximum_comparison_charts) {
        context.reject(diagnostic_code_t::invalid_resolution,
                       "comparison requires two or more charts within its configured cap");
        return;
    }
    std::unordered_set<std::uint64_t> charts;
    for (const auto chart : action.charts) {
        if (!chart || context.document.find_chart(chart) == nullptr ||
            !charts.insert(chart.value).second) {
            context.reject(diagnostic_code_t::invalid_id,
                           "comparison charts must be present and unique");
            return;
        }
    }
    context.proposal.comparison_charts = action.charts;
}

auto apply_action(workspace_action_context_t& context, const workspace_set_focus_action_t& action)
    -> void {
    const auto tab_index = index_of(action.tab);
    if (!tab_index.has_value() || !is_workspace_tab_enabled(action.tab)) {
        context.reject(diagnostic_code_t::unsupported_feature,
                       "focus cannot be assigned to an invalid or disabled tab");
    } else if (action.region.empty() ||
               action.region.size() > context.proposal.limits.maximum_focus_region_bytes) {
        context.reject(diagnostic_code_t::invalid_resolution,
                       "logical focus region is empty or exceeds its byte cap");
    } else if (!has_focus_target(context.document, action.target)) {
        context.reject(diagnostic_code_t::invalid_id,
                       "logical focus target does not exist in the bound document");
    } else {
        context.proposal.tab_focus[*tab_index] = {.region = action.region, .target = action.target};
    }
}

auto apply_action(workspace_action_context_t& context, const workspace_clear_focus_action_t& action)
    -> void {
    const auto tab_index = index_of(action.tab);
    if (!tab_index.has_value()) {
        context.reject(diagnostic_code_t::invalid_id, "workspace tab value is invalid");
    } else {
        context.proposal.tab_focus[*tab_index] = {};
    }
}

auto apply_audio_selection(workspace_action_context_t& context,
                           const workspace_select_asset_action_t& action) -> void {
    if (action.asset.has_value() &&
        (!*action.asset || context.document.find_audio_asset(*action.asset) == nullptr)) {
        context.reject(diagnostic_code_t::invalid_id,
                       "selected audio asset does not exist in the bound document");
    } else if (context.proposal.transport.audio_asset != action.asset) {
        context.proposal.transport = {};
        context.proposal.transport.audio_asset = action.asset;
    }
}

auto apply_visual_selection(workspace_action_context_t& context,
                            const workspace_select_asset_action_t& action) -> void {
    if (action.asset &&
        (!*action.asset ||
         std::ranges::none_of(context.document.visual_assets, [&](const auto& asset) {
             return asset.id == *action.asset && asset.kind == asset_kind_t::image;
         }))) {
        context.reject(diagnostic_code_t::invalid_id,
                       "selected image asset does not exist in the bound document");
    } else {
        context.proposal.selected_visual_asset = action.asset;
    }
}

auto apply_action(workspace_action_context_t& context,
                  const workspace_select_asset_action_t& action) -> void {
    if (action.slot == workspace_asset_slot_t::visual)
        apply_visual_selection(context, action);
    else
        apply_audio_selection(context, action);
}

auto apply_action(workspace_action_context_t& context, const workspace_select_clip_action_t& action)
    -> void {
    if (!action.clip.has_value()) {
        context.proposal.transport.selected_clip.reset();
        return;
    }
    const auto clip = find_clip(context.document, *action.clip);
    if (!*action.clip || clip.clip == nullptr || clip.duplicate) {
        context.reject(diagnostic_code_t::invalid_id,
                       "selected audio clip does not exist in the bound document");
        return;
    }
    const auto* asset = context.document.find_audio_asset(clip.clip->source);
    if (asset == nullptr || !is_audio_clip_valid_for_asset(*clip.clip, *asset)) {
        context.reject(diagnostic_code_t::invalid_id,
                       "selected audio clip refers to an invalid source range");
        return;
    }
    context.proposal.transport.audio_asset = asset->id;
    context.proposal.transport.selected_clip = action.clip;
    context.proposal.transport.position_frame = clip.clip->begin_frame;
    context.proposal.transport.playing = false;
    context.proposal.transport.loop_frames.reset();
}

auto apply_action(workspace_action_context_t& context, const workspace_seek_action_t& action)
    -> void {
    if (!context.proposal.transport.audio_asset.has_value()) {
        context.reject(diagnostic_code_t::invalid_id, "cannot seek without a selected audio asset");
        return;
    }
    const auto* asset = context.document.find_audio_asset(*context.proposal.transport.audio_asset);
    if (asset == nullptr ||
        (asset->stream.frame_count.has_value() && action.frame > *asset->stream.frame_count)) {
        context.reject(diagnostic_code_t::invalid_resolution,
                       "seek frame is outside the selected audio asset");
    } else {
        context.proposal.transport.position_frame = action.frame;
    }
}

auto apply_action(workspace_action_context_t& context, const workspace_set_playing_action_t& action)
    -> void {
    if (action.playing && !context.proposal.transport.audio_asset.has_value()) {
        context.reject(diagnostic_code_t::invalid_id,
                       "cannot start transport without a selected audio asset");
    } else {
        context.proposal.transport.playing = action.playing;
    }
}

auto apply_action(workspace_action_context_t& context, const workspace_set_loop_action_t& action)
    -> void {
    if (!action.frames.has_value()) {
        context.proposal.transport.loop_frames.reset();
        return;
    }
    if (!context.proposal.transport.audio_asset.has_value()) {
        context.reject(diagnostic_code_t::invalid_id,
                       "cannot set a loop without a selected audio asset");
        return;
    }
    const auto [begin, end] = *action.frames;
    const auto* asset = context.document.find_audio_asset(*context.proposal.transport.audio_asset);
    if (begin >= end || asset == nullptr ||
        (asset->stream.frame_count.has_value() && end > *asset->stream.frame_count)) {
        context.reject(diagnostic_code_t::invalid_resolution,
                       "loop frame range is empty or outside the selected audio asset");
    } else {
        context.proposal.transport.loop_frames = action.frames;
    }
}

struct workspace_action_visitor_t {
    [[nodiscard]] static auto from(workspace_action_context_t& context) -> workspace_action_visitor_t {
        return {.context = context};
    }
    template <class Action> void operator()(const Action& action) const { apply_action(context, action); }
    workspace_action_context_t& context;
};

auto apply_workspace_action(workspace_controller_t& workspace, const song_document_t& document,
                                     const workspace_action_t& action) -> dispatch_outcome_t {
    dispatch_outcome_t result{};
    const document_stamp_t binding{workspace.song, workspace.document_revision};
    if (!binding.is_current(document)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  stale_action_message("Workspace action"));
        return result;
    }

    auto proposal = workspace;
    workspace_action_context_t context{proposal, document, result.diagnostics};
    std::visit(workspace_action_visitor_t::from(context), action);

    if (has_errors(result.diagnostics)) {
        return result;
    }
    workspace = std::move(proposal);
    result.value = true;
    return result;
}

struct workspace_view_build_context_t {
    const workspace_controller_t& workspace;
    const song_document_t& document;
    const chart_document_t* chart;
    workspace_view_outcome_t& result;
    workspace_view_t& view;
};

struct metadata_view_budget_t {
    std::size_t item_count{};
    std::size_t text_bytes{};
    std::size_t maximum_text_bytes{};
};

auto account_metadata_text(metadata_view_budget_t& budget, std::string_view text) -> bool {
    return checked_add(budget.text_bytes, text.size()) &&
           budget.text_bytes <= budget.maximum_text_bytes;
}

auto account_metadata_groups(metadata_view_budget_t& budget,
                             const std::vector<format_metadata_t>& groups) -> bool {
    for (const auto& group : groups) {
        if (!checked_add(budget.item_count, group.ordered_fields.size())) {
            return false;
        }
        for (const auto& [name, value] : group.ordered_fields) {
            if (!account_metadata_text(budget, name) || !account_metadata_text(budget, value)) {
                return false;
            }
        }
    }
    return true;
}

auto append_metadata_groups(workspace_metadata_view_t& content,
                            const std::vector<format_metadata_t>& groups, bool chart_scoped)
    -> void {
    constexpr auto format_family_count = static_cast<std::size_t>(format_family_t::quaver) + 1U;
    std::array<bool, format_family_count> seen_families{};
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const auto& group = groups[group_index];
        const auto family_index = static_cast<std::size_t>(group.family);
        const auto first_group_for_family =
            family_index < seen_families.size() && !seen_families[family_index];
        if (family_index < seen_families.size()) {
            seen_families[family_index] = true;
        }
        for (std::size_t field_index = 0; field_index < group.ordered_fields.size();
             ++field_index) {
            const auto& [name, value] = group.ordered_fields[field_index];
            content.format_fields.push_back({
                .chart_scoped = chart_scoped,
                .family = group.family,
                .first_group_for_family = first_group_for_family,
                .group_index = group_index,
                .field_index = field_index,
                .group_field_count = group.ordered_fields.size(),
                .name = name,
                .value = value,
                .source_order =
                    field_index < group.ordered_field_source_orders.size()
                        ? std::optional<std::size_t>{group.ordered_field_source_orders[field_index]}
                        : std::nullopt,
            });
        }
    }
}

auto build_metadata_tab(workspace_view_build_context_t& context) -> bool {
    const auto& [workspace, document, chart, result, view] = context;
    metadata_view_budget_t budget{
        .item_count = document.metadata.ordered_search_tags.size(),
        .maximum_text_bytes = workspace.limits.maximum_metadata_text_bytes,
    };
    if (!account_metadata_text(budget, document.metadata.title) ||
        !account_metadata_text(budget, document.metadata.artist)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                  "metadata text exceeds the configured view byte cap");
        return false;
    }
    for (const auto& tag : document.metadata.ordered_search_tags) {
        if (!account_metadata_text(budget, tag)) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "metadata text exceeds the configured view byte cap");
            return false;
        }
    }
    if ((chart != nullptr && (!account_metadata_text(budget, chart->metadata.name) ||
                              !account_metadata_text(budget, chart->metadata.author))) ||
        !account_metadata_groups(budget, document.metadata.format_groups) ||
        (chart != nullptr && !account_metadata_groups(budget, chart->metadata.format_groups))) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                  "metadata field count or text bytes overflow the configured view bounds");
        return false;
    }
    if (!enforce_summary_cap(budget.item_count, workspace, "metadata field list",
                             result.diagnostics)) {
        return false;
    }
    workspace_metadata_view_t content{
        .title = document.metadata.title,
        .artist = document.metadata.artist,
        .search_tags = document.metadata.ordered_search_tags,
        .selected_difficulty = view.selected_difficulty,
        .chart_author = chart == nullptr ? std::string{} : chart->metadata.author,
        .beat_zero_audio_offset_seconds =
            chart == nullptr ? signed_rational_t{} : chart->beat_zero_audio_offset_seconds,
    };
    content.format_fields.reserve(budget.item_count - document.metadata.ordered_search_tags.size());
    append_metadata_groups(content, document.metadata.format_groups, false);
    if (chart != nullptr) {
        append_metadata_groups(content, chart->metadata.format_groups, true);
    }
    view.tab_view = std::move(content);
    return true;
}

auto build_setup_tab(workspace_view_build_context_t& context) -> bool {
    const auto& [workspace, document, chart, result, view] = context;
    if (!enforce_summary_cap(document.charts.size(), workspace, "chart list", result.diagnostics) ||
        !enforce_summary_cap(document.audio_assets.size(), workspace, "audio asset list",
                             result.diagnostics) ||
        !enforce_summary_cap(document.visual_assets.size(), workspace, "visual asset list",
                             result.diagnostics)) {
        return false;
    }
    workspace_setup_view_t content{
        .title = document.metadata.title,
        .artist = document.metadata.artist,
        .audio_asset_count = document.audio_assets.size(),
        .visual_asset_count = document.visual_assets.size(),
    };
    content.charts.reserve(document.charts.size());
    for (const auto& item : document.charts) {
        content.charts.push_back(workspace_chart_option_t::from(item));
    }
    view.tab_view = std::move(content);
    return true;
}

auto build_timing_tab(workspace_view_build_context_t& context) -> bool {
    const auto& [workspace, document, chart, result, view] = context;
    if (chart == nullptr) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "timing tab requires a selected chart");
        return false;
    }
    std::size_t item_count = chart->measures.size();
    const auto counts_valid = checked_add(item_count, chart->bpms.size()) &&
                              checked_add(item_count, chart->pauses.size()) &&
                              checked_add(item_count, chart->warps.size()) &&
                              checked_add(item_count, chart->bookmarks.size()) &&
                              checked_add(item_count, chart->sections.size()) &&
                              checked_add(item_count, chart->timing_anchors.size());
    if (!counts_valid) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                  "timing summary count overflows the supported range");
        return false;
    }
    if (!enforce_summary_cap(item_count, workspace, "timing summary", result.diagnostics)) {
        return false;
    }
    view.tab_view = workspace_timing_view_t{
        .chart = chart->id,
        .measure_count = chart->measures.size(),
        .bpm_count = chart->bpms.size(),
        .pause_count = chart->pauses.size(),
        .warp_count = chart->warps.size(),
        .bookmark_count = chart->bookmarks.size(),
        .section_count = chart->sections.size(),
        .anchor_count = chart->timing_anchors.size(),
    };
    return true;
}

auto build_compose_tab(workspace_view_build_context_t& context,
                       workspace_bms_branch_outline_cache_t* branch_cache) -> bool {
    const auto& [workspace, document, chart, result, view] = context;
    if (chart == nullptr) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "compose tab requires a selected chart");
        return false;
    }
    std::size_t note_count = 0;
    std::size_t lane_count = 0;
    bool counts_valid = true;
    for (const auto& measure : chart->measures) {
        counts_valid = counts_valid && checked_add(note_count, measure.notes.size());
    }
    for (const auto& layout : chart->lane_layouts) {
        counts_valid = counts_valid && checked_add(lane_count, layout.layout.lanes.size());
    }
    std::size_t item_count = chart->measures.size();
    counts_valid = counts_valid && checked_add(item_count, note_count) &&
                   checked_add(item_count, chart->lane_layouts.size()) &&
                   checked_add(item_count, lane_count) &&
                   checked_add(item_count, chart->semantic_layers.size());
    if (!counts_valid ||
        !enforce_summary_cap(item_count, workspace, "compose summary", result.diagnostics)) {
        if (!counts_valid) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "compose summary count overflows the supported range");
        }
        return false;
    }
    view.tab_view = workspace_compose_view_t{
        .chart = chart->id,
        .measure_count = chart->measures.size(),
        .note_count = note_count,
        .layout_segment_count = chart->lane_layouts.size(),
        .lane_count = lane_count,
        .layer_count = chart->semantic_layers.size(),
        .shared_bms_branch_outline = workspace_bms_branch_outline_view_t::from(
            document, *chart, workspace.limits, branch_cache),
    };
    return true;
}

auto build_audio_slices_tab(workspace_view_build_context_t& context) -> bool {
    const auto& [workspace, document, chart, result, view] = context;
    std::size_t clip_count = 0;
    for (const auto& item : document.charts) {
        if (!checked_add(clip_count, item.sound_channels.size())) {
            add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                      "audio clip count overflows the supported range");
            return false;
        }
    }
    if (!enforce_summary_cap(document.audio_assets.size(), workspace, "audio asset list",
                             result.diagnostics) ||
        !enforce_summary_cap(clip_count, workspace, "audio clip list", result.diagnostics)) {
        return false;
    }
    workspace_audio_slices_view_t content{
        .clip_count = clip_count,
        .transport = workspace.transport,
    };
    content.audio_assets.reserve(document.audio_assets.size());
    for (const auto& asset : document.audio_assets) {
        content.audio_assets.push_back(asset.id);
    }
    if (chart != nullptr) {
        content.sound_channel_count = chart->sound_channels.size();
    }
    view.tab_view = std::move(content);
    return true;
}

auto build_compare_validate_tab(workspace_view_build_context_t& context,
                                const diagnostic_list_t* cached_diagnostics) -> bool {
    const auto& [workspace, document, chart, result, view] = context;
    auto diagnostics = cached_diagnostics ? *cached_diagnostics : validate_document(document);
    if (diagnostics.size() > workspace.limits.maximum_validation_diagnostics) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                  "document diagnostics exceed the workspace validation cap");
        return false;
    }
    workspace_compare_validate_view_t content{
        .comparison_charts = workspace.comparison_charts,
    };
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == diagnostic_severity_t::error) {
            ++content.error_count;
        } else if (diagnostic.severity == diagnostic_severity_t::warning) {
            ++content.warning_count;
        }
    }
    content.diagnostics = std::move(diagnostics);
    view.tab_view = std::move(content);
    return true;
}

auto build_requested_workspace_tab(workspace_view_build_context_t& context, workspace_tab_t requested_tab,
    const diagnostic_list_t* cached_diagnostics, workspace_bms_branch_outline_cache_t* branch_cache) -> bool {
    switch (requested_tab) {
    case workspace_tab_t::setup: return build_setup_tab(context);
    case workspace_tab_t::metadata: return build_metadata_tab(context);
    case workspace_tab_t::timing: return build_timing_tab(context);
    case workspace_tab_t::compose: return build_compose_tab(context, branch_cache);
    case workspace_tab_t::storyboard:
        context.view.tab_view = workspace_storyboard_view_t{.enabled = false,
            .placeholder = "Storyboard editing is not available yet.",
            .storyboard_count = context.document.storyboards.size()};
        return true;
    case workspace_tab_t::audio_slices: return build_audio_slices_tab(context);
    case workspace_tab_t::compare_validate: return build_compare_validate_tab(context, cached_diagnostics);
    }
    return true;
}

auto workspace_view_t::from(const workspace_controller_t& workspace,
                            const song_document_t& document) -> workspace_view_outcome_t {
    return workspace_view_t::from(workspace, document, workspace.active_tab);
}

auto workspace_view_t::from(const workspace_controller_t& workspace,
                            const song_document_t& document, workspace_tab_t requested_tab,
                            const diagnostic_list_t* cached_diagnostics,
                            workspace_bms_branch_outline_cache_t* branch_cache,
                            workspace_binding_validation_cache_t* binding_cache)
    -> workspace_view_outcome_t {
    workspace_view_outcome_t result{};
    if (!document_stamp_t{.project = workspace.song, .revision = workspace.document_revision}
             .is_current(document)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id, stale_action_message("Workspace view"));
        return result;
    }
    result.diagnostics = binding_diagnostics(document, workspace.limits, binding_cache);
    if (has_errors(result.diagnostics)) {
        return result;
    }
    const auto tab_index = index_of(requested_tab);
    if (!tab_index.has_value()) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace contains an invalid active tab value");
        return result;
    }
    if (workspace.selected_chart.has_value() &&
        document.find_chart(*workspace.selected_chart) == nullptr) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace selected chart no longer exists; rebind the workspace");
        return result;
    }
    if (!is_comparison_selection_valid(document, workspace.comparison_charts,
                                    workspace.limits.maximum_comparison_charts)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace comparison selection is invalid; rebind the workspace");
        return result;
    }
    const auto& focus = workspace.tab_focus[*tab_index];
    if ((!focus.region.empty() &&
         focus.region.size() > workspace.limits.maximum_focus_region_bytes) ||
        !has_focus_target(document, focus.target)) {
        add_error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "workspace active-tab focus is invalid; rebind the workspace");
        return result;
    }

    std::optional<workspace_chart_option_t> selected_difficulty;
    const chart_document_t* chart = nullptr;
    if (workspace.selected_chart.has_value()) {
        chart = document.find_chart(*workspace.selected_chart);
        selected_difficulty = workspace_chart_option_t::from(*chart);
    }

    workspace_view_t view{
        .song = document.id,
        .document_revision = document.revision,
        .active_tab = requested_tab,
        .tab_enabled = is_workspace_tab_enabled(requested_tab),
        .selected_difficulty = selected_difficulty,
        .transport = workspace.transport,
        .selected_visual_asset = workspace.selected_visual_asset,
        .history = workspace.history,
        .focus = focus,
    };
    workspace_view_build_context_t context{workspace, document, chart, result, view};
    if (!build_requested_workspace_tab(context, requested_tab, cached_diagnostics, branch_cache))
        return result;
    result.value = std::move(view);
    return result;
}

} // namespace wormedit
