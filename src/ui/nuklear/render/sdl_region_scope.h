#pragma once

#include <wormedit/nuklear_config.h>

namespace wormedit::ui::nuklear {
enum class sdl_region_t {
    header, status, toolbar, inspector, sounds, images, measures, layers, markers
};

// Widgets and input always execute. These barriers only delimit the converted
// drawing that the SDL backend may retain after exact geometry comparison.
auto begin_sdl_region(nk_context*, sdl_region_t, nk_color background) -> bool;
void end_sdl_region(nk_context*, sdl_region_t);
void convert_sdl_region_marker(void*, short, short, unsigned short, unsigned short, nk_handle);

class sdl_region_scope_t {
  public:
    sdl_region_scope_t(nk_context* context, sdl_region_t region)
        : context_(context), region_(region) {
        const auto& item = context->style.window.fixed_background;
        active_ = item.type == NK_STYLE_ITEM_COLOR && item.data.color.a == 255 &&
                  begin_sdl_region(context, region, item.data.color);
    }
    ~sdl_region_scope_t() {
        if (active_) end_sdl_region(context_, region_);
    }
    sdl_region_scope_t(const sdl_region_scope_t&) = delete;
    auto operator=(const sdl_region_scope_t&) -> sdl_region_scope_t& = delete;

  private:
    nk_context* context_;
    sdl_region_t region_;
    bool active_{};
};
} // namespace wormedit::ui::nuklear
