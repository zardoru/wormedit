#include "ui/nuklear/metadata_song_section.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

static int metadata_song_changed(const WormUiMetadataView* metadata) {
    if (!metadata || !state.song_buffers_valid || !state.tag_buffers_valid || !metadata->title ||
        !metadata->artist) {
        return 0;
    }
    if (metadata->search_tag_count != state.search_tags.size()) {
        return 1;
    }
    if (strcmp(state.title.data(), metadata->title) != 0 ||
        strcmp(state.artist.data(), metadata->artist) != 0) {
        return 1;
    }
    for (size_t index = 0; index < state.search_tags.size(); ++index) {
        if (!metadata->search_tags ||
            strcmp(state.search_tags[index].data(), metadata->search_tags[index]) != 0) {
            return 1;
        }
    }
    return 0;
}

static void set_metadata_song_action(WormUiWorkspaceAction* action,
                                     const WormUiMetadataView* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !state.tag_buffers_valid ||
        state.search_tags.size() > WORM_UI_METADATA_SEARCH_TAG_CAPACITY) {
        return;
    }
    state.search_tag_pointers.clear();
    state.search_tag_pointers.reserve(state.search_tags.size());
    for (const auto& tag : state.search_tags) {
        state.search_tag_pointers.push_back(tag.data());
    }
    action->metadata_edit = WORM_UI_METADATA_EDIT_SONG;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_title = state.title.data();
    action->metadata_artist = state.artist.data();
    action->metadata_search_tags = state.search_tag_pointers.data();
    action->metadata_search_tag_count = state.search_tag_pointers.size();
}

void reset_song_draft(const WormUiMetadataView& metadata) {
    (void)copy_metadata_text(state.title.data(), state.title.size(), metadata.title,
                             metadata.title_bytes);
    (void)copy_metadata_text(state.artist.data(), state.artist.size(), metadata.artist,
                             metadata.artist_bytes);
    state.search_tags.clear();
    for (size_t i = 0; i < metadata.search_tag_count; ++i) {
        std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> tag{};
        if (!copy_metadata_text(tag.data(), tag.size(), metadata.search_tags[i],
                                metadata.search_tag_bytes[i])) break;
        state.search_tags.push_back(tag);
    }
    state.new_search_tag.fill('\0');
}

// Returns true when the row erased its tag; the caller must stop iterating.
auto render_metadata_tag_row(nk_context* context, std::size_t index, bool editable) -> bool {
    char remove_label[32];
    (void)snprintf(remove_label, sizeof(remove_label), "Remove %zu", index + 1);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 5);
    nk_layout_row_push(context, 0.28f);
    nk_labelf(context, NK_TEXT_LEFT, "%zu", index + 1);
    nk_layout_row_push(context, 0.41f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.search_tags[index].data(),
                                         state.search_tags[index].size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.07f);
    const auto can_up = editable && index != 0;
    if (!can_up) nk_widget_disable_begin(context);
    const auto up = nk_button_label(context, "Up");
    if (!can_up) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.08f);
    const auto can_down = editable && index + 1 < state.search_tags.size();
    if (!can_down) nk_widget_disable_begin(context);
    const auto down = nk_button_label(context, "Down");
    if (!can_down) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.16f);
    if (!editable) nk_widget_disable_begin(context);
    const auto remove = nk_button_label(context, remove_label);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (up && can_up) std::swap(state.search_tags[index], state.search_tags[index - 1]);
    else if (down && can_down) std::swap(state.search_tags[index], state.search_tags[index + 1]);
    else if (remove && editable) {
        state.search_tags.erase(state.search_tags.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    return false;
}

void render_metadata_tag_editor(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    const auto editable = metadata.song_fields_editable && state.song_buffers_valid &&
                          state.tag_buffers_valid;
    if (!state.tag_buffers_valid || !metadata.song_fields_editable) {
        muted_label(context, "Tags are read-only because the song metadata exceeds an edit cap.");
        return;
    }
    const auto visible = std::min(state.search_tags.size(),
                                  worm_ui_nuklear_metadata_state_t::visible_tag_count);
    for (std::size_t index = 0; index < visible; ++index) {
        if (render_metadata_tag_row(context, index, editable))
            break;
    }
    if (state.search_tags.size() > worm_ui_nuklear_metadata_state_t::visible_tag_count)
        muted_label(context, "Some search tags are hidden by the row limit.");
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 3);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Search tags", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.52f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.new_search_tag.data(),
                                         state.new_search_tag.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.20f);
    const auto can_add = editable && state.search_tags.size() < WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
                         state.new_search_tag[0] != '\0';
    if (!can_add) nk_widget_disable_begin(context);
    const auto add = nk_button_label(context, "Add tag");
    if (!can_add) nk_widget_disable_end(context);
    if (add && can_add) {
        state.search_tags.push_back(state.new_search_tag);
        state.new_search_tag.fill('\0');
    }
    nk_layout_row_end(context);
}

} // namespace

void render_song_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    if (!metadata_tree_push(context, NK_TREE_TAB, "Song", NK_MAXIMIZED, 1532)) return;
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
    nk_tree_pop(context);
}
