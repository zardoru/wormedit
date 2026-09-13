#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION

#include "ui/frontend.h"

#include "wormedit/nuklear_config.h"

#include "nuklear_sdl_gl3_sdl3.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <string_view>
#include <vector>

#define WORMEDIT_MAX_VERTEX_MEMORY (512 * 1024)
#define WORMEDIT_MAX_ELEMENT_MEMORY (128 * 1024)

static SDL_Window* g_window;
static struct nk_context* g_context;
static char g_recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
static constexpr size_t metadata_visible_tag_count = 64;
static uint64_t g_metadata_buffer_revision = std::numeric_limits<uint64_t>::max();
static uint64_t g_metadata_buffer_chart_id{};
static bool g_metadata_buffer_has_chart{};
static bool g_metadata_song_buffers_valid{};
static bool g_metadata_chart_buffers_valid{};
static bool g_metadata_tag_buffers_valid{};
static std::array<char, WORM_UI_METADATA_TITLE_BYTES + 1> g_metadata_title{};
static std::array<char, WORM_UI_METADATA_TITLE_BYTES + 1> g_metadata_artist{};
static std::array<char, WORM_UI_METADATA_CHART_TEXT_BYTES + 1> g_metadata_chart_name{};
static std::array<char, WORM_UI_METADATA_CHART_TEXT_BYTES + 1> g_metadata_chart_author{};
static std::array<char, 64> g_metadata_chart_rating_numerator{};
static std::array<char, 64> g_metadata_chart_rating_denominator{};
static std::vector<std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1>> g_metadata_search_tags;
static std::vector<const char*> g_metadata_search_tag_pointers;
static std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> g_metadata_new_search_tag{};
static size_t g_metadata_selected_format_field = std::numeric_limits<size_t>::max();
static bool g_metadata_format_target_valid{};
static bool g_metadata_format_target_chart_scoped{};
static uint8_t g_metadata_format_target_family{};
static size_t g_metadata_format_target_index{};
static size_t g_metadata_format_target_field_count{};
static bool g_metadata_format_insert_mode{};
static bool g_metadata_format_buffers_valid{};
static std::vector<char> g_metadata_format_name;
static std::vector<char> g_metadata_format_value;

static int recovery_action_offered(const WormUiRecoveryCandidateView* candidate, size_t action) {
    return candidate && (candidate->action_mask & action) != 0;
}

static void set_recovery_action(WormUiWorkspaceAction* action,
                                const WormUiRecoveryCandidateView* candidate,
                                WormUiWorkspaceActionKind kind) {
    if (!action || !candidate) {
        return;
    }
    action->kind = kind;
    action->recovery_scan_generation = candidate->scan_generation;
    action->recovery_row_index = candidate->row_index;
    action->recovery_candidate_index = candidate->candidate_index;
    if (kind == WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS) {
        memcpy(action->recovery_save_as_destination, g_recovery_save_as_destination,
               sizeof(action->recovery_save_as_destination));
    }
}

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
    g_metadata_buffer_revision = metadata->document_revision;
    g_metadata_buffer_has_chart = metadata->has_chart;
    g_metadata_buffer_chart_id = metadata->has_chart ? metadata->chart_id : 0;
    g_metadata_song_buffers_valid =
        metadata->song_fields_editable &&
        copy_metadata_text(g_metadata_title.data(), g_metadata_title.size(), metadata->title,
                           metadata->title_bytes) &&
        copy_metadata_text(g_metadata_artist.data(), g_metadata_artist.size(), metadata->artist,
                           metadata->artist_bytes);
    g_metadata_chart_buffers_valid =
        metadata->has_chart && metadata->chart_fields_editable &&
        copy_metadata_text(g_metadata_chart_name.data(), g_metadata_chart_name.size(),
                           metadata->chart_name, metadata->chart_name_bytes) &&
        copy_metadata_text(g_metadata_chart_author.data(), g_metadata_chart_author.size(),
                           metadata->chart_author, metadata->chart_author_bytes);
    g_metadata_chart_rating_numerator.fill('\0');
    g_metadata_chart_rating_denominator.fill('\0');
    if (metadata->has_chart) {
        const auto numerator = metadata->has_chart_rating
                                   ? std::to_string(metadata->chart_rating_numerator)
                                   : std::string{};
        const auto denominator = metadata->has_chart_rating
                                     ? std::to_string(metadata->chart_rating_denominator)
                                     : std::string{"1"};
        (void)copy_metadata_text(g_metadata_chart_rating_numerator.data(),
                                 g_metadata_chart_rating_numerator.size(), numerator.c_str(),
                                 numerator.size());
        (void)copy_metadata_text(g_metadata_chart_rating_denominator.data(),
                                 g_metadata_chart_rating_denominator.size(), denominator.c_str(),
                                 denominator.size());
    }
    g_metadata_tag_buffers_valid =
        metadata->search_tags_editable &&
        metadata->search_tag_count <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
        (metadata->search_tag_count == 0 || (metadata->search_tags && metadata->search_tag_bytes));
    g_metadata_search_tags.clear();
    if (g_metadata_tag_buffers_valid) {
        g_metadata_search_tags.reserve(metadata->search_tag_count);
        for (size_t index = 0; index < metadata->search_tag_count; ++index) {
            std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> tag{};
            if (!copy_metadata_text(tag.data(), tag.size(), metadata->search_tags[index],
                                    metadata->search_tag_bytes[index])) {
                g_metadata_tag_buffers_valid = 0;
                g_metadata_search_tags.clear();
                break;
            }
            g_metadata_search_tags.push_back(tag);
        }
    }
    g_metadata_search_tag_pointers.clear();
    g_metadata_new_search_tag.fill('\0');
    g_metadata_selected_format_field = std::numeric_limits<size_t>::max();
    g_metadata_format_target_valid = 0;
    g_metadata_format_target_chart_scoped = 0;
    g_metadata_format_target_family = 0;
    g_metadata_format_target_index = 0;
    g_metadata_format_target_field_count = 0;
    g_metadata_format_insert_mode = 0;
    g_metadata_format_buffers_valid = 0;
    g_metadata_format_name.clear();
    g_metadata_format_value.clear();
}

