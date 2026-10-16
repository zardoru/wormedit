#pragma once

#include "ui/frontend.h"
#include <wormedit/core/notifications.h>
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/workspace.h>

#include <array>
#include <tuple>
#include <optional>
#include <string_view>

namespace wormedit::sys {
struct notification_history_target_t {
    WormUiNotificationTargetAvailability availability{WORM_UI_NOTIFICATION_NO_TARGET};
    chart_id_t chart;
    local_position_t position;
};

auto resolve_notification_target(const song_document_t& document,
                                 const notification_target_t& target)
    -> notification_history_target_t;

auto find_notification(const notification_center_t& center, notification_id_t id)
    -> const notification_entry_t*;
auto notification_copy_payload(const notification_center_t& center, notification_id_t id)
    -> std::optional<std::string_view>;

// Frame-thread storage and borrowed strings survive through UI drawing and
// action dispatch. Apply center mutations before the next inbox drain.
struct notification_history_runtime_t {
    bool visible{};
    std::array<WormUiNotificationHistoryRow, WORM_UI_NOTIFICATION_HISTORY_CAPACITY> rows{};
    using target_entries_t = std::array<
        keyed_cache_t<notification_id_t, WormUiNotificationTargetAvailability>,
        WORM_UI_NOTIFICATION_HISTORY_CAPACITY>;
    keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, std::uint64_t>,
                  target_entries_t> target_cache;
    auto project(const notification_center_t& center, const song_document_t& document,
                 notification_time_t now, std::uint64_t document_generation = 0) -> WormUiNotificationHistoryView;
    auto apply(const WormUiNotificationAction& action, notification_center_t& center) -> bool;
};

auto go_to_notification(const notification_center_t& center, notification_id_t id,
                        document_history_t& history, workspace_controller_t& workspace,
                        std::optional<editor_interaction_state_t>& interaction)
    -> std::optional<std::string_view>;
} // namespace wormedit::sys
