#pragma once
#include "sys/bookmark_chooser_view.h"
#include "sys/compose_action_outcome.h"
#include <chrono>
#include <optional>
#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/workspace.h>
namespace wormedit::sys {
struct application_command_state_t;
class sdl_preview_output_t;
namespace runtime { struct chart_preview_t; }
inline constexpr auto playing_seek_repeat_window = std::chrono::milliseconds{300};

struct bookmark_chooser_backend_t {
    bool (*open)(void*, std::uint64_t project, std::uint64_t chart, std::uint64_t revision){};
    void* context{};
    auto operator==(const bookmark_chooser_backend_t&) const -> bool = default;
};
struct bookmark_navigation_t {
    struct bookmark_seek_record_t {
        chart_id_t chart{};
        std::uint64_t row{};
        std::chrono::steady_clock::time_point at{};
        auto operator==(const bookmark_seek_record_t&) const -> bool = default;
    };
    struct project_t {
        std::optional<bookmark_seek_record_t> last_playing_bookmark_seek{};
    } project;
    bookmark_chooser_backend_t chooser_backend{};
    // Deterministic clocks inject a value that may advance between event calls.
    const std::chrono::steady_clock::time_point* fixed_clock{};
    const workspace_controller_t* workspace{};
    sdl_preview_output_t* preview_output{};
    runtime::chart_preview_t* preview{};
    void reset_project() { project = {}; }
    void clear_seek_record() { project.last_playing_bookmark_seek.reset(); }
    [[nodiscard]] auto now() const -> std::chrono::steady_clock::time_point {
        return fixed_clock ? *fixed_clock : std::chrono::steady_clock::now();
    }
    [[nodiscard]] auto recent_seek_row(chart_id_t chart) const -> std::optional<std::uint64_t> {
        const auto& record = project.last_playing_bookmark_seek;
        if (!record || record->chart != chart || now() - record->at > playing_seek_repeat_window)
            return std::nullopt;
        return record->row;
    }
    void record_seek(chart_id_t chart, std::uint64_t row) {
        project.last_playing_bookmark_seek = bookmark_seek_record_t{.chart = chart, .row = row, .at = now()};
    }
    [[nodiscard]] auto has_chooser_backend() const -> bool { return chooser_backend.open != nullptr; }
    [[nodiscard]] auto open_chooser(std::uint64_t project_id, std::uint64_t chart, std::uint64_t revision) -> bool {
        return chooser_backend.open && chooser_backend.open(chooser_backend.context, project_id, chart, revision);
    }
    [[nodiscard]] auto preserve_preview_revision(chart_id_t, std::uint64_t previous, std::uint64_t next) -> bool;
    [[nodiscard]] auto navigate_beat_bookmark(document_history_t&, editor_interaction_state_t&,
                                              application_command_state_t&,
                                              marker_navigation_direction_t)
        -> compose_action_outcome_t;
    [[nodiscard]] auto navigate_to_bookmark(document_history_t&, editor_interaction_state_t&,
                                            application_command_state_t&, std::uint64_t, chart_id_t,
                                            object_id_t) -> compose_action_outcome_t;
    [[nodiscard]] auto add_beat_bookmark_here(document_history_t&, editor_interaction_state_t&,
                                              application_command_state_t&)
        -> compose_action_outcome_t;
    [[nodiscard]] auto rename_bookmark(document_history_t&, std::uint64_t, chart_id_t, object_id_t,
                                       std::string_view) -> compose_action_outcome_t;
    [[nodiscard]] static auto build_bookmark_chooser_view(const song_document_t&, chart_id_t,
        std::size_t first_row, std::size_t row_limit, std::string_view query = {}) -> bookmark_chooser_outcome_t;
};
} // namespace wormedit::sys