static int metadata_buffer_key_changed(const WormUiMetadataView* metadata) {
    const uint64_t chart_id = metadata && metadata->has_chart ? metadata->chart_id : 0;
    const int has_chart = metadata && metadata->has_chart;
    return metadata &&
           (g_metadata_buffer_revision != metadata->document_revision ||
            g_metadata_buffer_chart_id != chart_id || g_metadata_buffer_has_chart != has_chart);
}

static int metadata_song_changed(const WormUiMetadataView* metadata) {
    if (!metadata || !g_metadata_song_buffers_valid || !g_metadata_tag_buffers_valid ||
        !metadata->title || !metadata->artist) {
        return 0;
    }
    if (metadata->search_tag_count != g_metadata_search_tags.size()) {
        return 1;
    }
    if (strcmp(g_metadata_title.data(), metadata->title) != 0 ||
        strcmp(g_metadata_artist.data(), metadata->artist) != 0) {
        return 1;
    }
    for (size_t index = 0; index < g_metadata_search_tags.size(); ++index) {
        if (!metadata->search_tags ||
            strcmp(g_metadata_search_tags[index].data(), metadata->search_tags[index]) != 0) {
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

static auto metadata_chart_rating_value(const WormUiMetadataView* metadata)
    -> std::optional<metadata_rating_value_t> {
    if (!metadata)
        return std::nullopt;
    const std::string_view numerator{g_metadata_chart_rating_numerator.data()};
    const std::string_view denominator{g_metadata_chart_rating_denominator.data()};
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
    if (!metadata || !metadata->has_chart || !g_metadata_chart_buffers_valid ||
        !metadata->chart_name || !metadata->chart_author) {
        return 0;
    }
    const bool text_changed = strcmp(g_metadata_chart_name.data(), metadata->chart_name) != 0 ||
                              strcmp(g_metadata_chart_author.data(), metadata->chart_author) != 0;
    const auto rating = metadata_chart_rating_value(metadata);
    const bool rating_changed =
        rating &&
        (rating->has_rating != metadata->has_chart_rating ||
         (rating->has_rating && (rating->numerator != metadata->chart_rating_numerator ||
                                 rating->denominator != metadata->chart_rating_denominator)));
    return text_changed || rating_changed;
}

static void metadata_input_row(const char* label, char* buffer, size_t capacity, int enabled) {
    nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(g_context, 0.20f);
    nk_label(g_context, label, NK_TEXT_LEFT);
    nk_layout_row_push(g_context, 0.80f);
    if (!enabled) {
        nk_widget_disable_begin(g_context);
    }
    (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD, buffer, capacity,
                                         nk_filter_default);
    if (!enabled) {
        nk_widget_disable_end(g_context);
    }
    nk_layout_row_end(g_context);
}

static void set_metadata_song_action(WormUiWorkspaceAction* action,
                                     const WormUiMetadataView* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !g_metadata_tag_buffers_valid ||
        g_metadata_search_tags.size() > WORM_UI_METADATA_SEARCH_TAG_CAPACITY) {
        return;
    }
    g_metadata_search_tag_pointers.clear();
    g_metadata_search_tag_pointers.reserve(g_metadata_search_tags.size());
    for (const auto& tag : g_metadata_search_tags) {
        g_metadata_search_tag_pointers.push_back(tag.data());
    }
    action->metadata_edit = WORM_UI_METADATA_EDIT_SONG;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_title = g_metadata_title.data();
    action->metadata_artist = g_metadata_artist.data();
    action->metadata_search_tags = g_metadata_search_tag_pointers.data();
    action->metadata_search_tag_count = g_metadata_search_tag_pointers.size();
}

static void set_metadata_chart_action(WormUiWorkspaceAction* action,
                                      const WormUiMetadataView* metadata, int unset_rating) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !metadata->has_chart || !g_metadata_chart_buffers_valid) {
        return;
    }
    const auto rating = unset_rating ? std::optional<metadata_rating_value_t>{}
                                     : metadata_chart_rating_value(metadata);
    if (!unset_rating && !rating)
        return;
    action->metadata_edit = WORM_UI_METADATA_EDIT_CHART;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = metadata->chart_id;
    action->metadata_chart_name = g_metadata_chart_name.data();
    action->metadata_chart_author = g_metadata_chart_author.data();
    action->metadata_has_chart_rating = !unset_rating && rating->has_rating;
    if (action->metadata_has_chart_rating) {
        action->metadata_chart_rating_numerator_text = g_metadata_chart_rating_numerator.data();
        action->metadata_chart_rating_denominator_text = g_metadata_chart_rating_denominator.data();
    }
}

static const char* metadata_format_family_name(uint8_t family) {
    static const char* names[] = {"Native", "BMS",  "StepMania", "osu!mania",
                                  "BMSON",  "MIDI", "Quaver"};
    return family < sizeof(names) / sizeof(names[0]) ? names[family] : "Unknown";
}

static int initialize_format_buffers(const WormUiMetadataFormatFieldView* field, int insert_mode) {
    g_metadata_format_name.assign(WORM_UI_METADATA_FORMAT_NAME_BYTES + 1, '\0');
    g_metadata_format_value.assign(WORM_UI_METADATA_FORMAT_VALUE_BYTES + 1, '\0');
    g_metadata_format_insert_mode = insert_mode != 0;
    if (insert_mode) {
        g_metadata_format_buffers_valid = field && field->first_group_for_family;
        return g_metadata_format_buffers_valid;
    }
    if (!field || !field->first_group_for_family || !field->name || !field->value ||
        field->group_field_count > WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY) {
        g_metadata_format_buffers_valid = 0;
        return 0;
    }
    g_metadata_format_buffers_valid =
        copy_metadata_text(g_metadata_format_name.data(), g_metadata_format_name.size(),
                           field->name, field->name_bytes) &&
        copy_metadata_text(g_metadata_format_value.data(), g_metadata_format_value.size(),
                           field->value, field->value_bytes);
    return g_metadata_format_buffers_valid;
}

