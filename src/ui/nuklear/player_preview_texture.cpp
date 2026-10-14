#include "ui/nuklear/player_preview_texture.h"
#include "ui/nuklear/color.h"
#include "ui/nuklear/image_quad.h"

#include "wormedit/nuklear_config.h"

#include <wormedit/core/visual_layout.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_render.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <vector>

namespace {

constexpr std::size_t maximum_texture_entries = 64;
constexpr std::size_t maximum_texture_bytes = 256U * 1024U * 1024U;

struct preview_texture_t {
    std::uint64_t project_id{};
    std::uint64_t document_revision{};
    std::uint64_t resource_id{};
    std::uint64_t content_id{};
    bool black_key{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t rgba_bytes{};
    SDL_Texture* texture{};
    std::uint64_t last_use{};
};

std::array<preview_texture_t, maximum_texture_entries> textures{};
std::size_t texture_count{};
std::size_t texture_bytes{};
std::uint64_t texture_use_sequence{};

struct texture_result_t {
    SDL_Texture* texture{};
    worm_ui_nuklear_preview_visual_failure_t failure{
        worm_ui_nuklear_preview_visual_failure_t::none};
};

auto next_texture_use() -> std::uint64_t {
    ++texture_use_sequence;
    if (texture_use_sequence == 0) {
        texture_use_sequence = 1;
        for (std::size_t index = 0; index < texture_count; ++index) {
            textures[index].last_use = 0;
        }
    }
    return texture_use_sequence;
}

void erase_texture(std::size_t index) {
    if (index >= texture_count) {
        return;
    }
    auto& entry = textures[index];
    if (entry.texture) {
        SDL_DestroyTexture(entry.texture);
    }
    texture_bytes -= entry.rgba_bytes;
    std::move(textures.begin() + static_cast<std::ptrdiff_t>(index + 1),
              textures.begin() + static_cast<std::ptrdiff_t>(texture_count),
              textures.begin() + static_cast<std::ptrdiff_t>(index));
    --texture_count;
    textures[texture_count] = {};
}

void evict_texture() {
    if (texture_count == 0) {
        return;
    }
    const auto end = textures.begin() + static_cast<std::ptrdiff_t>(texture_count);
    const auto oldest =
        std::min_element(textures.begin(), end, [](const auto& left, const auto& right) {
            return left.last_use < right.last_use;
        });
    erase_texture(static_cast<std::size_t>(oldest - textures.begin()));
}

template <typename ImageView>
auto preview_upload_pixels(const ImageView& layer, bool black_key,
                           std::vector<std::uint8_t>& keyed_pixels) -> const std::uint8_t* {
    if (!black_key) return layer.rgba_pixels;
    try {
        keyed_pixels.assign(layer.rgba_pixels, layer.rgba_pixels + layer.rgba_byte_count);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    for (std::size_t offset = 0; offset + 3 < keyed_pixels.size(); offset += 4)
        if (keyed_pixels[offset] == 0 && keyed_pixels[offset + 1] == 0 && keyed_pixels[offset + 2] == 0)
            keyed_pixels[offset + 3] = 0;
    return keyed_pixels.data();
}

auto bga_composite_stage(WormUiPlayerPreviewVisualKind kind) -> int {
    switch (kind) {
    case WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER: return 1;
    case WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER2: return 2;
    case WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD: return 3;
    default: return 0;
    }
}

void order_bga_composite_layers(std::span<const WormUiPlayerPreviewVisualLayerView*> ordered) {
    std::array<const WormUiPlayerPreviewVisualLayerView*, WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY> bga{};
    std::size_t count{};
    for (const auto* layer : ordered)
        if (layer->kind != WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD) bga[count++] = layer;
    std::stable_sort(bga.begin(), bga.begin() + static_cast<std::ptrdiff_t>(count),
        [](const auto* left, const auto* right) { return bga_composite_stage(left->kind) < bga_composite_stage(right->kind); });
    std::size_t index{};
    for (auto& layer : ordered)
        if (layer->kind != WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD) layer = bga[index++];
}

template <typename ImageView>
auto texture_for(SDL_Renderer* renderer, const WormUiPlayerPreviewView& view,
                 const ImageView& layer, bool black_key = false) -> texture_result_t {
    if (!renderer || !layer.image_ready || view.project_id == 0 || view.document_revision == 0 ||
        layer.resource_id == 0 || layer.rgba_byte_count == 0 || !layer.rgba_pixels ||
        layer.rgba_byte_count > maximum_texture_bytes) {
        return {.failure = worm_ui_nuklear_preview_visual_failure_t::invalid_image};
    }
    const auto end = textures.begin() + static_cast<std::ptrdiff_t>(texture_count);
    const auto found = std::find_if(textures.begin(), end, [&](const auto& entry) {
        return entry.project_id == view.project_id &&
               entry.document_revision == view.document_revision &&
               entry.resource_id == layer.resource_id &&
               entry.content_id == layer.image_content_id && entry.width == layer.image_width &&
               entry.height == layer.image_height && entry.black_key == black_key;
    });
    if (found != end) {
        found->last_use = next_texture_use();
        return {.texture = found->texture};
    }
    while (texture_count != 0 && (texture_count >= maximum_texture_entries ||
                                  texture_bytes > maximum_texture_bytes - layer.rgba_byte_count)) {
        evict_texture();
    }

    std::vector<std::uint8_t> keyed_pixels;
    const auto* upload_pixels = preview_upload_pixels(layer, black_key, keyed_pixels);
    if (!upload_pixels)
        return {.failure = worm_ui_nuklear_preview_visual_failure_t::upload_out_of_memory};
    SDL_ClearError();
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                      static_cast<int>(layer.image_width),
                                      static_cast<int>(layer.image_height));
    if (!texture) {
        const auto* error = SDL_GetError();
        const auto out_of_memory = error && std::strstr(error, "Out of memory");
        return {.failure = out_of_memory
                               ? worm_ui_nuklear_preview_visual_failure_t::upload_out_of_memory
                               : worm_ui_nuklear_preview_visual_failure_t::texture_allocation};
    }
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
        !SDL_UpdateTexture(texture, nullptr, upload_pixels,
                           static_cast<int>(layer.image_width * 4U))) {
        const auto* error = SDL_GetError();
        const auto out_of_memory = error && std::strstr(error, "Out of memory");
        SDL_DestroyTexture(texture);
        return {.failure = out_of_memory
                               ? worm_ui_nuklear_preview_visual_failure_t::upload_out_of_memory
                               : worm_ui_nuklear_preview_visual_failure_t::upload_rejected};
    }
    float uploaded_width{};
    float uploaded_height{};
    if (!SDL_GetTextureSize(texture, &uploaded_width, &uploaded_height) ||
        uploaded_width != static_cast<float>(layer.image_width) ||
        uploaded_height != static_cast<float>(layer.image_height)) {
        SDL_DestroyTexture(texture);
        return {.failure = worm_ui_nuklear_preview_visual_failure_t::upload_size_mismatch};
    }

