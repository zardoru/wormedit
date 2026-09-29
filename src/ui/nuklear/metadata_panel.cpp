#include "ui/nuklear/metadata_panel.h"
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/metadata_state.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <nlohmann/json.hpp>

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

static auto& state = worm_ui_nuklear_metadata_state();

struct MetadataPanel {
    nk_context* context;
    const WormUiMetadataView* metadata;
    WormUiWorkspaceAction* action;
    bool window_focused;
};

static constexpr std::uint8_t bms_format_family = 1;
static constexpr std::uint8_t bmson_format_family = 4;
static constexpr std::array<const char*, worm_ui_nuklear_metadata_state_t::bmson_field_count>
    bmson_field_names{"info.subtitle", "info.subartists", "info.genre", "info.judge_rank",
                      "info.total"};
static constexpr std::array<const char*, worm_ui_nuklear_metadata_state_t::bmson_field_count>
    bmson_field_labels{"Subtitle", "Subartists (one per line)", "Genre", "Judge rank", "Total"};
static constexpr std::array<const char*, worm_ui_nuklear_metadata_state_t::bms_field_count>
    bms_field_names{"GENRE",     "SUBARTIST", "STAGEFILE", "BANNER",  "PREVIEW",
                    "SUBTITLE",  "PLAYLEVEL", "RANK",      "TOTAL",   "DIFFICULTY",
                    "BACKBMP",   "COMMENT",   "URL",       "VOLWAV"};
enum class bms_field_value_kind_t { text, unsigned_integer, unsigned_decimal };
static constexpr std::array<bms_field_value_kind_t,
                            worm_ui_nuklear_metadata_state_t::bms_field_count>
    bms_field_value_kinds{
        bms_field_value_kind_t::text,             bms_field_value_kind_t::text,
        bms_field_value_kind_t::text,             bms_field_value_kind_t::text,
        bms_field_value_kind_t::text,             bms_field_value_kind_t::text,
        bms_field_value_kind_t::unsigned_decimal, bms_field_value_kind_t::unsigned_integer,
        bms_field_value_kind_t::unsigned_decimal, bms_field_value_kind_t::unsigned_integer,
        bms_field_value_kind_t::text,             bms_field_value_kind_t::text,
        bms_field_value_kind_t::text,             bms_field_value_kind_t::unsigned_decimal,
    };

static auto bms_field_numeric_valid(size_t field, std::string_view value) -> bool {
    const auto kind = bms_field_value_kinds[field];
    if (kind == bms_field_value_kind_t::text)
        return true;
    if (value.empty())
        return false;
    if (kind == bms_field_value_kind_t::unsigned_integer) {
        std::uint64_t parsed{};
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size();
    }
    bool digit = false;
    bool point = false;
    size_t digits = 0;
    for (const char character : value) {
        if (character >= '0' && character <= '9') {
            digit = true;
            if (++digits > 40)
                return false;
        } else if (character == '.' && !point) {
            point = true;
        } else {
            return false;
        }
    }
    return digit;
}

static auto bms_field_name_matches(const char* name, const char* expected) -> bool {
    if (!name || !expected)
        return false;
    while (*name && *expected) {
        if (std::toupper(static_cast<unsigned char>(*name)) !=
            std::toupper(static_cast<unsigned char>(*expected)))
            return false;
        ++name;
        ++expected;
    }
    return *name == '\0' && *expected == '\0';
}

static void SDLCALL metadata_file_dialog_callback(void* userdata, const char* const* filelist,
                                                  int /*selected_filter*/) {
    std::unique_ptr<std::uint64_t> generation{static_cast<std::uint64_t*>(userdata)};
    if (!generation)
        return;
    if (!filelist) {
        const auto* error = SDL_GetError();
        worm_ui_nuklear_metadata_file_dialog_complete(
            *generation, {}, error && *error ? error : "File picker failed.", false);
    } else if (!filelist[0]) {
        worm_ui_nuklear_metadata_file_dialog_complete(*generation, {}, {}, true);
    } else {
        std::size_t length{};
        while (length <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES && filelist[0][length] != '\0')
            ++length;
        if (length > WORM_UI_COMPOSE_IMPORT_PATH_BYTES) {
            worm_ui_nuklear_metadata_file_dialog_complete(
                *generation, {}, "Selected file path exceeds the import limit.", false);
        } else {
            worm_ui_nuklear_metadata_file_dialog_complete(
                *generation, std::string{filelist[0], length}, {}, false);
        }
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

static void initialize_bms_field_buffers(const WormUiMetadataView* metadata) {
    state.bms_field_indices.fill(std::numeric_limits<size_t>::max());
    state.bms_field_occurrences.fill(0);
    state.bms_field_valid.fill(true);
    for (auto& value : state.bms_fields)
        value.fill('\0');
    if (!metadata || !metadata->format_fields)
        return;
    for (size_t index = 0; index < metadata->format_field_count; ++index) {
        const auto& field = metadata->format_fields[index];
        if (field.chart_scoped || field.family != bms_format_family ||
            !field.first_group_for_family)
            continue;
        for (size_t typed = 0; typed < bms_field_names.size(); ++typed) {
            if (!bms_field_name_matches(field.name, bms_field_names[typed]))
                continue;
            ++state.bms_field_occurrences[typed];
            if (state.bms_field_indices[typed] != std::numeric_limits<size_t>::max())
                break;
            state.bms_field_indices[typed] = field.field_index;
            state.bms_field_valid[typed] =
                copy_metadata_text(state.bms_fields[typed].data(),
                                   state.bms_fields[typed].size(), field.value,
                                   field.value_bytes) != 0;
            break;
        }
    }
}

static auto bmson_human_value(size_t typed, std::string_view source)
    -> std::optional<std::string> {
    const auto value = nlohmann::json::parse(source, nullptr, false);
    if (value.is_discarded())
        return std::nullopt;
    if (typed == 0 || typed == 2) {
        if (!value.is_string())
            return std::nullopt;
        return value.get<std::string>();
    }
    if (typed == 1) {
        if (!value.is_array())
            return std::nullopt;
        std::string joined;
        bool first = true;
        for (const auto& item : value) {
            if (!item.is_string())
                return std::nullopt;
            const auto text = item.get<std::string>();
            if (text.empty() || text.find('\n') != std::string::npos ||
                text.find('\r') != std::string::npos)
                return std::nullopt;
            if (!first)
                joined.push_back('\n');
            joined += text;
            first = false;
        }
        return joined;
    }
    if (!value.is_number() || !std::isfinite(value.get<double>()) ||
        value.get<double>() < 0.0)
        return std::nullopt;
    return std::string(source);
}

static auto bmson_json_value(size_t typed, std::string_view human)
    -> std::optional<std::string> {
    if (typed == 0 || typed == 2)
        return nlohmann::json(std::string(human)).dump();
    if (typed == 1) {
        auto values = nlohmann::json::array();
        if (!human.empty()) {
            size_t start = 0;
            while (start <= human.size()) {
                const auto end = human.find('\n', start);
                const auto line = human.substr(start, end == std::string_view::npos
                                                          ? human.size() - start
                                                          : end - start);
                if (line.empty() || line.find('\r') != std::string_view::npos)
                    return std::nullopt;
                values.push_back(std::string(line));
                if (end == std::string_view::npos)
                    break;
                start = end + 1;
            }
        }
        return values.dump();
    }
    const auto parsed = nlohmann::json::parse(human, nullptr, false);
    if (!parsed.is_number() || !std::isfinite(parsed.get<double>()) ||
        parsed.get<double>() < 0.0)
        return std::nullopt;
    return std::string(human);
}

static void initialize_bmson_field_buffers(const WormUiMetadataView* metadata) {
    state.bmson_field_indices.fill(std::numeric_limits<size_t>::max());
    state.bmson_field_chart_scoped.fill(false);
    state.bmson_field_valid.fill(true);
    for (auto& value : state.bmson_fields)
        value.fill('\0');
    if (!metadata || !metadata->format_fields)
        return;
    for (size_t typed = 0; typed < bmson_field_names.size(); ++typed) {
        for (const bool chart_scoped : {false, true}) {
            bool found = false;
            for (size_t index = 0; index < metadata->format_field_count; ++index) {
                const auto& field = metadata->format_fields[index];
                if (field.chart_scoped != chart_scoped ||
                    field.family != bmson_format_family ||
                    !field.first_group_for_family || !field.name || !field.value ||
                    std::string_view{field.name} != bmson_field_names[typed])
                    continue;
                state.bmson_field_indices[typed] = field.field_index;
                state.bmson_field_chart_scoped[typed] = chart_scoped;
                const auto human = bmson_human_value(
                    typed, std::string_view{field.value, field.value_bytes});
                state.bmson_field_valid[typed] =
                    human && copy_metadata_text(state.bmson_fields[typed].data(),
                                                state.bmson_fields[typed].size(),
                                                human->c_str(), human->size()) != 0;
                found = true;
                break;
            }
            if (found)
                break;
        }
    }
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
    initialize_bms_field_buffers(metadata);
    initialize_bmson_field_buffers(metadata);
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

static auto find_format_source(const WormUiMetadataView& metadata, std::uint8_t family,
                               bool chart_scoped, std::size_t field_index)
    -> const WormUiMetadataFormatFieldView* {
    for (size_t index = 0; index < metadata.format_field_count; ++index) {
        const auto& field = metadata.format_fields[index];
        if (field.chart_scoped == chart_scoped && field.family == family &&
            field.first_group_for_family && field.field_index == field_index)
            return &field;
    }
    return nullptr;
}

static auto format_target(const WormUiMetadataView& metadata, std::uint8_t family)
    -> const WormUiMetadataFormatTargetView* {
    for (size_t index = 0; index < metadata.format_target_count; ++index) {
        const auto& target = metadata.format_targets[index];
        if (!target.chart_scoped && target.family == family) return &target;
    }
    return nullptr;
}

static void apply_typed_format_action(const MetadataPanel& panel, std::uint8_t family,
                                     std::size_t field_index, std::size_t insert_index,
                                     const char* name, const char* value, bool exists, bool clear,
                                     bool chart_scoped = false) {
    auto* action = panel.action;
    const auto& metadata = *panel.metadata;
    action->metadata_edit = clear ? WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD
        : exists ? WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD
                 : WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD;
    action->metadata_document_revision = metadata.document_revision;
    action->metadata_chart_id = exists && chart_scoped ? metadata.chart_id : 0;
    action->metadata_format_chart_scoped = exists && chart_scoped;
    action->metadata_format_family = family;
    action->metadata_format_index = exists ? field_index : insert_index;
    action->metadata_format_name = name;
    action->metadata_format_value = value;
}

static void render_bms_field_buttons(const MetadataPanel& panel,
                                     const WormUiMetadataFormatTargetView& target,
                                     size_t typed, const WormUiMetadataFormatFieldView* source,
                                     bool editable, bool numeric_valid) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    auto* action = panel.action;
    const auto field_index = state.bms_field_indices[typed];
    const auto exists = field_index != std::numeric_limits<size_t>::max();
    auto& buffer = state.bms_fields[typed];
    const auto changed = exists && source && source->value &&
                         std::strcmp(source->value, buffer.data()) != 0;
    const auto room = target.field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
    const auto can_apply = editable && numeric_valid &&
        action->metadata_edit == WORM_UI_METADATA_EDIT_NONE &&
        (exists ? changed : buffer[0] != '\0' && room);
    nk_layout_row_push(context, 0.17f);
    if (!can_apply) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, exists ? "Apply" : "Add");
    if (!can_apply) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.15f);
    const auto can_clear = exists && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
    if (!can_clear) nk_widget_disable_begin(context);
    const auto clear = nk_button_label(context, "Clear");
    if (!can_clear) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if ((apply && can_apply) || (clear && can_clear))
        apply_typed_format_action(panel, bms_format_family, field_index, target.field_count,
                                  bms_field_names[typed], buffer.data(), exists, clear);
    (void)metadata;
}