static void initialize_format_target_buffers(const WormUiMetadataView* metadata,
                                             const WormUiMetadataFormatTargetView* target) {
    if (!metadata || !target || target->family > 6 ||
        (target->chart_scoped && !metadata->has_chart) ||
        target->field_count > WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY) {
        g_metadata_format_target_valid = 0;
        g_metadata_format_buffers_valid = 0;
        return;
    }
    g_metadata_selected_format_field = std::numeric_limits<size_t>::max();
    g_metadata_format_target_valid = 1;
    g_metadata_format_target_chart_scoped = target->chart_scoped;
    g_metadata_format_target_family = target->family;
    g_metadata_format_target_field_count = target->field_count;
    g_metadata_format_target_index = target->field_count;
    g_metadata_format_insert_mode = 1;
    g_metadata_format_name.assign(WORM_UI_METADATA_FORMAT_NAME_BYTES + 1, '\0');
    g_metadata_format_value.assign(WORM_UI_METADATA_FORMAT_VALUE_BYTES + 1, '\0');
    g_metadata_format_buffers_valid = 1;
}

static void set_metadata_format_action(WormUiWorkspaceAction* action,
                                       const WormUiMetadataView* metadata,
                                       WormUiMetadataEditKind kind) {
    const int needs_text = kind != WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD;
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !g_metadata_format_target_valid || (needs_text && !g_metadata_format_buffers_valid) ||
        g_metadata_format_target_family > 6 ||
        (g_metadata_format_target_chart_scoped && !metadata->has_chart)) {
        return;
    }
    action->metadata_edit = kind;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = g_metadata_format_target_chart_scoped ? metadata->chart_id : 0;
    action->metadata_format_chart_scoped = g_metadata_format_target_chart_scoped;
    action->metadata_format_family = g_metadata_format_target_family;
    action->metadata_format_index = g_metadata_format_target_index;
    action->metadata_format_name = needs_text ? g_metadata_format_name.data() : nullptr;
    action->metadata_format_value = needs_text ? g_metadata_format_value.data() : nullptr;
}

