#include "ui/nuklear/metadata/metadata_panel.h"
#include "ui/nuklear/audio/audio_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/metadata/metadata_panel_internal.h"
#include "ui/nuklear/metadata/metadata_audio_section.h"
#include "ui/nuklear/metadata/metadata_difficulty_section.h"
#include "ui/nuklear/metadata/metadata_format_fields.h"
#include "ui/nuklear/metadata/metadata_song_section.h"
#include "ui/nuklear/metadata/metadata_state.h"
#include "ui/nuklear/render/theme.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

worm_ui_nuklear_metadata_state_t& state = worm_ui_nuklear_metadata_state();

metadata_tree_identity_t metadata_tree_identity(int original_line) {
    switch (original_line) {
        case 707: return {__FILE__ ":707", 707};
        case 1532: return {__FILE__ ":1532", 1532};
        case 1589: return {__FILE__ ":1589", 1589};
        case 1672: return {__FILE__ ":1672", 1672};
        case 1728: return {__FILE__ ":1728", 1728};
        case 1896: return {__FILE__ ":1896", 1896};
        default: return {__FILE__, original_line};
    }
}

int metadata_tree_push(nk_context* context, int tree_type, const char* title, int initial_state,
                      int original_line) {
    const auto identity = metadata_tree_identity(original_line);
    return nk_tree_push_hashed(context, static_cast<enum nk_tree_type>(tree_type), title,
                               static_cast<enum nk_collapse_states>(initial_state),
                               identity.hash, static_cast<int>(strlen(identity.hash)),
                               identity.seed);
}

void style_metadata_footer_button(nk_style_button& style, bool enabled, bool accented) {
    if (!accented || !enabled)
        style.normal = nk_style_item_color(wormedit::ui::theme::ui(
            enabled ? wormedit::ui::theme::UiColor::surface
                    : wormedit::ui::theme::UiColor::background));
    style.border_color = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::border);
    style.border = 1.0f;
    style.text_normal = wormedit::ui::theme::ui(
        enabled ? accented ? wormedit::ui::theme::UiColor::selected_text
                           : wormedit::ui::theme::UiColor::text
                : wormedit::ui::theme::UiColor::text_muted);
}

