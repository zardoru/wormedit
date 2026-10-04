#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

#include <cstdint>

struct bookmark_rename_editor_t {
    bool active{};
    bool focus_pending{};
    bool release_pending{};
    bool popup_open{};
    bool close_pending{};
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t document_revision{};
    std::uint64_t bookmark_id{};
    float popup_bounds[4]{};
    char label[WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1]{};
};

// Opens the editor for a visible bookmark ID, or returns false if it is absent.
auto begin_compose_bookmark_rename(bookmark_rename_editor_t& editor,
                                   const WormUiComposeView& view,
                                   std::uint64_t bookmark_id) -> bool;
void cancel_compose_bookmark_rename(bookmark_rename_editor_t& editor);

// Pointer coordinates are in the view's document-pixel space. The bounded target sits near
// the right edge of the playable field, so clicking a bookmark line does not steal note clicks.
auto begin_compose_bookmark_rename_at_pointer(bookmark_rename_editor_t& editor,
                                             const WormUiComposeView& view,
                                             double pointer_x, double pointer_y,
                                             bool pressed, bool available) -> bool;

auto compose_bookmark_rename_owns_input(const bookmark_rename_editor_t& editor,
                                        const WormUiComposeView& view,
                                        bool window_focused) -> bool;

void render_compose_bookmark_rename(nk_context* context, const WormUiComposeView& view,
                                    WormUiWorkspaceAction& action,
                                    bookmark_rename_editor_t& editor,
                                    struct nk_rect viewport, bool window_focused);
