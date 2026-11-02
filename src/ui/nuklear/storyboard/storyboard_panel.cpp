#include "ui/nuklear/storyboard/storyboard_panel.h"
#include "ui/nuklear/storyboard/canvas.h"
#include "ui/nuklear/storyboard/hierarchy.h"
#include "ui/nuklear/storyboard/assets.h"
#include "ui/nuklear/storyboard/inspector.h"
#include "ui/nuklear/storyboard/timeline.h"
#include "ui/nuklear/storyboard/scenario.h"
#include "ui/nuklear/storyboard/transport.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_layout.h"
#include "ui/nuklear/dock.h"
#include <algorithm>
#include <array>
#include <utility>

namespace wormedit::ui {
namespace {
// Three retained layouts belong to this widget, one for each dock width. They
// contain presentation only; document ownership and Detach capture stay in view.
struct ownership_layout_t {
    song_id_t project;
    std::uint64_t session{};
    std::optional<recipe_id_t> owner;
    std::string module;
    const nk_user_font* font{};
    nk_text_width_f measure{};
    void* font_data{};
    float width{}, left{}, height{}, metric{};
    std::vector<std::string> lines;
    nk_scroll scroll{};
    auto matches(const storyboard_view_t& view, const storyboard_entry_view_t& entry,
                 const nk_user_font* current, float available, float dock_left) const -> bool {
        return project == view.project && session == view.session_generation && owner == entry.generator_recipe &&
            module == entry.generator_module && font == current && measure == current->width &&
            font_data == current->userdata.ptr && width == available && left == dock_left && height == current->height &&
            metric == chrome::text_width(current, "Wm0123456789");
    }
    void refill(const storyboard_view_t& view, const storyboard_entry_view_t& entry,
                const nk_user_font* current, float available, float dock_left) {
        project = view.project; session = view.session_generation; owner = entry.generator_recipe;
        module.assign(entry.generator_module); font = current; width = available; left = dock_left; height = current->height;
        measure = current->width; font_data = current->userdata.ptr;
        metric = chrome::text_width(current, "Wm0123456789");
        const std::string_view text = !owner ? "Generator owner unavailable." :
            module.empty() ? std::string_view{"recipe"} : std::string_view{module};
        const auto wrapped = chrome::wrap_text(font, text, width);
        lines.assign(wrapped.begin(), wrapped.end());
        scroll = {};
    }
};
class storyboard_pointer_t {
  public:
    void bind(const storyboard_view_t& view, bool input_available) {
        if (project_ != view.project || session_ != view.session_generation) { dock_ = {}; assets_ = false; }
        if (!input_available) dock_ = {};
        project_ = view.project;
        session_ = view.session_generation;
    }
    auto dock() -> dock_pointer_t& { return dock_; }
    auto assets() -> bool& { return assets_; }
    void cancel() { dock_ = {}; }
    auto ownership(const storyboard_view_t& view, const storyboard_entry_view_t& entry,
                   const nk_user_font* font, float width, float left) -> ownership_layout_t& {
        const auto found = std::ranges::find_if(ownership_, [&](const auto& layout) {
            return layout.matches(view, entry, font, width, left);
        });
        if (found != ownership_.end()) return *found;
        auto& layout = ownership_[next_ownership_++ % ownership_.size()];
        layout.refill(view, entry, font, width, left);
        return layout;
    }
  private:
    song_id_t project_;
    std::uint64_t session_{};
    dock_pointer_t dock_;
    bool assets_{};
    std::array<ownership_layout_t, 3> ownership_;
    std::size_t next_ownership_{};
};
storyboard_pointer_t pointer;

void render_ownership_lines(nk_context* context, const ownership_layout_t& layout) {
    const auto spacing = context->style.window.spacing.y;
    context->style.window.spacing.y = 2;
    for (const auto& line : layout.lines) {
        nk_layout_row_dynamic(context, context->style.font->height, 1);
        nk_label(context, line.c_str(), NK_TEXT_LEFT);
    }
    context->style.window.spacing.y = spacing;
}
void render_ownership_module(nk_context* context, const storyboard_view_t& view,
                             const storyboard_entry_view_t& entry) {
    const auto bounds = nk_window_get_content_region(context);
    const auto width = std::max(1.0f, bounds.w -
        2 * context->style.window.padding.x - context->style.window.scrollbar_size.x -
        2 * context->style.window.group_border - 2 * context->style.text.padding.x);
    auto& layout = pointer.ownership(view, entry, context->style.font, width, bounds.x);
    if (layout.lines.size() <= 4) { render_ownership_lines(context, layout); return; }
    // Long modules scroll independently; the owner and Detach button never scroll
    // out of reach behind an unbounded paragraph.
    nk_layout_row_dynamic(context, 4 * (context->style.font->height + 2) + 2 * context->style.window.padding.y, 1);
    if (nk_group_scrolled_begin(context, &layout.scroll, "Generator module", NK_WINDOW_BORDER)) {
        render_ownership_lines(context, layout);
        nk_group_end(context);
    }
}

void draw_add(nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    const auto x = bounds.x + bounds.w * 0.5f, y = bounds.y + bounds.h * 0.5f;
    nk_stroke_line(canvas, x - 5, y, x + 5, y, 1.5f, tint);
    nk_stroke_line(canvas, x, y - 5, x, y + 5, 1.5f, tint);
}

void render_picker(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action,
                   bool input_available) {
    const auto* selected = view.selected();
    nk_layout_row_template_begin(context, 28);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 150);
    nk_layout_row_template_push_static(context, 112);
    chrome::IconButton::layout_columns(context, 2);
    nk_layout_row_template_end(context);
    const auto* label = selected ? selected->name.c_str() : "Choose storyboard";
    if (chrome::safe_combo_begin_label(context, label, nk_vec2(320, 260))) {
        nk_layout_row_dynamic(context, 26, 1);
        for (const auto& item : view.storyboards) {
            const auto text = item.name + "  [" + std::string(item.badge()) + "]";
            if (nk_combo_item_label(context, text.c_str(), NK_TEXT_LEFT) && input_available)
                action = storyboard_action_t::from(view, storyboard_action_kind_t::select, item.id);
        }
        chrome::safe_combo_end(context);
    }
    nk_label_colored(context, selected ? selected->badge().data() : "No selection", NK_TEXT_LEFT,
                     chrome::color(chrome::UiColor::text_muted));
    if (nk_button_label(context, "Export video…") && input_available) {
        action = storyboard_action_t::from(view, storyboard_action_kind_t::video_export, view.state.selection.storyboard);
        action.video_export.kind = storyboard_video_action_kind_t::open;
    }
    if (chrome::DrawnIconButton{.draw = draw_add, .tooltip = "New storyboard",
            .enabled = input_available && view.create_available}.render(context))
        action = storyboard_action_t::from(view, storyboard_action_kind_t::create);
    if (chrome::IconButton{.glyph = lucide::Glyph::x, .tooltip = "Delete storyboard",
            .enabled = input_available && view.can_delete()}.render(context))
        action = storyboard_action_t::from(view, storyboard_action_kind_t::erase, view.state.selection.storyboard);
}
void render_left(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action,
    bool input_available, SDL_Renderer* renderer) {
    if (!nk_group_begin(context, "Storyboard hierarchy and assets", NK_WINDOW_BORDER)) return;
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Hierarchy") && input_available) pointer.assets() = false;
    if (nk_button_label(context, "Assets") && input_available) {
        cancel_storyboard_hierarchy_input(); pointer.assets() = true;
    }
    if (pointer.assets()) render_storyboard_assets(context, renderer, view, action, input_available);
    else {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Hierarchy / Layers", NK_TEXT_LEFT);
        render_storyboard_hierarchy(context, view, action, input_available);
        clear_storyboard_assets_demand(view, action);
    }
    nk_group_end(context);
}
void render_canvas(nk_context* context, const storyboard_view_t& view, const dock_geometry_t& geometry,
                   float width, float height, SDL_Renderer* renderer, storyboard_action_t& action, bool input_available) {
    nk_layout_space_push(context, nk_rect(0, 8, std::max(1.0f, width - geometry.reserved_width), height - 16));
    if (!nk_group_begin(context, "Storyboard canvas", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) return;
    render_storyboard_canvas(context, renderer, view, action, input_available);
    nk_group_end(context);
}
void render_right(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action, bool input_available) {
    for (const auto& pane : view.state.dock.visible_panels()) {
        if (!pane || *pane != dock_panel_t::inspector) continue;
        render_storyboard_inspector(context, view, action, input_available);
    }
}
void render_main(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action,
                 float height, bool input_available, SDL_Renderer* renderer) {
    if (!nk_group_begin(context, "Storyboard canvas and dock", NK_WINDOW_NO_SCROLLBAR)) return;
    const auto bounds = nk_window_get_content_region(context);
    std::array<bool, dock_panel_count> available{};
    available[static_cast<std::size_t>(dock_panel_t::inspector)] = true;
    const auto geometry = dock_geometry_t::from(bounds.w, height, view.state.dock, available);
    auto dock = view.state.dock;
    nk_layout_space_begin(context, NK_STATIC, height, geometry.widget_count() + 1);
    render_canvas(context, view, geometry, bounds.w, height, renderer, action, input_available);
    for (const auto& pane : geometry.panes) {
        if (!pane) continue;
        const auto& r = pane->bounds;
        nk_layout_space_push(context, nk_rect(static_cast<float>(r.left), static_cast<float>(r.top),
            static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top)));
        if (!nk_group_begin(context, "Storyboard inspector", NK_WINDOW_BORDER)) continue;
        render_right(context, view, action, input_available);
        nk_group_end(context);
    }
    render_dock_controls(context, geometry, dock, pointer.dock(), available, input_available);
    nk_layout_space_end(context);
    nk_group_end(context);
    if (dock != view.state.dock && action.kind == storyboard_action_kind_t::none) {
        action = storyboard_action_t::from(view, storyboard_action_kind_t::set_dock);
        action.dock = std::move(dock);
    }
}
void render_body(nk_context* context, const storyboard_view_t& view, storyboard_action_t& action,
                 bool input_available, SDL_Renderer* renderer) {
    const auto bounds = nk_window_get_content_region(context);
    const auto remaining = std::max(160.0f, bounds.y + bounds.h - nk_widget_position(context).y - 8);
    const auto timeline_height = std::clamp(remaining * 0.28f, 190.0f, 220.0f);
    const auto height = std::max(80.0f, remaining - timeline_height - 8);
    const auto left = std::clamp(bounds.w * 0.2f, 160.0f, 240.0f);
    nk_layout_row_begin(context, NK_STATIC, height, 2);
    nk_layout_row_push(context, left);
    render_left(context, view, action, input_available, renderer);
    nk_layout_row_push(context, std::max(1.0f, bounds.w - left - 8));
    render_main(context, view, action, height, input_available, renderer);
    nk_layout_row_end(context);
    nk_layout_row_dynamic(context, timeline_height, 1);
    if (nk_group_begin(context, "Storyboard timeline", NK_WINDOW_BORDER)) {
        render_storyboard_timeline(context, view, action, input_available);
        nk_group_end(context);
    }
}
} // namespace

