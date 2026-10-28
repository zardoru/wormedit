#include "ui/nuklear/compose/compose_recipe_form.h"

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose/compose_panel_internal.h"

#include "ui/nuklear/chrome/file_dialog_slot.h"
#include <SDL3/SDL.h>
#include <wormedit/core/numeric_text.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace {

namespace chrome = wormedit::ui::chrome;
namespace metrics = wormedit::ui::metrics;
using form_t = wormedit::ui::timing_recipe_form_t;
using rational_text_t = wormedit::ui::timing_recipe_rational_text_t;

constexpr float row_height = metrics::control_compact;
constexpr nk_flags edit_flags = static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER;
constexpr float label_share = 0.34f;

// What the controls did this frame: changed the candidate, and/or settled it so it can be applied.
struct edit_t {
    bool changed{};
    bool settled{};
    auto operator|=(edit_t other) -> edit_t& {
        changed |= other.changed;
        settled |= other.settled;
        return *this;
    }
    auto operator|(edit_t other) const -> edit_t { return edit_t{*this} |= other; }
    // A pick (a button, a dropdown, a segment) changes and settles at once.
    static auto picked(bool chosen) -> edit_t { return {.changed = chosen, .settled = chosen}; }
};

// Decimal and fraction characters; the parser owns the syntax and the core validator the ranges.
nk_bool number_filter(const struct nk_text_edit*, nk_rune rune) {
    return (rune >= '0' && rune <= '9') || rune == '-' || rune == '.' || rune == '/' ||
           rune == 'e' || rune == 'E';
}

// How far one drag step moves a value; Shift takes the fine step.
struct number_step_t {
    wormedit::signed_rational_t coarse;
    wormedit::signed_rational_t fine;
};
constexpr number_step_t value_step{{1, 20}, {1, 100}};
constexpr number_step_t span_step{{1, 16}, {1, 64}};
constexpr number_step_t bend_step{{1, 4}, {1, 20}};
constexpr number_step_t length_step{{1, 4}, {1, 16}};
constexpr number_step_t tolerance_step{{1, 1000}, {1, 10000}};

void hint_label(nk_context* context, const char* label, const char* tooltip = nullptr) {
    const auto bounds = nk_widget_bounds(context);
    nk_label(context, label, NK_TEXT_LEFT);
    if (tooltip && wormedit::ui::nuklear::tooltip::hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(context, tooltip);
}

auto number_field(nk_context* context, char* text, int capacity, const char* tooltip)
    -> chrome::NumberField::Result {
    return chrome::NumberField{.text = text, .capacity = capacity, .filter = number_filter,
                               .drag = &state.recipe_editor.recipe_number_drag,
                               .tooltip = tooltip}.render(context);
}

auto rational_input(nk_context* context, rational_text_t& value, number_step_t step,
                    const char* tooltip) -> edit_t {
    const auto result = number_field(context, value.text, sizeof(value.text), tooltip);
    edit_t edit{.changed = result.edited, .settled = result.settled};
    if (result.steps == 0) return edit;
    const auto current = wormedit::parse_exact_rational(value.text).value_or(wormedit::signed_rational_t{});
    if (const auto next = wormedit::offset_by_steps(current, result.fine ? step.fine : step.coarse,
                                                    result.steps)) {
        std::snprintf(value.text, sizeof(value.text), "%s", wormedit::format_exact_rational(*next).c_str());
        edit.changed = true;
    }
    return edit;
}

// A whole number of at least `minimum`; dragging moves it by `step` (by one with Shift).
auto count_input(nk_context* context, char* buffer, int capacity, std::uint64_t minimum,
                 std::uint64_t step, const char* tooltip) -> edit_t {
    const auto result = number_field(context, buffer, capacity, tooltip);
    edit_t edit{.changed = result.edited, .settled = result.settled};
    if (result.steps == 0) return edit;
    const auto current = static_cast<long double>(
        wormedit::parse_integer<std::uint64_t>(buffer).value_or(minimum));
    const auto moved = current + static_cast<long double>(result.steps) *
        static_cast<long double>(result.fine ? 1 : step);
    const auto next = moved <= static_cast<long double>(minimum) ? minimum
        : static_cast<std::uint64_t>(std::min(moved, 1.0e18L));
    std::snprintf(buffer, static_cast<std::size_t>(capacity), "%llu", static_cast<unsigned long long>(next));
    edit.changed = true;
    return edit;
}

void label_row(nk_context* context, const char* label, const char* tooltip) {
    // Nuklear reads the ratios while the row fills, after this function returns.
    static constexpr float shares[] = {label_share, 1.0f - label_share};
    nk_layout_row(context, NK_DYNAMIC, row_height, 2, shares);
    hint_label(context, label, tooltip);
}

auto labeled_rational(nk_context* context, const char* label, const char* tooltip,
                      rational_text_t& value, number_step_t step) -> edit_t {
    label_row(context, label, tooltip);
    return rational_input(context, value, step, tooltip);
}

// "label [a] word [b]": two values that read as one phrase, such as "From 1 to 2".
auto phrase_rationals(nk_context* context, const char* label, const char* word, const char* tooltip,
                      rational_text_t& first, rational_text_t& second, number_step_t step) -> edit_t {
    static constexpr float shares[] = {label_share, 0.29f, 0.08f, 0.29f};
    nk_layout_row(context, NK_DYNAMIC, row_height, 4, shares);
    hint_label(context, label, tooltip);
    auto edit = rational_input(context, first, step, tooltip);
    nk_label(context, word, NK_TEXT_CENTERED);
    edit |= rational_input(context, second, step, tooltip);
    return edit;
}

void set_rational(rational_text_t& value, const char* text) {
    std::snprintf(value.text, sizeof(value.text), "%s", text);
}

// ---- Header, order and commands --------------------------------------------------------------

using recipe_command_t = wormedit::ui::compose_recipe_command_t;

void queue(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
           WormUiComposeActionKind kind) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = kind;
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_recipe_id.value = view.recipe_form.recipe_id.value;
    action.compose_recipe_form = state.recipe_editor.recipe_edit_form;
}

