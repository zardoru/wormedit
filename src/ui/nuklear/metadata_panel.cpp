#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/metadata_state.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

static auto& state = worm_ui_nuklear_metadata_state();

static int copy_metadata_text(char* destination, size_t capacity, const char* source,
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

static void initialize_metadata_buffers(const WormUiMetadataView* metadata) {
    if (!metadata) {
        return;
    }
    state.buffer_revision = metadata->document_revision;
    state.buffer_has_chart = metadata->has_chart;
    state.buffer_chart_id = metadata->has_chart ? metadata->chart_id : 0;
    state.song_buffers_valid = metadata->song_fields_editable &&
                               copy_metadata_text(state.title.data(), state.title.size(),
                                                  metadata->title, metadata->title_bytes) &&
                               copy_metadata_text(state.artist.data(), state.artist.size(),
                                                  metadata->artist, metadata->artist_bytes);
    state.chart_buffers_valid =
        metadata->has_chart && metadata->chart_fields_editable &&
        copy_metadata_text(state.chart_name.data(), state.chart_name.size(), metadata->chart_name,
                           metadata->chart_name_bytes) &&
        copy_metadata_text(state.chart_author.data(), state.chart_author.size(),
                           metadata->chart_author, metadata->chart_author_bytes);
    state.chart_rating_numerator.fill('\0');
    state.chart_rating_denominator.fill('\0');
    state.audio_offset_numerator.fill('\0');
    state.audio_offset_denominator.fill('\0');
    if (metadata->has_chart) {
        const auto numerator = metadata->has_chart_rating
                                   ? std::to_string(metadata->chart_rating_numerator)
                                   : std::string{};
        const auto denominator = metadata->has_chart_rating
                                     ? std::to_string(metadata->chart_rating_denominator)
                                     : std::string{"1"};
        (void)copy_metadata_text(state.chart_rating_numerator.data(),
                                 state.chart_rating_numerator.size(), numerator.c_str(),
                                 numerator.size());
        (void)copy_metadata_text(state.chart_rating_denominator.data(),
                                 state.chart_rating_denominator.size(), denominator.c_str(),
                                 denominator.size());
        const auto offset_numerator = std::to_string(metadata->audio_offset_numerator);
        const auto offset_denominator = std::to_string(metadata->audio_offset_denominator);
        (void)copy_metadata_text(state.audio_offset_numerator.data(),
                                 state.audio_offset_numerator.size(), offset_numerator.c_str(),
                                 offset_numerator.size());
        (void)copy_metadata_text(state.audio_offset_denominator.data(),
                                 state.audio_offset_denominator.size(), offset_denominator.c_str(),
                                 offset_denominator.size());
    }
    state.tag_buffers_valid =
        metadata->search_tags_editable &&
        metadata->search_tag_count <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
        (metadata->search_tag_count == 0 || (metadata->search_tags && metadata->search_tag_bytes));
    state.search_tags.clear();
    if (state.tag_buffers_valid) {
        state.search_tags.reserve(metadata->search_tag_count);
        for (size_t index = 0; index < metadata->search_tag_count; ++index) {
            std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> tag{};
            if (!copy_metadata_text(tag.data(), tag.size(), metadata->search_tags[index],
                                    metadata->search_tag_bytes[index])) {
                state.tag_buffers_valid = 0;
                state.search_tags.clear();
                break;
            }
            state.search_tags.push_back(tag);
        }
    }
    state.search_tag_pointers.clear();
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
}

static int metadata_buffer_key_changed(const WormUiMetadataView* metadata) {
    const uint64_t chart_id = metadata && metadata->has_chart ? metadata->chart_id : 0;
    const int has_chart = metadata && metadata->has_chart;
    return metadata && (state.buffer_revision != metadata->document_revision ||
                        state.buffer_chart_id != chart_id || state.buffer_has_chart != has_chart);
}

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

struct metadata_rating_value_t {
    bool has_rating{};
    int64_t numerator{};
    uint64_t denominator{1};
};

static auto metadata_audio_offset_value() -> std::optional<metadata_rating_value_t> {
    const std::string_view numerator{state.audio_offset_numerator.data()};
    const std::string_view denominator{state.audio_offset_denominator.data()};
    if (numerator.empty() || denominator.empty() || numerator.size() > 21 ||
        denominator.size() > 21) {
        return std::nullopt;
    }
    auto numerator_text = numerator;
    if (numerator_text.front() == '+')
        numerator_text.remove_prefix(1);
    int64_t parsed_numerator{};
    const auto numerator_result = std::from_chars(
        numerator_text.data(), numerator_text.data() + numerator_text.size(), parsed_numerator, 10);
    if (numerator_result.ec != std::errc{} ||
        numerator_result.ptr != numerator_text.data() + numerator_text.size()) {
        return std::nullopt;
    }
    auto denominator_text = denominator;
    if (denominator_text.front() == '+')
        denominator_text.remove_prefix(1);
    uint64_t parsed_denominator{};
    const auto denominator_result =
        std::from_chars(denominator_text.data(), denominator_text.data() + denominator_text.size(),
                        parsed_denominator, 10);
    if (denominator_result.ec != std::errc{} ||
        denominator_result.ptr != denominator_text.data() + denominator_text.size() ||
        parsed_denominator == 0) {
        return std::nullopt;
    }
    return metadata_rating_value_t{true, parsed_numerator, parsed_denominator};
}

static auto metadata_chart_rating_value(const WormUiMetadataView* metadata)
    -> std::optional<metadata_rating_value_t> {
    if (!metadata)
        return std::nullopt;
    const std::string_view numerator{state.chart_rating_numerator.data()};
    const std::string_view denominator{state.chart_rating_denominator.data()};
    if (numerator.empty() && !metadata->has_chart_rating && denominator == "1") {
        return metadata_rating_value_t{};
    }
    if (numerator.empty() || denominator.empty() || numerator.size() > 21 ||
        denominator.size() > 21) {
        return std::nullopt;
    }

    const auto numerator_digits =
        numerator.front() == '+' || numerator.front() == '-' ? numerator.substr(1) : numerator;
    if (numerator_digits.empty() || numerator_digits.size() > 20)
        return std::nullopt;
    auto numerator_text = numerator;
    if (numerator_text.front() == '+')
        numerator_text.remove_prefix(1);
    int64_t parsed_numerator{};
    const auto numerator_result = std::from_chars(
        numerator_text.data(), numerator_text.data() + numerator_text.size(), parsed_numerator, 10);
    if (numerator_result.ec != std::errc{} ||
        numerator_result.ptr != numerator_text.data() + numerator_text.size()) {
        return std::nullopt;
    }

    auto denominator_text = denominator;
    if (denominator_text.front() == '+')
        denominator_text.remove_prefix(1);
    if (denominator_text.empty() || denominator_text.size() > 20)
        return std::nullopt;
    uint64_t parsed_denominator{};
    const auto denominator_result =
        std::from_chars(denominator_text.data(), denominator_text.data() + denominator_text.size(),
                        parsed_denominator, 10);
    if (denominator_result.ec != std::errc{} ||
        denominator_result.ptr != denominator_text.data() + denominator_text.size() ||
        parsed_denominator == 0) {
        return std::nullopt;
    }
    return metadata_rating_value_t{true, parsed_numerator, parsed_denominator};
}

static int metadata_chart_changed(const WormUiMetadataView* metadata) {
    if (!metadata || !metadata->has_chart || !state.chart_buffers_valid || !metadata->chart_name ||
        !metadata->chart_author) {
        return 0;
    }
    const bool text_changed = strcmp(state.chart_name.data(), metadata->chart_name) != 0 ||
                              strcmp(state.chart_author.data(), metadata->chart_author) != 0;
    const auto rating = metadata_chart_rating_value(metadata);
    const bool rating_changed =
        rating &&
        (rating->has_rating != metadata->has_chart_rating ||
         (rating->has_rating && (rating->numerator != metadata->chart_rating_numerator ||
                                 rating->denominator != metadata->chart_rating_denominator)));
    return text_changed || rating_changed;
}

static void metadata_input_row(struct nk_context* context, const char* label, char* buffer,
                               size_t capacity, int enabled) {
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.20f);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.80f);
    if (!enabled) {
        nk_widget_disable_begin(context);
    }
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, buffer, capacity,
                                         nk_filter_default);
    if (!enabled) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
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

