#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

#include <cstdint>
#include <optional>

// The inline single-line label editor shared by bookmarks and note labels, so both own keyboard
// and pointer input through the same flags. `kind` picks the commit action.
enum class inline_label_kind_t { bookmark, note_label, section_label };

struct inline_label_editor_t {
    inline_label_kind_t kind{inline_label_kind_t::bookmark};
    float anchor_x{};
    float anchor_y{};
    // Bookmark editors opened from the inspector list sit at the anchor, not on the canvas line.
    bool anchored{};
    bool active{};
    bool focus_pending{};
    bool release_pending{};
    bool popup_open{};
    bool close_pending{};
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t document_revision{};
    std::uint64_t bookmark_id{};  // Also the section id for kind section_label.
    float popup_bounds[4]{};
    char label[WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1]{};
};

// Opens the editor for a visible bookmark ID, or returns false if it is absent.
auto begin_compose_bookmark_rename(inline_label_editor_t& editor,
                                   const WormUiComposeView& view,
                                   std::uint64_t bookmark_id) -> bool;
void cancel_compose_bookmark_rename(inline_label_editor_t& editor);

// Roughly one text line; the label box sits just above its bookmark line.
inline constexpr double compose_bookmark_label_zone_height = 18.0;

// The visible bookmark whose full-width hit band holds the pointer (document pixels). Hover
// highlight and double-click rename both use it. Notes are the caller's concern.
auto compose_bookmark_at(const WormUiComposeView& view, double pointer_x, double pointer_y)
    -> std::optional<std::uint64_t>;

// Double-click entry: the line or its label opens the editor. Notes are the caller's concern.
auto begin_compose_bookmark_rename_at_double_click(inline_label_editor_t& editor,
                                                  const WormUiComposeView& view,
                                                  double pointer_x, double pointer_y) -> bool;

auto compose_bookmark_rename_owns_input(const inline_label_editor_t& editor,
                                        const WormUiComposeView& view,
                                        bool window_focused) -> bool;

void render_compose_bookmark_rename(nk_context* context, const WormUiComposeView& view,
                                    WormUiWorkspaceAction& action,
                                    inline_label_editor_t& editor,
                                    struct nk_rect viewport, bool window_focused);