static void render_bms_field_hint(nk_context* context, size_t typed, bool editable,
                                  bool numeric_valid) {
    if (editable && numeric_valid && state.bms_field_occurrences[typed] <= 1) return;
    nk_layout_row_dynamic(context, 18, 1);
    if (!editable)
        nk_label(context, "Source value exceeds the typed editor limit; use the ordered field editor.",
                 NK_TEXT_LEFT);
    else if (!numeric_valid)
        nk_label(context, "Enter an unsigned number before applying this field.", NK_TEXT_LEFT);
    else
        nk_labelf(context, NK_TEXT_LEFT, "%zu ordered entries; this control edits the first.",
                  state.bms_field_occurrences[typed]);
}

static void render_bms_field(const MetadataPanel& panel,
                             const WormUiMetadataFormatTargetView& target, size_t typed) {
    auto* context = panel.context;
    auto& buffer = state.bms_fields[typed];
    const auto field_index = state.bms_field_indices[typed];
    const auto exists = field_index != std::numeric_limits<size_t>::max();
    const auto* source = exists ? find_format_source(*panel.metadata, bms_format_family,
                                                      false, field_index) : nullptr;
    const auto editable = state.bms_field_valid[typed] && (!exists || source != nullptr);
    const auto numeric_valid = bms_field_numeric_valid(typed, buffer.data());
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(context, 0.17f);
    nk_label(context, bms_field_names[typed], NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.51f);
    if (!editable) nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, buffer.data(), buffer.size(),
                                         nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    render_bms_field_buttons(panel, target, typed, source, editable, numeric_valid);
    render_bms_field_hint(context, typed, editable, numeric_valid);
}

static void render_bms_fields(struct nk_context* context, const WormUiMetadataView* metadata,
                              WormUiWorkspaceAction* action) {
    const MetadataPanel panel{context, metadata, action, false};
    if (metadata->selected_chart_source_family != bms_format_family || !metadata->has_chart)
        return;
    const auto* target = format_target(*metadata, bms_format_family);
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "BMS fields", NK_TEXT_LEFT);
    if (!target || metadata->format_fields_truncated) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "BMS fields are unavailable while metadata rows are truncated.",
                 NK_TEXT_LEFT);
        return;
    }
    bool additional_open = false;
    for (size_t typed = 0; typed < bms_field_names.size(); ++typed) {
        if (typed == 5) {
            additional_open = nk_tree_push(context, NK_TREE_TAB, "Additional BMS fields",
                                           NK_MINIMIZED);
            if (!additional_open) break;
        }
        render_bms_field(panel, *target, typed);
    }
    if (additional_open) nk_tree_pop(context);
}

static void render_bmson_field_input(const MetadataPanel& panel, size_t typed, bool editable) {
    auto* context = panel.context;
    auto& buffer = state.bmson_fields[typed];
    nk_layout_row_begin(context, NK_DYNAMIC, typed == 1 ? 58 : 26, 2);
    nk_layout_row_push(context, 0.27f);
    nk_label(context, bmson_field_labels[typed], NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.73f);
    if (!editable) nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, typed == 1 ? NK_EDIT_BOX : NK_EDIT_FIELD,
                                         buffer.data(), buffer.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_end(context);
}