void queue_recipe_input(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                        wormedit::recipe_id_t id, WormUiComposeInputKind kind, recipe_command_t command = {}) {
    append_compose_input(&action, kind, 0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, id.value, 0, false,
                         wormedit::timing_lane_kind_t::scroll, view.chart_id.value, view.document_revision);
    if (!action.compose_inputs.empty())
        action.compose_inputs.back().span_edge = static_cast<std::uint8_t>(command);
}

void queue_command(const ComposeInspectorContext& inspector, recipe_command_t command) {
    const auto& view = *inspector.view;
    queue_recipe_input(view, *inspector.action, view.recipe_form.recipe_id,
                       WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, command);
}

void render_more_menu(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    if (!chrome::safe_combo_begin_label(context, "More", nk_vec2(280.0f, 3.0f * (row_height + 4.0f) + 16.0f)))
        return;
    nk_layout_row_dynamic(context, metrics::menu_item_height, 1);
    if (chrome::menu_item(context, "Duplicate as independent", nullptr, false, true))
        queue_command(inspector, recipe_command_t::duplicate);
    if (chrome::menu_item(context, "Materialize into events", nullptr, false, true))
        queue_command(inspector, recipe_command_t::materialize);
    if (chrome::menu_item(context, "Delete", nullptr, false, true))
        queue_command(inspector, recipe_command_t::remove);
    chrome::safe_combo_end(context);
}

// The recipe's name, whether it is on, and the commands that act on the whole recipe.
void render_header(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    // Keep the popup anchored inside the dock even when the dock reaches the window edge.
    static constexpr float shares[] = {0.28f, 0.5f, 0.22f};
    nk_layout_row(context, NK_DYNAMIC, row_height, 3, shares);
    render_more_menu(inspector);
    nk_text(context, view.recipe_form.name.data(), static_cast<int>(view.recipe_form.name.size()), NK_TEXT_LEFT);
    const chrome::Checkbox enabled{.label = "On",
        .state = view.recipe_form.enabled ? chrome::CheckState::on : chrome::CheckState::off,
        .tooltip = "A recipe that is off keeps its settings but writes nothing"};
    if (enabled.render(context)) queue_command(inspector, recipe_command_t::toggle_enabled);
    if (view.recipe_form.definition_id) {
        nk_layout_row_dynamic(context, 2 * row_height, 1);
        nk_label_wrap(context, "Shared definition: edits affect every use");
    }
}

auto overlapping_count(const wormedit::ui::compose_view_t& view) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count_if(view.recipe_entries,
        &wormedit::ui::compose_recipe_entry_view_t::overlaps_selection));
}

void render_order_buttons(const ComposeInspectorContext& inspector,
                          const wormedit::ui::compose_recipe_entry_view_t* earlier,
                          const wormedit::ui::compose_recipe_entry_view_t* later) {
    static constexpr float shares[] = {label_share, (1.0f - label_share) * 0.5f, (1.0f - label_share) * 0.5f};
    nk_layout_row(inspector.context, NK_DYNAMIC, row_height, 3, shares);
    nk_spacing(inspector.context, 1);
    if (chrome::Button{.label = "Earlier", .tooltip = "Move this recipe earlier in the order",
                       .enabled = earlier != nullptr}.render(inspector.context))
        queue_command(inspector, recipe_command_t::earlier);
    if (chrome::Button{.label = "Later", .tooltip = "Move this recipe later in the order",
                       .enabled = later != nullptr}.render(inspector.context))
        queue_command(inspector, recipe_command_t::later);
}

// Only shown when other recipes share rows with this one: they multiply together, and the
// order decides which one sets a SPEEDS transition.
void render_overlap_order(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    if (overlapping_count(view) < 2) return;
    static std::vector<const char*> names;
    static std::vector<const wormedit::ui::compose_recipe_entry_view_t*> entries;
    names.clear();
    entries.clear();
    int current = 0;
    for (const auto& entry : view.recipe_entries) {
        if (!entry.overlaps_selection) continue;
        if (entry.id == view.recipe_form.recipe_id) current = static_cast<int>(entries.size());
        names.push_back(entry.label.c_str());
        entries.push_back(&entry);
    }
    label_row(context, "Order",
              "Recipes sharing these rows multiply together, in this order; for Speeds the last one sets the transition");
    const auto choice = chrome::safe_combo(context, names.data(), static_cast<int>(names.size()), current,
                                           static_cast<int>(row_height), nk_vec2(nk_widget_width(context), 200));
    if (choice != current)
        queue_recipe_input(view, *inspector.action, entries[static_cast<std::size_t>(choice)]->id,
                           WORM_UI_COMPOSE_INPUT_RECIPE_SELECT);
    const auto* selected = entries[static_cast<std::size_t>(current)];
    const auto* first = view.recipe_entries.data();
    const auto* last = first + view.recipe_entries.size() - 1;
    render_order_buttons(inspector, selected != first ? selected - 1 : nullptr,
                         selected != last ? selected + 1 : nullptr);
}

