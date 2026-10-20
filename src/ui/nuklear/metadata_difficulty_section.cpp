#include "ui/nuklear/metadata_difficulty_section.h"
#include "ui/nuklear/tooltip.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {

static auto metadata_chart_rating_value(const wormedit::ui::metadata_view_t* metadata)
    -> std::optional<metadata_rating_value_t> {
    if (!metadata)
        return std::nullopt;
    const std::string_view text{state.chart_rating_text.data()};
    if (text.empty()) {
        return metadata_rating_value_t{};
    }
    const auto parsed = worm_ui_nuklear_parse_metadata_rational(text, false);
    if (!parsed) return std::nullopt;
    return metadata_rating_value_t{true, parsed->numerator, parsed->denominator};
}

static int metadata_chart_changed(const wormedit::ui::metadata_view_t* metadata) {
    if (!metadata || !metadata->has_chart || !state.chart_buffers_valid || !metadata->chart_name.c_str() ||
        !metadata->chart_author.c_str()) {
        return 0;
    }
    const bool text_changed = strcmp(state.chart_name.data(), metadata->chart_name.c_str()) != 0 ||
                              strcmp(state.chart_author.data(), metadata->chart_author.c_str()) != 0;
    const auto rating = metadata_chart_rating_value(metadata);
    const bool rating_changed =
        rating &&
        (rating->has_rating != metadata->has_chart_rating ||
         (rating->has_rating && (rating->numerator != metadata->chart_rating_numerator ||
                                 rating->denominator != metadata->chart_rating_denominator)));
    return text_changed || rating_changed;
}

static void set_metadata_chart_action(wormedit::ui::workspace_action_t* action,
                                      const wormedit::ui::metadata_view_t* metadata, int unset_rating) {
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
    action->metadata_chart_id = metadata->chart_id.value;
    action->metadata_chart_name = state.chart_name.data();
    action->metadata_chart_author = state.chart_author.data();
    action->metadata_has_chart_rating = !unset_rating && rating->has_rating;
    if (action->metadata_has_chart_rating) {
        const auto wire = state.rational_action_text(rating->numerator, rating->denominator);
        action->metadata_chart_rating_numerator_text = wire.numerator;
        action->metadata_chart_rating_denominator_text = wire.denominator;
    }
}

// Exact positive integer, with no sign, separators or surrounding text.
static auto parse_resolution_text(const char* text) -> std::optional<std::uint64_t> {
    const std::string_view view{text};
    std::uint64_t value{};
    const auto parsed = std::from_chars(view.data(), view.data() + view.size(), value);
    if (view.empty() || parsed.ec != std::errc{} || parsed.ptr != view.data() + view.size() ||
        value == 0)
        return std::nullopt;
    return value;
}

static void set_metadata_redivision_action(wormedit::ui::workspace_action_t* action,
                                           const wormedit::ui::metadata_view_t* metadata) {
    if (!action || !metadata || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE ||
        !metadata->has_chart)
        return;
    const auto resolution = parse_resolution_text(state.chart_resolution_text.data());
    if (!resolution)
        return;
    action->metadata_edit = WORM_UI_METADATA_EDIT_REDIVIDE_CHART;
    action->metadata_document_revision = metadata->document_revision;
    action->metadata_chart_id = metadata->chart_id.value;
    action->metadata_redivision_resolution = *resolution;
}

void reset_difficulty_draft(const wormedit::ui::metadata_view_t& metadata) {
    std::snprintf(state.chart_resolution_text.data(), state.chart_resolution_text.size(), "%llu",
                  static_cast<unsigned long long>(metadata.chart_divisions_per_standard_measure));
    (void)copy_metadata_text(state.chart_name.data(), state.chart_name.size(), metadata.chart_name.c_str(),
                             metadata.chart_name.size());
    (void)copy_metadata_text(state.chart_author.data(), state.chart_author.size(), metadata.chart_author.c_str(),
                             metadata.chart_author.size());
    state.chart_rating_text.fill('\0');
    if (metadata.has_chart_rating) {
        const auto text = worm_ui_nuklear_format_metadata_rational(
            {metadata.chart_rating_numerator, metadata.chart_rating_denominator});
        (void)copy_metadata_text(state.chart_rating_text.data(), state.chart_rating_text.size(),
                                 text.c_str(), text.size());
    }
}

