#include "ui/nuklear/metadata/metadata_format_fields.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include "wormedit/nuklear_config.h"

#include <wormedit/core/format_field_schema.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    if (const auto* schema = wormedit::find_format_field_schema(
            wormedit::format_family_t::bms, bms_field_names[field])) {
        // Names are matched case-insensitively in the file but spelled upper-case in the schema.
        return !wormedit::validate_format_field_value(*schema, value);
    }
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

void initialize_bms_field_buffers(const wormedit::ui::metadata_view_t* metadata) {
    state.bms_field_indices.fill(std::numeric_limits<size_t>::max());
    state.bms_field_occurrences.fill(0);
    state.bms_field_valid.fill(true);
    for (auto& value : state.bms_fields)
        value.fill('\0');
    if (!metadata)
        return;
    for (size_t index = 0; index < metadata->format_fields.size(); ++index) {
        const auto& field = metadata->format_fields[index];
        if (field.chart_scoped || static_cast<std::uint8_t>(field.family) != bms_format_family ||
            !field.first_group_for_family)
            continue;
        for (size_t typed = 0; typed < bms_field_names.size(); ++typed) {
            if (!bms_field_name_matches(field.name.c_str(), bms_field_names[typed]))
                continue;
            ++state.bms_field_occurrences[typed];
            if (state.bms_field_indices[typed] != std::numeric_limits<size_t>::max())
                break;
            state.bms_field_indices[typed] = field.field_index;
            state.bms_field_valid[typed] =
                copy_metadata_text(state.bms_fields[typed].data(),
                                   state.bms_fields[typed].size(), field.value.c_str(),
                                   field.value.size()) != 0;
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
    if (const auto* schema = wormedit::find_format_field_schema(
            wormedit::format_family_t::bmson, bmson_field_names[typed]);
        schema && wormedit::validate_format_field_value(*schema, human))
        return std::nullopt;
    const auto parsed = nlohmann::json::parse(human, nullptr, false);
    if (!parsed.is_number() || !std::isfinite(parsed.get<double>()) ||
        parsed.get<double>() < 0.0)
        return std::nullopt;
    return std::string(human);
}

void initialize_bmson_field_buffers(const wormedit::ui::metadata_view_t* metadata) {
    state.bmson_field_indices.fill(std::numeric_limits<size_t>::max());
    state.bmson_field_chart_scoped.fill(false);
    state.bmson_field_valid.fill(true);
    for (auto& value : state.bmson_fields)
        value.fill('\0');
    if (!metadata)
        return;
    for (size_t typed = 0; typed < bmson_field_names.size(); ++typed) {
        for (const bool chart_scoped : {false, true}) {
            bool found = false;
            for (size_t index = 0; index < metadata->format_fields.size(); ++index) {
                const auto& field = metadata->format_fields[index];
                if (field.chart_scoped != chart_scoped ||
                    static_cast<std::uint8_t>(field.family) != bmson_format_family ||
                    !field.first_group_for_family || !field.name.c_str() || !field.value.c_str() ||
                    std::string_view{field.name.c_str()} != bmson_field_names[typed])
                    continue;
                state.bmson_field_indices[typed] = field.field_index;
                state.bmson_field_chart_scoped[typed] = chart_scoped;
                const auto human = bmson_human_value(
                    typed, std::string_view{field.value.c_str(), field.value.size()});
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

static int initialize_format_buffers(const wormedit::ui::metadata_format_field_view_t* field, int insert_mode) {
    state.format_name.assign(wormedit::default_metadata_edit_limits.maximum_field_name_bytes + 1, '\0');
    state.format_value.assign(wormedit::default_metadata_edit_limits.maximum_field_value_bytes + 1, '\0');
    state.format_insert_mode = insert_mode != 0;
    if (insert_mode) {
        state.format_buffers_valid = field && field->first_group_for_family;
        return state.format_buffers_valid;
    }
    if (!field || !field->first_group_for_family || !field->name.c_str() || !field->value.c_str() ||
        field->group_field_count > wormedit::default_metadata_edit_limits.maximum_fields_per_group) {
        state.format_buffers_valid = 0;
        return 0;
    }
    state.format_buffers_valid =
        copy_metadata_text(state.format_name.data(), state.format_name.size(), field->name.c_str(),
                           field->name.size()) &&
        copy_metadata_text(state.format_value.data(), state.format_value.size(), field->value.c_str(),
                           field->value.size());
    return state.format_buffers_valid;
}

static void initialize_format_target_buffers(const wormedit::ui::metadata_view_t* metadata,
                                             const wormedit::ui::metadata_format_target_view_t* target) {
    if (!metadata || !target || static_cast<std::uint8_t>(target->family) > 6 ||
        (target->chart_scoped && !metadata->has_chart) ||
        target->field_count > wormedit::default_metadata_edit_limits.maximum_fields_per_group) {
        state.format_target_valid = 0;
        state.format_buffers_valid = 0;
        return;
    }
    state.selected_format_field = std::numeric_limits<size_t>::max();
    state.format_target_valid = 1;
    state.format_target_chart_scoped = target->chart_scoped;
    state.format_target_family = static_cast<std::uint8_t>(target->family);
    state.format_target_field_count = target->field_count;
    state.format_target_index = target->field_count;
    state.format_insert_mode = 1;
    state.format_name.assign(wormedit::default_metadata_edit_limits.maximum_field_name_bytes + 1, '\0');
    state.format_value.assign(wormedit::default_metadata_edit_limits.maximum_field_value_bytes + 1, '\0');
    state.format_buffers_valid = 1;
}

static void set_metadata_format_action(wormedit::ui::workspace_action_t* action,
                                       const wormedit::ui::metadata_view_t* metadata,
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
    action->metadata_chart_id = state.format_target_chart_scoped ? metadata->chart_id : wormedit::chart_id_t{};
    action->metadata_format_chart_scoped = state.format_target_chart_scoped;
    action->metadata_format_family = static_cast<wormedit::format_family_t>(state.format_target_family);
    action->metadata_format_index = state.format_target_index;
    action->metadata_format_name = needs_text ? std::optional<std::string>{state.format_name.data()} : std::nullopt;
    action->metadata_format_value = needs_text ? std::optional<std::string>{state.format_value.data()} : std::nullopt;
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
    action->metadata_chart_id = chart_scoped ? metadata.chart_id : wormedit::chart_id_t{};
    action->metadata_format_chart_scoped = chart_scoped;
    action->metadata_format_family = static_cast<wormedit::format_family_t>(family);
    action->metadata_format_index = exists ? field_index : insert_index;
    action->metadata_format_name = name;
    action->metadata_format_value = value;
}

static void render_bms_field_buttons(const MetadataPanel& panel,
                                     const wormedit::ui::metadata_format_target_view_t& target,
                                     size_t typed, const wormedit::ui::metadata_format_field_view_t* source,
                                     bool editable, bool numeric_valid) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    auto* action = panel.action;
    const auto field_index = state.bms_field_indices[typed];
    const auto exists = field_index != std::numeric_limits<size_t>::max();
    auto& buffer = state.bms_fields[typed];
    const auto changed = exists && source && source->value.c_str() &&
                         std::strcmp(source->value.c_str(), buffer.data()) != 0;
    const auto room = target.field_count < wormedit::default_metadata_edit_limits.maximum_fields_per_group;
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
    if (!editable) {
        muted_label(context, "Source value exceeds the typed editor limit; use the ordered field editor.");
    } else if (!numeric_valid) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label_colored(context, "Enter an unsigned number before applying this field.",
                         NK_TEXT_LEFT,
                         wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
    } else {
        char hint[128];
        (void)std::snprintf(hint, sizeof(hint),
                            "%zu ordered entries; this control edits the first.",
                            state.bms_field_occurrences[typed]);
        muted_label(context, hint);
    }
}

static void render_bms_field(const MetadataPanel& panel,
                             const wormedit::ui::metadata_format_target_view_t& target, size_t typed) {
    auto* context = panel.context;
    auto& buffer = state.bms_fields[typed];
    const auto field_index = state.bms_field_indices[typed];
    const auto exists = field_index != std::numeric_limits<size_t>::max();
    const auto* source = exists ? panel.metadata->find_format_field(static_cast<wormedit::format_family_t>(bms_format_family),
                                                      false, field_index) : nullptr;
    const auto editable = state.bms_field_valid[typed] && (!exists || source != nullptr);
    const auto numeric_valid = bms_field_numeric_valid(typed, buffer.data());
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(context, 0.17f);
    nk_label(context, bms_field_names[typed], NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.51f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, buffer.data(), buffer.size(),
                                         nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    render_bms_field_buttons(panel, target, typed, source, editable, numeric_valid);
    render_bms_field_hint(context, typed, editable, numeric_valid);
}

static void render_bms_fields(struct nk_context* context, const wormedit::ui::metadata_view_t* metadata,
                              wormedit::ui::workspace_action_t* action) {
    const MetadataPanel panel{context, metadata, action, false};
    if (static_cast<std::uint8_t>(metadata->selected_chart_source_family) != bms_format_family || !metadata->has_chart)
        return;
    const auto* target = metadata->find_song_format_target(static_cast<wormedit::format_family_t>(bms_format_family));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "BMS fields", NK_TEXT_LEFT);
    if (!target) {
        muted_label(context, "BMS fields are unavailable while metadata rows are truncated.");
        return;
    }
    bool additional_open = false;
    for (size_t typed = 0; typed < bms_field_names.size(); ++typed) {
        if (typed == 5) {
            additional_open = metadata_tree_push(context, NK_TREE_TAB, "Additional BMS fields",
                                                  NK_MINIMIZED, 707);
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
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, typed == 1 ? NK_EDIT_BOX : NK_EDIT_FIELD,
                                         buffer.data(), buffer.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_end(context);
}

static void render_bmson_field_actions(const MetadataPanel& panel, size_t typed,
                                      const wormedit::ui::metadata_format_target_view_t& target,
                                      const wormedit::ui::metadata_format_field_view_t* source, bool editable,
                                      const std::optional<std::string>& encoded) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    auto* action = panel.action;
    auto& buffer = state.bmson_fields[typed];
    const auto index = state.bmson_field_indices[typed];
    const auto exists = index != std::numeric_limits<size_t>::max();
    const auto chart_scoped = state.bmson_field_chart_scoped[typed];
    const auto changed = exists && source && source->value.c_str() && encoded &&
        std::string_view{source->value.c_str(), source->value.size()} != *encoded;
    const auto has_value = buffer[0] != '\0' || typed == 1;
    const auto room = target.field_count < wormedit::default_metadata_edit_limits.maximum_fields_per_group;
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
    if (!editable) {
        muted_label(panel.context,
                    "Retained JSON value cannot be shown in this editor; use the ordered field editor.");
        return;
    }
    nk_layout_row_dynamic(panel.context, 18, 1);
    nk_label_colored(panel.context, "Enter a valid nonnegative number or text value.",
                     NK_TEXT_LEFT,
                     wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
}

static void render_bmson_field(const MetadataPanel& panel,
                               const wormedit::ui::metadata_format_target_view_t& target, size_t typed) {
    auto& buffer = state.bmson_fields[typed];
    const auto index = state.bmson_field_indices[typed];
    const auto exists = index != std::numeric_limits<size_t>::max();
    const auto chart_scoped = state.bmson_field_chart_scoped[typed];
    const auto* source = exists ? panel.metadata->find_format_field(static_cast<wormedit::format_family_t>(bmson_format_family),
                                                      chart_scoped, index) : nullptr;
    const auto editable = state.bmson_field_valid[typed] && (!exists || source != nullptr);
    render_bmson_field_input(panel, typed, editable);
    const auto encoded = editable
        ? bmson_json_value(typed, std::string_view{buffer.data()})
        : std::optional<std::string>{};
    render_bmson_field_actions(panel, typed, target, source, editable, encoded);
    render_bmson_field_hint(panel, editable, encoded.has_value());
}

static void render_bmson_fields(struct nk_context* context, const wormedit::ui::metadata_view_t* metadata,
                                wormedit::ui::workspace_action_t* action) {
    const MetadataPanel panel{context, metadata, action, false};
    if (static_cast<std::uint8_t>(metadata->selected_chart_source_family) != bmson_format_family || !metadata->has_chart)
        return;
    const auto* target = metadata->find_song_format_target(static_cast<wormedit::format_family_t>(bmson_format_family));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "BMSON info", NK_TEXT_LEFT);
    if (!target) {
        muted_label(context, "BMSON fields are unavailable while metadata rows are truncated.");
        return;
    }
    for (size_t typed = 0; typed < bmson_field_names.size(); ++typed)
        render_bmson_field(panel, *target, typed);
}

// Schema fields that are not already covered by the bespoke BMS and BMSON editors above.
static auto is_bespoke_field(std::uint8_t family, std::string_view name) -> bool {
    if (family == bms_format_family)
        return std::ranges::any_of(bms_field_names, [&](const char* item) {
            return bms_field_name_matches(name.data(), item);
        });
    if (family == bmson_format_family)
        return std::ranges::any_of(bmson_field_names,
                                   [&](const char* item) { return name == item; });
    return false;
}

static auto generic_typed_schemas(std::uint8_t family)
    -> std::vector<const wormedit::format_field_schema_t*> {
    std::vector<const wormedit::format_field_schema_t*> result;
    for (const auto& schema : wormedit::format_field_schemas(
             static_cast<wormedit::format_family_t>(family))) {
        if (result.size() < worm_ui_nuklear_metadata_state_t::typed_field_capacity &&
            !is_bespoke_field(family, schema.name))
            result.push_back(&schema);
    }
    return result;
}

// BMS tokens match case-insensitively; every other format spells the stored name exactly.
static auto typed_name_matches(std::uint8_t family, const char* stored,
                               const wormedit::format_field_schema_t& schema) -> bool {
    if (!stored) return false;
    if (family == bms_format_family)
        return bms_field_name_matches(stored, schema.name.data());
    return schema.name == stored;
}

static void load_typed_field(const wormedit::ui::metadata_view_t& metadata, std::uint8_t family,
                             std::size_t slot, const wormedit::format_field_schema_t& schema) {
    for (const bool chart_scoped : {false, true}) {
        for (size_t index = 0; index < metadata.format_fields.size(); ++index) {
            const auto& field = metadata.format_fields[index];
            if (field.chart_scoped != chart_scoped || static_cast<std::uint8_t>(field.family) != family ||
                !field.first_group_for_family || !field.value.c_str() ||
                !typed_name_matches(family, field.name.c_str(), schema))
                continue;
            ++state.typed_field_occurrences[slot];
            if (state.typed_field_indices[slot] != std::numeric_limits<size_t>::max())
                continue;
            state.typed_field_indices[slot] = field.field_index;
            state.typed_field_chart_scoped[slot] = chart_scoped;
            state.typed_field_valid[slot] =
                copy_metadata_text(state.typed_fields[slot].data(),
                                   state.typed_fields[slot].size(), field.value.c_str(),
                                   field.value.size()) != 0;
        }
        if (state.typed_field_indices[slot] != std::numeric_limits<size_t>::max()) return;
    }
}

void initialize_typed_field_buffers(const wormedit::ui::metadata_view_t* metadata) {
    state.typed_field_indices.fill(std::numeric_limits<size_t>::max());
    state.typed_field_occurrences.fill(0);
    state.typed_field_chart_scoped.fill(false);
    state.typed_field_valid.fill(true);
    for (auto& value : state.typed_fields)
        value.fill('\0');
    if (!metadata || !metadata->has_chart)
        return;
    const auto family = static_cast<std::uint8_t>(metadata->selected_chart_source_family);
    const auto schemas = generic_typed_schemas(family);
    for (size_t slot = 0; slot < schemas.size(); ++slot)
        load_typed_field(*metadata, family, slot, *schemas[slot]);
}

// An existing field keeps its scope; a new one goes to the song group unless only the chart has one.
static auto typed_field_target(const wormedit::ui::metadata_view_t& metadata, std::uint8_t family,
                               bool exists, bool existing_scope)
    -> const wormedit::ui::metadata_format_target_view_t* {
    const wormedit::ui::metadata_format_target_view_t* song = nullptr;
    const wormedit::ui::metadata_format_target_view_t* chart = nullptr;
    for (size_t index = 0; index < metadata.format_targets.size(); ++index) {
        const auto& target = metadata.format_targets[index];
        if (static_cast<std::uint8_t>(target.family) != family) continue;
        (target.chart_scoped ? chart : song) = &target;
    }
    if (exists) return existing_scope ? chart : song;
    return (!song || !song->group_exists) && chart && chart->group_exists ? chart : song;
}

static void render_typed_field_buttons(const MetadataPanel& panel, std::size_t slot,
                                       const wormedit::format_field_schema_t& schema,
                                       const wormedit::ui::metadata_format_target_view_t& target,
                                       bool editable, bool valid) {
    auto* context = panel.context;
    auto* action = panel.action;
    const auto family = static_cast<std::uint8_t>(panel.metadata->selected_chart_source_family);
    const auto index = state.typed_field_indices[slot];
    const auto exists = index != std::numeric_limits<size_t>::max();
    const auto* source = exists ? panel.metadata->find_format_field(static_cast<wormedit::format_family_t>(family),
                                                      state.typed_field_chart_scoped[slot], index)
                                : nullptr;
    const auto& buffer = state.typed_fields[slot];
    const auto changed = source && source->value.c_str() && std::strcmp(source->value.c_str(), buffer.data()) != 0;
    const auto room = target.field_count < wormedit::default_metadata_edit_limits.maximum_fields_per_group;
    const auto idle = action->metadata_edit == WORM_UI_METADATA_EDIT_NONE;
    const auto can_apply = editable && valid && idle &&
        (exists ? changed : buffer[0] != '\0' && room);
    nk_layout_row_push(context, 0.17f);
    if (!can_apply) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, exists ? "Apply" : "Add");
    if (!can_apply) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.15f);
    const auto can_clear = exists && idle;
    if (!can_clear) nk_widget_disable_begin(context);
    const auto clear = nk_button_label(context, "Clear");
    if (!can_clear) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if ((apply && can_apply) || (clear && can_clear))
        apply_typed_format_action(panel, family, index, target.field_count, schema.name.data(),
                                  buffer.data(), exists, clear,
                                  exists ? state.typed_field_chart_scoped[slot] : target.chart_scoped);
}

static void render_typed_field(const MetadataPanel& panel, std::size_t slot,
                               const wormedit::format_field_schema_t& schema) {
    auto* context = panel.context;
    const auto family = static_cast<std::uint8_t>(panel.metadata->selected_chart_source_family);
    const auto index = state.typed_field_indices[slot];
    const auto exists = index != std::numeric_limits<size_t>::max();
    const auto* target = typed_field_target(*panel.metadata, family, exists,
                                            state.typed_field_chart_scoped[slot]);
    if (!target) return;
    auto& buffer = state.typed_fields[slot];
    const auto editable = state.typed_field_valid[slot];
    const auto error = wormedit::validate_format_field_value(schema, buffer.data());
    const auto empty_new = !exists && buffer[0] == '\0';
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(context, 0.17f);
    nk_label(context, std::string(schema.label).c_str(), NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.51f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, buffer.data(), buffer.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    render_typed_field_buttons(panel, slot, schema, *target, editable, !error);
    if (!editable) {
        muted_label(context, "Source value exceeds the typed editor limit; use the ordered field editor.");
    } else if (error && !empty_new) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label_colored(context, error->c_str(), NK_TEXT_LEFT,
                         wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
    }
}

static void render_typed_fields(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    if (!metadata.has_chart) return;
    const auto schemas = generic_typed_schemas(static_cast<std::uint8_t>(metadata.selected_chart_source_family));
    if (schemas.empty()) return;
    nk_layout_row_dynamic(panel.context, 24, 1);
    nk_label(panel.context, "Typed fields", NK_TEXT_LEFT);
    for (size_t slot = 0; slot < schemas.size(); ++slot)
        render_typed_field(panel, slot, *schemas[slot]);
}

namespace {

void render_format_field_rows(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto* metadata = panel.metadata;

    std::size_t rows = 0;
    for (size_t index = 0; index < metadata->format_fields.size(); ++index) {
        const auto* field = &metadata->format_fields[index];
        if (!field->first_group_for_family) continue;
        ++rows;
        char label[32];
        (void)snprintf(label, sizeof(label), "Edit %zu", index + 1);
        nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
        nk_layout_row_push(context, 0.78f);
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s · %s (%zu fields)",
                  field->chart_scoped ? "Chart" : "Song", metadata->format_families.label(static_cast<wormedit::format_family_t>(static_cast<std::uint8_t>(field->family)), "Unknown"),
                  field->name.c_str() ? field->name.c_str() : "(empty)", field->group_field_count);
        nk_layout_row_push(context, 0.22f);
        const auto editable = static_cast<std::uint8_t>(field->family) <= 6 && (!field->chart_scoped || metadata->has_chart);
        if (!editable) nk_widget_disable_begin(context);
        if (nk_button_label(context, label) && editable) {
            state.selected_format_field = index;
            state.format_target_valid = true;
            state.format_target_chart_scoped = field->chart_scoped;
            state.format_target_family = static_cast<std::uint8_t>(field->family);
            state.format_target_index = field->field_index;
            state.format_target_field_count = field->group_field_count;
            initialize_format_buffers(field, 0);
        }
        if (!editable) nk_widget_disable_end(context);
        nk_layout_row_end(context);
    }
    if (rows == 0) muted_label(context, "No format-specific fields.");
}

auto selected_format_field(const wormedit::ui::metadata_view_t& metadata)
    -> const wormedit::ui::metadata_format_field_view_t* {
    if (state.selected_format_field >= metadata.format_fields.size())
        return nullptr;
    return &metadata.format_fields[state.selected_format_field];
}

void render_format_target_selector(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    auto* context = panel.context;
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 3);
    nk_layout_row_push(context, 0.2f);
    if (wormedit::ui::chrome::safe_combo_begin_label(context, state.format_add_chart_scoped ? "Chart" : "Song",
                             nk_vec2(nk_widget_width(context), 70))) {
        nk_layout_row_dynamic(context, 22, 1);
        if (nk_combo_item_label(context, "Song", NK_TEXT_LEFT)) state.format_add_chart_scoped = false;
        if (metadata.has_chart && nk_combo_item_label(context, "Chart", NK_TEXT_LEFT))
            state.format_add_chart_scoped = true;
        wormedit::ui::chrome::safe_combo_end(context);
    }
    nk_layout_row_push(context, 0.5f);
    const auto family_label = metadata.format_families.label(static_cast<wormedit::format_family_t>(state.format_add_family), "Unknown");
    if (wormedit::ui::chrome::safe_combo_begin_label(context, family_label, nk_vec2(nk_widget_width(context), 180))) {
        nk_layout_row_dynamic(context, 22, 1);
        for (std::size_t index = 0; index < metadata.format_families.entries.size(); ++index) {
            const auto& family = metadata.format_families.entries[index];
            if (nk_combo_item_label(context, family.label.data(), NK_TEXT_LEFT))
                state.format_add_family = static_cast<std::uint8_t>(family.id);
        }
        wormedit::ui::chrome::safe_combo_end(context);
    }
    nk_layout_row_push(context, 0.3f);
    const wormedit::ui::metadata_format_target_view_t* target = nullptr;
    for (size_t i = 0; i < metadata.format_targets.size(); ++i) {
        const auto& item = metadata.format_targets[i];
        if (item.chart_scoped == state.format_add_chart_scoped &&
            static_cast<std::uint8_t>(item.family) == state.format_add_family) { target = &item; break; }
    }
    const auto can_add = target && target->field_count < wormedit::default_metadata_edit_limits.maximum_fields_per_group;
    if (!can_add) nk_widget_disable_begin(context);
    const auto add = nk_button_label(context, "Add field");
    if (!can_add) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (add && can_add) initialize_format_target_buffers(&metadata, target);
}

void render_format_editor_title(nk_context* context, const wormedit::ui::metadata_view_t& metadata,
                                const wormedit::ui::metadata_format_field_view_t* field) {
    nk_layout_row_dynamic(context, 22, 1);
    if (field && !state.format_insert_mode)
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s field #%zu",
                  field->chart_scoped ? "Chart" : "Song",
                  metadata.format_families.label(static_cast<wormedit::format_family_t>(static_cast<std::uint8_t>(field->family)), "Unknown"), field->field_index + 1);
    else
        nk_labelf(context, NK_TEXT_LEFT, "New %s · %s field",
                  state.format_target_chart_scoped ? "Chart" : "Song",
                  metadata.format_families.label(static_cast<wormedit::format_family_t>(state.format_target_family), "Unknown"));
}

void render_format_editor_unavailable(const MetadataPanel& panel,
                                      const wormedit::ui::metadata_format_field_view_t* field) {
    muted_label(panel.context, "Field is read-only because it exceeds the text cap or group scope.");
    if (!field || !field->first_group_for_family) return;
    nk_layout_row_dynamic(panel.context, 25, 1);
    if (nk_button_label(panel.context, "Delete field") &&
        panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
        set_metadata_format_action(panel.action, panel.metadata,
                                   WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
}

void render_format_editor_actions(const MetadataPanel& panel,
                                  const wormedit::ui::metadata_format_field_view_t* field) {
    if (state.format_insert_mode) {
        nk_layout_row_dynamic(panel.context, 25, 1);
        if (nk_button_label(panel.context, "Add field") &&
            state.format_target_field_count < wormedit::default_metadata_edit_limits.maximum_fields_per_group &&
            panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
            set_metadata_format_action(panel.action, panel.metadata,
                                       WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD);
        return;
    }
    nk_layout_row_begin(panel.context, NK_DYNAMIC, 25, 3);
    nk_layout_row_push(panel.context, 0.34f);
    if (nk_button_label(panel.context, "Replace field") && field &&
        panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
        set_metadata_format_action(panel.action, panel.metadata,
                                   WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD);
    nk_layout_row_push(panel.context, 0.33f);
    if (nk_button_label(panel.context, "Delete field") && field &&
        panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE)
        set_metadata_format_action(panel.action, panel.metadata,
                                   WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD);
    nk_layout_row_push(panel.context, 0.33f);
    if (nk_button_label(panel.context, "Prepare new field") && field &&
        panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
        state.format_target_index = field->group_field_count;
        initialize_format_buffers(field, 1);
    }
    nk_layout_row_end(panel.context);
}

void render_format_editor(const MetadataPanel& panel) {
    if (!state.format_target_valid) return;
    const auto* field = selected_format_field(*panel.metadata);
    render_format_editor_title(panel.context, *panel.metadata, field);
    if (!state.format_buffers_valid) {
        render_format_editor_unavailable(panel, field);
        return;
    }
    metadata_input_row(panel.context, "Name", state.format_name.data(), state.format_name.size(), 1);
    metadata_input_row(panel.context, "Value", state.format_value.data(), state.format_value.size(), 1);
    render_format_editor_actions(panel, field);
}

} // namespace

void render_format_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    if (!metadata_tree_push(context, NK_TREE_TAB, "Format-specific fields", NK_MINIMIZED, 1896))
        return;
    render_bms_fields(context, panel.metadata, panel.action);
    render_bmson_fields(context, panel.metadata, panel.action);
    render_typed_fields(panel);
    render_format_field_rows(panel);
    render_format_target_selector(panel);
    render_format_editor(panel);
    nk_tree_pop(context);
}