// ---- Output, curve and shape -------------------------------------------------------------------

// Segment order; the enum's values are stored numbers, not positions.
constexpr std::array<wormedit::sv_recipe_output_t, 2> outputs{wormedit::sv_recipe_output_t::sv,
                                                              wormedit::sv_recipe_output_t::speeds};
constexpr std::array<const char*, 2> output_labels{"SV", "Speeds"};
constexpr std::array<const char*, 5> curve_labels{"Constant", "Linear", "Exponential", "Bump", "Lua function"};
static_assert(curve_labels.size() == static_cast<std::size_t>(wormedit::sv_recipe_curve_t::lua_expression) + 1);

auto render_output_and_curve(nk_context* context, form_t& form) -> edit_t {
    label_row(context, "Output",
              "SV scales the chart's scroll speed (osu! SV, StepMania #SCROLLS); Speeds writes StepMania #SPEEDS");
    const auto current = static_cast<int>(std::ranges::find(outputs, form.output) - outputs.begin());
    const auto output = chrome::Segmented{.labels = output_labels.data(), .count = static_cast<int>(output_labels.size()),
        .selected = current, .focused = &state.recipe_editor.recipe_output_focused}.render(context);
    auto edit = edit_t::picked(output != current);
    form.output = outputs[static_cast<std::size_t>(std::clamp(output, 0, static_cast<int>(outputs.size()) - 1))];
    label_row(context, "Curve", "How the value changes across the span");
    const auto curve = chrome::safe_combo(context, curve_labels.data(), static_cast<int>(curve_labels.size()),
        static_cast<int>(form.curve), static_cast<int>(row_height), nk_vec2(nk_widget_width(context), 200));
    edit |= edit_t::picked(curve != static_cast<int>(form.curve));
    form.curve = static_cast<wormedit::sv_recipe_curve_t>(curve);
    return edit;
}

// The whole span's values, left to right, scaled to their own range, with that range labeled.
void render_shape(nk_context* context, const wormedit::ui::compose_recipe_form_view_t& view) {
    if (view.shape_points.size() < 2) return;
    nk_layout_row_dynamic(context, 44.0f, 1);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    auto* canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, metrics::radius_control, chrome::color(chrome::UiColor::surface));
    const auto plot = nk_rect(bounds.x + 40.0f, bounds.y + 6.0f, std::max(0.0f, bounds.w - 46.0f), bounds.h - 12.0f);
    const auto line = view.previewing ? nk_rgb(255, 196, 98) : nk_rgb(214, 226, 255);
    for (std::size_t index = 1; index < view.shape_points.size(); ++index) {
        const auto& from = view.shape_points[index - 1];
        const auto& to = view.shape_points[index];
        nk_stroke_line(canvas, plot.x + static_cast<float>(from.position) * plot.w,
                       plot.y + (1.0f - static_cast<float>(from.value)) * plot.h,
                       plot.x + static_cast<float>(to.position) * plot.w,
                       plot.y + (1.0f - static_cast<float>(to.value)) * plot.h, 1.5f, line);
    }
    char top[32]{}, bottom[32]{};
    std::snprintf(top, sizeof(top), "%.3g", view.shape_maximum);
    std::snprintf(bottom, sizeof(bottom), "%.3g", view.shape_minimum);
    const auto* font = context->style.font;
    const auto muted = chrome::color(chrome::UiColor::text_muted);
    const auto transparent = nk_rgba(0, 0, 0, 0);
    nk_draw_text(canvas, nk_rect(bounds.x + 4.0f, plot.y - 4.0f, 34.0f, font->height), top,
                 static_cast<int>(std::strlen(top)), font, transparent, muted);
    if (view.shape_maximum != view.shape_minimum)
        nk_draw_text(canvas, nk_rect(bounds.x + 4.0f, plot.y + plot.h - font->height + 4.0f, 34.0f, font->height),
                     bottom, static_cast<int>(std::strlen(bottom)), font, transparent, muted);
}

auto curve_values(nk_context* context, form_t& form) -> edit_t {
    switch (form.curve) {
    case wormedit::sv_recipe_curve_t::constant:
        return labeled_rational(context, "Value", "The multiplier across the whole span", form.value_from, value_step);
    case wormedit::sv_recipe_curve_t::linear:
        return phrase_rationals(context, "From", "to", "The value at the start and at the end of the span",
                                form.value_from, form.value_to, value_step);
    case wormedit::sv_recipe_curve_t::exponential: {
        auto edit = phrase_rationals(context, "From", "to", "The value at the start and at the end of the span",
                                     form.value_from, form.value_to, value_step);
        edit |= labeled_rational(context, "Bend",
            "0 is a straight line; above 0 starts slow and ends fast, below 0 the reverse", form.shape, bend_step);
        return edit;
    }
    case wormedit::sv_recipe_curve_t::bump: {
        auto edit = labeled_rational(context, "Bump value", "The value inside the bump", form.bump_x, value_step);
        edit |= labeled_rational(context, "Base value", "The value everywhere else", form.bump_y, value_step);
        edit |= phrase_rationals(context, "Bump at", "for",
            "Where the bump starts and how long it lasts, as fractions of the span (0 to 1)",
            form.bump_start, form.bump_length, span_step);
        return edit;
    }
    case wormedit::sv_recipe_curve_t::lua_expression:
        break;
    }
    return {};
}

