#include "ui/nuklear/metadata/metadata_song_section.h"

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/chrome/tooltip.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

static int metadata_song_changed(const wormedit::ui::metadata_view_t* metadata) {
    if (!metadata || !state.song_buffers_valid || !state.tag_buffers_valid || !metadata->title.c_str() ||
        !metadata->artist.c_str()) {
        return 0;
    }
    if (metadata->search_tags.size() != state.search_tags.size()) {
        return 1;
    }
    if (strcmp(state.title.data(), metadata->title.c_str()) != 0 ||
        strcmp(state.artist.data(), metadata->artist.c_str()) != 0) {
        return 1;
    }
    for (size_t index = 0; index < state.search_tags.size(); ++index) {
        if (metadata->search_tags.empty() ||
            strcmp(state.search_tags[index].data(), metadata->search_tags[index].c_str()) != 0) {
            return 1;
        }
    }
    return 0;
}

static void set_metadata_song_action(wormedit::ui::workspace_action_t* action,
                                     const wormedit::ui::metadata_view_t* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !state.tag_buffers_valid ||
        state.search_tags.size() > wormedit::default_metadata_edit_limits.maximum_search_tags) {
        return;
    }
    action->metadata_edit = WORM_UI_METADATA_EDIT_SONG;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_title = state.title.data();
    action->metadata_artist = state.artist.data();
    for (const auto& tag : state.search_tags)
        action->metadata_search_tags.emplace_back(tag.data());
}

void reset_song_draft(const wormedit::ui::metadata_view_t& metadata) {
    worm_ui_nuklear_cancel_metadata_tag_drag();
    (void)copy_metadata_text(state.title.data(), state.title.size(), metadata.title.c_str(),
                             metadata.title.size());
    (void)copy_metadata_text(state.artist.data(), state.artist.size(), metadata.artist.c_str(),
                             metadata.artist.size());
    state.search_tags.clear();
    for (size_t i = 0; i < metadata.search_tags.size(); ++i) {
        std::array<char, wormedit::default_metadata_edit_limits.maximum_search_tag_bytes + 1> tag{};
        if (!copy_metadata_text(tag.data(), tag.size(), metadata.search_tags[i].c_str(),
                                metadata.search_tags[i].size())) break;
        state.search_tags.push_back(tag);
    }
    state.new_search_tag.fill('\0');
}

auto render_tag_grip(nk_context* context, std::size_t index, bool editable) -> struct nk_rect {
    struct nk_rect bounds{};
    const auto widget = nk_widget(&bounds, context);
    using namespace wormedit::ui;
    lucide::draw(context, bounds, lucide::Glyph::grip_vertical,
        chrome::color(state.tag_drag.source == index ? chrome::UiColor::accent
                                                     : chrome::UiColor::text_muted));
    const bool hovered = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (hovered) nuklear::tooltip::text(context, "Drag to reorder tag");
    if (editable && widget == NK_WIDGET_VALID && hovered &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) {
        const auto mouse = context->input.mouse.pos;
        (void)worm_ui_nuklear_begin_metadata_tag_drag(index, mouse.x, mouse.y);
    }
    return bounds;
}

void preview_tag_drop(nk_context* context, std::size_t index, struct nk_rect bounds) {
    if (!state.tag_drag.source || !state.tag_drag.moved ||
        !nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip) ||
        !nk_input_is_mouse_hovering_rect(&context->input, bounds)) return;
    const bool after = context->input.mouse.pos.y >= bounds.y + bounds.h * 0.5f;
    worm_ui_nuklear_target_metadata_tag_drag(index, after);
    const auto slot = *state.tag_drag.insertion;
    if (slot == *state.tag_drag.source || slot == *state.tag_drag.source + 1) return;
    const auto y = after ? bounds.y + bounds.h : bounds.y;
    nk_stroke_line(nk_window_get_canvas(context), bounds.x, y, bounds.x + bounds.w, y, 2.0f,
        wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::accent));
}

void prepare_tag_drag(nk_context* context, bool editable) {
    if (!editable || nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE))
        worm_ui_nuklear_cancel_metadata_tag_drag();
    if (!state.tag_drag.source) return;
    state.tag_drag.insertion.reset();
    const auto mouse = context->input.mouse.pos;
    state.tag_drag.moved = state.tag_drag.moved ||
        std::hypot(mouse.x - state.tag_drag.start_x, mouse.y - state.tag_drag.start_y) >= 3.0f;
}

