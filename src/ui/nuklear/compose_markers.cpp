#include "ui/nuklear/compose_markers.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

struct compose_marker_state_t {
    std::uint64_t chart_id{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t document_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t bookmark_id{};
    std::uint64_t section_id{};
    std::array<char, WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1> bookmark_label{};
    std::array<char, WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1> section_label{};
};

compose_marker_state_t state;

void copy_label(std::array<char, WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1>& destination,
                const char* source) {
    destination.fill('\0');
    if (!source) {
        return;
    }
    const auto length = std::min(std::strlen(source), destination.size() - 1);
    std::memcpy(destination.data(), source, length);
    destination[length] = '\0';
}

auto selected_bookmark(const WormUiComposeView& view) -> const WormUiComposeBookmarkView* {
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        if (view.beat_bookmarks[index].bookmark_id == state.bookmark_id) {
            return &view.beat_bookmarks[index];
        }
    }
    return nullptr;
}

auto selected_section(const WormUiComposeView& view) -> const WormUiComposeSectionView* {
    for (std::size_t index = 0; index < view.beat_section_count; ++index) {
        if (view.beat_sections[index].section_id == state.section_id) {
            return &view.beat_sections[index];
        }
    }
    return nullptr;
}

void synchronize(const WormUiComposeView& view) {
    if (state.chart_id == view.chart_id && state.document_revision == view.document_revision) {
        return;
    }
    if (const auto* bookmark = selected_bookmark(view)) {
        copy_label(state.bookmark_label, bookmark->label);
    } else {
        state.bookmark_id = 0;
        state.bookmark_label.fill('\0');
    }
    if (const auto* section = selected_section(view)) {
        copy_label(state.section_label, section->label);
    } else {
        state.section_id = 0;
        state.section_label.fill('\0');
    }
    state.chart_id = view.chart_id;
    state.document_revision = view.document_revision;
}

void render_bookmarks(struct nk_context* context, const WormUiComposeView& view,
                      WormUiWorkspaceAction& action) {
    const auto* selected = selected_bookmark(view);
    char header[384]{};
    if (selected) {
        std::snprintf(header, sizeof(header), "Bookmark M%llu:%llu  %.260s",
                      static_cast<unsigned long long>(selected->measure_number),
                      static_cast<unsigned long long>(selected->row), selected->label);
    } else {
        std::snprintf(header, sizeof(header), "New bookmark at cursor%s",
                      view.beat_bookmarks_truncated ? " (first 64 shown)" : "");
    }

    nk_layout_row_begin(context, NK_DYNAMIC, 25, 5);
    nk_layout_row_push(context, 0.28F);
    if (nk_combo_begin_label(context, header, nk_vec2(440.0F, 280.0F))) {
        nk_layout_row_dynamic(context, 22, 1);
        if (nk_combo_item_label(context, "New bookmark at cursor", NK_TEXT_LEFT)) {
            state.bookmark_id = 0;
            state.bookmark_label.fill('\0');
        }
        for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
            const auto& bookmark = view.beat_bookmarks[index];
            char item[384]{};
            std::snprintf(item, sizeof(item), "M%llu:%llu  %.280s",
                          static_cast<unsigned long long>(bookmark.measure_number),
                          static_cast<unsigned long long>(bookmark.row), bookmark.label);
            if (nk_combo_item_label(context, item, NK_TEXT_LEFT)) {
                state.bookmark_id = bookmark.bookmark_id;
                copy_label(state.bookmark_label, bookmark.label);
            }
        }
        nk_combo_end(context);
    }
    nk_layout_row_push(context, 0.34F);
    if (!view.scene_available) {
        nk_widget_disable_begin(context);
    }
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.bookmark_label.data(),
                                         state.bookmark_label.size(), nk_filter_default);
    nk_layout_row_push(context, 0.16F);
    const auto apply_clicked =
        nk_button_label(context, selected ? "Apply rename" : "Add at cursor");
    if (!view.scene_available) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.11F);
    if (!view.scene_available || !selected) {
        nk_widget_disable_begin(context);
    }
    const auto move_clicked = nk_button_label(context, "Move here");
    nk_layout_row_push(context, 0.11F);
    const auto delete_clicked = nk_button_label(context, "Delete");
    if (!view.scene_available || !selected) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);

    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        return;
    }
    if (apply_clicked) {
        action.compose_bookmark_id = state.bookmark_id;
        std::memcpy(action.compose_marker_label, state.bookmark_label.data(),
                    sizeof(action.compose_marker_label));
        action.compose_action = selected ? WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                                         : WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK;
    } else if (move_clicked && selected) {
        action.compose_bookmark_id = selected->bookmark_id;
        action.compose_action = WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION;
    } else if (delete_clicked && selected) {
        action.compose_bookmark_id = selected->bookmark_id;
        action.compose_action = WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK;
    }
}