// ---- Points ------------------------------------------------------------------------------------

enum class points_mode_t { count, grid, automatic };
constexpr std::array<const char*, 3> points_labels{"Count", "Grid", "Auto"};

auto points_mode(const form_t& form) -> points_mode_t {
    if (form.sampling_mode == wormedit::sv_recipe_sampling_mode_t::adaptive) return points_mode_t::automatic;
    return form.fixed_extent == wormedit::ui::timing_recipe_fixed_extent_t::step_rows ? points_mode_t::grid
                                                                                     : points_mode_t::count;
}

void set_points_mode(form_t& form, points_mode_t mode) {
    form.sampling_mode = mode == points_mode_t::automatic ? wormedit::sv_recipe_sampling_mode_t::adaptive
                                                          : wormedit::sv_recipe_sampling_mode_t::fixed;
    form.fixed_extent = mode == points_mode_t::grid ? wormedit::ui::timing_recipe_fixed_extent_t::step_rows
                                                    : wormedit::ui::timing_recipe_fixed_extent_t::point_count;
}

// "Every 1/16": the chart's snap grids, plus the stored step when it is none of them.
auto grid_points(nk_context* context, const wormedit::ui::compose_recipe_form_view_t& view, form_t& form) -> edit_t {
    label_row(context, "Every", "One point on each line of this grid");
    static std::vector<std::array<char, 32>> labels;
    static std::vector<const char*> names;
    static std::vector<std::uint64_t> rows;
    labels.clear();
    rows.clear();
    const auto current_rows = wormedit::parse_integer<std::uint64_t>(form.fixed_step_rows).value_or(0);
    for (const auto& option : view.spacing_options) {
        auto& label = labels.emplace_back();
        std::snprintf(label.data(), label.size(), "1/%llu", static_cast<unsigned long long>(option.denominator));
        rows.push_back(option.rows);
    }
    if (std::ranges::find(rows, current_rows) == rows.end()) {
        auto& label = labels.emplace_back();
        std::snprintf(label.data(), label.size(), "%llu rows", static_cast<unsigned long long>(current_rows));
        rows.push_back(current_rows);
    }
    names.clear();
    for (const auto& label : labels) names.push_back(label.data());
    const auto current = static_cast<int>(std::ranges::find(rows, current_rows) - rows.begin());
    const auto choice = chrome::safe_combo(context, names.data(), static_cast<int>(names.size()), current,
                                           static_cast<int>(row_height), nk_vec2(nk_widget_width(context), 200));
    if (choice == current) return {};
    std::snprintf(form.fixed_step_rows, sizeof(form.fixed_step_rows), "%llu",
                  static_cast<unsigned long long>(rows[static_cast<std::size_t>(choice)]));
    return edit_t::picked(true);
}

// Switching to Grid starts on a sixteenth (or the first grid) unless the stored step is a grid.
void default_grid_step(const wormedit::ui::compose_recipe_form_view_t& view, form_t& form) {
    const auto rows = wormedit::parse_integer<std::uint64_t>(form.fixed_step_rows).value_or(0);
    const auto& options = view.spacing_options;
    if (options.empty() || std::ranges::find(options, rows, &wormedit::ui::compose_recipe_spacing_view_t::rows) != options.end())
        return;
    const auto sixteenth = std::ranges::find(options, 16, &wormedit::ui::compose_recipe_spacing_view_t::denominator);
    const auto& chosen = sixteenth != options.end() ? *sixteenth : options.front();
    std::snprintf(form.fixed_step_rows, sizeof(form.fixed_step_rows), "%llu", static_cast<unsigned long long>(chosen.rows));
}

auto automatic_points(nk_context* context, form_t& form) -> edit_t {
    auto edit = labeled_rational(context, "Max error",
        "Points are added where the curve bends until the result stays this close to it",
        form.tolerance, tolerance_step);
    label_row(context, "Max points", "The most points Auto may use");
    edit |= count_input(context, form.adaptive_max_points, sizeof(form.adaptive_max_points), 2, 8,
                        "The most points Auto may use");
    return edit;
}

auto points_values(nk_context* context, const wormedit::ui::compose_recipe_form_view_t& view, form_t& form) -> edit_t {
    label_row(context, "Points",
              "Count: evenly spaced points. Grid: one per grid line. Auto: as many as the curve needs");
    const auto mode = static_cast<int>(points_mode(form));
    const auto chosen = chrome::Segmented{.labels = points_labels.data(), .count = static_cast<int>(points_labels.size()),
        .selected = mode, .focused = &state.recipe_editor.recipe_points_focused}.render(context);
    auto edit = edit_t::picked(chosen != mode);
    set_points_mode(form, static_cast<points_mode_t>(chosen));
    if (edit.changed && points_mode(form) == points_mode_t::grid) default_grid_step(view, form);
    switch (points_mode(form)) {
    case points_mode_t::count:
        label_row(context, "Count", "Evenly spaced points across the span, ends included");
        edit |= count_input(context, form.fixed_point_count, sizeof(form.fixed_point_count), 2, 1,
                            "Evenly spaced points across the span, ends included");
        break;
    case points_mode_t::grid: edit |= grid_points(context, view, form); break;
    case points_mode_t::automatic: edit |= automatic_points(context, form); break;
    }
    return edit;
}