auto difficulty_draft_changed(const wormedit::ui::metadata_view_t& metadata,
                              const std::optional<metadata_rating_value_t>& rating) -> bool {
    const auto source_rating = metadata.has_chart_rating
        ? worm_ui_nuklear_format_metadata_rational(
              {metadata.chart_rating_numerator, metadata.chart_rating_denominator})
        : std::string{};
    const auto text_changed = !metadata.chart_name.c_str() || !metadata.chart_author.c_str() ||
        std::strcmp(state.chart_name.data(), metadata.chart_name.c_str()) != 0 ||
        std::strcmp(state.chart_author.data(), metadata.chart_author.c_str()) != 0;
    if (!rating)
        return text_changed ||
               std::strcmp(state.chart_rating_text.data(), source_rating.c_str()) != 0;
    const auto rating_changed = rating->has_rating != metadata.has_chart_rating ||
        (rating->has_rating &&
         (rating->numerator != metadata.chart_rating_numerator ||
          rating->denominator != metadata.chart_rating_denominator));
    return text_changed || rating_changed;
}

} // namespace

// Resolution field plus Redivide button; a click on a valid, different resolution sets the action.
static void render_resolution_row(const MetadataPanel& panel, bool editable) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    const auto resolution = parse_resolution_text(state.chart_resolution_text.data());
    const auto redivide_enabled = editable && resolution &&
        *resolution != metadata.chart_divisions_per_standard_measure;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 3);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Resolution", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.57f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
                                         state.chart_resolution_text.data(),
                                         state.chart_resolution_text.size(), nk_filter_decimal);
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.15f);
    if (!redivide_enabled) nk_widget_disable_begin(context);
    const auto redivide = nk_button_label(context, "Redivide") != 0 && redivide_enabled;
    if (!redivide_enabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0)
        wormedit::ui::nuklear::tooltip::text(context, "Divisions per standard measure. Redivide moves every position exactly, or changes nothing.");
    if (redivide) set_metadata_redivision_action(panel.action, &metadata);
}

// Reloads the channel-map text from the view when the document or the layout segment changed.
static void sync_channel_map_buffers(const wormedit::ui::metadata_view_t& metadata) {
    const auto count = metadata.channel_map_lanes.size();
    if (state.channel_map_buffers_valid &&
        state.channel_map_buffer_revision == metadata.document_revision &&
        state.channel_map_buffer_segment == metadata.channel_map_layout_segment_id.value &&
        state.channel_map_texts.size() == count)
        return;
    state.channel_map_texts.assign(count, {});
    state.channel_map_lane_ids.assign(count, 0);
    for (std::size_t lane = 0; lane < count; ++lane) {
        const auto& source = metadata.channel_map_lanes[lane];
        state.channel_map_lane_ids[lane] = source.lane_id.value;
        for (std::size_t channel = 0; channel < 4; ++channel)
            (void)copy_metadata_text(state.channel_map_texts[lane][channel].data(),
                                     state.channel_map_texts[lane][channel].size(),
                                     source.channels[channel].c_str(), std::strlen(source.channels[channel].c_str()));
    }
    state.channel_map_buffer_revision = metadata.document_revision;
    state.channel_map_buffer_segment = metadata.channel_map_layout_segment_id.value;
    state.channel_map_buffers_valid = true;
}

static void render_channel_map_lane_row(nk_context* context, const wormedit::ui::metadata_channel_map_lane_view_t& lane,
                                        std::size_t lane_index, bool editable) {
    static constexpr const char* channel_names[4] = {"Visible", "Invisible", "Long", "Mine"};
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 5);
    nk_layout_row_push(context, 0.2f);
    nk_label(context, lane.name.c_str(), NK_TEXT_LEFT);
    for (std::size_t channel = 0; channel < 4; ++channel) {
        nk_layout_row_push(context, 0.2f);
        if (!editable) nk_widget_disable_begin(context);
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
            context, NK_EDIT_FIELD, state.channel_map_texts[lane_index][channel].data(),
            state.channel_map_texts[lane_index][channel].size(), nk_filter_default);
        if (!editable) nk_widget_disable_end(context);
        if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0)
            wormedit::ui::nuklear::tooltip::text(context, channel_names[channel]);
    }
    nk_layout_row_end(context);
}

