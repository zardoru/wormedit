#include "ui/nuklear/player_preview_texture.h"

#include "wormedit/nuklear_config.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

constexpr std::size_t maximum_texture_entries = 64;
constexpr std::size_t maximum_texture_bytes = 256U * 1024U * 1024U;

struct preview_texture_t {
    std::uint64_t project_id{};
    std::uint64_t document_revision{};
    std::uint64_t resource_id{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t rgba_bytes{};
    GLuint texture{};
    std::uint64_t last_use{};
};

std::array<preview_texture_t, maximum_texture_entries> textures{};
std::size_t texture_count{};
std::size_t texture_bytes{};
std::uint64_t texture_use_sequence{};

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
    if (entry.texture != 0) {
        glDeleteTextures(1, &entry.texture);
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

auto texture_for(const WormUiPlayerPreviewView& view,
                 const WormUiPlayerPreviewVisualLayerView& layer) -> GLuint {
    if (!layer.image_ready || view.project_id == 0 || view.document_revision == 0 ||
        layer.resource_id == 0 || layer.rgba_byte_count == 0 || !layer.rgba_pixels ||
        layer.rgba_byte_count > maximum_texture_bytes) {
        return 0;
    }
    const auto end = textures.begin() + static_cast<std::ptrdiff_t>(texture_count);
    const auto found = std::find_if(textures.begin(), end, [&](const auto& entry) {
        return entry.project_id == view.project_id &&
               entry.document_revision == view.document_revision &&
               entry.resource_id == layer.resource_id && entry.width == layer.image_width &&
               entry.height == layer.image_height;
    });
    if (found != end) {
        found->last_use = next_texture_use();
        return found->texture;
    }
    while (texture_count != 0 && (texture_count >= maximum_texture_entries ||
                                  texture_bytes > maximum_texture_bytes - layer.rgba_byte_count)) {
        evict_texture();
    }

    GLuint texture{};
    glGenTextures(1, &texture);
    if (texture == 0) {
        return 0;
    }
    GLint previous_texture{};
    GLint previous_unpack_alignment{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack_alignment);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(layer.image_width),
                 static_cast<GLsizei>(layer.image_height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 layer.rgba_pixels);
    GLint uploaded_width{};
    GLint uploaded_height{};
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &uploaded_width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &uploaded_height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    if (uploaded_width != static_cast<GLint>(layer.image_width) ||
        uploaded_height != static_cast<GLint>(layer.image_height)) {
        glDeleteTextures(1, &texture);
        return 0;
    }

    textures[texture_count++] = {.project_id = view.project_id,
                                 .document_revision = view.document_revision,
                                 .resource_id = layer.resource_id,
                                 .width = layer.image_width,
                                 .height = layer.image_height,
                                 .rgba_bytes = layer.rgba_byte_count,
                                 .texture = texture,
                                 .last_use = next_texture_use()};
    texture_bytes += layer.rgba_byte_count;
    return texture;
}

} // namespace

bool worm_ui_nuklear_draw_player_preview_visual_layers(struct nk_command_buffer* canvas,
                                                       struct nk_rect viewport,
                                                       const WormUiPlayerPreviewView* view) {
    if (!canvas || !view) {
        return false;
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
    bool drew_image{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto texture = texture_for(*view, *ordered[index]);
        if (texture == 0 || texture > static_cast<GLuint>(std::numeric_limits<int>::max())) {
            continue;
        }
        const auto image = nk_image_id(static_cast<int>(texture));
        nk_draw_image(canvas, viewport, &image, nk_rgba(255, 255, 255, 255));
        drew_image = true;
    }
    return drew_image;
}

void worm_ui_nuklear_shutdown_player_preview_textures(void) {
    while (texture_count != 0) {
        erase_texture(texture_count - 1);
    }
    texture_count = 0;
    texture_bytes = 0;
    texture_use_sequence = 0;
}