static void render_bmson_field_actions(const MetadataPanel& panel, size_t typed,
                                      const WormUiMetadataFormatTargetView& target,
                                      const WormUiMetadataFormatFieldView* source, bool editable,
                                      const std::optional<std::string>& encoded) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    auto* action = panel.action;
    auto& buffer = state.bmson_fields[typed];
    const auto index = state.bmson_field_indices[typed];
    const auto exists = index != std::numeric_limits<size_t>::max();
    const auto chart_scoped = state.bmson_field_chart_scoped[typed];
    const auto changed = exists && source && source->value && encoded &&
        std::string_view{source->value, source->value_bytes} != *encoded;
    const auto has_value = buffer[0] != '\0' || typed == 1;
    const auto room = target.field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
    const auto can_apply = editable && encoded &&
        action->metadata_edit == WORM_UI_METADATA_EDIT_NONE &&
        (exists ? changed : has_value && room);
    const auto can_clear = exists && action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.5f);
    if (!can_apply) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, exists ? "Apply" : "Add");
    if (!can_apply) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.5f);
    if (!can_clear) nk_widget_disable_begin(context);
    const auto clear = nk_button_label(context, "Clear");
    if (!can_clear) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if ((apply && can_apply) || (clear && can_clear)) {
        state.bmson_action_json_value = encoded.value_or(std::string{});
        apply_typed_format_action(panel, bmson_format_family, index, target.field_count,
                                  bmson_field_names[typed], state.bmson_action_json_value.c_str(),
                                  exists, clear, chart_scoped);
    }
    (void)metadata;
}

static void render_bmson_field_hint(const MetadataPanel& panel, bool editable, bool encoded) {
    if (editable && encoded) return;
    nk_layout_row_dynamic(panel.context, 18, 1);
    nk_label(panel.context, editable
        ? "Enter a valid nonnegative number or text value."
        : "Retained JSON value cannot be shown in this editor; use the ordered field editor.",
        NK_TEXT_LEFT);
}

static void render_bmson_field(const MetadataPanel& panel,
                               const WormUiMetadataFormatTargetView& target, size_t typed) {
    auto& buffer = state.bmson_fields[typed];
    const auto index = state.bmson_field_indices[typed];
    const auto exists = index != std::numeric_limits<size_t>::max();
    const auto chart_scoped = state.bmson_field_chart_scoped[typed];
    const auto* source = exists ? find_format_source(*panel.metadata, bmson_format_family,
                                                      chart_scoped, index) : nullptr;
    const auto editable = state.bmson_field_valid[typed] && (!exists || source != nullptr);
    render_bmson_field_input(panel, typed, editable);
    const auto encoded = editable
        ? bmson_json_value(typed, std::string_view{buffer.data()})
        : std::optional<std::string>{};
    render_bmson_field_actions(panel, typed, target, source, editable, encoded);
    render_bmson_field_hint(panel, editable, encoded.has_value());
}

static void render_bmson_fields(struct nk_context* context, const WormUiMetadataView* metadata,
                                WormUiWorkspaceAction* action) {
    const MetadataPanel panel{context, metadata, action, false};
    if (metadata->selected_chart_source_family != bmson_format_family || !metadata->has_chart)
        return;
    const auto* target = format_target(*metadata, bmson_format_family);
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "BMSON info", NK_TEXT_LEFT);
    if (!target || metadata->format_fields_truncated) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "BMSON fields are unavailable while metadata rows are truncated.",
                 NK_TEXT_LEFT);
        return;
    }
    for (size_t typed = 0; typed < bmson_field_names.size(); ++typed)
        render_bmson_field(panel, *target, typed);
}

static void metadata_register_drop_target(struct nk_context* context,
                                          WormUiMetadataDropTarget target) {
    if (target.destination == WORM_UI_METADATA_DROP_NONE)
        return;
    const auto widget = nk_widget_bounds(context);
    const auto clip = nk_window_get_content_region(context);
    const auto left = std::max(widget.x, clip.x);
    const auto top = std::max(widget.y, clip.y);
    const auto right = std::min(widget.x + widget.w, clip.x + clip.w);
    const auto bottom = std::min(widget.y + widget.h, clip.y + clip.h);
    worm_ui_nuklear_set_metadata_drop_target(target, {.x = left,
                                                      .y = top,
                                                      .width = std::max(0.0f, right - left),
                                                      .height = std::max(0.0f, bottom - top)});
}

static void metadata_read_only_path(struct nk_context* context, const char* label,
                                    const std::string& path, WormUiMetadataDropTarget target = {}) {
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, label, NK_TEXT_LEFT);
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    nk_layout_row_dynamic(context, 25, 1);
    metadata_register_drop_target(context, target);
    (void)nk_edit_string_zero_terminated(
        context, static_cast<nk_flags>(NK_EDIT_FIELD) | static_cast<nk_flags>(NK_EDIT_READ_ONLY),
        buffer.data(), buffer.size(), nk_filter_default);
}

static void set_metadata_music_import_action(WormUiWorkspaceAction* action,
                                             const WormUiMetadataView* metadata, const char* path) {
    if (!metadata->has_chart || !path || !path[0] ||
        std::strlen(path) > WORM_UI_COMPOSE_IMPORT_PATH_BYTES ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        action->metadata_import_copy_project || action->metadata_import_copy_export ||
        metadata->import_copy_pending)
        return;
    std::snprintf(action->compose_import_path, sizeof(action->compose_import_path), "%s", path);
    action->compose_resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    action->compose_input_chart_id = metadata->chart_id;
    action->compose_input_document_revision = metadata->document_revision;
    action->compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET;
}

static void metadata_copy_inspection_button(struct nk_context* context,
                                            const WormUiMetadataView* metadata,
                                            WormUiWorkspaceAction* action, bool export_copy,
                                            size_t group_index) {
    nk_layout_row_dynamic(context, 25, 1);
    if (metadata->copy_preview_pending)
        nk_widget_disable_begin(context);
    const auto inspect = nk_button_label(context, export_copy ? "Inspect export destination"
                                                              : "Inspect project destination");
    if (metadata->copy_preview_pending)
        nk_widget_disable_end(context);
    if (inspect && !metadata->copy_preview_pending && !action->metadata_inspect_copy &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->metadata_inspect_copy = true;
        action->metadata_inspect_export = export_copy;
        action->compose_resource_asset_id = metadata->selected_music_asset_id;
        action->compose_output_group_index = group_index;
        action->compose_input_chart_id = metadata->chart_id;
        action->compose_input_document_revision = metadata->document_revision;
    }
    if (metadata->copy_preview_text && !metadata->copy_preview_import &&
        metadata->copy_preview_asset_id == metadata->selected_music_asset_id &&
        metadata->copy_preview_export == export_copy &&
        (!export_copy || metadata->copy_preview_group_index == group_index)) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, metadata->copy_preview_text, NK_TEXT_LEFT);
    }
}

static auto external_copy_allowed(const MetadataPanel& panel, const char* source_path,
                                 const WormUiComposeOutputGroupView* export_group) -> bool {
    const auto& metadata = *panel.metadata;
    if (!source_path || !source_path[0] || !metadata.project_directory ||
        !metadata.project_directory[0]) return false;
    if (export_group && !export_group->relative_asset_directory[0]) return false;
    const std::filesystem::path source{source_path};
    return metadata.has_chart && metadata.next_import_asset_id != 0 &&
           !metadata.import_copy_pending && source.is_absolute() &&
           std::strlen(source_path) <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES;
}

static void render_external_copy_destination(const MetadataPanel& panel,
                                             const std::filesystem::path& source,
                                             const WormUiComposeOutputGroupView* export_group) {
    const auto& metadata = *panel.metadata;
    if (metadata.next_import_asset_id == 0 || !source.is_absolute()) return;
    const auto directory = export_group
        ? std::filesystem::path{export_group->relative_asset_directory}
        : std::filesystem::path{"assets"};
    const auto relative = directory / (std::to_string(metadata.next_import_asset_id) + "-" +
                                       source.filename().string());
    metadata_read_only_path(panel.context,
        export_group ? "Import-copy export destination" : "Import-copy project reference",
        relative.generic_string());
}