static void set_metadata_chart_action(WormUiWorkspaceAction* action,
                                      const WormUiMetadataView* metadata, int unset_rating) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !metadata->has_chart || !state.chart_buffers_valid) {
        return;
    }
    const auto rating = unset_rating ? std::optional<metadata_rating_value_t>{}
                                     : metadata_chart_rating_value(metadata);
    if (!unset_rating && !rating)
        return;
    action->metadata_edit = WORM_UI_METADATA_EDIT_CHART;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = metadata->chart_id;
    action->metadata_chart_name = state.chart_name.data();
    action->metadata_chart_author = state.chart_author.data();
    action->metadata_has_chart_rating = !unset_rating && rating->has_rating;
    if (action->metadata_has_chart_rating) {
        action->metadata_chart_rating_numerator_text = state.chart_rating_numerator.data();
        action->metadata_chart_rating_denominator_text = state.chart_rating_denominator.data();
    }
}

static void set_metadata_audio_offset_action(WormUiWorkspaceAction* action,
                                             const WormUiMetadataView* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !metadata->has_chart || !metadata->audio_offset_editable || metadata->timing_locked ||
        !metadata_audio_offset_value()) {
        return;
    }
    action->metadata_edit = WORM_UI_METADATA_EDIT_AUDIO_OFFSET;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = metadata->chart_id;
    action->metadata_audio_offset_numerator_text = state.audio_offset_numerator.data();
    action->metadata_audio_offset_denominator_text = state.audio_offset_denominator.data();
}