constexpr std::array<const char*, 2> transition_units{"Beats", "Seconds"};

auto transition_values(nk_context* context, form_t& form) -> edit_t {
    if (form.output != wormedit::sv_recipe_output_t::speeds) return {};
    static constexpr float shares[] = {label_share, 0.30f, 1.0f - label_share - 0.30f};
    nk_layout_row(context, NK_DYNAMIC, row_height, 3, shares);
    hint_label(context, "Transition", "How long each speed change takes to reach its value");
    auto edit = rational_input(context, form.transition_duration, length_step,
                               "How long each speed change takes to reach its value");
    const auto unit = chrome::Segmented{.labels = transition_units.data(), .count = 2,
        .selected = form.transition_is_seconds ? 1 : 0}.render(context);
    edit |= edit_t::picked(unit != (form.transition_is_seconds ? 1 : 0));
    form.transition_is_seconds = unit == 1;
    return edit;
}

// ---- Adjust (postprocess stages) ---------------------------------------------------------------

using postprocess_kind_t = wormedit::sv_recipe_postprocess_kind_t;
constexpr std::array<const char*, 3> normalization_labels{"Displacement", "Distance", "Average"};
constexpr std::array<const char*, 2> balance_labels{"Inside", "Outside"};

auto stage_label(postprocess_kind_t kind) -> const char* {
    switch (kind) {
    case postprocess_kind_t::multiply: return "Multiply by";
    case postprocess_kind_t::normalize: return "Normalize to";
    case postprocess_kind_t::balance_bump: return "Balance to";
    }
    return "";
}

auto stage_tooltip(postprocess_kind_t kind) -> const char* {
    switch (kind) {
    case postprocess_kind_t::multiply: return "Scales every value by this factor";
    case postprocess_kind_t::normalize:
        return "Scales the curve so the span travels this many times as far as it would without the recipe";
    case postprocess_kind_t::balance_bump:
        return "Sets the inside or outside value so the span travels this many times as far as it would without the recipe";
    }
    return "";
}

// The stage's setting under its value: what Normalize keeps, or which side Balance solves.
auto stage_option(nk_context* context, wormedit::ui::timing_recipe_postprocess_form_t& stage) -> edit_t {
    if (stage.kind == postprocess_kind_t::multiply) return {};
    const bool normalize = stage.kind == postprocess_kind_t::normalize;
    label_row(context, normalize ? "Measure" : "Solve",
              normalize ? "Distance counts reversed travel as forward; Displacement and Average keep its sign"
                        : "Which value Balance changes");
    if (normalize) {
        const auto mode = chrome::safe_combo(context, normalization_labels.data(), 3, static_cast<int>(stage.mode),
                                             static_cast<int>(row_height), nk_vec2(nk_widget_width(context), 120));
        const auto edit = edit_t::picked(mode != static_cast<int>(stage.mode));
        stage.mode = static_cast<wormedit::sv_recipe_normalization_t>(mode);
        return edit;
    }
    // Solving x changes the bump's own value (inside); y the base value (outside).
    const auto current = stage.solve == wormedit::sv_recipe_bump_solve_t::x ? 0 : 1;
    const auto side = chrome::Segmented{.labels = balance_labels.data(), .count = 2, .selected = current}.render(context);
    stage.solve = side == 0 ? wormedit::sv_recipe_bump_solve_t::x : wormedit::sv_recipe_bump_solve_t::y;
    return edit_t::picked(side != current);
}

// One stage: its value with a remove button, then its option. True in `removed` when removed.
auto stage_fields(nk_context* context, wormedit::ui::timing_recipe_postprocess_form_t& stage, bool& removed) -> edit_t {
    nk_layout_row_template_begin(context, row_height);
    nk_layout_row_template_push_static(context, 90.0f);
    nk_layout_row_template_push_dynamic(context);
    chrome::IconButton::layout_columns(context, 1, 28.0f);
    nk_layout_row_template_end(context);
    hint_label(context, stage_label(stage.kind), stage_tooltip(stage.kind));
    auto edit = rational_input(context, stage.value, value_step, stage_tooltip(stage.kind));
    removed = chrome::IconButton{.glyph = wormedit::ui::lucide::Glyph::x, .tooltip = "Remove this adjustment"}.render(context);
    edit |= edit_t::picked(removed);
    if (!removed) edit |= stage_option(context, stage);
    return edit;
}

auto add_stage_menu(nk_context* context, form_t& form) -> edit_t {
    if (form.postprocess.size() >= wormedit::maximum_sv_recipe_postprocess_stages) return {};
    const bool bump = form.curve == wormedit::sv_recipe_curve_t::bump;
    if (!chrome::safe_combo_begin_label(context, "Add…", nk_vec2(nk_widget_width(context), (bump ? 3.0f : 2.0f) * 28.0f + 16.0f)))
        return {};
    nk_layout_row_dynamic(context, metrics::menu_item_height, 1);
    std::optional<postprocess_kind_t> added;
    if (chrome::menu_item(context, "Multiply", nullptr, false, true)) added = postprocess_kind_t::multiply;
    if (chrome::menu_item(context, "Normalize", nullptr, false, true)) added = postprocess_kind_t::normalize;
    if (bump && chrome::menu_item(context, "Balance bump", nullptr, false, true)) added = postprocess_kind_t::balance_bump;
    chrome::safe_combo_end(context);
    if (!added) return {};
    auto& stage = form.postprocess.emplace_back(wormedit::ui::timing_recipe_postprocess_form_t{.kind = *added});
    set_rational(stage.value, "1");
    return edit_t::picked(true);
}