static void render_metadata_editor(const WormUiMetadataView* metadata,
                                   WormUiWorkspaceAction* action) {
    if (!metadata || !metadata->visible) {
        return;
    }
    if (metadata_buffer_key_changed(metadata)) {
        initialize_metadata_buffers(metadata);
    }

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context, "Song metadata", NK_TEXT_LEFT);
    metadata_input_row("Title", g_metadata_title.data(), g_metadata_title.size(),
                       metadata->song_fields_editable && g_metadata_song_buffers_valid);
    metadata_input_row("Artist", g_metadata_artist.data(), g_metadata_artist.size(),
                       metadata->song_fields_editable && g_metadata_song_buffers_valid);

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_labelf(g_context, NK_TEXT_LEFT, "Ordered search tags (%zu; duplicates are preserved)",
              metadata->search_tag_count);
    const int tags_editable = metadata->song_fields_editable && g_metadata_song_buffers_valid &&
                              g_metadata_tag_buffers_valid;
    if (!g_metadata_tag_buffers_valid || !metadata->song_fields_editable) {
        nk_layout_row_dynamic(g_context, 22, 1);
        nk_label(g_context, "Tags are read-only because the song metadata exceeds an edit cap.",
                 NK_TEXT_LEFT);
    } else {
        const size_t visible_tags =
            std::min(g_metadata_search_tags.size(), metadata_visible_tag_count);
        for (size_t index = 0; index < visible_tags; ++index) {
            char remove_label[32];
            (void)snprintf(remove_label, sizeof(remove_label), "Remove %zu", index + 1);
            nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 3);
            nk_layout_row_push(g_context, 0.10f);
            nk_labelf(g_context, NK_TEXT_LEFT, "%zu", index + 1);
            nk_layout_row_push(g_context, 0.75f);
            if (!tags_editable) {
                nk_widget_disable_begin(g_context);
            }
            (void)nk_edit_string_zero_terminated(
                g_context, NK_EDIT_FIELD, g_metadata_search_tags[index].data(),
                g_metadata_search_tags[index].size(), nk_filter_default);
            if (!tags_editable) {
                nk_widget_disable_end(g_context);
            }
            nk_layout_row_push(g_context, 0.15f);
            if (!tags_editable) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, remove_label)) {
                g_metadata_search_tags.erase(g_metadata_search_tags.begin() +
                                             static_cast<std::ptrdiff_t>(index));
                if (!tags_editable) {
                    nk_widget_disable_end(g_context);
                }
                nk_layout_row_end(g_context);
                break;
            }
            if (!tags_editable) {
                nk_widget_disable_end(g_context);
            }
            nk_layout_row_end(g_context);
        }
        if (g_metadata_search_tags.size() > metadata_visible_tag_count) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_labelf(g_context, NK_TEXT_LEFT, "Showing first %zu tags; the rest stay unchanged.",
                      metadata_visible_tag_count);
        }
        nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(g_context, 0.82f);
        if (!tags_editable) {
            nk_widget_disable_begin(g_context);
        }
        (void)nk_edit_string_zero_terminated(g_context, NK_EDIT_FIELD,
                                             g_metadata_new_search_tag.data(),
                                             g_metadata_new_search_tag.size(), nk_filter_default);
        if (!tags_editable) {
            nk_widget_disable_end(g_context);
        }
        nk_layout_row_push(g_context, 0.18f);
        const int can_add_tag =
            tags_editable && g_metadata_search_tags.size() < WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
            g_metadata_new_search_tag[0] != '\0';
        if (!can_add_tag) {
            nk_widget_disable_begin(g_context);
        }
        const int add_tag = nk_button_label(g_context, "Add tag");
        if (!can_add_tag) {
            nk_widget_disable_end(g_context);
        }
        if (add_tag && can_add_tag) {
            g_metadata_search_tags.push_back(g_metadata_new_search_tag);
            g_metadata_new_search_tag.fill('\0');
        }
        nk_layout_row_end(g_context);
    }

    const int song_changed = metadata_song_changed(metadata);
    nk_layout_row_dynamic(g_context, 26, 1);
    if (!song_changed || !metadata->song_fields_editable) {
        nk_widget_disable_begin(g_context);
    }
    if (nk_button_label(g_context, "Apply song metadata") && song_changed &&
        metadata->song_fields_editable) {
        set_metadata_song_action(action, metadata);
    }
    if (!song_changed || !metadata->song_fields_editable) {
        nk_widget_disable_end(g_context);
    }

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context,
             metadata->has_chart ? "Selected difficulty metadata"
                                 : "Select a difficulty to edit chart metadata",
             NK_TEXT_LEFT);
    if (metadata->has_chart) {
        metadata_input_row("Difficulty", g_metadata_chart_name.data(), g_metadata_chart_name.size(),
                           metadata->chart_fields_editable && g_metadata_chart_buffers_valid);
        metadata_input_row("Mapper", g_metadata_chart_author.data(), g_metadata_chart_author.size(),
                           metadata->chart_fields_editable && g_metadata_chart_buffers_valid);
        nk_layout_row_dynamic(g_context, 24, 1);
        nk_label(g_context, "Exact signed rational rating (stored without reduction)",
                 NK_TEXT_LEFT);
        const int rating_fields_editable =
            metadata->chart_fields_editable && g_metadata_chart_buffers_valid;
        metadata_input_row("Numerator", g_metadata_chart_rating_numerator.data(),
                           g_metadata_chart_rating_numerator.size(), rating_fields_editable);
        metadata_input_row("Denominator", g_metadata_chart_rating_denominator.data(),
                           g_metadata_chart_rating_denominator.size(), rating_fields_editable);
        const auto parsed_rating = metadata_chart_rating_value(metadata);
        const bool rating_input_started =
            g_metadata_chart_rating_numerator[0] != '\0' ||
            strcmp(g_metadata_chart_rating_denominator.data(), "1") != 0;
        if (!parsed_rating && (metadata->has_chart_rating || rating_input_started)) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context,
                     "Use a signed 64-bit numerator and positive 64-bit denominator; values are "
                     "not truncated or reduced.",
                     NK_TEXT_LEFT);
        }
        const int chart_changed = metadata_chart_changed(metadata);
        const int can_apply_chart = chart_changed && parsed_rating.has_value() &&
                                    metadata->chart_fields_editable &&
                                    g_metadata_chart_buffers_valid && action &&
                                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        const int can_unset_rating = metadata->has_chart_rating &&
                                     metadata->chart_fields_editable &&
                                     g_metadata_chart_buffers_valid && action &&
                                     action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
        nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(g_context, 0.5f);
        if (!can_apply_chart) {
            nk_widget_disable_begin(g_context);
        }
        const int apply_chart = nk_button_label(g_context, "Apply difficulty metadata");
        if (!can_apply_chart) {
            nk_widget_disable_end(g_context);
        }
        if (apply_chart && can_apply_chart) {
            set_metadata_chart_action(action, metadata, 0);
        }
        nk_layout_row_push(g_context, 0.5f);
        if (!can_unset_rating) {
            nk_widget_disable_begin(g_context);
        }
        const int unset_rating = nk_button_label(g_context, "Unset rating");
        if (!can_unset_rating) {
            nk_widget_disable_end(g_context);
        }
        if (unset_rating && can_unset_rating) {
            set_metadata_chart_action(action, metadata, 1);
        }
        nk_layout_row_end(g_context);
    }

    nk_layout_row_dynamic(g_context, 24, 1);
    nk_labelf(g_context, NK_TEXT_LEFT, "Format-specific fields (%zu; duplicates stay ordered)",
              metadata->format_field_total_count);
    if (metadata->format_field_count != 0 && !metadata->format_fields) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Format field list is unavailable.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < metadata->format_field_count; ++index) {
        const WormUiMetadataFormatFieldView* field = &metadata->format_fields[index];
        char edit_label[32];
        (void)snprintf(edit_label, sizeof(edit_label), "Edit %zu", index + 1);
        nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(g_context, 0.78f);
        nk_labelf(g_context, NK_TEXT_LEFT, "%s · %s · %s #%zu%s",
                  field->chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(field->family), field->name ? field->name : "(empty)",
                  field->field_index + 1, field->has_source_order ? " (source)" : "");
        nk_layout_row_push(g_context, 0.22f);
        const int field_editable = field->first_group_for_family && field->family <= 6 &&
                                   (!field->chart_scoped || metadata->has_chart);
        if (!field_editable) {
            nk_widget_disable_begin(g_context);
        }
        if (nk_button_label(g_context, edit_label) && field_editable) {
            g_metadata_selected_format_field = index;
            g_metadata_format_target_valid = 1;
            g_metadata_format_target_chart_scoped = field->chart_scoped;
            g_metadata_format_target_family = field->family;
            g_metadata_format_target_index = field->field_index;
            g_metadata_format_target_field_count = field->group_field_count;
            initialize_format_buffers(field, 0);
        }
        if (!field_editable) {
            nk_widget_disable_end(g_context);
        }
        nk_layout_row_end(g_context);
    }
    if (metadata->format_fields_truncated) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Additional format fields are hidden by the frontend row cap.",
                 NK_TEXT_LEFT);
    }
    nk_layout_row_dynamic(g_context, 24, 1);
    nk_label(g_context, "Add a field to a format group", NK_TEXT_LEFT);
    if (metadata->format_target_count != 0 && !metadata->format_targets) {
        nk_layout_row_dynamic(g_context, 20, 1);
        nk_label(g_context, "Format group targets are unavailable.", NK_TEXT_LEFT);
    } else {
        for (size_t index = 0; index < metadata->format_target_count; ++index) {
            const WormUiMetadataFormatTargetView* target = &metadata->format_targets[index];
            char add_label[32];
            (void)snprintf(add_label, sizeof(add_label), "Add field %zu", index + 1);
            nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
            nk_layout_row_push(g_context, 0.76f);
            nk_labelf(g_context, NK_TEXT_LEFT, "%s · %s · %s (%zu fields)",
                      target->chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(target->family),
                      target->group_exists ? "existing group" : "new group", target->field_count);
            nk_layout_row_push(g_context, 0.24f);
            const int target_editable =
                target->family <= 6 && (!target->chart_scoped || metadata->has_chart) &&
                target->field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
            if (!target_editable) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, add_label) && target_editable) {
                initialize_format_target_buffers(metadata, target);
            }
            if (!target_editable) {
                nk_widget_disable_end(g_context);
            }
            nk_layout_row_end(g_context);
        }
    }
    if (g_metadata_format_target_valid) {
        const WormUiMetadataFormatFieldView* field =
            g_metadata_selected_format_field < metadata->format_field_count
                ? &metadata->format_fields[g_metadata_selected_format_field]
                : nullptr;
        nk_layout_row_dynamic(g_context, 24, 1);
        if (field && !g_metadata_format_insert_mode) {
            nk_labelf(g_context, NK_TEXT_LEFT, "%s · %s field #%zu",
                      field->chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(field->family), field->field_index + 1);
        } else {
            nk_labelf(g_context, NK_TEXT_LEFT, "New %s · %s field",
                      g_metadata_format_target_chart_scoped ? "Chart" : "Song",
                      metadata_format_family_name(g_metadata_format_target_family));
        }
        if (!g_metadata_format_buffers_valid) {
            nk_layout_row_dynamic(g_context, 20, 1);
            nk_label(g_context,
                     "Field is read-only because it exceeds the text cap or group scope.",
                     NK_TEXT_LEFT);
            if (field && field->first_group_for_family) {
                nk_layout_row_dynamic(g_context, 26, 1);
                if (nk_button_label(g_context, "Delete field to repair metadata") &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
                }
            }
        } else {
            metadata_input_row("Name", g_metadata_format_name.data(), g_metadata_format_name.size(),
                               1);
            metadata_input_row("Value", g_metadata_format_value.data(),
                               g_metadata_format_value.size(), 1);
            if (g_metadata_format_insert_mode) {
                nk_layout_row_dynamic(g_context, 26, 1);
                const int insert_enabled = g_metadata_format_target_field_count <
                                           WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
                if (!insert_enabled) {
                    nk_widget_disable_begin(g_context);
                }
                if (nk_button_label(g_context, "Insert field at end of this format group") &&
                    insert_enabled && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD);
                }
                if (!insert_enabled) {
                    nk_widget_disable_end(g_context);
                }
            } else {
                nk_layout_row_begin(g_context, NK_DYNAMIC, 26, 3);
                nk_layout_row_push(g_context, 0.34f);
                if (nk_button_label(g_context, "Replace field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD);
                }
                nk_layout_row_push(g_context, 0.33f);
                if (nk_button_label(g_context, "Delete field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    set_metadata_format_action(action, metadata,
                                               WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
                }
                nk_layout_row_push(g_context, 0.33f);
                if (nk_button_label(g_context, "Prepare new field") && field &&
                    action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
                    g_metadata_format_target_index = field->group_field_count;
                    initialize_format_buffers(field, 1);
                }
                nk_layout_row_end(g_context);
            }
        }
    }
}