auto storyboard_assets_tab_active() -> bool { return pointer.assets(); }
void reset_storyboard_assets_tab() { pointer.assets() = false; }
void cancel_storyboard_panel_pointer() {
    pointer.cancel();
    cancel_storyboard_assets_input();
    cancel_storyboard_canvas_pointer();
    cancel_storyboard_hierarchy_input();
    cancel_storyboard_inspector_input();
    cancel_storyboard_timeline_input();
}

auto drain_storyboard_panel_cancel(storyboard_action_t& output) -> bool {
    const bool new_focus = output.kind == storyboard_action_kind_t::select_nodes ||
        (output.kind == storyboard_action_kind_t::inspector && output.inspector.phase == storyboard_inspector_phase_t::focus) ||
        (output.kind == storyboard_action_kind_t::timeline && output.timeline.kind == storyboard_timeline_action_kind_t::select_keys);
    const bool presentation_demand = output.kind == storyboard_action_kind_t::assets &&
        output.assets.kind == storyboard_assets_action_kind_t::set_thumbnail_demand;
    if (output.kind != storyboard_action_kind_t::none && !new_focus && !presentation_demand) return false;
    storyboard_action_t terminal;
    if (!drain_storyboard_canvas_cancel(terminal) && !drain_storyboard_inspector_cancel(terminal) &&
        !drain_storyboard_timeline_cancel(terminal)) return false;
    output = std::move(terminal);
    return true;
}