auto adjust_values(nk_context* context, form_t& form) -> edit_t {
    label_row(context, "Adjust", "Steps applied to the sampled values, top to bottom");
    auto edit = add_stage_menu(context, form);
    for (std::size_t index = 0; index < form.postprocess.size();) {
        bool removed = false;
        edit |= stage_fields(context, form.postprocess[index], removed);
        if (removed) form.postprocess.erase(form.postprocess.begin() + static_cast<std::ptrdiff_t>(index));
        else ++index;
    }
    return edit;
}

// ---- Impulses ------------------------------------------------------------------------------------

auto impulse_row(nk_context* context, wormedit::ui::timing_recipe_impulse_form_t& impulse, bool& removed) -> edit_t {
    nk_layout_row_template_begin(context, row_height);
    nk_layout_row_template_push_static(context, 22.0f);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 34.0f);
    nk_layout_row_template_push_dynamic(context);
    chrome::IconButton::layout_columns(context, 1, 28.0f);
    nk_layout_row_template_end(context);
    hint_label(context, "At", "Where the impulse sits, as a fraction of the span (0 to 1)");
    auto edit = rational_input(context, impulse.at, span_step, "Where the impulse sits, as a fraction of the span (0 to 1)");
    hint_label(context, "Area", "The signed jump in distance, in spans at value 1");
    edit |= rational_input(context, impulse.area, value_step, "The signed jump in distance, in spans at value 1");
    removed = chrome::IconButton{.glyph = wormedit::ui::lucide::Glyph::x, .tooltip = "Remove this impulse"}.render(context);
    return edit | edit_t::picked(removed);
}

auto impulse_values(nk_context* context, form_t& form) -> edit_t {
    char title[48]{};
    std::snprintf(title, sizeof(title), "Impulses (%zu)", form.impulses.size());
    static constexpr char key[] = "recipe-impulses";
    if (!nk_tree_push_hashed(context, NK_TREE_TAB, title, NK_MINIMIZED, key, sizeof(key) - 1, 0)) return {};
    edit_t edit;
    for (std::size_t index = 0; index < form.impulses.size();) {
        bool removed = false;
        edit |= impulse_row(context, form.impulses[index], removed);
        if (removed) form.impulses.erase(form.impulses.begin() + static_cast<std::ptrdiff_t>(index));
        else ++index;
    }
    edit |= labeled_rational(context, "Box width (ms)", "Each impulse is written as a short box this wide",
                             form.finite_box_width_milliseconds, length_step);
    nk_layout_row_dynamic(context, row_height, 1);
    if (chrome::Button{.label = "Add impulse",
                       .enabled = form.impulses.size() < wormedit::maximum_sv_recipe_impulses}.render(context)) {
        auto& impulse = form.impulses.emplace_back();
        set_rational(impulse.at, "0");
        set_rational(impulse.area, "1");
        edit |= edit_t::picked(true);
    }
    nk_tree_pop(context);
    return edit;
}

// ---- Lua ---------------------------------------------------------------------------------------

auto text_field(nk_context* context, const char* label, char* buffer, int capacity) -> edit_t {
    label_row(context, label, nullptr);
    const std::string before{buffer};
    const auto flags = chrome::worm_ui_nuklear_text_field(context, edit_flags, buffer,
                                                          capacity, nk_filter_default);
    return {.changed = before != buffer, .settled = (flags & (NK_EDIT_COMMITED | NK_EDIT_DEACTIVATED)) != 0};
}

wormedit::ui::file_dialog_slot_t module_picker;
struct module_picker_origin_t { std::uint64_t project{}, chart{}, revision{}, recipe{}; };
module_picker_origin_t module_picker_origin;
std::string module_picker_error;

void SDLCALL module_picked(void* userdata, const char* const* paths, int) {
    const auto generation = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userdata));
    const bool cancelled = paths && !paths[0];
    module_picker.complete(generation, paths && paths[0] ? std::string{paths[0]} : std::string{},
        paths ? std::string{} : std::string{SDL_GetError()}, cancelled);
}

void open_module_picker(const wormedit::ui::compose_view_t& view) {
    const auto generation = module_picker.begin();
    if (!generation) return;
    module_picker_origin = {view.project_id.value, view.chart_id.value, view.document_revision,
                            view.recipe_form.recipe_id.value};
    static constexpr SDL_DialogFileFilter filter{"Lua modules", "lua"};
    SDL_ShowOpenFileDialog(module_picked, reinterpret_cast<void*>(static_cast<std::uintptr_t>(*generation)),
        SDL_GetKeyboardFocus(), &filter, 1, nullptr, false);
}

auto take_picked_module(const wormedit::ui::compose_view_t& view, form_t& form) -> edit_t {
    auto picked = module_picker.take();
    if (!picked || module_picker_origin.project != view.project_id.value ||
        module_picker_origin.chart != view.chart_id.value || module_picker_origin.revision != view.document_revision ||
        module_picker_origin.recipe != view.recipe_form.recipe_id.value)
        return {};
    if (!*picked) {
        module_picker_error = picked->error().message;
        return {};
    }
    if (picked->value().canceled) return {};
    std::snprintf(form.lua_source_reference, sizeof(form.lua_source_reference), "%s", picked->value().path.c_str());
    form.lua_reload = true;
    module_picker_error.clear();
    return edit_t::picked(true);
}