void muted_label(nk_context* context, const char* text) {
    nk_layout_row_dynamic(context, 19, 1);
    nk_label_colored(context, text, NK_TEXT_LEFT,
                     wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
}

int copy_metadata_text(char* destination, size_t capacity, const char* source,
                       size_t source_bytes) {
    if (!destination || capacity == 0 || !source || source_bytes >= capacity ||
        strlen(source) != source_bytes) {
        if (destination && capacity != 0) {
            destination[0] = '\0';
        }
        return 0;
    }
    memcpy(destination, source, source_bytes);
    destination[source_bytes] = '\0';
    return 1;
}

static void initialize_metadata_buffers(const wormedit::ui::metadata_view_t* metadata) {
    if (!metadata) {
        return;
    }
    worm_ui_nuklear_cancel_metadata_tag_drag();
    state.buffer_revision = metadata->document_revision;
    state.buffer_has_chart = metadata->has_chart;
    state.buffer_chart_id = metadata->has_chart ? metadata->chart_id.value : 0;
    state.song_buffers_valid = metadata->song_fields_editable &&
                               copy_metadata_text(state.title.data(), state.title.size(),
                                                  metadata->title.c_str(), metadata->title.size()) &&
                               copy_metadata_text(state.artist.data(), state.artist.size(),
                                                  metadata->artist.c_str(), metadata->artist.size());
    state.chart_buffers_valid =
        metadata->has_chart && metadata->chart_fields_editable &&
        copy_metadata_text(state.chart_name.data(), state.chart_name.size(), metadata->chart_name.c_str(),
                           metadata->chart_name.size()) &&
        copy_metadata_text(state.chart_author.data(), state.chart_author.size(),
                           metadata->chart_author.c_str(), metadata->chart_author.size());
    state.chart_rating_text.fill('\0');
    state.audio_offset_text.fill('\0');
    state.chart_resolution_text.fill('\0');
    if (metadata->has_chart) {
        std::snprintf(state.chart_resolution_text.data(), state.chart_resolution_text.size(), "%llu",
                      static_cast<unsigned long long>(metadata->chart_divisions_per_standard_measure));
        if (metadata->has_chart_rating) {
            const auto rating = worm_ui_nuklear_format_metadata_rational(
                {metadata->chart_rating_numerator, metadata->chart_rating_denominator});
            (void)copy_metadata_text(state.chart_rating_text.data(), state.chart_rating_text.size(),
                                     rating.c_str(), rating.size());
        }
        const auto offset = worm_ui_nuklear_format_metadata_rational(
            {metadata->audio_offset_numerator, metadata->audio_offset_denominator});
        (void)copy_metadata_text(state.audio_offset_text.data(), state.audio_offset_text.size(),
                                 offset.c_str(), offset.size());
    }
    state.tag_buffers_valid =
        metadata->search_tags_editable &&
        metadata->search_tags.size() <= wormedit::default_metadata_edit_limits.maximum_search_tags;
    state.search_tags.clear();
    if (state.tag_buffers_valid) {
        state.search_tags.reserve(metadata->search_tags.size());
        for (size_t index = 0; index < metadata->search_tags.size(); ++index) {
            std::array<char, wormedit::default_metadata_edit_limits.maximum_search_tag_bytes + 1> tag{};
            if (!copy_metadata_text(tag.data(), tag.size(), metadata->search_tags[index].c_str(),
                                    metadata->search_tags[index].size())) {
                state.tag_buffers_valid = 0;
                state.search_tags.clear();
                break;
            }
            state.search_tags.push_back(tag);
        }
    }
    state.new_search_tag.fill('\0');
    state.selected_format_field = std::numeric_limits<size_t>::max();
    state.format_target_valid = 0;
    state.format_target_chart_scoped = 0;
    state.format_target_family = 0;
    state.format_target_index = 0;
    state.format_target_field_count = 0;
    state.format_insert_mode = 0;
    state.format_buffers_valid = 0;
    state.format_name.clear();
    state.format_value.clear();
    initialize_bms_field_buffers(metadata);
    initialize_bmson_field_buffers(metadata);
    initialize_typed_field_buffers(metadata);
}

static int metadata_buffer_key_changed(const wormedit::ui::metadata_view_t* metadata) {
    const uint64_t chart_id = metadata && metadata->has_chart ? metadata->chart_id.value : 0;
    const int has_chart = metadata && metadata->has_chart;
    return metadata && (state.buffer_revision != metadata->document_revision ||
                        state.buffer_chart_id != chart_id || state.buffer_has_chart != has_chart);
}

void metadata_input_row(struct nk_context* context, const char* label, char* buffer,
                        size_t capacity, int enabled) {
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    if (!enabled) {
        nk_widget_disable_begin(context);
    }
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, buffer, capacity,
                                         nk_filter_default);
    if (!enabled) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
}

void render_footer_buttons(nk_context* context, bool revert_enabled, bool apply_enabled,
                           const char* revert_label, const char* apply_label,
                           bool& revert, bool& apply) {
    nk_layout_row_template_begin(context, 23);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 96);
    nk_layout_row_template_push_static(context, 96);
    nk_layout_row_template_end(context);
    nk_spacing(context, 1);
    auto revert_style = wormedit::ui::chrome::button_style(context, false);
    style_metadata_footer_button(revert_style, revert_enabled, false);
    revert = nk_button_label_styled(context, &revert_style, revert_label) && revert_enabled;
    auto apply_style = wormedit::ui::chrome::button_style(context, apply_enabled);
    style_metadata_footer_button(apply_style, apply_enabled, true);
    apply = nk_button_label_styled(context, &apply_style, apply_label) && apply_enabled;
}

void worm_ui_nuklear_render_metadata_panel(struct nk_context* context,
                                           const wormedit::ui::metadata_view_t* metadata,
                                           wormedit::ui::workspace_action_t* action, bool window_focused) {
    if (!context || !metadata || !action || !metadata->visible) {
        worm_ui_nuklear_cancel_metadata_tag_drag();
        return;
    }
    if (!window_focused) worm_ui_nuklear_cancel_metadata_tag_drag();
    if (metadata_buffer_key_changed(metadata)) initialize_metadata_buffers(metadata);
    const MetadataPanel panel{context, metadata, action, window_focused};
    render_song_section(panel);
    nk_layout_row_dynamic(context, 6, 1); nk_spacing(context, 1);
    render_difficulty_section(panel);
    nk_layout_row_dynamic(context, 6, 1); nk_spacing(context, 1);
    render_audio_sync_section(panel);
    nk_layout_row_dynamic(context, 6, 1); nk_spacing(context, 1);
    render_metadata_paths(panel);
    nk_layout_row_dynamic(context, 6, 1); nk_spacing(context, 1);
    render_format_section(panel);
}