    textures[texture_count++] = {.project_id = view.project_id,
                                 .document_revision = view.document_revision,
                                 .resource_id = layer.resource_id,
                                 .content_id = layer.image_content_id,
                                 .black_key = black_key,
                                 .width = layer.image_width,
                                 .height = layer.image_height,
                                 .rgba_bytes = layer.rgba_byte_count,
                                 .texture = texture,
                                 .last_use = next_texture_use()};
    texture_bytes += layer.rgba_byte_count;
    return {.texture = texture};
}

auto visual_fit_policy(WormUiVisualFitPolicy fit) -> wormedit::visual_fit_policy_t {
    switch (fit) {
    case WORM_UI_VISUAL_FIT_STRETCH:
        return wormedit::visual_fit_policy_t::stretch;
    case WORM_UI_VISUAL_FIT_COVER:
        return wormedit::visual_fit_policy_t::cover;
    case WORM_UI_VISUAL_FIT_CONTAIN:
        return wormedit::visual_fit_policy_t::contain;
    }
    return wormedit::visual_fit_policy_t::contain;
}


} // namespace

const char* worm_ui_nuklear_preview_visual_failure_message(
    worm_ui_nuklear_preview_visual_failure_t failure) noexcept {
    switch (failure) {
    case worm_ui_nuklear_preview_visual_failure_t::none:
        return "";
    case worm_ui_nuklear_preview_visual_failure_t::invalid_image:
        return "invalid decoded image";
    case worm_ui_nuklear_preview_visual_failure_t::texture_allocation:
        return "texture allocation failed";
    case worm_ui_nuklear_preview_visual_failure_t::upload_out_of_memory:
        return "GPU memory exhausted";
    case worm_ui_nuklear_preview_visual_failure_t::upload_rejected:
        return "GPU rejected texture upload";
    case worm_ui_nuklear_preview_visual_failure_t::upload_size_mismatch:
        return "uploaded texture size mismatch";
    case worm_ui_nuklear_preview_visual_failure_t::texture_identifier_unsupported:
        return "texture identifier is unsupported";
    case worm_ui_nuklear_preview_visual_failure_t::layout_invalid:
        return "image layout is invalid";
    case worm_ui_nuklear_preview_visual_failure_t::command_capacity:
        return "preview draw-command capacity exceeded";
    }
    return "unknown renderer failure";
}