static void set_channel_map_preview_action(wormedit::ui::workspace_action_t* action,
                                           const wormedit::ui::metadata_view_t& metadata) {
    if (action->metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    state.channel_map_text_pointers.clear();
    for (const auto& lane : state.channel_map_texts)
        for (const auto& text : lane) state.channel_map_text_pointers.push_back(text.data());
    action->metadata_edit = WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP;
    action->metadata_document_revision = metadata.document_revision;
    action->metadata_chart_id = metadata.chart_id.value;
    action->metadata_channel_map_layout_segment_id = metadata.channel_map_layout_segment_id.value;
    action->metadata_channel_map_lane_count = state.channel_map_lane_ids.size();
    action->metadata_channel_map_lane_ids = state.channel_map_lane_ids.data();
    action->metadata_channel_map_texts = state.channel_map_text_pointers.data();
}

static void set_lane_profile_action(wormedit::ui::workspace_action_t* action, const wormedit::ui::metadata_view_t& metadata,
                                    WormUiMetadataEditKind kind, std::size_t index) {
    if (!action || action->metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    action->metadata_edit = kind;
    action->metadata_document_revision = metadata.document_revision;
    action->metadata_chart_id = metadata.chart_id.value;
    action->metadata_lane_profile_index = index;
}

// Key-count selector over the chart's family plus Custom. Preview and Apply go through the sealed
// key-count proposal; Custom stays disabled with its reason.
static void render_lane_profile_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    if (metadata.lane_profile_choices.size() == 0) return;
    if (!state.lane_profile_valid || state.lane_profile_chart != metadata.chart_id.value) {
        state.lane_profile_chart = metadata.chart_id.value;
        state.lane_profile_selected = metadata.lane_profile_current;
        state.lane_profile_valid = true;
    }
    const auto count = metadata.lane_profile_choices.size();
    state.lane_profile_labels.clear();
    for (std::size_t index = 0; index < count; ++index)
        state.lane_profile_labels.push_back(metadata.lane_profile_choices[index].label.c_str());
    if (state.lane_profile_selected >= count) state.lane_profile_selected = metadata.lane_profile_current;
    nk_layout_row_dynamic(context, 19, 1);
    muted_label(context, "Key count (this chart's family)");
    nk_layout_row_dynamic(context, 26, 1);
    const auto chosen = wormedit::ui::chrome::safe_combo(context, state.lane_profile_labels.data(),
                                                         static_cast<int>(count),
                                                         static_cast<int>(state.lane_profile_selected), 24,
                                                         nk_vec2(280, 160));
    const auto chosen_index = static_cast<std::size_t>(chosen);
    if (chosen_index < count && metadata.lane_profile_choices[chosen_index].enabled)
        state.lane_profile_selected = chosen_index;
    const auto& choice = metadata.lane_profile_choices[state.lane_profile_selected];
    if (!choice.enabled && choice.reason.c_str()) {
        nk_layout_row_dynamic(context, 19, 1);
        muted_label(context, choice.reason.c_str());
    }
    const auto editable = metadata.chart_fields_editable && choice.enabled;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.5f);
    if (!editable) nk_widget_disable_begin(context);
    const auto preview = nk_button_label(context, "Preview key count") != 0 && editable;
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.5f);
    const auto apply_enabled = editable && metadata.lane_profile_apply_enabled;
    if (!apply_enabled) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply key count") != 0 && apply_enabled;
    if (!apply_enabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (metadata.lane_profile_preview_present) {
        nk_layout_row_dynamic(context, 19, 1);
        const auto* message = metadata.lane_profile_preview_message.c_str() ? metadata.lane_profile_preview_message.c_str() : "";
        if (metadata.lane_profile_apply_enabled)
            nk_label(context, message, NK_TEXT_LEFT);
        else
            nk_label_colored(context, message, NK_TEXT_LEFT,
                             wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
    }
    if (preview) set_lane_profile_action(panel.action, metadata, WORM_UI_METADATA_EDIT_PREVIEW_LANE_PROFILE,
                                         state.lane_profile_selected);
    if (apply) set_lane_profile_action(panel.action, metadata, WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE,
                                       state.lane_profile_selected);
}

// Lane table with Preview and Apply. Apply carries no payload: it applies the sealed preview.
static void render_channel_map_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    if (metadata.channel_map_lanes.empty()) return;
    sync_channel_map_buffers(metadata);
    const auto editable = metadata.chart_fields_editable && metadata.channel_map_editable && state.channel_map_buffers_valid;
    nk_layout_row_dynamic(context, 19, 1);
    muted_label(context, "Channel maps (layout segment lanes)");
    for (std::size_t lane = 0; lane < metadata.channel_map_lanes.size(); ++lane)
        render_channel_map_lane_row(context, metadata.channel_map_lanes[lane], lane, editable);
    if (!metadata.channel_map_editable) {
        nk_layout_row_dynamic(context, 19, 1);
        muted_label(context, "Too many lanes to edit here; Apply is disabled.");
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.5f);
    if (!editable) nk_widget_disable_begin(context);
    const auto preview = nk_button_label(context, "Preview") != 0 && editable;
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.5f);
    const auto apply_enabled = editable && metadata.channel_map_apply_enabled;
    if (!apply_enabled) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply") != 0 && apply_enabled;
    if (!apply_enabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (metadata.channel_map_preview_present) {
        nk_layout_row_dynamic(context, 19, 1);
        const auto* message = metadata.channel_map_preview_message.c_str() ? metadata.channel_map_preview_message.c_str() : "";
        if (metadata.channel_map_apply_enabled)
            nk_label(context, message, NK_TEXT_LEFT);
        else
            nk_label_colored(context, message, NK_TEXT_LEFT,
                             wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
    }
    if (preview) set_channel_map_preview_action(panel.action, metadata);
    if (apply && panel.action->metadata_edit == WORM_UI_METADATA_EDIT_NONE) {
        panel.action->metadata_edit = WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP;
        panel.action->metadata_document_revision = metadata.document_revision;
        panel.action->metadata_chart_id = metadata.chart_id.value;
    }
}

static auto batch_chart_selected(std::uint64_t id) -> bool {
    const auto& selected = state.batch_selected_charts;
    return std::find(selected.begin(), selected.end(), id) != selected.end();
}

static void set_batch_chart_selected(std::uint64_t id, bool on) {
    auto& selected = state.batch_selected_charts;
    const auto found = std::find(selected.begin(), selected.end(), id);
    if (on && found == selected.end()) selected.push_back(id);
    if (!on && found != selected.end()) selected.erase(found);
}

// Drops selected ids that no longer exist so a stale selection can never reach the command.
static void prune_batch_selection(const wormedit::ui::metadata_view_t& metadata) {
    std::erase_if(state.batch_selected_charts, [&](std::uint64_t id) {
        return std::ranges::none_of(metadata.chart_choices,
                            [&](const auto& choice) { return choice.chart_id.value == id; });
    });
}

static void set_batch_chart_action(const MetadataPanel& panel, bool rating_edit) {
    auto* action = panel.action;
    const auto& metadata = *panel.metadata;
    if (action->metadata_edit != WORM_UI_METADATA_EDIT_NONE || state.batch_selected_charts.empty())
        return;
    const auto rating = metadata_chart_rating_value(&metadata);
    if (rating_edit && !rating) return;
    state.batch_action_chart_ids = state.batch_selected_charts;
    action->metadata_edit = WORM_UI_METADATA_EDIT_BATCH_CHART;
    action->metadata_document_revision = metadata.document_revision;
    action->metadata_batch_chart_ids = state.batch_action_chart_ids.data();
    action->metadata_batch_chart_count = state.batch_action_chart_ids.size();
    action->metadata_batch_edit_author = !rating_edit;
    action->metadata_batch_edit_rating = rating_edit;
    action->metadata_chart_author = state.chart_author.data();
    action->metadata_has_chart_rating = rating_edit && rating->has_rating;
    if (!action->metadata_has_chart_rating) return;
    const auto wire = state.rational_action_text(rating->numerator, rating->denominator);
    action->metadata_chart_rating_numerator_text = wire.numerator;
    action->metadata_chart_rating_denominator_text = wire.denominator;
}

// Checkbox per difficulty plus buttons that write the Mapper or Rating draft to all checked ones.
static void render_batch_section(const MetadataPanel& panel, bool editable) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    if (metadata.chart_choices.size() < 2) return;
    prune_batch_selection(metadata);
    nk_layout_row_dynamic(context, 19, 1);
    muted_label(context, "Apply Mapper or Rating to several difficulties");
    for (std::size_t index = 0; index < metadata.chart_choices.size(); ++index) {
        const auto& choice = metadata.chart_choices[index];
        const auto selected = batch_chart_selected(choice.chart_id.value);
        nk_layout_row_dynamic(context, 22, 1);
        const wormedit::ui::chrome::Checkbox box{choice.name.c_str() ? choice.name.c_str() : "",
            selected ? wormedit::ui::chrome::CheckState::on : wormedit::ui::chrome::CheckState::off,
            nullptr, editable};
        if (box.render(context) && editable) set_batch_chart_selected(choice.chart_id.value, !selected);
    }
    const auto enabled = editable && !state.batch_selected_charts.empty();
    nk_layout_row_dynamic(context, 26, 2);
    if (!enabled) nk_widget_disable_begin(context);
    const auto author = nk_button_label(context, "Apply Mapper") != 0 && enabled;
    const auto rating = nk_button_label(context, "Apply Rating") != 0 && enabled;
    if (!enabled) nk_widget_disable_end(context);
    if (author) set_batch_chart_action(panel, false);
    if (rating) set_batch_chart_action(panel, true);
}

