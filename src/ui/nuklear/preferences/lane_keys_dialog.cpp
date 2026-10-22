#include "ui/nuklear/preferences/lane_keys_dialog.h"
#include "ui/nuklear/chrome/dialog.h"
#include "ui/nuklear/render/theme.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace wormedit::ui::nuklear {
namespace {
chrome::Dialog dialog;
void emit(lane_keys_action_t& action, const lane_keys_view_t& view, lane_keys_action_kind_t kind) {
    if (action.kind == lane_keys_action_kind_t::none)
        action = {.kind = kind, .generation = view.generation};
}
void text(nk_context* context, struct nk_rect bounds, std::string_view label, nk_color background, nk_color color) {
    nk_draw_text(nk_window_get_canvas(context), bounds, label.data(), static_cast<int>(label.size()),
                 context->style.font, background, color);
}
struct key_tile_t {
    std::string_view key, label;
    nk_color fill;
    bool selected{};
    std::optional<nk_color> footer;
    const char* tooltip{};
    [[nodiscard]] auto render(nk_context*) const -> bool;
};
auto key_tile_t::render(nk_context* context) const -> bool {
    const chrome::NativeIconCell cell{context, selected, true, tooltip};
    const auto bounds = cell.bounds();
    auto* canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 5, fill);
    nk_stroke_rect(canvas, bounds, 5, selected ? 2 : 1,
                   selected ? nk_rgb(218, 169, 32) : nk_rgb(112, 106, 92));
    const auto inset = 8.0f;
    text(context, nk_rect(bounds.x + inset, bounds.y + 10, bounds.w - 2 * inset, 20),
         key, fill, nk_rgb(239, 238, 231));
    text(context, nk_rect(bounds.x + inset, bounds.y + 34, bounds.w - 2 * inset, 18),
         label, fill, nk_rgb(187, 185, 179));
    if (footer) nk_fill_rect(canvas,
        nk_rect(bounds.x + 1, bounds.y + bounds.h - 27, bounds.w - 2, 26), 3,
        *footer);
    return cell.is_clicked();
}
void stage(nk_context* context, const lane_keys_view_t& view, lane_keys_action_t& action,
            bool right, float height) {
    const char* id = right ? "Lane keys P2" : "Lane keys P1";
    if (!nk_group_begin(context, id, NK_WINDOW_NO_SCROLLBAR)) return;
    const auto count = std::ranges::count(view.lanes, right, &lane_key_lane_view_t::right_stage);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label_colored(context, right ? "P2 · RIGHT STAGE" : "P1 · LEFT STAGE", NK_TEXT_LEFT,
                     right ? nk_rgb(247, 167, 66) : nk_rgb(89, 178, 255));
    const auto columns = std::min<std::ptrdiff_t>(12, std::max<std::ptrdiff_t>(1, count));
    const auto rows = std::max<std::ptrdiff_t>(1, (count + columns - 1) / columns);
    nk_layout_row_dynamic(context, (height - 32) / static_cast<float>(rows), static_cast<int>(columns));
    for (const auto& lane : view.lanes) {
        if (lane.right_stage != right) continue;
        const auto fill = right ? nk_rgb(111, 62, 24) : nk_rgb(33, 57, 113);
        const auto footer = theme::compose_key_color(static_cast<std::uint8_t>(lane.color), lane.id.value).note_border;
        const auto tooltip = "Bind " + lane.name;
        const key_tile_t tile{
            .key = view.capturing && view.is_selected(lane.id) ? std::string_view{"…"} : lane.key,
            .label = lane.short_name, .fill = fill, .selected = view.is_selected(lane.id),
            .footer = footer, .tooltip = tooltip.c_str()};
        if (!tile.render(context)) continue;
        emit(action, view, lane_keys_action_kind_t::select_lane);
        action.lane = lane.id;
    }
    nk_group_end(context);
}
void lanes(nk_context* context, const lane_keys_view_t& view, lane_keys_action_t& action, float height) {
    const bool split = std::ranges::any_of(view.lanes, &lane_key_lane_view_t::right_stage);
    nk_layout_row_dynamic(context, height, split ? 2 : 1);
    stage(context, view, action, false, height);
    if (split) stage(context, view, action, true, height);
}
void keyboard(nk_context* context, const lane_keys_view_t& view, lane_keys_action_t& action, float height) {
    const auto width = std::min(70.0f, (nk_window_get_content_region_size(context).x - 60) / 10);
    const auto selected = std::ranges::find(view.lanes, view.selected, &lane_key_lane_view_t::id);
    for (unsigned row = 0; row < 4; ++row) {
        const auto count = std::ranges::count(view.keyboard, row, &lane_key_keyboard_view_t::row);
        nk_layout_row_begin(context, NK_STATIC, height, static_cast<int>(count + 1));
        nk_layout_row_push(context, row * 16.0f + 1);
        nk_spacing(context, 1);
        for (const auto& key : view.keyboard) {
            if (key.row != row) continue;
            nk_layout_row_push(context, width);
            const auto fill = key.lanes.empty() ? nk_rgb(26, 27, 35)
                : key.right_stage ? nk_rgb(139, 77, 25) : nk_rgb(34, 68, 163);
            const auto waiting = view.capturing && selected != view.lanes.end() && selected->usage == key.usage;
            const key_tile_t tile{.key = key.key, .label = key.lanes, .fill = fill,
                                  .selected = waiting, .tooltip = key.lanes.c_str()};
            if (!tile.render(context) || !view.selected) continue;
            emit(action, view, lane_keys_action_kind_t::bind);
            action.usage = key.usage;
        }
        nk_layout_row_end(context);
    }
}
void presets(nk_context* context, const lane_keys_view_t& view, lane_keys_action_t& action) {
    nk_layout_row_dynamic(context, 28, 2);
    nk_label(context, view.layout.c_str(), NK_TEXT_LEFT);
    constexpr std::array<std::pair<lane_key_preset_t, const char*>, 3> presets{{
        {lane_key_preset_t::wormedit, "Wormedit default"},
        {lane_key_preset_t::ibmsc, "iBMSC interleaved (DP)"},
        {lane_key_preset_t::custom, "Custom"}}};
    std::array<const char*, 3> labels{};
    std::array<lane_key_preset_t, 3> values{};
    int count{}, current{};
    for (const auto& [value, label] : presets) {
        if (value == lane_key_preset_t::ibmsc && !view.can_interleave) continue;
        if (value == view.preset) current = count;
        labels[static_cast<std::size_t>(count)] = label;
        values[static_cast<std::size_t>(count++)] = value;
    }
    const auto selected = chrome::safe_combo(context, labels.data(), count, current, 24, nk_vec2(300, 130));
    if (selected == current) return;
    emit(action, view, lane_keys_action_kind_t::preset);
    action.preset = values[static_cast<std::size_t>(selected)];
}
void footer(nk_context* context, const lane_keys_view_t& view, lane_keys_action_t& action) {
    chrome::dialog_footer_begin(dialog, context, 3, true, 150);
    if (chrome::dialog_button(dialog, context, "Reset to preset", chrome::DialogButtonRole::secondary))
        emit(action, view, lane_keys_action_kind_t::reset);
    nk_spacing(context, 1);
    if (chrome::dialog_button(dialog, context, "Cancel", chrome::DialogButtonRole::secondary))
        emit(action, view, lane_keys_action_kind_t::cancel);
    if (chrome::dialog_button(dialog, context, "Apply", chrome::DialogButtonRole::primary, view.can_apply))
        emit(action, view, lane_keys_action_kind_t::apply);
}
void body(nk_context* context, const lane_keys_view_t& view, lane_keys_action_t& action, bool compact) {
    presets(context, view, action);
    nk_layout_row_dynamic(context, 26, 1);
    nk_label(context, "Lanes  Click a lane, then press its key. Keys are physical positions on every keyboard language.", NK_TEXT_LEFT);
    lanes(context, view, action, compact ? 100.0f : 144.0f);
    nk_layout_row_dynamic(context, 30, 1);
    nk_label_colored(context, view.capturing
        ? "Press a key. Escape keeps the current key; Backspace unbinds the lane."
        : "Select a lane to bind it. Space, Tab, arrows and editing keys are reserved.",
        NK_TEXT_LEFT, view.capturing ? nk_rgb(244, 204, 75) : nk_rgb(186, 184, 176));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Keyboard · bound keys show their lane", NK_TEXT_LEFT);
    keyboard(context, view, action, compact ? 48.0f : 62.0f);
    nk_layout_row_dynamic(context, 28, 1);
    nk_label_colored(context, view.diagnostic.c_str(), NK_TEXT_LEFT,
                     view.can_apply ? nk_rgb(117, 209, 139) : nk_rgb(246, 161, 80));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Saved to your preferences · every chart with this layout uses these keys", NK_TEXT_LEFT);
}
}
void render_lane_keys_dialog(nk_context* context, const lane_keys_view_t& view,
                             lane_keys_action_t& action, int width, int height) {
    if (!view.visible) return;
    const bool compact = height < 800;
    if (chrome::dialog_begin(dialog, context, "Lane keys", "Lane keys", {
            .closable = true, .width = std::min(1168.0f, width - 40.0f),
            .area = nk_vec2(static_cast<float>(width), static_cast<float>(height)),
            .escape_cancels = !view.capturing, .enter_confirms = false,
            .resizable = true, .height = std::min(780.0f, height - 40.0f),
            .minimum_size = nk_vec2(600, 450), .movable = true})) {
        const auto content = nk_window_get_content_region(context);
        const auto bottom = std::min(content.y + content.h,
            dialog.bounds.y + dialog.bounds.h - chrome::dialog_padding);
        const auto body_height = std::max(1.0f, bottom - nk_widget_bounds(context).y -
            chrome::dialog_button_height - chrome::dialog_spacing);
        nk_layout_row_dynamic(context, body_height, 1);
        if (nk_group_begin(context, "Lane key bindings", 0)) {
            body(context, view, action, compact);
            nk_group_end(context);
        }
        footer(context, view, action);
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel)
        emit(action, view, lane_keys_action_kind_t::cancel);
}
}
