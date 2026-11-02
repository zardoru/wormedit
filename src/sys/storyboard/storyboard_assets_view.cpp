#include "sys/storyboard/storyboard_assets_view.h"
#include "sys/resources/bga_list_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include "ui/frontend/storyboard.h"
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/visual/visual_asset.h>
#include <algorithm>

namespace wormedit::sys {
namespace {
auto selected_storyboard(const song_document_t& song, storyboard_id_t id) -> const storyboard_document_t* {
    const auto found = std::ranges::find(song.storyboards, id, &storyboard_document_t::id);
    return found == song.storyboards.end() ? nullptr : &*found;
}
void append_image_users(std::vector<std::string>& users, std::string_view image, const osbx_document_t& scene) {
    for (const auto& node : scene.nodes) {
        if (!node.image || *node.image != image) continue;
        if (std::ranges::find(users, node.id) == users.end()) users.push_back(node.id);
    }
}
auto image_users_reason(std::span<const std::string> users) -> std::string {
    if (users.empty()) return {};
    std::string reason{"Image is used by: "};
    std::string_view separator;
    for (const auto& user : users) {
        reason += separator;
        reason += user;
        separator = ", ";
    }
    return reason;
}
void build_authored_images(ui::storyboard_assets_view_t& view, const ui::storyboard_view_t& root,
    const storyboard_document_t& board, const std::filesystem::path& directory, const osbx_document_t* prepared) {
    view.images.resize(root.inspector.images.size());
    for (std::size_t i = 0; i < root.inspector.images.size(); ++i) {
        const auto& choice = root.inspector.images[i];
        auto& row = view.images[i];
        row.id = choice.id; row.file = choice.label; row.thumbnail = {}; row.thumbnail_requested = false;
        build_storyboard_image_users(row.users, row.id, *board.osbx, prepared);
        row.removal_reason = image_users_reason(row.users);
        const auto source = inspect_project_path_source({.source_directory = directory,
            .relative_path = std::filesystem::path(row.file).lexically_normal()});
        row.missing = !source;
        row.status = source ? "" : source.error().message;
        row.caption = std::to_string(row.users.size()) + " uses" + (row.missing ? " · Missing" : "");
    }
}
void build_resource_images(ui::storyboard_assets_view_t& view, const song_document_t& song,
    const std::filesystem::path& directory) {
    const auto catalog = bga_list_view_t::from(song, {.row_limit = song.resources.size()});
    std::size_t used{};
    for (const auto& source : catalog.rows) {
        if (source.video) continue;
        if (used == view.resources.size()) view.resources.emplace_back();
        auto& row = view.resources[used++];
        row.resource = source.resource; row.name = source.name; row.file = source.source_path;
        row.thumbnail_requested = false;
        row.image.reset(); row.thumbnail = {.resource_id = source.resource, .static_image = true};
        const auto file = storyboard_resource_image_file(song, source.resource, directory);
        row.missing = source.missing || (!file && (file.error().code == failure_code_t::not_found ||
            file.error().code == failure_code_t::io_failed));
        row.available = file.has_value() || (!directory.empty() && !row.missing &&
            file.error().code == failure_code_t::unavailable);
        row.status = file ? "" : file.error().message;
        row.caption = row.missing ? "Missing" : "Song image";
        if (!file) continue;
        const auto existing = std::ranges::find_if(view.images, [&](const auto& image) {
            return std::filesystem::path(image.file).lexically_normal() == std::filesystem::path(*file).lexically_normal();
        });
        if (existing != view.images.end()) { row.image = existing->id; row.caption = "In storyboard"; }
    }
    view.resources.resize(used);
}
} // namespace

void build_storyboard_image_users(std::vector<std::string>& users, std::string_view image,
    const osbx_document_t& authored, const osbx_document_t* prepared) {
    users.clear();
    append_image_users(users, image, authored);
    if (prepared) append_image_users(users, image, *prepared);
}
auto validate_storyboard_image_removal(const storyboard_document_t& board, std::string_view image,
    const osbx_document_t* prepared) -> result_t<void, error_t<failure_code_t>> {
    if (!board.osbx || board.model || board.owning_generator_recipe)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable, "Storyboard images are read-only."});
    if (std::ranges::find(board.osbx->images, image, &osbx_image_t::id) == board.osbx->images.end())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found, "Storyboard image no longer exists."});
    std::vector<std::string> users;
    build_storyboard_image_users(users, image, *board.osbx, prepared);
    if (!users.empty())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, image_users_reason(users)});
    return {};
}
auto storyboard_resource_image_file(const song_document_t& song, resource_id_t resource,
    const std::filesystem::path& directory) -> result_t<std::string, error_t<failure_code_t>> {
    const auto base = directory.lexically_normal();
    const auto source = visual::resolved_visual_asset_t::from(song, resource, base);
    if (!source) return std::unexpected(source.error());
    if (source->kind != asset_kind_t::image)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unsupported, "Resource is not a static image."});
    std::error_code error;
    if (!std::filesystem::is_regular_file(source->path, error))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found, "Song image file is missing."});
    const auto relative = source->path.lexically_relative(base);
    const auto file = relative.generic_string();
    if (directory.empty() || !osbx_document_t::is_valid_project_path(file))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable, "Copy this image into the project before adding it."});
    const auto safe = inspect_project_path_source({.source_directory = base, .relative_path = relative});
    if (!safe) return std::unexpected(safe.error());
    return file;
}
void storyboard_assets_projection_t::refill(ui::storyboard_view_t& root, const song_document_t& song,
    const std::filesystem::path& directory, const osbx_document_t* prepared_scene) {
    const auto stamp = document_stamp_t::from(song);
    const auto board = root.state.selection.storyboard;
    if (document_ == &song && stamp_ == stamp && storyboard_ == board && session_ == root.session_generation &&
        prepared_ == root.canvas.prepared_generation && directory_ == directory) return;
    document_ = &song; stamp_ = stamp; storyboard_ = board; session_ = root.session_generation;
    prepared_ = root.canvas.prepared_generation; directory_ = directory;
    auto& view = root.assets;
    view.editable = false; view.import_available = false; view.reason.clear();
    const auto* source = selected_storyboard(song, board);
    if (!source || !source->osbx) {
        view.images.clear(); view.resources.clear();
        view.message = source ? "Convert this storyboard to OSBX to edit images." : "Choose a storyboard.";
        return;
    }
    view.editable = !source->model && !source->owning_generator_recipe;
    view.import_available = view.editable && !directory.empty();
    view.reason = view.editable ? "" : "Generated storyboard: detach before editing images.";
    view.message = "Add a song image, import an image, or drag an image onto the canvas.";
    build_authored_images(view, root, *source, directory, prepared_scene);
    build_resource_images(view, song, directory);
}
auto storyboard_assets_projection_t::prepare_thumbnails(ui::storyboard_view_t& root,
    storyboard_scene_presenter_t& scenes) -> std::span<ui::player_preview_visual_layer_view_t* const> {
    thumbnail_views_.clear();
    for (auto& row : root.assets.images) {
        if (!row.thumbnail_requested) { row.thumbnail = {}; continue; }
        const auto prepared = scenes.prepare_image_thumbnail(row.thumbnail, root.state.selection.storyboard, row.id);
        if (!prepared) { row.status = prepared.error().message; continue; }
        thumbnail_views_.push_back(&row.thumbnail);
    }
    for (auto& row : root.assets.resources) {
        row.thumbnail = {.resource_id = row.resource, .static_image = true};
        if (row.thumbnail_requested && !row.missing) thumbnail_views_.push_back(&row.thumbnail);
    }
    return thumbnail_views_;
}
} // namespace wormedit::sys