static void request_external_copy_inspection(const MetadataPanel& panel,
                                             const char* source_path, bool export_copy,
                                             std::size_t group_index, bool can_copy) {
    const auto& metadata = *panel.metadata;
    auto* action = panel.action;
    nk_layout_row_dynamic(panel.context, 25, 1);
    const auto disabled = !can_copy || metadata.copy_preview_pending;
    if (disabled) nk_widget_disable_begin(panel.context);
    const auto inspect = nk_button_label(panel.context,
        export_copy ? "Inspect external export copy" : "Inspect external project copy");
    if (disabled) nk_widget_disable_end(panel.context);
    if (!inspect || disabled || action->metadata_inspect_copy ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    std::snprintf(action->compose_import_path, sizeof(action->compose_import_path), "%s",
                  source_path);
    action->metadata_inspect_copy = true;
    action->metadata_inspect_import = true;
    action->metadata_inspect_export = export_copy;
    action->compose_resource_asset_id = metadata.next_import_asset_id;
    action->compose_output_group_index = group_index;
    action->compose_input_chart_id = metadata.chart_id;
    action->compose_input_document_revision = metadata.document_revision;
}

static auto external_copy_preview_matches(const WormUiMetadataView& metadata,
                                          const std::filesystem::path& source,
                                          bool export_copy, std::size_t group_index) -> bool {
    return metadata.copy_preview_text && metadata.copy_preview_import &&
        metadata.copy_preview_asset_id == metadata.next_import_asset_id &&
        metadata.copy_preview_export == export_copy &&
        (!export_copy || metadata.copy_preview_group_index == group_index) &&
        metadata.copy_preview_source &&
        std::filesystem::path{metadata.copy_preview_source} == source.lexically_normal();
}

static void render_external_copy_preview(const MetadataPanel& panel,
                                         const std::filesystem::path& source,
                                         bool export_copy, std::size_t group_index) {
    if (!external_copy_preview_matches(*panel.metadata, source, export_copy, group_index)) return;
    nk_layout_row_dynamic(panel.context, 19, 1);
    nk_label(panel.context, panel.metadata->copy_preview_text, NK_TEXT_LEFT);
}

static void request_external_copy(const MetadataPanel& panel, const char* source_path,
                                  const WormUiComposeOutputGroupView* export_group,
                                  bool can_copy) {
    const auto& metadata = *panel.metadata;
    auto* action = panel.action;
    const auto export_copy = export_group != nullptr;
    const auto group_index = export_copy ? export_group->document_index : 0;
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (!can_copy) nk_widget_disable_begin(panel.context);
    const auto* label = metadata.import_copy_pending ? "Copying and validating audio"
        : export_copy ? "Copy and link audio into export" : "Copy and link audio into project";
    const auto copy = nk_button_label(panel.context, label);
    if (!can_copy) nk_widget_disable_end(panel.context);
    if (!copy || !can_copy || action->metadata_import_copy_project ||
        action->metadata_import_copy_export || action->metadata_inspect_copy ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    std::snprintf(action->compose_import_path, sizeof(action->compose_import_path), "%s",
                  source_path);
    action->compose_input_chart_id = metadata.chart_id;
    action->compose_input_document_revision = metadata.document_revision;
    action->compose_resource_asset_id = metadata.next_import_asset_id;
    if (export_copy) {
        action->compose_output_group_index = group_index;
        action->metadata_import_copy_export = true;
    } else {
        action->metadata_import_copy_project = true;
    }
}

static void
metadata_external_copy_button(struct nk_context* context, const WormUiMetadataView* metadata,
                              WormUiWorkspaceAction* action, const char* source_path,
                              const WormUiComposeOutputGroupView* export_group = nullptr) {
    if (!source_path || !source_path[0] || !metadata->project_directory ||
        !metadata->project_directory[0] ||
        (export_group && !export_group->relative_asset_directory[0])) return;
    const MetadataPanel panel{context, metadata, action, false};
    const auto source = std::filesystem::path{source_path};
    const bool export_copy = export_group != nullptr;
    const auto can_copy = external_copy_allowed(panel, source_path, export_group);
    render_external_copy_destination(panel, source, export_group);
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context,
             export_copy ? "Export-copy: validate, stage, then link external BGM."
                         : "Import-copy: validate, publish, then add one BGM resource.",
             NK_TEXT_LEFT);
    const auto group_index = export_copy ? export_group->document_index : 0;
    request_external_copy_inspection(panel, source_path, export_copy, group_index, can_copy);
    render_external_copy_preview(panel, source, export_copy, group_index);
    request_external_copy(panel, source_path, export_group, can_copy);
}

static void consume_metadata_file_dialog_result() {
    auto picked = worm_ui_nuklear_metadata_file_dialog_take();
    if (!picked) return;
    state.external_music_error.fill('\0');
    if (!picked->error.empty()) {
        std::snprintf(state.external_music_error.data(), state.external_music_error.size(), "%s",
                      picked->error.c_str());
        return;
    }
    if (picked->canceled) return;
    if (picked->path.size() >= state.external_music_path.size() ||
        !std::filesystem::path{picked->path}.is_absolute()) {
        std::snprintf(state.external_music_error.data(), state.external_music_error.size(),
                      "Choose a bounded absolute file path.");
        return;
    }
    std::snprintf(state.external_music_path.data(), state.external_music_path.size(), "%s",
                  picked->path.c_str());
}

static void open_metadata_file_dialog() {
    const auto generation = worm_ui_nuklear_metadata_file_dialog_begin();
    if (!generation) return;
    auto* token = new (std::nothrow) std::uint64_t{*generation};
    if (!token) {
        worm_ui_nuklear_metadata_file_dialog_complete(
            *generation, {}, "File picker could not allocate callback state.", false);
        return;
    }
    state.external_music_error.fill('\0');
    SDL_ShowOpenFileDialog(metadata_file_dialog_callback, token, SDL_GetKeyboardFocus(),
                           nullptr, 0, nullptr, false);
}

static void render_metadata_audio_picker(const MetadataPanel& panel) {
    auto* context = panel.context;
    consume_metadata_file_dialog_result();
    const auto dialog_open = worm_ui_nuklear_metadata_file_dialog_open();
    nk_layout_row_dynamic(context, 25, 1);
    if (dialog_open) nk_widget_disable_begin(context);
    const auto browse = nk_button_label(context, dialog_open ? "Choosing audio file"
                                                             : "Choose external audio file");
    if (dialog_open) nk_widget_disable_end(context);
    if (browse && !dialog_open) open_metadata_file_dialog();
    if (state.external_music_error[0]) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, state.external_music_error.data(), NK_TEXT_LEFT);
    }
    if (!state.external_music_path[0]) return;
    const auto& metadata = *panel.metadata;
    metadata_read_only_path(context, "Chosen audio file", state.external_music_path.data());
    nk_layout_row_dynamic(context, 25, 1);
    const auto enabled = metadata.has_chart && !metadata.import_copy_pending;
    if (!enabled) nk_widget_disable_begin(context);
    const auto link = nk_button_label(context, "Link chosen file as BGM");
    if (!enabled) nk_widget_disable_end(context);
    if (link) set_metadata_music_import_action(panel.action, &metadata,
                                               state.external_music_path.data());
    metadata_external_copy_button(context, &metadata, panel.action,
                                  state.external_music_path.data());
}

static auto find_output_group(const WormUiMetadataView& metadata, std::size_t index)
    -> const WormUiComposeOutputGroupView* {
    for (std::size_t i = 0; i < metadata.output_group_count; ++i)
        if (metadata.output_groups[i].document_index == index) return &metadata.output_groups[i];
    return nullptr;
}

static auto find_dropped_music_group(const WormUiMetadataView& metadata)
    -> const WormUiComposeOutputGroupView* {
    if (metadata.dropped_music_target.destination != WORM_UI_METADATA_DROP_EXPORT) return nullptr;
    return find_output_group(metadata, metadata.dropped_music_target.output_group_index);
}