static const char* metadata_format_family_name(uint8_t family) {
    static const char* names[] = {"Native", "BMS",  "StepMania", "osu!mania",
                                  "BMSON",  "MIDI", "Quaver"};
    return family < sizeof(names) / sizeof(names[0]) ? names[family] : "Unknown";
}

static int initialize_format_buffers(const WormUiMetadataFormatFieldView* field, int insert_mode) {
    state.format_name.assign(WORM_UI_METADATA_FORMAT_NAME_BYTES + 1, '\0');
    state.format_value.assign(WORM_UI_METADATA_FORMAT_VALUE_BYTES + 1, '\0');
    state.format_insert_mode = insert_mode != 0;
    if (insert_mode) {
        state.format_buffers_valid = field && field->first_group_for_family;
        return state.format_buffers_valid;
    }
    if (!field || !field->first_group_for_family || !field->name || !field->value ||
        field->group_field_count > WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY) {
        state.format_buffers_valid = 0;
        return 0;
    }
    state.format_buffers_valid =
        copy_metadata_text(state.format_name.data(), state.format_name.size(), field->name,
                           field->name_bytes) &&
        copy_metadata_text(state.format_value.data(), state.format_value.size(), field->value,
                           field->value_bytes);
    return state.format_buffers_valid;
}

static void initialize_format_target_buffers(const WormUiMetadataView* metadata,
                                             const WormUiMetadataFormatTargetView* target) {
    if (!metadata || !target || target->family > 6 ||
        (target->chart_scoped && !metadata->has_chart) ||
        target->field_count > WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY) {
        state.format_target_valid = 0;
        state.format_buffers_valid = 0;
        return;
    }
    state.selected_format_field = std::numeric_limits<size_t>::max();
    state.format_target_valid = 1;
    state.format_target_chart_scoped = target->chart_scoped;
    state.format_target_family = target->family;
    state.format_target_field_count = target->field_count;
    state.format_target_index = target->field_count;
    state.format_insert_mode = 1;
    state.format_name.assign(WORM_UI_METADATA_FORMAT_NAME_BYTES + 1, '\0');
    state.format_value.assign(WORM_UI_METADATA_FORMAT_VALUE_BYTES + 1, '\0');
    state.format_buffers_valid = 1;
}

