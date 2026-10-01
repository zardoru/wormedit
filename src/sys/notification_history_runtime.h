#pragma once

#include "ui/frontend.h"
#include <wormedit/core/notifications.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/workspace.h>

#include <array>
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
    struct target_cache_entry_t {
        notification_id_t id{};
        WormUiNotificationTargetAvailability availability{WORM_UI_NOTIFICATION_NO_TARGET};
    };
    std::array<target_cache_entry_t, WORM_UI_NOTIFICATION_HISTORY_CAPACITY> target_cache{};
    const song_document_t* cached_document{};
    song_id_t cached_song{};
    std::uint64_t cached_revision{};
    std::uint64_t cached_generation{};
    auto project(const notification_center_t& center, const song_document_t& document,
                 notification_time_t now, std::uint64_t document_generation = 0) -> WormUiNotificationHistoryView;
    auto apply(const WormUiNotificationAction& action, notification_center_t& center) -> bool;
};

auto go_to_notification(const notification_center_t& center, notification_id_t id,
                        document_history_t& history, workspace_controller_t& workspace,
                        std::optional<editor_interaction_state_t>& interaction)
    -> std::optional<std::string_view>;
} // namespace wormedit::sys