void finish_tag_drag(nk_context* context) {
    if (!state.tag_drag.source || context->input.mouse.buttons[NK_BUTTON_LEFT].down) return;
    if (nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT))
        (void)worm_ui_nuklear_finish_metadata_tag_drag();
    else worm_ui_nuklear_cancel_metadata_tag_drag();
}

// Returns true when the row erased its tag; the caller must stop iterating.
auto render_metadata_tag_row(nk_context* context, std::size_t index, bool editable) -> bool {
    nk_layout_row_template_begin(context, 25);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_push_static(context, 30);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 26);
    nk_layout_row_template_end(context);
    auto row = render_tag_grip(context, index, editable);
    nk_labelf(context, NK_TEXT_LEFT, "%zu", index + 1);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.search_tags[index].data(), state.search_tags[index].size(), nk_filter_default);
    const auto remove_bounds = nk_widget_bounds(context);
    char remove_label[48];
    (void)snprintf(remove_label, sizeof(remove_label), "Remove tag %zu", index + 1);
    const auto remove = wormedit::ui::chrome::IconButton{
        wormedit::ui::lucide::Glyph::x, remove_label}.render(context);
    if (!editable) nk_widget_disable_end(context);
    row.w = remove_bounds.x + remove_bounds.w - row.x;
    preview_tag_drop(context, index, row);
    if (!remove || !editable) return false;
    worm_ui_nuklear_cancel_metadata_tag_drag();
    state.search_tags.erase(state.search_tags.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void render_metadata_tag_editor(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    const auto editable = panel.window_focused && metadata.song_fields_editable &&
                          metadata.search_tags_editable && state.song_buffers_valid && state.tag_buffers_valid;
    prepare_tag_drag(context, editable);
    if (!state.tag_buffers_valid || !metadata.song_fields_editable || !metadata.search_tags_editable) {
        muted_label(context, "Tags are read-only because the song metadata exceeds an edit cap.");
        return;
    }
    const auto visible = std::min(state.search_tags.size(),
                                  worm_ui_nuklear_metadata_state_t::visible_tag_count);
    for (std::size_t index = 0; index < visible; ++index) {
        if (render_metadata_tag_row(context, index, editable))
            break;
    }
    finish_tag_drag(context);
    if (state.search_tags.size() > worm_ui_nuklear_metadata_state_t::visible_tag_count)
        muted_label(context, "Some search tags are hidden by the row limit.");
    metadata_form_row(context, 25, {metadata_row_button_width});
    nk_label(context, "Search tags", NK_TEXT_LEFT);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.new_search_tag.data(),
                                         state.new_search_tag.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    const auto can_add = editable && state.search_tags.size() < wormedit::default_metadata_edit_limits.maximum_search_tags &&
                         state.new_search_tag[0] != '\0';
    if (!can_add) nk_widget_disable_begin(context);
    const auto add = nk_button_label(context, "Add tag");
    if (!can_add) nk_widget_disable_end(context);
    if (add && can_add) {
        worm_ui_nuklear_cancel_metadata_tag_drag();
        state.search_tags.push_back(state.new_search_tag);
        state.new_search_tag.fill('\0');
    }
}

} // namespace

void render_song_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    char heading[256];
    std::snprintf(heading, sizeof(heading), "Song · %s", metadata.title.c_str() ? metadata.title.c_str() : "");
    if (!metadata_tree_push(context, NK_TREE_TAB, heading, NK_MAXIMIZED, 1532)) {
        worm_ui_nuklear_cancel_metadata_tag_drag();
        return;
    }
    const auto editable = metadata.song_fields_editable && state.song_buffers_valid;
    metadata_input_row(context, "Title", state.title.data(), state.title.size(), editable);
    metadata_input_row(context, "Artist", state.artist.data(), state.artist.size(), editable);
    render_metadata_tag_editor(panel);
    const auto changed = metadata_song_changed(&metadata) != 0;
    bool revert = false, apply = false;
    const auto can_edit = editable && state.tag_buffers_valid;
    render_footer_buttons(context, changed && can_edit, changed && can_edit, "Revert", "Apply",
                          revert, apply);
    if (revert) reset_song_draft(metadata);
    if (apply) set_metadata_song_action(panel.action, &metadata);
    wormedit::ui::chrome::section_pop(context);
}
