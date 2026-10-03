#include "ui/nuklear/metadata_difficulty_section.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

static auto metadata_chart_rating_value(const WormUiMetadataView* metadata)
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
        std::snprintf(state.action_numerator_text.data(), state.action_numerator_text.size(), "%lld",
                      static_cast<long long>(rating->numerator));
        std::snprintf(state.action_denominator_text.data(), state.action_denominator_text.size(),
                      "%llu", static_cast<unsigned long long>(rating->denominator));
        action->metadata_chart_rating_numerator_text = state.action_numerator_text.data();
        action->metadata_chart_rating_denominator_text = state.action_denominator_text.data();
    }
}

void reset_difficulty_draft(const WormUiMetadataView& metadata) {
    (void)copy_metadata_text(state.chart_name.data(), state.chart_name.size(), metadata.chart_name,
                             metadata.chart_name_bytes);
    (void)copy_metadata_text(state.chart_author.data(), state.chart_author.size(), metadata.chart_author,
                             metadata.chart_author_bytes);
    state.chart_rating_text.fill('\0');
    if (metadata.has_chart_rating) {
        const auto text = worm_ui_nuklear_format_metadata_rational(
            {metadata.chart_rating_numerator, metadata.chart_rating_denominator});
        (void)copy_metadata_text(state.chart_rating_text.data(), state.chart_rating_text.size(),
                                 text.c_str(), text.size());
    }
}

auto difficulty_draft_changed(const WormUiMetadataView& metadata,
                              const std::optional<metadata_rating_value_t>& rating) -> bool {
    const auto source_rating = metadata.has_chart_rating
        ? worm_ui_nuklear_format_metadata_rational(
              {metadata.chart_rating_numerator, metadata.chart_rating_denominator})
        : std::string{};
    const auto text_changed = !metadata.chart_name || !metadata.chart_author ||
        std::strcmp(state.chart_name.data(), metadata.chart_name) != 0 ||
        std::strcmp(state.chart_author.data(), metadata.chart_author) != 0;
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

void render_difficulty_section(const MetadataPanel& panel) {
    auto* context = panel.context;
    const auto& metadata = *panel.metadata;
    char heading[256];
    if (metadata.has_chart)
        std::snprintf(heading, sizeof(heading), "Difficulty - %s",
                      metadata.chart_name ? metadata.chart_name : "");
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
        nk_tooltip(context, "Number or fraction, e.g. 12, 12.5 or 25/2. Stored exactly.");
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
                         wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::status));
    }
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
    nk_tree_pop(context);
}
