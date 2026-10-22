#pragma once
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/chrome/popup_lifecycle.h"
#include <algorithm>
#include <optional>
namespace wormedit::ui::nuklear {
struct CatalogConfirmation {
    bool& open;
    bool& release;
    std::uint64_t& project;
    std::uint64_t& chart;
    std::uint64_t& revision;
    std::vector<std::uint64_t>& resources;
};
inline auto catalog_confirmation(worm_ui_nuklear_compose_state_t& state, bool bga)
    -> CatalogConfirmation {
    if (bga) return {state.bga_catalog.bga_renumber_confirmation_open, state.bga_catalog.bga_renumber_release_pending,
        state.bga_catalog.bga_renumber_project, state.bga_catalog.bga_renumber_chart, state.bga_catalog.bga_renumber_revision,
        state.bga_catalog.bga_renumber_resources};
    return {state.keysound_slots.keysound_renumber_confirmation_open, state.keysound_slots.keysound_renumber_release_pending,
        state.keysound_slots.keysound_renumber_project, state.keysound_slots.keysound_renumber_chart, state.keysound_slots.keysound_renumber_revision,
        state.keysound_slots.keysound_renumber_resources};
}
inline void open_catalog_confirmation(CatalogConfirmation state, const wormedit::ui::compose_view_t& view,
                                     const std::vector<std::uint64_t>& resources) {
    state.open = true;
    state.project = view.project_id.value;
    state.chart = view.chart_id.value;
    state.revision = view.document_revision;
    state.resources = resources;
}
inline void close_catalog_confirmation(CatalogConfirmation state) {
    state.open = false;
    chrome::popup_arm_release(state.release);
    state.resources.clear();
}
// None means still open or cancelled; false/true select the captured subset/all.
inline auto render_catalog_confirmation(nk_context* context, const wormedit::ui::compose_view_t& view,
                                       CatalogConfirmation state, bool bga) -> std::optional<bool> {
    if (!state.open) return std::nullopt;
    const auto bounds = nk_rect(0, 60, std::min(360.0f, nk_window_get_content_region(context).w), 204);
    const auto* title = bga ? "Renumber BMP IDs" : "Renumber WAV IDs";
    const bool available = bga ? view.bga_ids_available : view.keysound_ids_available;
    if (!available || !chrome::popup_context_matches(
            {state.project, state.chart, state.revision}, wormedit::ui::chrome::PopupContextStamp::from(view))) {
        close_catalog_confirmation(state);
        chrome::dismiss_popup(context, title, bounds);
        return std::nullopt;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, title, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        close_catalog_confirmation(state);
        return std::nullopt;
    }
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, bga
        ? "Renumber BMP IDs in natural name order? Choose selected images or all BGA."
        : "Renumber WAV IDs in natural name order? Choose the selected sounds or the whole list.");
    nk_layout_row_dynamic(context, 28, 2);
    std::optional<bool> choice;
    if (!state.resources.empty() && nk_button_label(context, "Selected")) choice = false;
    if (nk_button_label(context, bga ? "All BGA" : "All sounds")) choice = true;
    if (nk_button_label(context, "Cancel")) close_catalog_confirmation(state);
    if (!state.open || choice.has_value()) nk_popup_close(context);
    nk_popup_end(context);
    return choice;
}
inline auto slot_review_active(const worm_ui_nuklear_compose_state_t& state) -> bool {
    return state.keysound_slots.keysound_slot_confirmation_open || state.keysound_slots.keysound_slot_release_pending ||
           state.keysound_slots.keysound_slot_batch.open || state.keysound_slots.keysound_slot_batch.commit_pending;
}
} // namespace wormedit::ui::nuklear
