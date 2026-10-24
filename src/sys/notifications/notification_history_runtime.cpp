#include "sys/project/workspace_navigation.h"
#include <wormedit/core/rational.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/interaction_dispatch.h>
#include "sys/notifications/notification_history_runtime.h"
#include "sys/commands/application_command_dispatch.h"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <tuple>
#include <type_traits>

namespace wormedit::sys {
auto invoke_notification_history_command(application_command_t command, bool auto_repeat,
    notification_history_runtime_t& runtime, notification_center_t& center)
    -> std::optional<wormedit::dispatch_outcome_t> {
    if (command.kind != application_command_kind_t::view_notifications) return std::nullopt;
    if (!auto_repeat) (void)runtime.apply({WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY, 0}, center);
    return wormedit::dispatch_outcome_t{.value = true};
}

namespace {
auto positioned_target(const chart_document_t& chart, local_position_t position)
    -> notification_history_target_t {
    const auto measure = chart.find_measure(position.measure);
    if (measure == nullptr) return {.availability = WORM_UI_NOTIFICATION_TARGET_MISSING};
    const auto capacity = measure_capacity_t::from(chart.divisions_per_standard_measure, measure->definition);
    if (!capacity || position.row >= capacity->row_capacity)
        return {.availability = WORM_UI_NOTIFICATION_TARGET_MISSING};
    return {.availability = WORM_UI_NOTIFICATION_TARGET_POSITION, .chart = chart.id, .position = position};
}

template <typename Item>
auto object_position(const Item& item) -> std::optional<local_position_t> {
    if constexpr (requires { item.position; }) return item.position;
    else if constexpr (requires { item.span; }) return item.span.begin;
    else if constexpr (requires { item.beat_span; })
        return item.beat_span ? std::optional{item.beat_span->begin} : std::nullopt;
    else return std::nullopt;
}

template <typename Items>
auto find_object_target(const chart_document_t& chart, const Items& items, object_id_t id)
    -> std::optional<notification_history_target_t> {
    const auto item = std::ranges::find(items, id, [](const auto& value) { return value.id; });
    if (item == items.end()) return std::nullopt;
    const auto position = object_position(*item);
    return position ? positioned_target(chart, *position)
                    : notification_history_target_t{.availability = WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL, .chart = chart.id};
}

auto chart_object_target(const chart_document_t& chart, object_id_t id)
    -> std::optional<notification_history_target_t> {
    std::optional<notification_history_target_t> result;
    const auto scan = [&](const auto& items) {
        if (!result) result = find_object_target(chart, items, id);
    };
    std::apply([&](const auto&... items) { (scan(items), ...); },
        std::tie(chart.lane_layouts, chart.bgm_source_lanes, chart.bpms, chart.pauses,
                 chart.warps, chart.scrolls, chart.speeds, chart.kiai, chart.timing_anchors,
                 chart.bookmarks, chart.sections, chart.bga_events, chart.sound_channels));
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    return result;
}
} // namespace

auto resolve_notification_target(const song_document_t& document,
                                 const notification_target_t& target)
    -> notification_history_target_t {
    if (const auto* exact = std::get_if<notification_chart_position_t>(&target)) {
        const auto chart = document.read_chart(exact->chart);
        return chart == nullptr ? notification_history_target_t{.availability = WORM_UI_NOTIFICATION_TARGET_MISSING}
                                              : positioned_target(*chart, exact->position);
    }
    const auto* object = std::get_if<object_id_t>(&target);
    if (!object) return {};
    for (const auto* chart : document.read_charts()) {
        if (const auto found = chart_object_target(*chart, *object)) return *found;
    }
    return {.availability = WORM_UI_NOTIFICATION_TARGET_MISSING};
}


auto find_notification(const notification_center_t& center, notification_id_t id)
    -> const notification_entry_t* {
    const auto entries = center.borrowed_snapshot(notification_capacity);
    const auto found = std::ranges::find(entries, id, &notification_entry_t::id);
    return found == entries.end() ? nullptr : &*found;
}

auto notification_copy_payload(const notification_center_t& center, notification_id_t id)
    -> std::optional<std::string_view> {
    const auto* entry = find_notification(center, id);
    if (!entry) return std::nullopt;
    return entry->detail.empty() ? entry->message : entry->detail;
}

} // namespace wormedit::sys

void wormedit::ui::notification_history_view_t::refill(sys::notification_history_runtime_t& runtime,
    const notification_center_t& center, const song_document_t& document,
    std::chrono::nanoseconds now, std::uint64_t document_generation) {
    visible = runtime.visible;
    rows.clear();
    if (!visible) return;
    auto& targets = runtime.target_cache.get_or_build(
        std::tuple{&document, document_stamp_t::from(document), document_generation},
        [](sys::notification_history_runtime_t::target_entries_t& entries) { entries.fill({}); });
    const auto entries = center.borrowed_snapshot(notification_capacity);
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        const auto availability = targets[rows.size()].get_or_build(entry->id,
            [&document, &entry] { return sys::resolve_notification_target(document, entry->target).availability; });
        const auto age = now > entry->timestamp ? now - entry->timestamp : notification_time_t{};
        rows.push_back({.id = entry->id, .severity = entry->severity, .source = entry->source,
            .message = entry->message, .detail = entry->detail, .repeat_count = entry->count,
            .age_seconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(age).count()),
            .read = entry->read, .target = availability});
    }
}

auto wormedit::ui::notification_history_view_t::from(sys::notification_history_runtime_t& runtime,
    const notification_center_t& center, const song_document_t& document,
    std::chrono::nanoseconds now, std::uint64_t document_generation) -> notification_history_view_t {
    notification_history_view_t result;
    result.refill(runtime, center, document, now, document_generation);
    return result;
}

namespace wormedit::sys {

auto notification_history_runtime_t::apply(const wormedit::ui::notification_action_t& action,
                                           notification_center_t& center) -> bool {
    switch (action.kind) {
    case WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY:
        if (!visible) center.mark_all_read();
        visible = true;
        return true;
    case WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY: visible = false; return true;
    case WORM_UI_NOTIFICATION_ACTION_DISMISS: (void)center.dismiss(action.id); return true;
    case WORM_UI_NOTIFICATION_ACTION_DISMISS_ALL: center.dismiss_all(); return true;
    default: return false;
    }
}
auto go_to_notification(const notification_center_t& center, notification_id_t id,
                        document_history_t& history, workspace_controller_t& workspace,
                        std::optional<editor_interaction_state_t>& interaction)
    -> std::optional<std::string_view> {
    const auto* entry = find_notification(center, id);
    if (!entry) return "Target no longer exists";
    const auto target = resolve_notification_target(history.document, entry->target);
    if (target.availability != WORM_UI_NOTIFICATION_TARGET_POSITION)
        return target.availability == WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL
            ? "Sin posición en el chart" : "Target no longer exists";
    auto navigation = workspace_navigation_t::from(history, workspace, interaction,
                                                   target.chart, target.position);
    if (!navigation) return "Target cannot be opened";
    std::move(*navigation).apply(workspace, interaction);
    return std::nullopt;
}
} // namespace wormedit::sys