bool worm_ui_frontend_init(SDL_Window* window) {
    if (!window || g_context) {
        return false;
    }

    struct nk_context* context = nk_sdl_init(window);
    if (!context) {
        return false;
    }
    struct nk_font_atlas* atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);
    (void)atlas;
    nk_sdl_font_stash_end();

    if (!SDL_StartTextInput(window)) {
        nk_sdl_shutdown();
        return false;
    }
    g_window = window;
    g_context = context;
    return true;
}

void worm_ui_frontend_shutdown(void) {
    if (!g_context) {
        return;
    }
    if (g_window) {
        SDL_StopTextInput(g_window);
    }
    nk_sdl_shutdown();
    g_window = NULL;
    g_context = NULL;
}

void worm_ui_frontend_begin_input(void) {
    if (g_context) {
        nk_input_begin(g_context);
    }
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    SDL_Event mutable_event = *event;
    return nk_sdl_handle_event(&mutable_event) != 0;
}

void worm_ui_frontend_end_input(void) {
    if (g_context) {
        nk_input_end(g_context);
        nk_sdl_handle_grab();
    }
}

static struct nk_rect compose_screen_rect(const WormUiComposeRect* source,
                                          const WormUiComposeView* view, struct nk_rect viewport) {
    const auto x_scale = viewport.w / static_cast<float>(view->viewport_width_pixels);
    const auto y_scale = viewport.h / static_cast<float>(view->viewport_height_pixels);
    const auto left = viewport.x + static_cast<float>(source->left) * x_scale;
    const auto top = viewport.y + static_cast<float>(source->top) * y_scale;
    const auto right = viewport.x + static_cast<float>(source->right) * x_scale;
    const auto bottom = viewport.y + static_cast<float>(source->bottom) * y_scale;
    const auto clipped_left = std::max(viewport.x, left);
    const auto clipped_top = std::max(viewport.y, top);
    const auto clipped_right = std::min(viewport.x + viewport.w, right);
    const auto clipped_bottom = std::min(viewport.y + viewport.h, bottom);
    return nk_rect(clipped_left, clipped_top, std::max(0.0f, clipped_right - clipped_left),
                   std::max(0.0f, clipped_bottom - clipped_top));
}

static void compose_fill_rect(struct nk_command_buffer* canvas, const WormUiComposeRect* source,
                              const WormUiComposeView* view, struct nk_rect viewport,
                              struct nk_color color) {
    const auto rect = compose_screen_rect(source, view, viewport);
    if (rect.w > 0.0f && rect.h > 0.0f) {
        nk_fill_rect(canvas, rect, 0.0f, color);
    }
}

static struct nk_color compose_lane_color(uint8_t role, size_t display_order) {
    const auto alternate = (display_order & 1U) != 0;
    switch (role) {
    case 1: // scratch
        return alternate ? nk_rgb(48, 44, 68) : nk_rgb(42, 39, 61);
    case 5: // free zone
        return alternate ? nk_rgb(47, 54, 63) : nk_rgb(42, 49, 58);
    case 6: // foot pedal
        return alternate ? nk_rgb(40, 58, 59) : nk_rgb(36, 52, 54);
    default:
        return alternate ? nk_rgb(37, 45, 59) : nk_rgb(33, 40, 53);
    }
}

static int compose_rect_valid(const WormUiComposeRect* rect) {
    if (!rect || !std::isfinite(rect->left) || !std::isfinite(rect->top) ||
        !std::isfinite(rect->right) || !std::isfinite(rect->bottom) || rect->right < rect->left ||
        rect->bottom < rect->top) {
        return 0;
    }
    constexpr double coordinate_limit = 10000000.0;
    return std::abs(rect->left) <= coordinate_limit && std::abs(rect->top) <= coordinate_limit &&
           std::abs(rect->right) <= coordinate_limit && std::abs(rect->bottom) <= coordinate_limit;
}

