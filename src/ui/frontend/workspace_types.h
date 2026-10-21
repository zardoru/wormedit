#pragma once

#include "common.h"
#include <wormedit/core/vocabulary.h>

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
struct compose_view_t;
struct workspace_view_t;
}

enum class new_project_lane_role_t {
    playable,
    scratch,
    autoplay,
    invisible,
    mine,
    free_zone,
    foot_pedal,
};

struct new_project_layout_entry_view_t {
    std::string_view id;
    std::string_view label;
    std::string_view game;
    std::string_view lane_shape;
    std::string_view variant;
    std::vector<new_project_lane_role_t> preview_lane_roles;
};

struct new_project_layout_catalog_view_t {
    std::span<const new_project_layout_entry_view_t> entries;

    [[nodiscard]] auto find(std::string_view id) const -> const new_project_layout_entry_view_t* {
        for (const auto& entry : entries)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }
};

typedef enum WormUiCloseDecision {
    WORM_UI_CLOSE_NONE = 0, WORM_UI_CLOSE_CANCEL, WORM_UI_CLOSE_DISCARD, WORM_UI_CLOSE_SAVE
} WormUiCloseDecision;
namespace wormedit::ui {

struct close_confirmation_view_t {
    bool visible, application, has_native_path;
    wormedit::song_id_t project_id;
    uint64_t revision;
    std::string_view status_text;

    auto operator==(const close_confirmation_view_t&) const -> bool = default;
};
struct compare_chart_view_t {
    wormedit::chart_id_t id;
    std::string_view label;

    auto operator==(const compare_chart_view_t&) const -> bool = default;
};
struct compare_diagnostic_view_t {
    std::string_view label;
    uint64_t row{};
    wormedit::measure_id_t measure;
    bool has_row{};

    auto operator==(const compare_diagnostic_view_t&) const -> bool = default;
};
struct compare_view_t {
    bool visible{};
    bool has_two_difficulties{};
    wormedit::chart_id_t first_chart_id, second_chart_id;
    std::string_view first_difficulty, second_difficulty;
    const wormedit::ui::compose_view_t* first_pane{};
    const wormedit::ui::compose_view_t* second_pane{};
    std::vector<compare_chart_view_t> charts;
    double scroll_pixels{};
    std::vector<compare_diagnostic_view_t> diagnostics;
    wormedit::chart_id_t diagnostic_chart_id;

    auto operator==(const compare_view_t&) const -> bool = default;
};

struct menu_command_view_t {
    std::string_view id;
    std::string_view label;
    std::string_view shortcut;
    bool enabled;
    bool checked;

    auto operator==(const menu_command_view_t&) const -> bool = default;
};

struct recent_file_view_t {
    std::string_view label;
    std::string_view path;

    auto operator==(const recent_file_view_t&) const -> bool = default;
};

struct command_menu_row_view_t {
    std::string_view id;
    std::string_view label;
    std::string_view shortcut; /* platform chord text; empty when the command has no key */
    bool enabled;

    auto operator==(const command_menu_row_view_t&) const -> bool = default;
};

} // namespace wormedit::ui