static void render_dropped_music_target_directory(
    const MetadataPanel& panel, const WormUiComposeOutputGroupView* dropped_group) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    const auto destination = metadata.dropped_music_target.destination;
    if (destination == WORM_UI_METADATA_DROP_PROJECT) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, "Drop target: project assets; different files are preserved.", NK_TEXT_LEFT);
        if (!metadata.project_directory || !metadata.project_directory[0]) {
            nk_layout_row_dynamic(context, 19, 1);
            nk_label(context, "Open a saved native project before copying a drop.", NK_TEXT_LEFT);
        } else {
            metadata_read_only_path(context, "Drop target directory",
                (std::filesystem::path{metadata.project_directory} / "assets").generic_string());
        }
    } else if (destination == WORM_UI_METADATA_DROP_EXPORT) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, dropped_group ? "Drop target: selected export group assets."
                                        : "The dropped export group is no longer available.",
                 NK_TEXT_LEFT);
        if (dropped_group && metadata.project_directory && metadata.project_directory[0] &&
            dropped_group->relative_asset_directory[0])
            metadata_read_only_path(context, "Drop target directory",
                (std::filesystem::path{metadata.project_directory} /
                 dropped_group->relative_asset_directory).lexically_normal().generic_string());
    }
}

static auto dropped_music_is_selected_asset(const WormUiMetadataView& metadata,
                                            const std::filesystem::path& project) -> bool {
    const auto selected_source = std::filesystem::path{
        metadata.selected_music_source ? metadata.selected_music_source : ""};
    if (!metadata.selected_music_asset_id || project.empty() || selected_source.empty()) return false;
    const auto resolved = selected_source.is_absolute()
        ? selected_source.lexically_normal() : (project / selected_source).lexically_normal();
    return std::filesystem::path{metadata.dropped_music_path}.lexically_normal() == resolved;
}

static void render_dropped_selected_music_copy(
    const MetadataPanel& panel, const WormUiComposeOutputGroupView* group,
    const std::filesystem::path& project) {
    const auto& metadata = *panel.metadata;
    const auto destination = metadata.dropped_music_target.destination;
    const auto relative_directory = destination == WORM_UI_METADATA_DROP_PROJECT
        ? std::filesystem::path{"assets"}
        : std::filesystem::path{group->relative_asset_directory};
    const auto selected_source = std::filesystem::path{metadata.selected_music_source};
    const auto filename = std::to_string(metadata.selected_music_asset_id) + "-" +
                         selected_source.filename().string();
    metadata_read_only_path(panel.context, "Normalized project-relative destination",
                            (relative_directory / filename).generic_string());
    metadata_copy_inspection_button(panel.context, &metadata, panel.action,
                                    destination == WORM_UI_METADATA_DROP_EXPORT,
                                    group ? group->document_index : 0);
    nk_layout_row_dynamic(panel.context, 25, 1);
    const auto label = destination == WORM_UI_METADATA_DROP_PROJECT
        ? "Confirm drop: copy selected music to project"
        : "Confirm drop: copy selected music to export";
    const auto clicked = nk_button_label(panel.context, label);
    if (!clicked || panel.action->metadata_inspect_copy ||
        panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE || !metadata.has_chart) return;
    auto* action = panel.action;
    action->compose_resource_asset_id = metadata.selected_music_asset_id;
    action->compose_input_chart_id = metadata.chart_id;
    action->compose_input_document_revision = metadata.document_revision;
    if (destination == WORM_UI_METADATA_DROP_PROJECT)
        action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    else {
        action->compose_output_group_index = group->document_index;
        action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    }
    (void)project;
}

static void render_dropped_music_link(const MetadataPanel& panel,
                                      const WormUiComposeOutputGroupView* group) {
    const auto& metadata = *panel.metadata;
    const auto can_link = metadata.has_chart && !metadata.import_copy_pending &&
        std::strlen(metadata.dropped_music_path) <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES;
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (!can_link) nk_widget_disable_begin(panel.context);
    const auto link = nk_button_label(panel.context, "Link dropped file as BGM");
    if (!can_link) nk_widget_disable_end(panel.context);
    if (link && can_link)
        set_metadata_music_import_action(panel.action, &metadata, metadata.dropped_music_path);
    const auto destination = metadata.dropped_music_target.destination;
    if (destination == WORM_UI_METADATA_DROP_PROJECT)
        metadata_external_copy_button(panel.context, &metadata, panel.action,
                                      metadata.dropped_music_path);
    else if (destination == WORM_UI_METADATA_DROP_EXPORT && group)
        metadata_external_copy_button(panel.context, &metadata, panel.action,
                                      metadata.dropped_music_path, group);
}

static void render_dropped_music(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    if (!metadata.dropped_music_path || !metadata.dropped_music_path[0]) return;
    metadata_read_only_path(panel.context, "Dropped audio file", metadata.dropped_music_path);
    const auto* group = find_dropped_music_group(metadata);
    render_dropped_music_target_directory(panel, group);
    const auto project = std::filesystem::path{
        metadata.project_directory ? metadata.project_directory : ""};
    const auto copy_target_valid = metadata.dropped_music_target.destination ==
            WORM_UI_METADATA_DROP_PROJECT ||
        (group && group->relative_asset_directory[0]);
    if (copy_target_valid && dropped_music_is_selected_asset(metadata, project))
        render_dropped_selected_music_copy(panel, group, project);
    else
        render_dropped_music_link(panel, group);
}

static auto render_output_group_combo(const MetadataPanel& panel)
    -> const WormUiComposeOutputGroupView* {
    const auto& metadata = *panel.metadata;
    if (metadata.output_group_count == 0) return nullptr;
    auto* selected = find_output_group(metadata, state.selected_output_group_index);
    if (!selected) {
        selected = &metadata.output_groups[0];
        state.selected_output_group_index = selected->document_index;
    }
    nk_layout_row_dynamic(panel.context, 24, 1);
    const auto* label = selected->name[0] ? selected->name : selected->relative_output_path;
    if (nk_combo_begin_label(panel.context, label,
                             nk_vec2(nk_widget_width(panel.context), 180.0f))) {
        nk_layout_row_dynamic(panel.context, 22, 1);
        for (size_t index = 0; index < metadata.output_group_count; ++index) {
            const auto& group = metadata.output_groups[index];
            const auto* group_label = group.name[0] ? group.name : group.relative_output_path;
            if (!nk_combo_item_label(panel.context, group_label, NK_TEXT_LEFT)) continue;
            selected = &group;
            state.selected_output_group_index = group.document_index;
        }
        nk_combo_end(panel.context);
    }
    return selected;
}

static void synchronize_output_path_buffer(const WormUiMetadataView& metadata,
                                          const WormUiComposeOutputGroupView& selected) {
    if (state.output_path_buffer_revision == metadata.document_revision &&
        state.output_path_buffer_group_index == selected.document_index &&
        std::strcmp(state.output_path_source.data(), selected.relative_output_path) == 0) return;
    state.output_path_buffer_revision = metadata.document_revision;
    state.output_path_buffer_group_index = selected.document_index;
    state.output_path_buffer_valid = copy_metadata_text(
        state.output_path.data(), state.output_path.size(), selected.relative_output_path,
        std::strlen(selected.relative_output_path));
    state.output_path_buffer_valid = copy_metadata_text(
        state.output_path_source.data(), state.output_path_source.size(),
        selected.relative_output_path, std::strlen(selected.relative_output_path)) &&
        state.output_path_buffer_valid;
}