static int compose_geometry_valid(const WormUiComposeView* view) {
    if (!view || !std::isfinite(view->pixels_per_standard_measure) ||
        view->pixels_per_standard_measure < WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE ||
        view->pixels_per_standard_measure > WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE ||
        !std::isfinite(view->viewport_width_pixels) || view->viewport_width_pixels <= 0.0 ||
        view->viewport_width_pixels > 10000000.0 || !std::isfinite(view->viewport_height_pixels) ||
        view->viewport_height_pixels <= 0.0 || view->viewport_height_pixels > 10000000.0)
        return 0;
    for (size_t index = 0; index < view->band_count; ++index) {
        if (!compose_rect_valid(&view->bands[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->lane_count; ++index) {
        if (!compose_rect_valid(&view->lanes[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        if (!std::isfinite(view->grid_lines[index].y_pixels) ||
            std::abs(view->grid_lines[index].y_pixels) > 10000000.0)
            return 0;
    }
    for (size_t index = 0; index < view->difference_count; ++index) {
        if (!compose_rect_valid(&view->differences[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        if (!compose_rect_valid(&view->hold_bodies[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        if (!compose_rect_valid(&view->note_endpoints[index].bounds))
            return 0;
    }
    return !view->cursor || compose_rect_valid(&view->cursor->bounds);
}

static void render_compose_field(const WormUiComposeView* view, WormUiWorkspaceAction* action) {
    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 7);
    nk_layout_row_push(g_context, 0.07f);
    if (nk_button_label(g_context, "Zoom -") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_OUT;
    nk_layout_row_push(g_context, 0.07f);
    if (nk_button_label(g_context, "Zoom +") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_IN;
    nk_layout_row_push(g_context, 0.10f);
    if (nk_button_label(g_context, "Page up") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_SCROLL_UP;
    nk_layout_row_push(g_context, 0.10f);
    if (nk_button_label(g_context, "Page down") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_SCROLL_DOWN;
    nk_layout_row_push(g_context, 0.08f);
    if (nk_button_label(g_context, "Reset") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW;
    nk_layout_row_push(g_context, 0.12f);
    nk_labelf(g_context, NK_TEXT_LEFT, "Zoom %.0f px/measure", view->pixels_per_standard_measure);
    nk_layout_row_push(g_context, 0.46f);
    if (view->truncated) {
        nk_label(g_context, "Visible geometry capped", NK_TEXT_RIGHT);
    } else {
        nk_labelf(g_context, NK_TEXT_RIGHT, "%zu note endpoints · %zu lanes · %zu grid lines",
                  view->note_endpoint_count, view->lane_count, view->grid_line_count);
    }
    nk_layout_row_end(g_context);

    const auto group_bounds = nk_window_get_content_region(g_context);
    const auto surface_height = std::max(32.0f, group_bounds.h - 29.0f);
    nk_layout_space_begin(g_context, NK_DYNAMIC, surface_height, 1);
    nk_layout_space_push(g_context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, g_context);
    nk_layout_space_end(g_context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f)
        return;

    auto* canvas = nk_window_get_canvas(g_context);
    if (!canvas)
        return;
    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    nk_fill_rect(canvas, viewport, 0.0f, nk_rgb(22, 25, 33));
    if (!view->scene_available) {
        const auto* status = view->status_text ? view->status_text : "Chart field is unavailable.";
        nk_draw_text(canvas, viewport, status, static_cast<int>(strlen(status)),
                     g_context->style.font, nk_rgb(22, 25, 33), nk_rgb(190, 196, 207));
        nk_push_scissor(canvas, parent_clip);
        return;
    }

    for (size_t band_index = 0; band_index < view->band_count; ++band_index) {
        const auto& band = view->bands[band_index];
        const auto color = band.split_stage ? nk_rgb(27, 31, 43) : nk_rgb(25, 29, 39);
        compose_fill_rect(canvas, &band.bounds, view, viewport, color);
        const auto top = viewport.y + static_cast<float>(band.bounds.top) * viewport.h /
                                          static_cast<float>(view->viewport_height_pixels);
        const auto bottom = viewport.y + static_cast<float>(band.bounds.bottom) * viewport.h /
                                             static_cast<float>(view->viewport_height_pixels);
        nk_stroke_line(canvas, viewport.x, top, viewport.x + viewport.w, top, 1.0f,
                       nk_rgb(80, 91, 112));
        nk_stroke_line(canvas, viewport.x, bottom, viewport.x + viewport.w, bottom, 1.0f,
                       nk_rgb(61, 70, 87));
    }
    for (size_t lane_index = 0; lane_index < view->lane_count; ++lane_index) {
        const auto& lane = view->lanes[lane_index];
        const auto rect = compose_screen_rect(&lane.bounds, view, viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f)
            continue;
        nk_fill_rect(canvas, rect, 0.0f, compose_lane_color(lane.role, lane.display_order));
        nk_stroke_line(canvas, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f, nk_rgb(62, 70, 86));
        if (rect.w >= 28.0f && rect.h >= 18.0f) {
            char label[16]{};
            const auto label_length = snprintf(label, sizeof(label), "%u", lane.display_order + 1U);
            if (label_length > 0) {
                const auto label_bounds =
                    nk_rect(rect.x + 2.0f, rect.y + 2.0f, std::min(28.0f, rect.w - 4.0f), 15.0f);
                nk_draw_text(canvas, label_bounds, label, static_cast<int>(strlen(label)),
                             g_context->style.font, nk_rgb(37, 43, 55), nk_rgb(122, 133, 153));
            }
        }
    }
    for (size_t index = 0; index < view->difference_count; ++index) {
        compose_fill_rect(canvas, &view->differences[index].bounds, view, viewport,
                          nk_rgba(229, 190, 58, 74));
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        const auto& line = view->grid_lines[index];
        if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
            line.y_pixels > view->viewport_height_pixels)
            continue;
        const auto y = viewport.y + static_cast<float>(line.y_pixels) * viewport.h /
                                        static_cast<float>(view->viewport_height_pixels);
        struct nk_color color = nk_rgb(42, 49, 63);
        float thickness = 0.7f;
        if (line.kind == 3) {
            color = nk_rgb(95, 107, 128);
            thickness = 1.2f;
        } else if (line.kind == 1 || line.kind == 2) {
            color = nk_rgb(60, 70, 88);
            thickness = line.emphasis >= 2 ? 1.1f : 0.9f;
        } else if (line.emphasis >= 2) {
            color = nk_rgb(52, 61, 78);
        }
        nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, thickness, color);
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        const auto& hold = view->hold_bodies[index];
        compose_fill_rect(canvas, &hold.bounds, view, viewport,
                          hold.selected ? nk_rgb(62, 170, 204) : nk_rgb(48, 111, 151));
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        const auto& note = view->note_endpoints[index];
        auto color = note.selected ? nk_rgb(111, 221, 236) : nk_rgb(133, 182, 222);
        if (note.kind == 4) {
            color = nk_rgb(214, 91, 104);
        } else if (note.kind == 5) {
            color = nk_rgb(236, 194, 112);
        } else if (note.tail) {
            color = nk_rgb(113, 201, 216);
        }
        const auto rect = compose_screen_rect(&note.bounds, view, viewport);
        if (rect.w > 0.0f && rect.h > 0.0f) {
            nk_fill_rect(canvas, rect, 1.5f, color);
            nk_stroke_rect(canvas, rect, 1.5f, 1.0f, nk_rgb(17, 24, 34));
        }
    }
    if (view->cursor) {
        const auto rect = compose_screen_rect(&view->cursor->bounds, view, viewport);
        if (rect.w > 0.0f && rect.h > 0.0f) {
            nk_fill_rect(canvas, rect, 0.0f, nk_rgb(248, 226, 115));
            nk_stroke_line(canvas, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                           nk_rgb(255, 243, 174));
            nk_stroke_line(canvas, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0f,
                           nk_rgb(255, 243, 174));
        }
    }
    nk_push_scissor(canvas, parent_clip);
}

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_NONE;
    action.tab = WORM_UI_WORKSPACE_TAB_SETUP;
    action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
    if (!g_context || !g_window || !view || !view->project_title || !view->artist ||
        !view->selected_difficulty || !view->status_text || !view->git_hash || !view->tab_heading ||
        view->summary_line_count > WORM_UI_WORKSPACE_SUMMARY_CAPACITY ||
        view->recovery_candidate_count > WORM_UI_RECOVERY_ROW_CAPACITY ||
        view->recovery_diagnostic_count > WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY ||
        (view->metadata.visible &&
         (view->metadata.format_field_count > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY ||
          view->metadata.format_target_count > WORM_UI_METADATA_FORMAT_TARGET_CAPACITY)) ||
        (view->compose.visible &&
         (view->compose.band_count > WORM_UI_COMPOSE_BAND_CAPACITY ||
          view->compose.lane_count > WORM_UI_COMPOSE_LANE_CAPACITY ||
          view->compose.grid_line_count > WORM_UI_COMPOSE_GRID_CAPACITY ||
          view->compose.difference_count > WORM_UI_COMPOSE_DIFFERENCE_CAPACITY ||
          view->compose.hold_body_count > WORM_UI_COMPOSE_HOLD_CAPACITY ||
          view->compose.note_endpoint_count > WORM_UI_COMPOSE_NOTE_CAPACITY ||
          !std::isfinite(view->compose.viewport_width_pixels) ||
          view->compose.viewport_width_pixels <= 0.0 ||
          !std::isfinite(view->compose.viewport_height_pixels) ||
          view->compose.viewport_height_pixels <= 0.0 ||
          !std::isfinite(view->compose.pixels_per_standard_measure) ||
          view->compose.pixels_per_standard_measure < WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE ||
          view->compose.pixels_per_standard_measure > WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE ||
          (view->compose.band_count != 0 && !view->compose.bands) ||
          (view->compose.lane_count != 0 && !view->compose.lanes) ||
          (view->compose.grid_line_count != 0 && !view->compose.grid_lines) ||
          (view->compose.difference_count != 0 && !view->compose.differences) ||
          (view->compose.hold_body_count != 0 && !view->compose.hold_bodies) ||
          (view->compose.note_endpoint_count != 0 && !view->compose.note_endpoints) ||
          !compose_geometry_valid(&view->compose))) ||
        view->active_tab < WORM_UI_WORKSPACE_TAB_SETUP ||
        view->active_tab > WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE) {
        return action;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(g_window, &width, &height) || width <= 0 || height <= 0) {
        return action;
    }

    if (nk_begin(g_context, "wormedit-workspace", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 30, 4);
        nk_layout_row_push(g_context, 0.24f);
        nk_labelf(g_context, NK_TEXT_LEFT, "%s%s", view->project_title, view->dirty ? " *" : "");
        nk_layout_row_push(g_context, 0.18f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Artist: %s", view->artist);
        nk_layout_row_push(g_context, 0.22f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Difficulty: %s", view->selected_difficulty);
        nk_layout_row_push(g_context, 0.36f);
        nk_label(g_context, view->status_text, NK_TEXT_RIGHT);
        nk_layout_row_end(g_context);

        nk_layout_row_dynamic(g_context, 34, WORM_UI_WORKSPACE_TAB_COUNT);
        for (int tab = 0; tab < WORM_UI_WORKSPACE_TAB_COUNT; ++tab) {
            const WormUiWorkspaceTabItem* item = &view->tabs[tab];
            if (!item->label) {
                continue;
            }
            if (!item->enabled) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, item->label) && item->enabled &&
                tab != (int)view->active_tab) {
                action.kind = WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
                action.tab = (WormUiWorkspaceTab)tab;
            }
            if (!item->enabled) {
                nk_widget_disable_end(g_context);
            }
        }

        nk_layout_row_dynamic(g_context, 28, 1);
        nk_label(g_context, view->tab_heading, NK_TEXT_LEFT);

        float recovery_height = 0.0f;
        if (view->recovery_panel_visible) {
            recovery_height = 40.0f +
                              (view->recovery_notice && view->recovery_notice[0] ? 22.0f : 0.0f) +
                              (float)view->recovery_candidate_count * 48.0f +
                              (float)view->recovery_diagnostic_count * 18.0f;
            for (size_t row = 0; row < view->recovery_candidate_count; ++row) {
                const WormUiRecoveryCandidateView* candidate = &view->recovery_candidates[row];
                if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
                    recovery_height += 25.0f;
                    if (g_recovery_save_as_destination[0] == '\0') {
                        recovery_height += 18.0f;
                    }
                }
                if (candidate->disabled_reason && candidate->disabled_reason[0]) {
                    recovery_height += 20.0f;
                }
            }
            nk_layout_row_dynamic(g_context, 24, 1);
            nk_label(g_context, view->recovery_heading ? view->recovery_heading : "Recovery",
                     NK_TEXT_LEFT);
            if (view->recovery_notice && view->recovery_notice[0]) {
                nk_layout_row_dynamic(g_context, 22, 1);
                nk_label(g_context, view->recovery_notice, NK_TEXT_LEFT);
            }
            for (size_t row = 0; row < view->recovery_candidate_count; ++row) {
                const WormUiRecoveryCandidateView* candidate = &view->recovery_candidates[row];
                nk_layout_row_dynamic(g_context, 20, 1);
                nk_labelf(g_context, NK_TEXT_LEFT, "%s  %s",
                          candidate->path_label ? candidate->path_label : "(unknown recovery path)",
                          candidate->summary ? candidate->summary : "");

                nk_layout_row_dynamic(g_context, 25, 5);
                const struct {
                    size_t bit;
                    const char* label;
                    WormUiWorkspaceActionKind kind;
                } buttons[] = {
                    {WORM_UI_RECOVERY_ACTION_INSPECT, "Inspect",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT},
                    {WORM_UI_RECOVERY_ACTION_RECOVER, "Recover",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER},
                    {WORM_UI_RECOVERY_ACTION_SAVE_AS, "Save as",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS},
                    {WORM_UI_RECOVERY_ACTION_DISCARD, "Discard",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD},
                    {WORM_UI_RECOVERY_ACTION_QUARANTINE, "Quarantine",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE},
                };
                for (size_t button = 0; button < sizeof(buttons) / sizeof(buttons[0]); ++button) {
                    const int offered = recovery_action_offered(candidate, buttons[button].bit);
                    if (!offered) {
                        nk_widget_disable_begin(g_context);
                    }
                    const int clicked = nk_button_label(g_context, buttons[button].label);
                    if (!offered) {
                        nk_widget_disable_end(g_context);
                    }
                    int save_as_ready = 1;
                    if (buttons[button].kind == WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS) {
                        save_as_ready = g_recovery_save_as_destination[0] != '\0';
                    }
                    if (offered && save_as_ready && clicked &&
                        action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                        set_recovery_action(&action, candidate, buttons[button].kind);
                    }
                }
                if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
                    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
                    nk_layout_row_push(g_context, 0.18f);
                    nk_label(g_context, "New project path", NK_TEXT_LEFT);
                    nk_layout_row_push(g_context, 0.82f);
                    (void)nk_edit_string_zero_terminated(
                        g_context, NK_EDIT_FIELD, g_recovery_save_as_destination,
                        sizeof(g_recovery_save_as_destination), nk_filter_default);
                    nk_layout_row_end(g_context);
                    if (g_recovery_save_as_destination[0] == '\0') {
                        nk_layout_row_dynamic(g_context, 18, 1);
                        nk_label(g_context,
                                 "Save as is disabled until a new destination is entered.",
                                 NK_TEXT_LEFT);
                    }
                }
                if (candidate->disabled_reason && candidate->disabled_reason[0]) {
                    nk_layout_row_dynamic(g_context, 20, 1);
                    nk_label(g_context, candidate->disabled_reason, NK_TEXT_LEFT);
                }
            }
            if (view->recovery_rows_truncated) {
                nk_layout_row_dynamic(g_context, 18, 1);
                nk_label(g_context, "Additional recovery candidates are not shown.", NK_TEXT_LEFT);
            }
            for (size_t diagnostic = 0; diagnostic < view->recovery_diagnostic_count;
                 ++diagnostic) {
                if (view->recovery_diagnostics[diagnostic]) {
                    nk_layout_row_dynamic(g_context, 18, 1);
                    nk_label(g_context, view->recovery_diagnostics[diagnostic], NK_TEXT_LEFT);
                }
            }
        }

        float content_height = height > 300 ? (float)height - 165.0f - recovery_height : 80.0f;
        if (content_height < 80.0f) {
            content_height = 80.0f;
        }
        nk_layout_row_dynamic(g_context, content_height, 1);
        if (nk_group_begin(g_context, "workspace-content", NK_WINDOW_BORDER)) {
            if (view->metadata.visible) {
                render_metadata_editor(&view->metadata, &action);
            } else if (view->compose.visible) {
                render_compose_field(&view->compose, &action);
            } else {
                nk_layout_row_dynamic(g_context, 24, 1);
                if (view->summary_line_count == 0) {
                    nk_label(g_context, "No items in this view.", NK_TEXT_LEFT);
                }
                for (size_t line = 0; line < view->summary_line_count; ++line) {
                    if (view->summary_lines[line]) {
                        nk_label(g_context, view->summary_lines[line], NK_TEXT_LEFT);
                    }
                }
            }
            nk_group_end(g_context);
        }

        nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 5);
        nk_layout_row_push(g_context, 0.16f);
        nk_labelf(g_context, NK_TEXT_LEFT, "History: %s",
                  view->history_available ? "available" : "unavailable");
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Undo: %zu", view->undo_depth);
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Redo: %zu", view->redo_depth);
        nk_layout_row_push(g_context, 0.52f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Revision build: %s", view->git_hash);
        nk_layout_row_push(g_context, 0.12f);
        if (nk_button_label(g_context, "Quit")) {
            action.kind = WORM_UI_WORKSPACE_ACTION_QUIT;
        }
        nk_layout_row_end(g_context);
    }
    nk_end(g_context);
    return action;
}

void worm_ui_frontend_render(int window_width, int window_height) {
    (void)window_width;
    (void)window_height;
    if (g_context) {
        nk_sdl_render(NK_ANTI_ALIASING_ON, WORMEDIT_MAX_VERTEX_MEMORY, WORMEDIT_MAX_ELEMENT_MEMORY);
    }
}