auto module_file(nk_context* context, const wormedit::ui::compose_view_t& view, form_t& form) -> edit_t {
    auto edit = text_field(context, "Module file", form.lua_source_reference, sizeof(form.lua_source_reference));
    nk_layout_row_dynamic(context, row_height, 2);
    if (chrome::Button{.label = "Choose module…"}.render(context)) open_module_picker(view);
    if (chrome::Button{.label = "Reload", .tooltip = "Read the module file again"}.render(context)) {
        form.lua_reload = true;
        edit |= edit_t::picked(true);
    }
    edit |= take_picked_module(view, form);
    if (!module_picker_error.empty()) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_wrap(context, module_picker_error.c_str());
    }
    return edit;
}

auto lua_parameter_row(nk_context* context, wormedit::ui::timing_recipe_lua_parameter_form_t& parameter,
                       bool& removed) -> edit_t {
    nk_layout_row_template_begin(context, row_height);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_dynamic(context);
    chrome::IconButton::layout_columns(context, 1, 28.0f);
    nk_layout_row_template_end(context);
    const std::string before{parameter.name};
    const auto flags = chrome::worm_ui_nuklear_text_field(context, edit_flags, parameter.name,
                                                          sizeof(parameter.name), nk_filter_default);
    edit_t edit{.changed = before != parameter.name,
                .settled = (flags & (NK_EDIT_COMMITED | NK_EDIT_DEACTIVATED)) != 0};
    edit |= rational_input(context, parameter.value, value_step, "The parameter's value");
    removed = chrome::IconButton{.glyph = wormedit::ui::lucide::Glyph::x, .tooltip = "Remove this parameter"}.render(context);
    return edit | edit_t::picked(removed);
}

auto lua_parameter_fields(nk_context* context, form_t& form) -> edit_t {
    edit_t edit;
    for (std::size_t index = 0; index < form.lua_parameters.size();) {
        bool removed = false;
        edit |= lua_parameter_row(context, form.lua_parameters[index], removed);
        if (removed) form.lua_parameters.erase(form.lua_parameters.begin() + static_cast<std::ptrdiff_t>(index));
        else ++index;
    }
    nk_layout_row_dynamic(context, row_height, 1);
    if (chrome::Button{.label = "Add parameter", .enabled = form.lua_parameters.size() < 64}.render(context)) {
        auto& parameter = form.lua_parameters.emplace_back();
        std::snprintf(parameter.name, sizeof(parameter.name), "parameter%zu", form.lua_parameters.size());
        set_rational(parameter.value, "0");
        edit |= edit_t::picked(true);
    }
    return edit;
}

auto lua_cycle_fields(nk_context* context, form_t& form) -> edit_t {
    edit_t edit;
    for (auto& count : form.lua_cycles) {
        int value = static_cast<int>(count);
        nk_layout_row_dynamic(context, row_height, 1);
        nk_property_int(context, "Cycle emissions", 1, &value, 4096, 1, 1);
        edit |= edit_t::picked(value != static_cast<int>(count));
        count = static_cast<std::uint32_t>(value);
    }
    nk_layout_row_dynamic(context, row_height, 2);
    if (chrome::Button{.label = "Add cycle", .enabled = form.lua_cycles.size() < 4096}.render(context)) {
        form.lua_cycles.push_back(1);
        edit |= edit_t::picked(true);
    }
    if (chrome::Button{.label = "Remove cycle", .enabled = !form.lua_cycles.empty()}.render(context)) {
        form.lua_cycles.pop_back();
        edit |= edit_t::picked(true);
    }
    return edit;
}

constexpr std::array<const char*, 2> lua_mode_labels{"Pure", "Ordered"};

auto lua_fields(nk_context* context, const wormedit::ui::compose_view_t& view, form_t& form) -> edit_t {
    auto edit = module_file(context, view, form);
    edit |= text_field(context, "Entry point", form.lua_entry_point, sizeof(form.lua_entry_point));
    label_row(context, "Seed", "Seeds the module's random numbers");
    edit |= count_input(context, form.lua_seed, sizeof(form.lua_seed), 0, 1, "Seeds the module's random numbers");
    label_row(context, "Execution", "Pure: each point on its own. Ordered: points in sequence, in cycles");
    const auto mode = static_cast<int>(form.lua_mode);
    const auto chosen = chrome::Segmented{.labels = lua_mode_labels.data(), .count = 2, .selected = mode}.render(context);
    edit |= edit_t::picked(chosen != mode);
    form.lua_mode = static_cast<wormedit::sv_recipe_lua_mode_t>(chosen);
    edit |= lua_parameter_fields(context, form);
    if (form.lua_mode == wormedit::sv_recipe_lua_mode_t::ordered) edit |= lua_cycle_fields(context, form);
    return edit;
}

// ---- Feedback and commit -----------------------------------------------------------------------