worm_ui_nuklear_preview_visual_result_t worm_ui_nuklear_draw_player_preview_visual_layers(
    struct nk_command_buffer* canvas, struct nk_rect viewport, struct SDL_Renderer* renderer,
    const WormUiPlayerPreviewView* view) {
    worm_ui_nuklear_preview_visual_result_t result;
    if (!canvas || !view) {
        return result;
    }
    std::array<const WormUiPlayerPreviewVisualLayerView*,
               WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY>
        ordered{};
    std::size_t count{};
    for (std::size_t index = 0; index < view->visual_layer_count; ++index) {
        if (view->visual_layers[index].image_ready) {
            ordered[count++] = &view->visual_layers[index];
        }
    }
    std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(count),
              [](const auto* left, const auto* right) {
                  if (left->draw_order != right->draw_order) {
                      return left->draw_order < right->draw_order;
                  }
                  if (left->source_order != right->source_order) {
                      return left->source_order < right->source_order;
                  }
                  return left->kind < right->kind;
              });
    order_bga_composite_layers(std::span(ordered).first(count));
    const auto record_failure = [&](std::uint64_t resource_id,
                                    worm_ui_nuklear_preview_visual_failure_t failure) {
        ++result.failure_count;
        if (result.first_failure == worm_ui_nuklear_preview_visual_failure_t::none) {
            result.first_failed_resource_id = resource_id;
            result.first_failure = failure;
        }
    };
    for (std::size_t index = 0; index < count; ++index) {
        const auto uploaded = texture_for(renderer, *view, *ordered[index],
            ordered[index]->kind == WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER ||
            ordered[index]->kind == WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER2);
        if (uploaded.failure != worm_ui_nuklear_preview_visual_failure_t::none) {
            record_failure(ordered[index]->resource_id, uploaded.failure);
            continue;
        }
        const auto& layer = *ordered[index];
        const auto layout =
            wormedit::visual_image_layout_t::from({.source_width = layer.image_width,
                                                .source_height = layer.image_height,
                                                .destination = {.left = viewport.x,
                                                                .top = viewport.y,
                                                                .width = viewport.w,
                                                                .height = viewport.h},
                                                .fit = visual_fit_policy(view->visual_fit)});
        if (!layout) {
            record_failure(layer.resource_id,
                           worm_ui_nuklear_preview_visual_failure_t::layout_invalid);
            continue;
        }
        const auto& source = layout->source;
        const auto full_source = source.left == 0 && source.top == 0 &&
                                 source.width == layer.image_width &&
                                 source.height == layer.image_height;
        const auto image =
            full_source
                ? nk_image_ptr(uploaded.texture)
                : nk_subimage_ptr(
                      uploaded.texture, static_cast<nk_ushort>(layer.image_width),
                      static_cast<nk_ushort>(layer.image_height),
                      nk_rect(static_cast<float>(source.left), static_cast<float>(source.top),
                              static_cast<float>(source.width), static_cast<float>(source.height)));
        const auto destination = nk_rect(static_cast<float>(layout->destination.left),
                                         static_cast<float>(layout->destination.top),
                                         static_cast<float>(layout->destination.width),
                                         static_cast<float>(layout->destination.height));
        nk_draw_image(canvas, destination, &image, nk_rgba(255, 255, 255, 255));
        result.drew_image = true;
    }
    return result;
}