static void apply_output_path_action(const MetadataPanel& panel,
                                     const WormUiComposeOutputGroupView& selected,
                                     bool changed) {
    const auto& metadata = *panel.metadata;
    auto* action = panel.action;
    if (!changed || !state.output_path_buffer_valid || state.output_path[0] == '\0') return;
    if (action->metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    action->metadata_edit = WORM_UI_METADATA_EDIT_OUTPUT_PATH;
    action->metadata_document_revision = metadata.document_revision;
    action->metadata_output_group_index = selected.document_index;
    action->metadata_output_path = state.output_path.data();
}

static void render_output_path_editor(const MetadataPanel& panel,
                                     const WormUiComposeOutputGroupView& selected) {
    auto* context = panel.context;
    synchronize_output_path_buffer(*panel.metadata, selected);
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Output path relative to project", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 25, 1);
    metadata_register_drop_target(context,
        {WORM_UI_METADATA_DROP_OUTPUT_PATH, selected.document_index});
    if (!state.output_path_buffer_valid) nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.output_path.data(),
                                         state.output_path.size(), nk_filter_default);
    if (!state.output_path_buffer_valid) nk_widget_disable_end(context);
    const auto changed = state.output_path_buffer_valid &&
        std::strcmp(state.output_path.data(), selected.relative_output_path) != 0;
    nk_layout_row_dynamic(context, 25, 1);
    if (!changed || state.output_path[0] == '\0') nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply output path");
    if (!changed || state.output_path[0] == '\0') nk_widget_disable_end(context);
    if (apply) apply_output_path_action(panel, selected, changed);
}

static void render_dropped_output_path(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    if (!metadata.dropped_output_relative_path || !metadata.dropped_output_relative_path[0]) return;
    const auto* group = find_output_group(metadata, metadata.dropped_output_group_index);
    metadata_read_only_path(panel.context, "Dropped output path",
                            metadata.dropped_output_relative_path);
    nk_layout_row_dynamic(panel.context, 19, 1);
    nk_label(panel.context, group
        ? "Apply this path to the output group under the drop target."
        : "The dropped output group is no longer available.", NK_TEXT_LEFT);
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (!group) nk_widget_disable_begin(panel.context);
    const auto apply = nk_button_label(panel.context, "Apply dropped output path");
    if (!group) nk_widget_disable_end(panel.context);
    if (!apply || !group || panel.action->metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    panel.action->metadata_edit = WORM_UI_METADATA_EDIT_OUTPUT_PATH;
    panel.action->metadata_document_revision = metadata.document_revision;
    panel.action->metadata_output_group_index = group->document_index;
    panel.action->metadata_output_path = metadata.dropped_output_relative_path;
}

struct MusicCopyInfo {
    std::filesystem::path project;
    std::string destination_name;
    bool has_music{};
};

auto render_project_copy_info(const MetadataPanel& panel) -> std::optional<MusicCopyInfo> {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    if (!metadata.project_directory || !metadata.project_directory[0]) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, "Save and reopen a native project to copy resource files.", NK_TEXT_LEFT);
        return std::nullopt;
    }
    MusicCopyInfo info{.project = std::filesystem::path{metadata.project_directory},
                       .destination_name = {}, .has_music = false};
    metadata_read_only_path(context, "Project directory", info.project.generic_string(),
                            {WORM_UI_METADATA_DROP_PROJECT, 0});
    info.has_music = metadata.selected_music_asset_id && metadata.selected_music_source;
    if (!info.has_music) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, "Select a music asset for project or export copies.", NK_TEXT_LEFT);
        return info;
    }
    const std::filesystem::path source{metadata.selected_music_source};
    metadata_read_only_path(context, "Selected music source",
        source.is_absolute() ? source.generic_string()
                             : (info.project / source).lexically_normal().generic_string());
    nk_layout_row_dynamic(context, 19, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Asset references: %zu catalog resources, %zu sound channels.",
              metadata.selected_music_resource_count, metadata.selected_music_channel_count);
    info.destination_name = std::to_string(metadata.selected_music_asset_id) + "-" +
                            source.filename().string();
    metadata_read_only_path(context, "Project copy destination",
                            (info.project / "assets" / info.destination_name).generic_string());
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context,
             "Project copy relinks this asset after publication; different files are preserved.",
             NK_TEXT_LEFT);
    metadata_copy_inspection_button(context, &metadata, panel.action, false, 0);
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, "Copy music into project assets") &&
        !panel.action->metadata_inspect_copy &&
        panel.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        panel.action->compose_resource_asset_id = metadata.selected_music_asset_id;
        panel.action->compose_input_chart_id = metadata.chart_id;
        panel.action->compose_input_document_revision = metadata.document_revision;
        panel.action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    }
    return info;
}

static void render_export_copy_info(const MetadataPanel& panel,
                                    const WormUiComposeOutputGroupView& selected,
                                    const MusicCopyInfo& info) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    if (selected.relative_asset_directory[0] == '\0') {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, "The output group has no safe asset directory.", NK_TEXT_LEFT);
        return;
    }
    metadata_read_only_path(context, "Output group directory",
        (info.project / std::filesystem::path{selected.relative_asset_directory}.parent_path())
            .lexically_normal().generic_string(),
        {WORM_UI_METADATA_DROP_EXPORT, selected.document_index});
    if (state.external_music_path[0])
        metadata_external_copy_button(context, &metadata, panel.action,
                                      state.external_music_path.data(), &selected);
    if (!info.has_music) return;
    metadata_read_only_path(context, "Export copy destination",
        (info.project / selected.relative_asset_directory / info.destination_name)
            .lexically_normal().generic_string());
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Export copy is verified at export; different files are preserved.",
             NK_TEXT_LEFT);
    metadata_copy_inspection_button(context, &metadata, panel.action, true,
                                    selected.document_index);
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, "Copy music into selected export folder") &&
        !panel.action->metadata_inspect_copy &&
        panel.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        panel.action->compose_resource_asset_id = metadata.selected_music_asset_id;
        panel.action->compose_output_group_index = selected.document_index;
        panel.action->compose_input_chart_id = metadata.chart_id;
        panel.action->compose_input_document_revision = metadata.document_revision;
        panel.action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    }
}

static void render_project_and_export_copies(const MetadataPanel& panel,
                                             const WormUiComposeOutputGroupView* selected) {
    const auto info = render_project_copy_info(panel);
    if (!info) return;
    if (!selected) {
        nk_layout_row_dynamic(panel.context, 19, 1);
        nk_label(panel.context, "No output group includes this difficulty.", NK_TEXT_LEFT);
        return;
    }
    render_export_copy_info(panel, *selected, *info);
}

static void render_metadata_paths(struct nk_context* context, const WormUiMetadataView* metadata,
                                  WormUiWorkspaceAction* action) {
    const MetadataPanel panel{context, metadata, action, false};
    worm_ui_nuklear_clear_metadata_drop_targets();
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Music and output paths", NK_TEXT_LEFT);
    render_metadata_audio_picker(panel);
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Drop audio here to link, or on a directory to stage a copy.", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Drop a project file or directory on Output path to set it.", NK_TEXT_LEFT);
    render_dropped_music(panel);
    const auto* selected = render_output_group_combo(panel);
    if (selected) render_output_path_editor(panel, *selected);
    render_dropped_output_path(panel);
    render_project_and_export_copies(panel, selected);
    if (metadata->output_groups_truncated) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label(context, "Additional output groups exceed the visible limit.", NK_TEXT_LEFT);
    }
}