// Keep the edit buffers on the selected recipe: reload them when the selection changes, and when
// the document moved on after an apply or while nothing was being edited.
void synchronize_buffers(const wormedit::ui::compose_view_t& view) {
    auto& editor = state.recipe_editor;
    const auto& stored = view.recipe_form;
    const bool selection_changed = editor.recipe_edit_id != stored.recipe_id.value;
    const bool revision_changed = editor.recipe_edit_revision != view.document_revision;
    if (selection_changed || (revision_changed && (!editor.recipe_edit_dirty || editor.recipe_edit_apply_pending))) {
        editor.recipe_edit_form = stored.form;
        editor.recipe_edit_dirty = false;
        editor.recipe_edit_apply_pending = false;
        editor.recipe_edit_commit_pending = false;
    }
    editor.recipe_edit_id = stored.recipe_id.value;
    editor.recipe_edit_revision = view.document_revision;
}

void revert(const ComposeInspectorContext& inspector) {
    auto& editor = state.recipe_editor;
    editor.recipe_edit_form = inspector.view->recipe_form.form;
    editor.recipe_edit_dirty = false;
    editor.recipe_edit_apply_pending = false;
    editor.recipe_edit_commit_pending = false;
    queue(*inspector.view, *inspector.action, WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW);
}

// Edits preview as they happen and apply, as one undo step, once they settle and the preview of
// the settled values is ready.
void commit_edit(const ComposeInspectorContext& inspector, edit_t edit) {
    const auto& view = *inspector.view;
    auto& editor = state.recipe_editor;
    if (edit.changed) {
        editor.recipe_edit_dirty = true;
        queue(view, *inspector.action, WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM);
        editor.recipe_edit_form.lua_reload = false;
    }
    if (edit.settled && editor.recipe_edit_dirty) editor.recipe_edit_commit_pending = true;
    if (!editor.recipe_edit_commit_pending || edit.changed || view.recipe_form.evaluation_busy ||
        editor.recipe_number_drag.dragging || inspector.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    editor.recipe_edit_commit_pending = false;
    editor.recipe_edit_apply_pending = true;
    queue(view, *inspector.action, WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM);
}

// One line saying where the edit stands, with Revert while unapplied values are rejected.
void render_status(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    const auto& editor = state.recipe_editor;
    const bool rejected = !view.recipe_form.status_text.empty();
    if (view.recipe_form.evaluation_busy || editor.recipe_edit_commit_pending) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, editor.recipe_edit_commit_pending ? "Applying…" : "Evaluating…", NK_TEXT_LEFT,
                         chrome::color(chrome::UiColor::text_muted));
    } else if (rejected && editor.recipe_edit_dirty) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_colored_wrap(context, view.recipe_form.status_text.data(), wormedit::ui::chrome::destructive_text());
        nk_layout_row_dynamic(context, row_height, 2);
        nk_spacing(context, 1);
        if (chrome::Button{.label = "Revert", .tooltip = "Go back to the stored values"}.render(context))
            revert(inspector);
    } else if (editor.recipe_edit_dirty) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, "Press Enter or leave the field to apply", NK_TEXT_LEFT,
                         chrome::color(chrome::UiColor::text_muted));
    }
}

void render_lua_preview(nk_context* context, const wormedit::ui::compose_view_t& view, bool lua) {
    if (!lua || view.recipe_form.sampled_points.empty()) return;
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%zu points; %zu after combining", view.recipe_form.sampled_points.size(),
              view.recipe_form.composed_point_count);
    if (nk_tree_push(context, NK_TREE_TAB, "Sampled points", NK_MINIMIZED)) {
        for (const auto& point : view.recipe_form.sampled_points) {
            nk_layout_row_dynamic(context, 18, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Row %llu: %.8g", static_cast<unsigned long long>(point.absolute_row),
                      point.value);
        }
        nk_tree_pop(context);
    }
}

void render_impulse_preview(nk_context* context, const wormedit::ui::compose_view_t& view) {
    if (view.recipe_form.impulse_boxes.empty()) return;
    if (nk_tree_push(context, NK_TREE_TAB, "Compiled impulse boxes", NK_MINIMIZED)) {
        for (const auto& box : view.recipe_form.impulse_boxes) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Rows %llu to %llu (%.6g ms)",
                static_cast<unsigned long long>(box.entry_row), static_cast<unsigned long long>(box.exit_row),
                box.width_milliseconds);
            nk_layout_row_dynamic(context, 20, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Area %.8g; error %.3g", box.area, box.area_error);
        }
        nk_tree_pop(context);
    }
}

auto render_values(const ComposeInspectorContext& inspector, form_t& form) -> edit_t {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto edit = render_output_and_curve(context, form);
    render_shape(context, view.recipe_form);
    const bool lua = form.curve == wormedit::sv_recipe_curve_t::lua_expression;
    edit |= lua ? lua_fields(context, view, form) : curve_values(context, form);
    edit |= points_values(context, view.recipe_form, form);
    edit |= transition_values(context, form);
    edit |= adjust_values(context, form);
    edit |= impulse_values(context, form);
    return edit;
}

} // namespace

void render_compose_recipe_form(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    if (!view.recipe_form.visible) {
        state.recipe_editor.recipe_edit_id = 0;
        state.recipe_editor.recipe_edit_dirty = false;
        state.recipe_editor.recipe_edit_commit_pending = false;
        return;
    }
    synchronize_buffers(view);
    render_header(inspector);
    render_overlap_order(inspector);
    auto& form = state.recipe_editor.recipe_edit_form;
    commit_edit(inspector, render_values(inspector, form));
    render_status(inspector);
    render_lua_preview(context, view, form.curve == wormedit::sv_recipe_curve_t::lua_expression);
    render_impulse_preview(context, view);
}