void render_sections(struct nk_context* context, const WormUiComposeView& view,
                     WormUiWorkspaceAction& action) {
    const auto* selected = selected_section(view);
    char header[384]{};
    if (selected) {
        std::snprintf(header, sizeof(header), "Section M%llu:%llu-M%llu:%llu  %.220s",
                      static_cast<unsigned long long>(selected->begin_measure_number),
                      static_cast<unsigned long long>(selected->begin_row),
                      static_cast<unsigned long long>(selected->end_measure_number),
                      static_cast<unsigned long long>(selected->end_row), selected->label);
    } else {
        if (view.beat_section_selection_span_available) {
            std::snprintf(
                header, sizeof(header), "New section M%llu:%llu-M%llu:%llu%s",
                static_cast<unsigned long long>(view.beat_section_selection_begin_measure_number),
                static_cast<unsigned long long>(view.beat_section_selection_begin_row),
                static_cast<unsigned long long>(view.beat_section_selection_end_measure_number),
                static_cast<unsigned long long>(view.beat_section_selection_end_row),
                view.beat_sections_truncated ? " (first 64 shown)" : "");
        } else {
            std::snprintf(header, sizeof(header), "New one-snap section at cursor%s",
                          view.beat_sections_truncated ? " (first 64 shown)" : "");
        }
    }

    nk_layout_row_begin(context, NK_DYNAMIC, 25, 6);
    nk_layout_row_push(context, 0.25F);
    if (nk_combo_begin_label(context, header, nk_vec2(480.0F, 280.0F))) {
        nk_layout_row_dynamic(context, 22, 1);
        const auto* new_section_label = view.beat_section_selection_span_available
                                            ? "New section from selected span"
                                            : "New one-snap section at cursor";
        if (nk_combo_item_label(context, new_section_label, NK_TEXT_LEFT)) {
            state.section_id = 0;
            state.section_label.fill('\0');
        }
        for (std::size_t index = 0; index < view.beat_section_count; ++index) {
            const auto& section = view.beat_sections[index];
            char item[384]{};
            std::snprintf(item, sizeof(item), "M%llu:%llu-M%llu:%llu  %.240s",
                          static_cast<unsigned long long>(section.begin_measure_number),
                          static_cast<unsigned long long>(section.begin_row),
                          static_cast<unsigned long long>(section.end_measure_number),
                          static_cast<unsigned long long>(section.end_row), section.label);
            if (nk_combo_item_label(context, item, NK_TEXT_LEFT)) {
                state.section_id = section.section_id;
                copy_label(state.section_label, section.label);
            }
        }
        nk_combo_end(context);
    }
    nk_layout_row_push(context, 0.29F);
    if (!view.scene_available) {
        nk_widget_disable_begin(context);
    }
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.section_label.data(),
                                         state.section_label.size(), nk_filter_default);
    nk_layout_row_push(context, 0.16F);
    const auto apply_clicked =
        nk_button_label(context, selected                                     ? "Apply rename"
                                 : view.beat_section_selection_span_available ? "Add selected span"
                                                                              : "Add snap span");
    if (!view.scene_available) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.10F);
    if (!view.scene_available || !selected) {
        nk_widget_disable_begin(context);
    }
    const auto set_begin_clicked = nk_button_label(context, "Set start");
    nk_layout_row_push(context, 0.10F);
    const auto set_end_clicked = nk_button_label(context, "Set end");
    nk_layout_row_push(context, 0.10F);
    const auto delete_clicked = nk_button_label(context, "Delete");
    if (!view.scene_available || !selected) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);

    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        return;
    }
    if (apply_clicked) {
        action.compose_section_id = state.section_id;
        std::memcpy(action.compose_marker_label, state.section_label.data(),
                    sizeof(action.compose_marker_label));
        action.compose_action = selected ? WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL
                                         : WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION;
    } else if (set_begin_clicked && selected) {
        action.compose_section_id = selected->section_id;
        action.compose_action = WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN;
    } else if (set_end_clicked && selected) {
        action.compose_section_id = selected->section_id;
        action.compose_action = WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END;
    } else if (delete_clicked && selected) {
        action.compose_section_id = selected->section_id;
        action.compose_action = WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION;
    }
}

} // namespace

void worm_ui_nuklear_reset_compose_markers(void) {
    state = {};
}

void worm_ui_nuklear_render_compose_markers(struct nk_context* context,
                                            const WormUiComposeView* view,
                                            WormUiWorkspaceAction* action) {
    if (!context || !view || !action) {
        return;
    }
    synchronize(*view);
    render_bookmarks(context, *view, *action);
    render_sections(context, *view, *action);
}