namespace {

void render_metadata_header(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Song metadata", NK_TEXT_LEFT);
    metadata_input_row(context, "Title", state.title.data(), state.title.size(),
                       metadata.song_fields_editable && state.song_buffers_valid);
    metadata_input_row(context, "Artist", state.artist.data(), state.artist.size(),
                       metadata.song_fields_editable && state.song_buffers_valid);
    render_bms_fields(context, &metadata, panel.action);
    render_bmson_fields(context, &metadata, panel.action);
    render_metadata_paths(context, &metadata, panel.action);
}

auto render_metadata_tag_row(nk_context* context, std::size_t index, bool editable) -> bool {
    char remove_label[32];
    (void)snprintf(remove_label, sizeof(remove_label), "Remove %zu", index + 1);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 5);
    nk_layout_row_push(context, 0.07f);
    nk_labelf(context, NK_TEXT_LEFT, "%zu", index + 1);
    nk_layout_row_push(context, 0.57f);
    if (!editable) nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.search_tags[index].data(),
                                         state.search_tags[index].size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.09f);
    const auto can_move_up = editable && index != 0;
    if (!can_move_up) nk_widget_disable_begin(context);
    const auto move_up = nk_button_label(context, "Up");
    if (!can_move_up) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.11f);
    const auto can_move_down = editable && index + 1 < state.search_tags.size();
    if (!can_move_down) nk_widget_disable_begin(context);
    const auto move_down = nk_button_label(context, "Down");
    if (!can_move_down) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.16f);
    if (!editable) nk_widget_disable_begin(context);
    const auto remove = nk_button_label(context, remove_label);
    if (!editable) nk_widget_disable_end(context);
    if (move_up && can_move_up)
        std::swap(state.search_tags[index], state.search_tags[index - 1]);
    else if (move_down && can_move_down)
        std::swap(state.search_tags[index], state.search_tags[index + 1]);
    else if (remove && editable) {
        state.search_tags.erase(state.search_tags.begin() + static_cast<std::ptrdiff_t>(index));
        nk_layout_row_end(context);
        return true;
    }
    nk_layout_row_end(context);
    return false;
}

void render_metadata_tag_add(nk_context* context, bool editable) {
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.82f);
    if (!editable) nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.new_search_tag.data(),
                                         state.new_search_tag.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.18f);
    const auto can_add = editable &&
        state.search_tags.size() < WORM_UI_METADATA_SEARCH_TAG_CAPACITY &&
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

void render_metadata_tags(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Ordered search tags (%zu; duplicates are preserved)",
              metadata.search_tag_count);
    const auto editable = metadata.song_fields_editable && state.song_buffers_valid &&
                          state.tag_buffers_valid;
    if (!state.tag_buffers_valid || !metadata.song_fields_editable) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Tags are read-only because the song metadata exceeds an edit cap.",
                 NK_TEXT_LEFT);
        return;
    }
    const auto visible = std::min(state.search_tags.size(),
                                  worm_ui_nuklear_metadata_state_t::visible_tag_count);
    for (std::size_t index = 0; index < visible; ++index) {
        if (render_metadata_tag_row(context, index, editable)) break;
    }
    if (state.search_tags.size() > worm_ui_nuklear_metadata_state_t::visible_tag_count) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Showing first %zu tags; the rest stay unchanged.",
                  worm_ui_nuklear_metadata_state_t::visible_tag_count);
    }
    render_metadata_tag_add(context, editable);
}

void render_song_apply(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    const auto changed = metadata_song_changed(&metadata);
    nk_layout_row_dynamic(panel.context, 26, 1);
    if (!changed || !metadata.song_fields_editable) nk_widget_disable_begin(panel.context);
    if (nk_button_label(panel.context, "Apply song metadata") && changed &&
        metadata.song_fields_editable)
        set_metadata_song_action(panel.action, &metadata);
    if (!changed || !metadata.song_fields_editable) nk_widget_disable_end(panel.context);
}

void render_audio_offset_fields(const MetadataPanel& panel, bool editable) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    metadata_input_row(context, "Offset seconds numerator", state.audio_offset_numerator.data(),
                       state.audio_offset_numerator.size(), editable);
    metadata_input_row(context, "Offset seconds denominator", state.audio_offset_denominator.data(),
                       state.audio_offset_denominator.size(), editable);
    if (metadata.selected_chart_source_family == bms_format_family) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "BMS exports this offset as #OFFSET; player support varies.", NK_TEXT_LEFT);
    } else if (metadata.selected_chart_source_family == bmson_format_family) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "BMSON 1.0 export requires a zero offset.", NK_TEXT_LEFT);
    }
}

void render_audio_offset_validation(const MetadataPanel& panel, bool editable) {
    const auto& metadata = *panel.metadata;
    const auto parsed = metadata_audio_offset_value();
    const auto changed = parsed && (parsed->numerator != metadata.audio_offset_numerator ||
                                    parsed->denominator != metadata.audio_offset_denominator);
    if (!parsed) {
        nk_layout_row_dynamic(panel.context, 20, 1);
        nk_label(panel.context, "Use a signed 64-bit numerator and positive 64-bit denominator.",
                 NK_TEXT_LEFT);
    } else if (metadata.timing_locked) {
        nk_layout_row_dynamic(panel.context, 20, 1);
        nk_label(panel.context, "Unlock timing to change audio synchronization.", NK_TEXT_LEFT);
    }
    const auto can_apply = editable && changed &&
                           panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
    nk_layout_row_dynamic(panel.context, 26, 1);
    if (!can_apply) nk_widget_disable_begin(panel.context);
    const auto apply = nk_button_label(panel.context, "Apply exact audio offset");
    if (!can_apply) nk_widget_disable_end(panel.context);
    if (apply && can_apply) set_metadata_audio_offset_action(panel.action, &metadata);
}

void render_audio_waveform(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    const WormUiAudioSlicesView waveform{
        .visible = true, .assets_truncated = false, .asset_ids = nullptr, .asset_count = 0,
        .has_selected_asset = metadata.audio_waveform_has_selected_asset,
        .selected_asset_id = metadata.audio_waveform_selected_asset_id,
        .analysis_pending = metadata.audio_waveform_analysis_pending,
        .waveform_available = metadata.audio_waveform_available,
        .total_frames = metadata.audio_waveform_total_frames,
        .sample_rate = metadata.audio_waveform_sample_rate,
        .waveform_columns = metadata.audio_waveform_columns,
        .waveform_column_count = metadata.audio_waveform_column_count,
        .has_chart = metadata.has_chart, .chart_id = metadata.chart_id,
        .document_revision = metadata.document_revision, .timing_locked = metadata.timing_locked,
        .offset_numerator = metadata.audio_offset_numerator,
        .offset_denominator = metadata.audio_offset_denominator,
        .offset_drag_active = metadata.audio_offset_drag_active,
        .offset_drag_delta_frames = metadata.audio_offset_drag_delta_frames,
        .proposed_offset_numerator = metadata.proposed_audio_offset_numerator,
        .proposed_offset_denominator = metadata.proposed_audio_offset_denominator,
    };
    worm_ui_nuklear_render_audio_slices(panel.context, &waveform, panel.action, false,
                                        panel.window_focused);
}

void render_audio_sync(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Audio and synchronization", NK_TEXT_LEFT);
    if (!metadata.has_chart) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Select a difficulty to edit its beat-zero audio offset.", NK_TEXT_LEFT);
        return;
    }
    const auto editable = metadata.audio_offset_editable && !metadata.timing_locked;
    render_audio_offset_fields(panel, editable);
    render_audio_offset_validation(panel, editable);
    render_audio_waveform(panel);
}

void render_chart_rating_fields(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    metadata_input_row(context, "Difficulty", state.chart_name.data(), state.chart_name.size(),
                       metadata.chart_fields_editable && state.chart_buffers_valid);
    metadata_input_row(context, "Mapper", state.chart_author.data(), state.chart_author.size(),
                       metadata.chart_fields_editable && state.chart_buffers_valid);
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Exact signed rational rating (stored without reduction)", NK_TEXT_LEFT);
    const auto editable = metadata.chart_fields_editable && state.chart_buffers_valid;
    metadata_input_row(context, "Numerator", state.chart_rating_numerator.data(),
                       state.chart_rating_numerator.size(), editable);
    metadata_input_row(context, "Denominator", state.chart_rating_denominator.data(),
                       state.chart_rating_denominator.size(), editable);
    const auto parsed = metadata_chart_rating_value(&metadata);
    const auto started = state.chart_rating_numerator[0] != '\0' ||
                         strcmp(state.chart_rating_denominator.data(), "1") != 0;
    if (!parsed && (metadata.has_chart_rating || started)) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context,
                 "Use a signed 64-bit numerator and positive 64-bit denominator; values are "
                 "not truncated or reduced.", NK_TEXT_LEFT);
    }
}