static void set_metadata_format_action(WormUiWorkspaceAction* action,
                                       const WormUiMetadataView* metadata,
                                       WormUiMetadataEditKind kind) {
    const int needs_text = kind != WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD;
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !state.format_target_valid || (needs_text && !state.format_buffers_valid) ||
        state.format_target_family > 6 ||
        (state.format_target_chart_scoped && !metadata->has_chart)) {
        return;
    }
    action->metadata_edit = kind;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = state.format_target_chart_scoped ? metadata->chart_id : 0;
    action->metadata_format_chart_scoped = state.format_target_chart_scoped;
    action->metadata_format_family = state.format_target_family;
    action->metadata_format_index = state.format_target_index;
    action->metadata_format_name = needs_text ? state.format_name.data() : nullptr;
    action->metadata_format_value = needs_text ? state.format_value.data() : nullptr;
}

void worm_ui_nuklear_render_metadata_panel(struct nk_context* context,
                                           const WormUiMetadataView* metadata,
                                           WormUiWorkspaceAction* action, bool window_focused) {
    if (!context || !metadata || !action || !metadata->visible) {
        return;
    }
    if (metadata_buffer_key_changed(metadata)) {
        initialize_metadata_buffers(metadata);
    }

    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Song metadata", NK_TEXT_LEFT);
    metadata_input_row(context, "Title", state.title.data(), state.title.size(),
                       metadata->song_fields_editable && state.song_buffers_valid);
    metadata_input_row(context, "Artist", state.artist.data(), state.artist.size(),
                       metadata->song_fields_editable && state.song_buffers_valid);

    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Ordered search tags (%zu; duplicates are preserved)",
              metadata->search_tag_count);
    const int tags_editable =
        metadata->song_fields_editable && state.song_buffers_valid && state.tag_buffers_valid;
    if (!state.tag_buffers_valid || !metadata->song_fields_editable) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Tags are read-only because the song metadata exceeds an edit cap.",
                 NK_TEXT_LEFT);
    } else {
        const size_t visible_tags =
            std::min(state.search_tags.size(), worm_ui_nuklear_metadata_state_t::visible_tag_count);
        for (size_t index = 0; index < visible_tags; ++index) {
            char remove_label[32];
            (void)snprintf(remove_label, sizeof(remove_label), "Remove %zu", index + 1);
            nk_layout_row_begin(context, NK_DYNAMIC, 25, 5);
            nk_layout_row_push(context, 0.07f);
            nk_labelf(context, NK_TEXT_LEFT, "%zu", index + 1);
            nk_layout_row_push(context, 0.57f);
            if (!tags_editable) {
                nk_widget_disable_begin(context);
            }
            (void)nk_edit_string_zero_terminated(
                context, NK_EDIT_FIELD, state.search_tags[index].data(),
                state.search_tags[index].size(), nk_filter_default);
            if (!tags_editable) {
                nk_widget_disable_end(context);
            }
            nk_layout_row_push(context, 0.09f);
            const int can_move_up = tags_editable && index != 0;
            if (!can_move_up) {
                nk_widget_disable_begin(context);
            }
            const int move_up = nk_button_label(context, "Up");
            if (!can_move_up) {
                nk_widget_disable_end(context);
            }
            nk_layout_row_push(context, 0.11f);
            const int can_move_down = tags_editable && index + 1 < state.search_tags.size();
            if (!can_move_down) {
                nk_widget_disable_begin(context);
            }
            const int move_down = nk_button_label(context, "Down");
            if (!can_move_down) {
                nk_widget_disable_end(context);
            }
            nk_layout_row_push(context, 0.16f);
            if (!tags_editable) {
                nk_widget_disable_begin(context);
            }
            const int remove = nk_button_label(context, remove_label);
            if (!tags_editable) {
                nk_widget_disable_end(context);
            }
            if (move_up && can_move_up) {
                std::swap(state.search_tags[index], state.search_tags[index - 1]);
            } else if (move_down && can_move_down) {
                std::swap(state.search_tags[index], state.search_tags[index + 1]);
            } else if (remove && tags_editable) {
                state.search_tags.erase(state.search_tags.begin() +
                                        static_cast<std::ptrdiff_t>(index));
                nk_layout_row_end(context);
                break;
            }
            nk_layout_row_end(context);
        }
        if (state.search_tags.size() > worm_ui_nuklear_metadata_state_t::visible_tag_count) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Showing first %zu tags; the rest stay unchanged.",
                      worm_ui_nuklear_metadata_state_t::visible_tag_count);
        }
        nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(context, 0.82f);
        if (!tags_editable) {
            nk_widget_disable_begin(context);
        }
        (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.new_search_tag.data(),
                                             state.new_search_tag.size(), nk_filter_default);
        if (!tags_editable) {
            nk_widget_disable_end(context);
        }
        nk_layout_row_push(context, 0.18f);
        const int can_add_tag = tags_editable &&
                                state.search_tags.size() < WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
                                state.new_search_tag[0] != '\0';
        if (!can_add_tag) {
            nk_widget_disable_begin(context);
        }
        const int add_tag = nk_button_label(context, "Add tag");
        if (!can_add_tag) {
            nk_widget_disable_end(context);
        }
        if (add_tag && can_add_tag) {
            state.search_tags.push_back(state.new_search_tag);
            state.new_search_tag.fill('\0');
        }
        nk_layout_row_end(context);
    }

    const int song_changed = metadata_song_changed(metadata);
    nk_layout_row_dynamic(context, 26, 1);
    if (!song_changed || !metadata->song_fields_editable) {
        nk_widget_disable_begin(context);
    }
    if (nk_button_label(context, "Apply song metadata") && song_changed &&
        metadata->song_fields_editable) {
        set_metadata_song_action(action, metadata);
    }
    if (!song_changed || !metadata->song_fields_editable) {
        nk_widget_disable_end(context);
    }

    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Audio and synchronization", NK_TEXT_LEFT);
    if (metadata->has_chart) {
        const int offset_editable = metadata->audio_offset_editable && !metadata->timing_locked;
        metadata_input_row(context, "Offset seconds numerator", state.audio_offset_numerator.data(),
                           state.audio_offset_numerator.size(), offset_editable);
        metadata_input_row(context, "Offset seconds denominator",
                           state.audio_offset_denominator.data(),
                           state.audio_offset_denominator.size(), offset_editable);
        const auto parsed_offset = metadata_audio_offset_value();
        const int offset_changed =
            parsed_offset && (parsed_offset->numerator != metadata->audio_offset_numerator ||
                              parsed_offset->denominator != metadata->audio_offset_denominator);
        if (!parsed_offset) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context, "Use a signed 64-bit numerator and positive 64-bit denominator.",
                     NK_TEXT_LEFT);
        } else if (metadata->timing_locked) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context, "Unlock timing to change audio synchronization.", NK_TEXT_LEFT);
        }
        const int can_apply_offset = offset_editable && offset_changed && action &&
                                     action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        nk_layout_row_dynamic(context, 26, 1);
        if (!can_apply_offset)
            nk_widget_disable_begin(context);
        const int apply_offset = nk_button_label(context, "Apply exact audio offset");
        if (!can_apply_offset)
            nk_widget_disable_end(context);
        if (apply_offset && can_apply_offset)
            set_metadata_audio_offset_action(action, metadata);

        const WormUiAudioSlicesView offset_waveform{
            .visible = true,
            .assets_truncated = false,
            .asset_ids = nullptr,
            .asset_count = 0,
            .has_selected_asset = metadata->audio_waveform_has_selected_asset,
            .selected_asset_id = metadata->audio_waveform_selected_asset_id,
            .analysis_pending = metadata->audio_waveform_analysis_pending,
            .waveform_available = metadata->audio_waveform_available,
            .total_frames = metadata->audio_waveform_total_frames,
            .sample_rate = metadata->audio_waveform_sample_rate,
            .waveform_columns = metadata->audio_waveform_columns,
            .waveform_column_count = metadata->audio_waveform_column_count,
            .has_chart = metadata->has_chart,
            .chart_id = metadata->chart_id,
            .document_revision = metadata->document_revision,
            .timing_locked = metadata->timing_locked,
            .offset_numerator = metadata->audio_offset_numerator,
            .offset_denominator = metadata->audio_offset_denominator,
            .offset_drag_active = metadata->audio_offset_drag_active,
            .offset_drag_delta_frames = metadata->audio_offset_drag_delta_frames,
            .proposed_offset_numerator = metadata->proposed_audio_offset_numerator,
            .proposed_offset_denominator = metadata->proposed_audio_offset_denominator,
        };
        worm_ui_nuklear_render_audio_slices(context, &offset_waveform, action, false,
                                            window_focused);
    } else {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Select a difficulty to edit its beat-zero audio offset.", NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context,
             metadata->has_chart ? "Selected difficulty metadata"
                                 : "Select a difficulty to edit chart metadata",
             NK_TEXT_LEFT);
    if (metadata->has_chart) {
        metadata_input_row(context, "Difficulty", state.chart_name.data(), state.chart_name.size(),
                           metadata->chart_fields_editable && state.chart_buffers_valid);
        metadata_input_row(context, "Mapper", state.chart_author.data(), state.chart_author.size(),
                           metadata->chart_fields_editable && state.chart_buffers_valid);
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Exact signed rational rating (stored without reduction)", NK_TEXT_LEFT);
        const int rating_fields_editable =
            metadata->chart_fields_editable && state.chart_buffers_valid;
        metadata_input_row(context, "Numerator", state.chart_rating_numerator.data(),
                           state.chart_rating_numerator.size(), rating_fields_editable);
        metadata_input_row(context, "Denominator", state.chart_rating_denominator.data(),
                           state.chart_rating_denominator.size(), rating_fields_editable);
        const auto parsed_rating = metadata_chart_rating_value(metadata);
        const bool rating_input_started = state.chart_rating_numerator[0] != '\0' ||
                                          strcmp(state.chart_rating_denominator.data(), "1") != 0;
        if (!parsed_rating && (metadata->has_chart_rating || rating_input_started)) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context,
                     "Use a signed 64-bit numerator and positive 64-bit denominator; values are "
                     "not truncated or reduced.",
                     NK_TEXT_LEFT);
        }
        const int chart_changed = metadata_chart_changed(metadata);
        const int can_apply_chart = chart_changed && parsed_rating.has_value() &&
                                    metadata->chart_fields_editable && state.chart_buffers_valid &&
                                    action && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        const int can_unset_rating = metadata->has_chart_rating &&
                                     metadata->chart_fields_editable && state.chart_buffers_valid &&
                                     action && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(context, 0.5f);
        if (!can_apply_chart) {
            nk_widget_disable_begin(context);
        }
        const int apply_chart = nk_button_label(context, "Apply difficulty metadata");
        if (!can_apply_chart) {
            nk_widget_disable_end(context);
        }
        if (apply_chart && can_apply_chart) {
            set_metadata_chart_action(action, metadata, 0);
        }
        nk_layout_row_push(context, 0.5f);
        if (!can_unset_rating) {
            nk_widget_disable_begin(context);
        }
        const int unset_rating = nk_button_label(context, "Unset rating");
        if (!can_unset_rating) {
            nk_widget_disable_end(context);
        }
        if (unset_rating && can_unset_rating) {
            set_metadata_chart_action(action, metadata, 1);
        }
        nk_layout_row_end(context);
    }

    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Format-specific fields (%zu; duplicates stay ordered)",
              metadata->format_field_total_count);
    if (metadata->format_field_count != 0 && !metadata->format_fields) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Format field list is unavailable.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < metadata->format_field_count; ++index) {
        const WormUiMetadataFormatFieldView* field = &metadata->format_fields[index];
        char edit_label[32];
        (void)snprintf(edit_label, sizeof(edit_label), "Edit %zu", index + 1);
        nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(context, 0.78f);
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s · %s #%zu%s",
                  field->chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(field->family), field->name ? field->name : "(empty)",
                  field->field_index + 1, field->has_source_order ? " (source)" : "");
        nk_layout_row_push(context, 0.22f);
        const int field_editable = field->first_group_for_family && field->family <= 6 &&
                                   (!field->chart_scoped || metadata->has_chart);
        if (!field_editable) {
            nk_widget_disable_begin(context);
        }
        if (nk_button_label(context, edit_label) && field_editable) {
            state.selected_format_field = index;
            state.format_target_valid = 1;
            state.format_target_chart_scoped = field->chart_scoped;
            state.format_target_family = field->family;
            state.format_target_index = field->field_index;
            state.format_target_field_count = field->group_field_count;
            initialize_format_buffers(field, 0);
        }
        if (!field_editable) {
            nk_widget_disable_end(context);
        }
        nk_layout_row_end(context);
    }
    if (metadata->format_fields_truncated) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Additional format fields are hidden by the frontend row cap.",
                 NK_TEXT_LEFT);
    }
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Add a field to a format group", NK_TEXT_LEFT);
    if (metadata->format_target_count != 0 && !metadata->format_targets) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Format group targets are unavailable.", NK_TEXT_LEFT);
    } else {
        for (size_t index = 0; index < metadata->format_target_count; ++index) {
            const WormUiMetadataFormatTargetView* target = &metadata->format_targets[index];
            char add_label[32];
            (void)snprintf(add_label, sizeof(add_label), "Add field %zu", index + 1);
            nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
            nk_layout_row_push(context, 0.76f);
            nk_labelf(context, NK_TEXT_LEFT, "%s · %s · %s (%zu fields)",
                      target->chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(target->family),
                      target->group_exists ? "existing group" : "new group", target->field_count);
            nk_layout_row_push(context, 0.24f);
            const int target_editable =
                target->family <= 6 && (!target->chart_scoped || metadata->has_chart) &&
                target->field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
            if (!target_editable) {
                nk_widget_disable_begin(context);
            }
            if (nk_button_label(context, add_label) && target_editable) {
                initialize_format_target_buffers(metadata, target);
            }
            if (!target_editable) {
                nk_widget_disable_end(context);
            }
            nk_layout_row_end(context);
        }
    }
    if (state.format_target_valid) {
        const WormUiMetadataFormatFieldView* field =
            state.selected_format_field < metadata->format_field_count
                ? &metadata->format_fields[state.selected_format_field]
                : nullptr;
        nk_layout_row_dynamic(context, 24, 1);
        if (field && !state.format_insert_mode) {
            nk_labelf(context, NK_TEXT_LEFT, "%s · %s field #%zu",
                      field->chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(field->family), field->field_index + 1);
        } else {
            nk_labelf(context, NK_TEXT_LEFT, "New %s · %s field",
                      state.format_target_chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(state.format_target_family));
        }
        if (!state.format_buffers_valid) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context, "Field is read-only because it exceeds the text cap or group scope.",
                     NK_TEXT_LEFT);
            if (field && field->first_group_for_family) {
                nk_layout_row_dynamic(context, 26, 1);
                if (nk_button_label(context, "Delete field to repair metadata") &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
                }
            }
        } else {
            metadata_input_row(context, "Name", state.format_name.data(), state.format_name.size(),
                               1);
            metadata_input_row(context, "Value", state.format_value.data(),
                               state.format_value.size(), 1);
            if (state.format_insert_mode) {
                nk_layout_row_dynamic(context, 26, 1);
                const int insert_enabled =
                    state.format_target_field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
                if (!insert_enabled) {
                    nk_widget_disable_begin(context);
                }
                if (nk_button_label(context, "Insert field at end of this format group") &&
                    insert_enabled && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD);
                }
                if (!insert_enabled) {
                    nk_widget_disable_end(context);
                }
            } else {
                nk_layout_row_begin(context, NK_DYNAMIC, 26, 3);
                nk_layout_row_push(context, 0.34f);
                if (nk_button_label(context, "Replace field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD);
                }
                nk_layout_row_push(context, 0.33f);
                if (nk_button_label(context, "Delete field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
                }
                nk_layout_row_push(context, 0.33f);
                if (nk_button_label(context, "Prepare new field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    state.format_target_index = field->group_field_count;
                    initialize_format_buffers(field, 1);
                }
                nk_layout_row_end(context);
            }
        }
    }
}
