#include <wormedit/core/rational.h>
#include "sys/notification_history_runtime.h"
#include "sys/application_command_dispatch.h"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <tuple>
#include <type_traits>

namespace wormedit::sys {
auto invoke_notification_history_command(application_command_t command, bool auto_repeat,
    notification_history_runtime_t& runtime, notification_center_t& center)
    -> std::optional<wormedit::command_invocation_result_t> {
    if (command.kind != application_command_kind_t::view_notifications) return std::nullopt;
    if (!auto_repeat) (void)runtime.apply({WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY, 0}, center);
    return wormedit::command_invocation_result_t{.accepted = true};
}

namespace {
auto positioned_target(const chart_document_t& chart, local_position_t position)
    -> notification_history_target_t {
    const auto measure = chart.find_measure(position.measure);
    if (measure == nullptr) return {WORM_UI_NOTIFICATION_TARGET_MISSING};
    const auto capacity = validate_measure(chart.divisions_per_standard_measure, measure->definition);
    if (!capacity || position.row >= *capacity.row_capacity)
        return {WORM_UI_NOTIFICATION_TARGET_MISSING};
    return {WORM_UI_NOTIFICATION_TARGET_POSITION, chart.id, position};
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
                    : notification_history_target_t{WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL, chart.id};
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
        const auto chart = document.find_chart(exact->chart);
        return chart == nullptr ? notification_history_target_t{WORM_UI_NOTIFICATION_TARGET_MISSING}
                                              : positioned_target(*chart, exact->position);
    }
    const auto* object = std::get_if<object_id_t>(&target);
    if (!object) return {};
    for (const auto& chart : document.charts) {
        if (const auto found = chart_object_target(chart, *object)) return *found;
    }
    return {WORM_UI_NOTIFICATION_TARGET_MISSING};
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

auto notification_history_runtime_t::project(const notification_center_t& center,
    const song_document_t& document, notification_time_t now,
    std::uint64_t document_generation) -> WormUiNotificationHistoryView {
    WormUiNotificationHistoryView view{visible, 0, rows.data()};
    if (!visible) return view;
    if (cached_document != &document || cached_song != document.id ||
        cached_revision != document.revision || cached_generation != document_generation) {
        target_cache.fill({});
        cached_document = &document;
        cached_song = document.id;
        cached_revision = document.revision;
        cached_generation = document_generation;
    }
    const auto entries = center.borrowed_snapshot(notification_capacity);
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        auto& cached = target_cache[view.row_count];
        if (cached.id != entry->id) {
            cached.id = entry->id;
            cached.availability = resolve_notification_target(document, entry->target).availability;
        }
        const auto age = now > entry->timestamp ? now - entry->timestamp : notification_time_t{};
        rows[view.row_count++] = {
            entry->id, static_cast<WormUiNotificationSeverity>(entry->severity),
            static_cast<std::uint32_t>(entry->source), entry->message.c_str(), entry->detail.c_str(),
            entry->count, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(age).count()),
            entry->read, cached.availability};
    }
    return view;
}

auto notification_history_runtime_t::apply(const WormUiNotificationAction& action,
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
namespace {
auto target_lane(const chart_document_t& chart, local_position_t position) -> lane_id_t {
    const auto built = build_measure_index(chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    if (!index) return {};
    const auto row = (*index).row_of(position);
    if (!row) return {};
    const lane_layout_segment_t* active{};
    std::uint64_t active_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto start = (*index).row_of(segment.position);
        if (start && *start <= *row && (!active || *start >= active_row)) {
            active = &segment;
            active_row = *start;
        }
    }
    if (!active || active->layout.lanes.empty()) return {};
    return active->layout.lanes.front().id;
}

auto navigation_interaction(const document_history_t& history,
                             const notification_history_target_t& target,
                             const std::optional<editor_interaction_state_t>& previous)
    -> std::optional<editor_interaction_state_t> {
    if (previous && previous->chart == target.chart) return previous;
    const auto chart = history.document.find_chart(target.chart);
    if (chart == nullptr) return std::nullopt;
    const auto snaps = previous ? std::span<const std::uint64_t>{previous->supported_snap_denominators}
                                : std::span<const std::uint64_t>{wormedit::default_snap_denominators};
    auto setup = editor_interaction_state_t::from(history, target.chart, target_lane(*chart, target.position),
        target.position, snaps, previous ? previous->active_snap_denominator : 16,
        previous ? previous->viewport_height_pixels : 1.0);
    return std::move(setup.state);
}
} // namespace

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
    auto candidate = navigation_interaction(history, target, interaction);
    if (!candidate) return "Target cannot be opened";
    auto next_workspace = workspace;
    const auto revision = history.document.revision;
    if (!apply_workspace_action(next_workspace, history.document, revision,
                                workspace_select_chart_action_t{target.chart}) ||
        !apply_workspace_action(next_workspace, history.document, revision,
                                workspace_switch_tab_action_t{workspace_tab_t::compose}))
        return "Target cannot be opened";
    if (!dispatch_editor_action(history, *candidate,
                                jump_edit_cursor_action_t{target.position, revision}))
        return "Target cannot be opened";
    workspace = std::move(next_workspace);
    interaction = std::move(candidate);
    return std::nullopt;
}
} // namespace wormedit::sys
