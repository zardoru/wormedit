#include "ui/nuklear/storyboard/assets.h"
#include "ui/frontend/storyboard_assets.h"
#include "ui/nuklear/audio/player_preview_texture.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_layout.h"
#include "ui/nuklear/render/vector_icons.h"
#include <wormedit/core/storyboard_canvas.h>
#include <algorithm>

namespace wormedit::ui {
namespace {
using kind_t = storyboard_assets_action_kind_t;
auto capture(const storyboard_view_t& view, kind_t kind, std::string_view image = {}, resource_id_t resource = {})
    -> storyboard_action_t {
    auto action = storyboard_action_t::from(view, storyboard_action_kind_t::assets, view.state.selection.storyboard);
    action.assets = {.kind = kind, .image = std::string(image), .resource = resource,
        .position = {.x = view.canvas.width * 0.5, .y = view.canvas.height * 0.5}, .layer = view.assets.spawn_layer};
    return action;
}
class assets_input_t {
  public:
    void bind(const storyboard_view_t& view, bool available) {
        if (!available || project_ != view.project || session_ != view.session_generation || board_ != view.state.selection.storyboard)
            cancel();
        project_ = view.project; session_ = view.session_generation; board_ = view.state.selection.storyboard;
        if (drag_ && (drag_->revision != view.revision || drag_->prepared_generation != view.canvas.prepared_generation)) drag_.reset();
    }
    void cancel() { drag_.reset(); focused_image_.clear(); focused_resource_ = {}; }
    [[nodiscard]] auto is_dragging() const -> bool { return drag_.has_value(); }
    void begin_frame() { images_.clear(); resources_.clear(); }
    void demand(std::string_view image, resource_id_t resource) {
        if (resource) {
            if (std::ranges::find(resources_, resource) == resources_.end()) resources_.push_back(resource);
            return;
        }
        if (!image.empty()) {
            if (std::ranges::find(images_, image) == images_.end()) images_.emplace_back(image);
        }
    }
    void press(const storyboard_view_t& view, std::string_view image, resource_id_t resource) {
        focused_image_ = image; focused_resource_ = resource;
        drag_ = capture(view, kind_t::add_sprite, image, resource);
    }
    [[nodiscard]] auto drop(nk_context* context, const storyboard_view_t& view,
        const storyboard_canvas_geometry_t& geometry, storyboard_action_t& output, bool available) -> bool {
        const bool owned = is_dragging();
        bind(view, available);
        if (!drag_) return owned;
        if (!nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT)) return true;
        const visual_point_t mouse{context->input.mouse.pos.x, context->input.mouse.pos.y};
        const bool demand_only = output.kind == storyboard_action_kind_t::assets && output.assets.kind == kind_t::set_thumbnail_demand;
        if (geometry.contains(mouse) && (output.kind == storyboard_action_kind_t::none || demand_only)) {
            output = std::move(*drag_);
            output.assets.position = geometry.to_canvas(mouse);
        }
        drag_.reset();
        return true;
    }
    void finish_frame(const storyboard_view_t& view, storyboard_action_t& output) {
        demand(focused_image_, focused_resource_);
        const bool images_changed = std::ranges::any_of(view.assets.images, [&](const auto& row) {
            return row.thumbnail_requested != (std::ranges::find(images_, row.id) != images_.end());
        });
        const bool resources_changed = std::ranges::any_of(view.assets.resources, [&](const auto& row) {
            return row.thumbnail_requested != (std::ranges::find(resources_, row.resource) != resources_.end());
        });
        if ((!images_changed && !resources_changed) || output.kind != storyboard_action_kind_t::none) return;
        output = capture(view, kind_t::set_thumbnail_demand);
        output.assets.images = images_; output.assets.resources = resources_;
    }
  private:
    song_id_t project_;
    storyboard_id_t board_;
    std::uint64_t session_{};
    std::optional<storyboard_action_t> drag_;
    std::string focused_image_;
    resource_id_t focused_resource_;
    std::vector<std::string> images_;
    std::vector<resource_id_t> resources_;
};
assets_input_t input;

auto is_visible(nk_context* context, struct nk_rect bounds) -> bool {
    const auto clip = context->current->buffer.clip;
    return bounds.x < clip.x + clip.w && bounds.x + bounds.w > clip.x &&
        bounds.y < clip.y + clip.h && bounds.y + bounds.h > clip.y;
}
void draw_text(nk_context* context, struct nk_rect bounds, std::string_view text, nk_color tint) {
    nk_draw_text(nk_window_get_canvas(context), bounds, text.data(), static_cast<int>(text.size()),
        context->style.font, nk_rgba(0, 0, 0, 0), tint);
}
void thumbnail(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& root,
    const player_preview_visual_layer_view_t& image, struct nk_rect bounds) {
    nk_fill_rect(nk_window_get_canvas(context), bounds, 2, chrome::color(chrome::UiColor::surface));
    if (!renderer || !image.image_ready) return;
    const auto result = worm_ui_nuklear_draw_player_preview_thumbnail(nk_window_get_canvas(context), bounds,
        renderer, root.project, root.revision, image);
    if (result.failure_count && nk_input_is_mouse_hovering_rect(&context->input, bounds))
        nk_tooltip(context, worm_ui_nuklear_preview_visual_failure_message(result.first_failure));
}
auto begin_row(nk_context* context) -> bool {
    const auto required = 30 + chrome::text_width(context->style.font, "Missing file") +
        2 * chrome::IconButton::width + 3 * context->style.window.spacing.x;
    const bool wrapped = nk_window_get_content_region(context).w < required;
    nk_layout_row_template_begin(context, 34);
    nk_layout_row_template_push_static(context, 30);
    nk_layout_row_template_push_dynamic(context);
    if (!wrapped) chrome::IconButton::layout_columns(context, 2);
    nk_layout_row_template_end(context);
    return wrapped;
}
void begin_row_actions(nk_context* context, bool wrapped) {
    if (!wrapped) return;
    nk_layout_row_template_begin(context, 24);
    nk_layout_row_template_push_dynamic(context);
    chrome::IconButton::layout_columns(context, 2);
    nk_layout_row_template_end(context);
    nk_spacing(context, 1);
}
template<class Row>
void row_content(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& root,
    const Row& row, bool available) {
    const auto image_bounds = nk_widget_bounds(context); nk_spacing(context, 1);
    thumbnail(context, renderer, root, row.thumbnail, image_bounds);
    const auto label = nk_widget_bounds(context); nk_spacing(context, 1);
    draw_text(context, nk_rect(label.x, label.y, label.w, 17), row.label(), chrome::color(chrome::UiColor::text));
    draw_text(context, nk_rect(label.x, label.y + 17, label.w, 17), row.caption, chrome::color(chrome::UiColor::text_muted));
    if (is_visible(context, image_bounds)) input.demand(row.image_id(), row.resource_id());
    const auto target = nk_rect(image_bounds.x, image_bounds.y, label.x + label.w - image_bounds.x, label.h);
    if (available && row.can_drag() && nk_input_is_mouse_hovering_rect(&context->input, target) &&
        nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip) &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) input.press(root, row.image_id(), row.resource_id());
    if (nk_input_is_mouse_hovering_rect(&context->input, target)) {
        const auto status = row.thumbnail.diagnostic_text.empty() ? std::string_view(row.status) : row.thumbnail.diagnostic_text;
        if (!status.empty()) nk_tooltip(context, status.data());
    }
}
void image_row(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& root,
    const storyboard_image_view_t& row, storyboard_action_t& output, bool available) {
    const auto wrapped = begin_row(context);
    row_content(context, renderer, root, row, available);
    begin_row_actions(context, wrapped);
    if (chrome::DrawnIconButton{.draw = vector_icons::plus, .tooltip = "Add sprite from storyboard image",
        .enabled = available && !row.missing}.render(context)) output = capture(root, kind_t::add_sprite, row.id);
    if (chrome::IconButton{.glyph = lucide::Glyph::x,
        .tooltip = row.removal_reason.empty() ? "Remove unused storyboard image" : row.removal_reason.c_str(),
        .enabled = available}.render(context)) output = capture(root, kind_t::remove_image, row.id);
}
void resource_row(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& root,
    const storyboard_resource_image_view_t& row, storyboard_action_t& output, bool available) {
    const auto wrapped = begin_row(context);
    row_content(context, renderer, root, row, available);
    begin_row_actions(context, wrapped);
    if (chrome::DrawnIconButton{.draw = vector_icons::plus, .tooltip = "Add song image to storyboard",
        .enabled = available && row.available && !row.image}.render(context))
        output = capture(root, kind_t::add_image, {}, row.resource);
    if (chrome::IconButton{.glyph = lucide::Glyph::layers, .tooltip = "Add sprite from song image",
        .enabled = available && row.available}.render(context))
        output = capture(root, kind_t::add_sprite, row.image.value_or(""), row.resource);
}
} // namespace
void cancel_storyboard_assets_input() { input.cancel(); }
auto storyboard_assets_drag_active() -> bool { return input.is_dragging(); }
void clear_storyboard_assets_demand(const storyboard_view_t& root, storyboard_action_t& output) {
    input.cancel(); input.begin_frame(); input.finish_frame(root, output);
}
auto handle_storyboard_asset_drop(nk_context* context, const storyboard_view_t& root,
    const storyboard_canvas_geometry_t& geometry, storyboard_action_t& output, bool available) -> bool {
    return context && input.drop(context, root, geometry, output, available);
}
void render_storyboard_assets(nk_context* context, SDL_Renderer* renderer, const storyboard_view_t& root,
    storyboard_action_t& output, bool available) {
    input.bind(root, available); input.begin_frame();
    nk_layout_row_dynamic(context, 26, 1);
    if (chrome::DrawnIconButton{.draw = vector_icons::plus, .tooltip = "Import storyboard image",
        .enabled = available && root.assets.import_available}.render(context)) output = capture(root, kind_t::import_images);
    if (!root.assets.reason.empty()) { nk_layout_row_dynamic(context, 38, 1); nk_label_wrap(context, root.assets.reason.c_str()); }
    if (root.assets.images.empty() && root.assets.resources.empty() && !root.assets.message.empty()) {
        nk_layout_row_dynamic(context, 38, 1); nk_label_wrap(context, root.assets.message.c_str());
    }
    nk_layout_row_dynamic(context, 22, 1); nk_label(context, "Storyboard images", NK_TEXT_LEFT);
    for (const auto& row : root.assets.images) image_row(context, renderer, root, row, output, available && root.assets.editable);
    nk_layout_row_dynamic(context, 22, 1); nk_label(context, "Song images", NK_TEXT_LEFT);
    for (const auto& row : root.assets.resources) resource_row(context, renderer, root, row, output, available && root.assets.editable);
    input.finish_frame(root, output);
}
} // namespace wormedit::ui