void render_chart_rating_actions(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    auto* action = panel.action;
    const auto parsed = metadata_chart_rating_value(&metadata);
    const auto changed = metadata_chart_changed(&metadata);
    const auto can_apply = changed && parsed.has_value() && metadata.chart_fields_editable &&
                           state.chart_buffers_valid &&
                           action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
    const auto can_unset = metadata.has_chart_rating && metadata.chart_fields_editable &&
                           state.chart_buffers_valid &&
                           action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.5f);
    if (!can_apply) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply difficulty metadata");
    if (!can_apply) nk_widget_disable_end(context);
    if (apply && can_apply) set_metadata_chart_action(action, &metadata, 0);
    nk_layout_row_push(context, 0.5f);
    if (!can_unset) nk_widget_disable_begin(context);
    const auto unset = nk_button_label(context, "Unset rating");
    if (!can_unset) nk_widget_disable_end(context);
    if (unset && can_unset) set_metadata_chart_action(action, &metadata, 1);
    nk_layout_row_end(context);
}

void render_chart_metadata(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, metadata.has_chart ? "Selected difficulty metadata"
                                         : "Select a difficulty to edit chart metadata",
             NK_TEXT_LEFT);
    if (!metadata.has_chart) return;
    render_chart_rating_fields(panel);
    render_chart_rating_actions(panel);
}

void render_format_field_rows(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto* metadata = panel.metadata;
    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Format-specific fields (%zu; duplicates stay ordered)",
              metadata->format_field_total_count);
    if (metadata->format_field_count != 0 && !metadata->format_fields) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Format field list is unavailable.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < metadata->format_field_count; ++index) {
        const auto* field = &metadata->format_fields[index];
        char label[32];
        (void)snprintf(label, sizeof(label), "Edit %zu", index + 1);
        nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(context, 0.78f);
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s · %s #%zu%s",
                  field->chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(field->family), field->name ? field->name : "(empty)",
                  field->field_index + 1, field->has_source_order ? " (source)" : "");
        nk_layout_row_push(context, 0.22f);
        const auto editable = field->first_group_for_family && field->family <= 6 &&
                              (!field->chart_scoped || metadata->has_chart);
        if (!editable) nk_widget_disable_begin(context);
        if (nk_button_label(context, label) && editable) {
            state.selected_format_field = index;
            state.format_target_valid = 1;
            state.format_target_chart_scoped = field->chart_scoped;
            state.format_target_family = field->family;
            state.format_target_index = field->field_index;
            state.format_target_field_count = field->group_field_count;
            initialize_format_buffers(field, 0);
        }
        if (!editable) nk_widget_disable_end(context);
        nk_layout_row_end(context);
    }
    if (metadata->format_fields_truncated) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Additional format fields are hidden by the frontend row cap.",
                 NK_TEXT_LEFT);
    }
}

void render_format_target_rows(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto* metadata = panel.metadata;
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Add a field to a format group", NK_TEXT_LEFT);
    if (metadata->format_target_count != 0 && !metadata->format_targets) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Format group targets are unavailable.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < metadata->format_target_count; ++index) {
        const auto* target = &metadata->format_targets[index];
        char label[32];
        (void)snprintf(label, sizeof(label), "Add field %zu", index + 1);
        nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(context, 0.76f);
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s · %s (%zu fields)",
                  target->chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(target->family),
                  target->group_exists ? "existing group" : "new group", target->field_count);
        nk_layout_row_push(context, 0.24f);
        const auto editable = target->family <= 6 &&
            (!target->chart_scoped || metadata->has_chart) &&
            target->field_count < WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
        if (!editable) nk_widget_disable_begin(context);
        if (nk_button_label(context, label) && editable)
            initialize_format_target_buffers(metadata, target);
        if (!editable) nk_widget_disable_end(context);
        nk_layout_row_end(context);
    }
}

auto selected_format_field(const WormUiMetadataView& metadata)
    -> const WormUiMetadataFormatFieldView* {
    if (state.selected_format_field >= metadata.format_field_count || !metadata.format_fields)
        return nullptr;
    return &metadata.format_fields[state.selected_format_field];
}

void render_format_editor_title(nk_context* context, const WormUiMetadataFormatFieldView* field) {
    nk_layout_row_dynamic(context, 24, 1);
    if (field && !state.format_insert_mode)
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s field #%zu",
                  field->chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(field->family), field->field_index + 1);
    else
        nk_labelf(context, NK_TEXT_LEFT, "New %s · %s field",
                  state.format_target_chart_scoped ? "Chart" : "Song",
                  metadata_format_family_name(state.format_target_family));
}

void render_format_editor_repair(const MetadataPanel& panel,
                                 const WormUiMetadataFormatFieldView* field) {
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Field is read-only because it exceeds the text cap or group scope.",
             NK_TEXT_LEFT);
    if (!field || !field->first_group_for_family) return;
    nk_layout_row_dynamic(context, 26, 1);
    if (nk_button_label(context, "Delete field to repair metadata") &&
        panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
        set_metadata_format_action(panel.action, panel.metadata,
                                   WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
}

void render_format_editor_actions(const MetadataPanel& panel,
                                  const WormUiMetadataFormatFieldView* field) {
    auto* context = panel.context;
    auto* action = panel.action;
    const auto* metadata = panel.metadata;
    if (state.format_insert_mode) {
        nk_layout_row_dynamic(context, 26, 1);
        const auto enabled = state.format_target_field_count <
                             WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY;
        if (!enabled) nk_widget_disable_begin(context);
        if (nk_button_label(context, "Insert field at end of this format group") && enabled &&
            action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
            set_metadata_format_action(action, metadata,
                                       WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD);
        if (!enabled) nk_widget_disable_end(context);
        return;
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 3);
    nk_layout_row_push(context, 0.34f);
    if (nk_button_label(context, "Replace field") && field &&
        action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
        set_metadata_format_action(action, metadata, WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD);
    nk_layout_row_push(context, 0.33f);
    if (nk_button_label(context, "Delete field") && field &&
        action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
        set_metadata_format_action(action, metadata, WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
    nk_layout_row_push(context, 0.33f);
    if (nk_button_label(context, "Prepare new field") && field &&
        action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
        state.format_target_index = field->group_field_count;
        initialize_format_buffers(field, 1);
    }
    nk_layout_row_end(context);
}

void render_format_editor(const MetadataPanel& panel) {
    if (!state.format_target_valid) return;
    const auto* field = selected_format_field(*panel.metadata);
    render_format_editor_title(panel.context, field);
    if (!state.format_buffers_valid) {
        render_format_editor_repair(panel, field);
        return;
    }
    metadata_input_row(panel.context, "Name", state.format_name.data(), state.format_name.size(), 1);
    metadata_input_row(panel.context, "Value", state.format_value.data(),
                       state.format_value.size(), 1);
    render_format_editor_actions(panel, field);
}

void render_format_fields(const MetadataPanel& panel) {
    render_format_field_rows(panel);
    render_format_target_rows(panel);
    render_format_editor(panel);
}

} // namespace

void worm_ui_nuklear_render_metadata_panel(struct nk_context* context,
                                           const WormUiMetadataView* metadata,
                                           WormUiWorkspaceAction* action, bool window_focused) {
    if (!context || !metadata || !action || !metadata->visible) {
        return;
    }
    if (metadata_buffer_key_changed(metadata)) initialize_metadata_buffers(metadata);
    const MetadataPanel panel{context, metadata, action, window_focused};
    render_metadata_header(panel);
    render_metadata_tags(panel);
    render_song_apply(panel);
    render_audio_sync(panel);
    render_chart_metadata(panel);
    render_format_fields(panel);
}