worm_ui_nuklear_preview_visual_result_t worm_ui_nuklear_draw_player_preview_storyboard_sprites(
    struct nk_command_buffer* canvas, struct nk_rect viewport, struct SDL_Renderer* renderer,
    const WormUiPlayerPreviewView* view, bool foreground) {
    worm_ui_nuklear_preview_visual_result_t result;
    if (!canvas || !view) {
        return result;
    }
    const auto record_failure = [&](std::uint64_t resource_id,
                                    worm_ui_nuklear_preview_visual_failure_t failure) {
        ++result.failure_count;
        if (result.first_failure == worm_ui_nuklear_preview_visual_failure_t::none) {
            result.first_failed_resource_id = resource_id;
            result.first_failure = failure;
        }
    };
    for (std::size_t index = 0; index < view->storyboard_sprite_count; ++index) {
        const auto& sprite = view->storyboard_sprites[index];
        if ((sprite.layer_order >= 3) != foreground) {
            continue;
        }
        if (!sprite.image_ready || sprite.opacity <= 0.0 || sprite.scale_x == 0.0 ||
            sprite.scale_y == 0.0) {
            continue;
        }
        const auto uploaded = texture_for(renderer, *view, sprite);
        if (uploaded.failure != worm_ui_nuklear_preview_visual_failure_t::none) {
            record_failure(sprite.resource_id, uploaded.failure);
            continue;
        }
        const auto quad =
            wormedit::visual_image_quad_t::from({.source_width = sprite.image_width,
                                              .source_height = sprite.image_height,
                                              .destination = {.left = viewport.x,
                                                              .top = viewport.y,
                                                              .width = viewport.w,
                                                              .height = viewport.h},
                                              .x = sprite.x,
                                              .y = sprite.y,
                                              .origin_x = sprite.origin_x,
                                              .origin_y = sprite.origin_y,
                                              .scale_x = sprite.scale_x,
                                              .scale_y = sprite.scale_y,
                                              .rotation_radians = sprite.rotation_radians});
        if (!quad) {
            record_failure(sprite.resource_id,
                           worm_ui_nuklear_preview_visual_failure_t::layout_invalid);
            continue;
        }
        std::array<struct nk_vec2, 4> points{};
        std::ranges::transform(quad->points, points.begin(), [](const auto& point) {
            return nk_vec2(static_cast<float>(point.x), static_cast<float>(point.y));
        });
        const auto image = nk_image_ptr(uploaded.texture);
        if (!worm_ui_nuklear_push_image_quad(
                canvas, &image, points.data(),
                nk_rgba(wormedit::ui::chrome::color_byte(sprite.red), wormedit::ui::chrome::color_byte(sprite.green), wormedit::ui::chrome::color_byte(sprite.blue),
                        wormedit::ui::chrome::color_byte(sprite.opacity * 255.0)))) {
            record_failure(sprite.resource_id,
                           worm_ui_nuklear_preview_visual_failure_t::command_capacity);
            continue;
        }
        result.drew_image = true;
    }
    return result;
}

void worm_ui_nuklear_shutdown_player_preview_textures(void) {
    while (texture_count != 0) {
        erase_texture(texture_count - 1);
    }
    texture_count = 0;
    texture_bytes = 0;
    texture_use_sequence = 0;
}