void render_difficulty_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    char heading[256];
    if (metadata.has_chart)
        std::snprintf(heading, sizeof(heading), "Difficulty - %s",
                      metadata.chart_name.c_str() ? metadata.chart_name.c_str() : "");
    else
        std::snprintf(heading, sizeof(heading), "Difficulty");
    if (!metadata_tree_push(context, NK_TREE_TAB, heading, NK_MAXIMIZED, 1589)) return;
    if (!metadata.has_chart) {
        muted_label(context, "Select a difficulty to edit it.");
        nk_tree_pop(context);
        return;
    }
    const auto editable = metadata.chart_fields_editable && state.chart_buffers_valid;
    metadata_input_row(context, "Name", state.chart_name.data(), state.chart_name.size(), editable);
    metadata_input_row(context, "Mapper", state.chart_author.data(), state.chart_author.size(), editable);
    const auto has_rating = metadata.has_chart_rating;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, has_rating ? 3 : 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Rating", NK_TEXT_LEFT);
    nk_layout_row_push(context, has_rating ? 0.57f : 0.72f);
    if (!editable) nk_widget_disable_begin(context);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.chart_rating_text.data(),
                                         state.chart_rating_text.size(), nk_filter_default);
    if (!editable) nk_widget_disable_end(context);
    if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0)
        wormedit::ui::nuklear::tooltip::text(context, "Number or fraction, e.g. 12, 12.5 or 25/2. Stored exactly.");
    bool clear = false;
    if (has_rating) {
        nk_layout_row_push(context, 0.15f);
        if (!editable) nk_widget_disable_begin(context);
        clear = nk_button_label(context, "Clear") != 0 && editable;
        if (!editable) nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
    const auto rating = metadata_chart_rating_value(&metadata);
    if (!rating) {
        nk_layout_row_dynamic(context, 19, 1);
        nk_label_colored(context, "Enter a number or fraction, e.g. 12 or 25/2.", NK_TEXT_LEFT,
                         wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict));
    }
    render_resolution_row(panel, editable);
    const auto changed = metadata_chart_changed(&metadata) != 0;
    const auto dirty = difficulty_draft_changed(metadata, rating);
    bool revert = false, apply = false;
    render_footer_buttons(context, dirty && editable,
                          changed && editable && rating.has_value(), "Revert", "Apply",
                          revert, apply);
    if (clear && metadata.has_chart_rating) {
        state.chart_rating_text.fill('\0');
        set_metadata_chart_action(panel.action, &metadata, 1);
    }
    if (revert) reset_difficulty_draft(metadata);
    if (apply) set_metadata_chart_action(panel.action, &metadata, 0);
    render_batch_section(panel, editable);
    render_lane_profile_section(panel);
    render_channel_map_section(panel);
    nk_tree_pop(context);
}