void render_storyboard_generator_ownership(nk_context* context, const storyboard_view_t& view,
    storyboard_action_t& action, bool input_available) {
    const auto* selected = view.selected();
    if (!selected || !selected->generator_owned) return;
    if (selected->generator_recipe) {
        nk_layout_row_dynamic(context, context->style.font->height, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Generated by #%llu",
            static_cast<unsigned long long>(selected->generator_recipe->value));
        render_ownership_module(context, view, *selected);
    } else render_ownership_module(context, view, *selected);
    nk_layout_row_dynamic(context, 26, 1);
    const bool available = input_available && selected->kind == storyboard_kind_t::osbx &&
        selected->generator_recipe && *selected->generator_recipe;
    if (!available) nk_widget_disable_begin(context);
    const bool pressed = nk_button_label(context, "Detach") != 0;
    if (!available) nk_widget_disable_end(context);
    if (pressed && available && action.kind == storyboard_action_kind_t::none) {
        action = storyboard_action_t::from(view, storyboard_action_kind_t::detach_generator, selected->id);
        action.expected_owner = *selected->generator_recipe;
    }
}

void render_storyboard_panel(nk_context* context, const storyboard_view_t& view,
                             storyboard_action_t& action, SDL_Renderer* renderer, bool input_available) {
    if (!context || !view.visible) return;
    pointer.bind(view, input_available);
    if (!input_available) nk_widget_disable_begin(context);
    render_picker(context, view, action, input_available);
    if (!input_available) nk_widget_disable_end(context);
    render_storyboard_scenario(context, view, action, input_available && !storyboard_lua_editor_active());
    if (const auto* selected = view.selected(); selected && !selected->is_editable()) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, selected->generator_owned ? "Generated storyboard — detach before editing."
            : "Imported OSB storyboard — read-only.", NK_TEXT_LEFT);
    }
    storyboard_transport_action_t transport;
    if (render_storyboard_transport(context, view.transport, transport, input_available) &&
        action.kind == storyboard_action_kind_t::none) {
        action = storyboard_action_t::from(view, storyboard_action_kind_t::transport, view.state.selection.storyboard);
        action.transport = transport;
    }
    render_body(context, view, action, input_available && !storyboard_lua_editor_active(), renderer);
    if (input_available) render_storyboard_lua_editor(context, view, action);
}
} // namespace wormedit::ui
