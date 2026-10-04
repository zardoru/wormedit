#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct nk_context;

struct bookmark_chooser_state_t {
    bool open{};
    bool release_pending{};
    bool close_pending{};
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t document_revision{};
    std::size_t first_row{};
    std::size_t row_limit{12};
    std::size_t selected_row{};
    std::uint64_t selected_bookmark_id{};
    std::array<char, 96> status{};
};

void worm_ui_nuklear_bookmark_chooser_open(bookmark_chooser_state_t& state,
                                           std::uint64_t project_id,
                                           std::uint64_t chart_id,
                                           std::uint64_t document_revision);
void worm_ui_nuklear_bookmark_chooser_cancel(nk_context* context,
                                            bookmark_chooser_state_t& state);
[[nodiscard]] auto worm_ui_nuklear_bookmark_chooser_owns_input(
    const bookmark_chooser_state_t& state) -> bool;
[[nodiscard]] auto worm_ui_nuklear_bookmark_chooser_release_pending(
    const bookmark_chooser_state_t& state) -> bool;
void worm_ui_nuklear_bookmark_chooser_release_delivered(
    nk_context* context, bookmark_chooser_state_t& state);

// Returns true while the chooser owns keyboard or pointer input.
[[nodiscard]] auto worm_ui_nuklear_render_compose_bookmark_chooser(
    nk_context* context, const WormUiBookmarkChooserView& view,
    WormUiWorkspaceAction* action, bool window_focused,
    bookmark_chooser_state_t* state) -> bool;
