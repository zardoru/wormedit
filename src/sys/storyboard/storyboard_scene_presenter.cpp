#include "sys/storyboard/storyboard_scene_presenter.h"
#include <wormedit/core/storyboard_canvas.h>
#include <wormedit/core/storyboard_preview.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/osbx_edit.h>
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif
#include <wormedit/visual/visual_asset.h>
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

namespace wormedit::sys {
namespace {
class storyboard_source_t {
  public:
    explicit storyboard_source_t(const storyboard_document_t& source) : source_(&source) {
        if (source.osbx) {
            auto index = osbx_evaluation_index_t::from(*source.osbx);
            if (!index.value) {
                status_ = index.diagnostics.empty() ? "OSBX scene is unavailable." : index.diagnostics.front().message;
                return;
            }
            osbx_ = std::move(*index.value);
            scene_ = osbx_->prepared_owner ? osbx_->prepared_owner.get() : &*source.osbx;
            build_osbx_sources();
        } else if (source.model) {
            auto index = storyboard_preview_index_t::from(*source.model);
            if (!index.value) {
                status_ = index.diagnostics.empty() ? "OSB scene is unavailable." : index.diagnostics.front().message;
                return;
            }
            osb_ = std::move(*index.value);
            for (const auto& sprite : source.model->sprites)
                legacy_ids_.emplace(sprite.stable_id, storyboard_canvas_target_t::legacy_node_id(sprite.stable_id));
        }
    }
    void reset_frame() { last_frame_ = {}; }
    [[nodiscard]] auto id() const -> storyboard_id_t { return source_->id; }
    [[nodiscard]] auto scene() const -> const osbx_document_t* { return scene_; }
    [[nodiscard]] auto is_animation(const storyboard_draw_instance_t& draw) const -> bool {
        if (!source_ || !source_->model || draw.document_order >= source_->model->sprites.size()) return false;
        const auto& sprite = source_->model->sprites[draw.document_order];
        return sprite.stable_id == draw.stable_id && sprite.animation && sprite.animation->is_valid();
    }
    [[nodiscard]] auto node_state(std::string_view id) const -> const osbx_node_frame_t* {
        return osbx_ ? last_frame_.node_state(*osbx_, id) : nullptr;
    }
    [[nodiscard]] auto op_space(std::string_view node, std::string_view op, storyboard_time_t time) const
        -> result_t<osbx_op_space_t, failure_code_t> {
        if (!osbx_) return std::unexpected(failure_code_t::unavailable);
        return last_frame_.op_space(*osbx_, node, op, time);
    }
    [[nodiscard]] auto node(const storyboard_draw_instance_t& draw) const -> std::string_view {
        if (!osbx_) return legacy_ids_.at(draw.stable_id);
        return nodes_[draw.stable_id - 1];
    }
    auto refill(storyboard_time_t time, std::size_t cap, storyboard_conditional_layer_t conditional_layer)
        -> diagnosed_t<std::span<const storyboard_draw_instance_t>> {
        frame_.clear();
        if (!status_.empty()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding, status_)}};
        if (osbx_) return refill_osbx(time, cap, conditional_layer);
        if (!osb_ || !source_->model) return {};
        const auto frame = evaluate_storyboard_frame(*source_->model, *osb_, time, arena_,
            {.maximum_instances = std::max<std::size_t>(cap, 1), .conditional_layer = conditional_layer});
        if (!frame.is_success()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding,
            frame.diagnostic ? frame.diagnostic->message : "Storyboard frame storage exceeded its bounds.")}};
        if (frame.instances.size() > cap) return capacity_error();
        return {.value = frame.instances};
    }
  private:
    const storyboard_document_t* source_{};
    const osbx_document_t* scene_{};
    std::optional<storyboard_preview_index_t> osb_;
    std::optional<osbx_evaluation_index_t> osbx_;
    std::unordered_map<std::string_view, storyboard_draw_instance_t> sources_;
    std::unordered_map<std::uint64_t, std::string> legacy_ids_;
    std::vector<std::string_view> nodes_;
    frame_arena_t arena_;
    std::vector<storyboard_draw_instance_t> frame_;
    osbx_frame_effect_t last_frame_;
    std::string status_;
    void build_osbx_sources() {
        for (const auto i : osbx_->draw_order) {
            const auto& node = *osbx_->nodes[i].source;
            if (node.kind != osbx_node_kind_t::sprite && node.kind != osbx_node_kind_t::animation) continue;
            if (!node.image) continue; // Incomplete legacy placeholders fail in the shared evaluator.
            const auto image = std::ranges::find(scene_->images, *node.image, &osbx_image_t::id);
            auto draw = storyboard_draw_instance_t::from(*scene_, node, *image);
            draw.stable_id = i + 1;
            draw.source_order = sources_.size();
            sources_.emplace(node.id, draw);
        }
    }
    auto capacity_error() const -> diagnosed_t<std::span<const storyboard_draw_instance_t>> {
        return {.diagnostics = {error(diagnostic_code_t::invalid_resolution, "Shared storyboard sprite budget exceeded.")}};
    }
    auto refill_osbx(storyboard_time_t time, std::size_t cap, storyboard_conditional_layer_t conditional_layer)
        -> diagnosed_t<std::span<const storyboard_draw_instance_t>> {
        last_frame_ = evaluate_osbx_frame(*osbx_, time, arena_, {.conditional_layer = conditional_layer});
        const auto& frame = last_frame_;
        if (!frame.is_success()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding,
            frame.diagnostics.empty() ? "OSBX frame storage exceeded its bounds." : std::string{frame.diagnostics.front().message})}};
        if (frame.instances.size() > cap) return capacity_error();
        frame_.reserve(frame.instances.size());
        nodes_.resize(osbx_->nodes.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i) nodes_[i] = osbx_->nodes[i].source->id;
        for (const auto& instance : frame.instances) {
            const auto draw = sources_.at(instance.id).with_evaluation(instance, arena_);
            if (!draw) return {.diagnostics = {error(diagnostic_code_t::invalid_binding,
                "Storyboard Animation frame path exceeded its current arena bounds.")}};
            frame_.push_back(*draw);
        }
        return {.value = frame_};
    }
};

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
void append_image_status(std::string& status, std::string_view message) {
    if (message.empty() || status.find(message) != std::string::npos) return;
    if (!status.empty()) status += "; ";
    status += message;
}
void append_thumbnail_requests(std::vector<resource_id_t>& resources,
    std::span<ui::player_preview_visual_layer_view_t* const> thumbnails, std::size_t limit) {
    for (auto* view : thumbnails) {
        view->image_ready = false; view->image_pending = false; view->rgba_pixels = {};
        view->image_content_id = 0; view->image_width = 0; view->image_height = 0;
        view->diagnostic_text = {};
        if (!view->static_image || !view->resource_id) continue;
        const auto where = std::ranges::lower_bound(resources, view->resource_id);
        if (where != resources.end() && *where == view->resource_id) continue;
        if (resources.size() >= limit) {
            view->diagnostic_text = "Thumbnail exceeds the shared image request budget.";
            continue;
        }
        resources.insert(where, view->resource_id);
    }
}
void mark_unavailable_thumbnail(std::span<ui::player_preview_visual_layer_view_t* const> thumbnails,
    resource_id_t resource) {
    for (auto* view : thumbnails)
        if (view->resource_id == resource) view->diagnostic_text = "Thumbnail image source is unavailable.";
}
struct storyboard_image_catalog_t {
    struct path_entry_t {
        result_t<resource_id_t, error_t<failure_code_t>> resource;
        bool thumbnail_only{};
    };
    // Preview geometry may replace scene strings while media stays bound.
    // Own bounded keys; transparent lookup keeps warmed queries allocation-free.
    std::map<std::string, path_entry_t, std::less<>> paths;
    std::vector<visual::resolved_visual_asset_t> sources;
    std::uint64_t next_identity{std::numeric_limits<std::uint64_t>::max()};
    void clear() { paths.clear(); sources.clear(); }
    auto admit_path(bool thumbnail) -> bool {
        if (paths.size() < ui::maximum_preview_storyboard_instances) return true;
        if (thumbnail) return false;
        const auto victim = std::ranges::find_if(paths, [](const auto& item) { return item.second.thumbnail_only; });
        if (victim == paths.end()) return false;
        paths.erase(victim);
        std::erase_if(sources, [&](const auto& source) {
            return std::ranges::none_of(paths, [&](const auto& item) {
                return item.second.resource && *item.second.resource == source.resource;
            });
        });
        return true;
    }
    auto resolve(const song_document_t& document, std::string_view path, const std::filesystem::path& directory,
        bool thumbnail = false)
        -> result_t<resource_id_t, error_t<failure_code_t>> {
        if (const auto found = paths.find(path); found != paths.end()) {
            if (!thumbnail) found->second.thumbnail_only = false;
            return found->second.resource;
        }
        if (!admit_path(thumbnail))
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, "Storyboard image catalog exceeds its bounds."});
        auto source = build_source(document, path, directory);
        if (!source) {
            if (source.error().code == failure_code_t::capacity_exceeded) return std::unexpected(std::move(source.error()));
            auto failure = std::unexpected(std::move(source.error()));
            paths.emplace(std::string(path), path_entry_t{.resource = failure, .thumbnail_only = thumbnail});
            return failure;
        }
        const auto existing = std::ranges::find(sources, source->path, &visual::resolved_visual_asset_t::path);
        const auto resource = existing == sources.end() ? source->resource : existing->resource;
        if (existing == sources.end()) sources.push_back(std::move(*source));
        paths.emplace(std::string(path), path_entry_t{.resource = resource, .thumbnail_only = thumbnail});
        return resource;
    }
    auto build_source(const song_document_t& document, std::string_view path, const std::filesystem::path& directory)
        -> result_t<visual::resolved_visual_asset_t, error_t<failure_code_t>> {
        while (next_identity && std::ranges::find(document.resources, resource_id_t{next_identity}, &resource_entry_t::id) != document.resources.end())
            --next_identity;
        if (!next_identity)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, "Storyboard image catalog exceeds its bounds."});
        return visual::resolved_visual_asset_t::from_project_image(std::filesystem::path{path}, resource_id_t{next_identity--}, directory);
    }
};
#endif
} // namespace

struct storyboard_scene_presenter_t::impl_t {
    const song_document_t* document{};
    document_stamp_t stamp;
    std::uint64_t session{}, prepared{};
    std::filesystem::path directory;
    std::vector<storyboard_source_t> sources;
    std::shared_ptr<const osbx_edit_preview_t> preview_owner;
    void bind_preview(std::shared_ptr<const osbx_edit_preview_t> preview) {
        if (preview_owner == preview) return;
        // Destroy borrowed indices before releasing their source owner. Other
        // storyboards always borrow the committed document and remain valid.
        std::erase_if(sources, [&](const auto& source) {
            return (preview_owner && source.id() == preview_owner->storyboard) ||
                (preview && source.id() == preview->storyboard);
        });
        preview_owner = std::move(preview);
    }
    struct node_diagnostic_t { storyboard_id_t storyboard; std::string node, message; };
    std::vector<node_diagnostic_t> diagnostics;
    void report(const ui::player_preview_storyboard_sprite_view_t& sprite, std::string message) {
        const auto found = std::ranges::find_if(diagnostics, [&](const auto& item) {
            return item.storyboard == sprite.storyboard && item.node == sprite.node_id;
        });
        if (found != diagnostics.end()) found->message = std::move(message);
        else if (diagnostics.size() < ui::maximum_preview_storyboard_instances)
            diagnostics.push_back({sprite.storyboard, std::string{sprite.node_id}, std::move(message)});
    }
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    storyboard_image_catalog_t images;
    std::vector<resource_id_t> requested;
    std::vector<resource_id_t> unavailable_requests; // Diagnostics for the same captured request union.
    std::shared_ptr<const visual::image_job_result_t> decoded;
    std::uint64_t content{};
    bool pending{};
    std::string image_failure;
    void poll_images(visual::image_job_t& job) {
        auto completion = job.poll();
        if (!completion) return;
        const auto& identity = *completion ? completion->value().identity : completion->error().identity;
        if (identity.project != stamp.project || identity.document_revision != stamp.revision) return;
        if (*completion) {
            decoded = std::move(completion->value().payload);
            content = completion->value().generation;
            image_failure.clear();
        } else image_failure = completion->error().cause.message.empty() ?
            "Image decode task failed before producing a result." : completion->error().cause.message;
    }
#endif
    template<class Image> void populate_image(Image& view) const {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        if (view.resource_id && decoded) {
            if (const auto* item = decoded->find(view.resource_id)) {
                view.image_ready = true;
                view.image_content_id = content;
                view.image_width = item->payload->width;
                view.image_height = item->payload->height;
                view.rgba_pixels = item->payload->pixels;
            }
        }
        view.image_pending = view.resource_id && !view.image_ready && pending;
#else
        static_cast<void>(view);
#endif
    }
    auto source(storyboard_id_t id) -> storyboard_source_t& {
        const auto found = std::ranges::find(sources, id, &storyboard_source_t::id);
        if (found != sources.end()) return *found;
        const auto& geometry = preview_owner && preview_owner->storyboard == id ? preview_owner->document : *document;
        const auto prepared_scenes = preview_owner ? std::span<const osbx_lua_prepared_scene_t>{preview_owner->prepared} :
            std::span<const osbx_lua_prepared_scene_t>{};
        const osbx_lua_prepared_scope_t scope{prepared_scenes, prepared};
        sources.emplace_back(*std::ranges::find(geometry.storyboards, id, &storyboard_document_t::id));
        return sources.back();
    }
    auto sprite(const storyboard_source_t& source, const storyboard_draw_instance_t& instance,
        std::vector<ui::player_preview_storyboard_sprite_view_t>& output) -> diagnostic_list_t {
        const auto origin = storyboard_origin_t::from(instance.origin).value_or(storyboard_origin_t{});
        ui::player_preview_storyboard_sprite_view_t sprite{.storyboard = source.id(), .node_id = source.node(instance),
            .stable_id = instance.stable_id, .resource_id = document->storyboard_resource(instance.path),
            .source_order = output.size(), .layer_order = instance.layer_order, .transform = instance.transform.values,
            .logical_width = instance.logical_width, .logical_height = instance.logical_height,
            .origin_x = origin.x, .origin_y = origin.y,
            .blend = !instance.blend ? ui::image_blend_t::inherit :
                *instance.blend == osbx_blend_t::additive ? ui::image_blend_t::additive : ui::image_blend_t::normal,
            .opacity = instance.opacity, .red = instance.red, .green = instance.green, .blue = instance.blue};
        diagnostic_list_t diagnostics;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        if (!sprite.resource_id && (source.scene() || source.is_animation(instance))) {
            const auto resource = images.resolve(*document, instance.path, directory);
            if (resource) sprite.resource_id = *resource;
            else diagnostics.push_back(error(diagnostic_code_t::dangling_asset, "Storyboard image " + std::string{instance.path} + ": " + resource.error().message));
        }
#endif
        if (!sprite.resource_id && diagnostics.empty()) diagnostics.push_back(error(diagnostic_code_t::dangling_asset,
            "Storyboard image " + std::string{instance.path} + ": resource is missing."));
        if (!diagnostics.empty()) report(sprite, diagnostics.front().message);
        populate_image(sprite);
        output.push_back(sprite);
        return diagnostics;
    }
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    auto resolve_image_requests(std::span<const resource_id_t> resources,
        std::span<ui::player_preview_visual_layer_view_t* const> thumbnails,
        std::vector<visual::resolved_visual_asset_t>& inputs) -> std::string {
        unavailable_requests.clear();
        std::string failure;
        for (const auto resource : resources) {
            auto resolved = visual::resolved_visual_asset_t::from(*document, resource, directory);
            const auto temporary = std::ranges::find(images.sources, resource, &visual::resolved_visual_asset_t::resource);
            if (temporary != images.sources.end()) resolved = *temporary;
            if (resolved && resolved->kind == asset_kind_t::image) inputs.push_back(*resolved);
            else {
                unavailable_requests.push_back(resource);
                mark_unavailable_thumbnail(thumbnails, resource);
                if (failure.empty()) failure = resolved ? "Storyboard resource is not an image." : resolved.error().message;
            }
        }
        return failure;
    }
    auto request_images(std::span<const ui::player_preview_visual_layer_view_t> layers,
        std::span<const ui::player_preview_storyboard_sprite_view_t> player,
        std::span<const ui::player_preview_storyboard_sprite_view_t> canvas, visual::image_job_t& job,
        std::span<ui::player_preview_visual_layer_view_t* const> thumbnails) -> std::string {
        std::vector<resource_id_t> resources;
        for (const auto& view : layers) if (view.static_image) resources.push_back(view.resource_id);
        for (const auto& view : player) resources.push_back(view.resource_id);
        for (const auto& view : canvas) resources.push_back(view.resource_id);
        std::ranges::sort(resources);
        const auto unique = std::ranges::unique(resources);
        resources.erase(unique.begin(), unique.end());
        std::erase(resources, resource_id_t{});
        const bool primary_covered = std::ranges::includes(requested, resources);
        append_thumbnail_requests(resources, thumbnails, job.maximum_sources_per_request());
        // Thumbnail-only changes wait for the current primary work. A view-only
        // demand update must not cancel an already admitted primary decode.
        if (resources == requested || (job.is_busy() && primary_covered)) { pending = job.is_busy(); return {}; }
        std::vector<visual::resolved_visual_asset_t> inputs;
        auto failure = resolve_image_requests(resources, thumbnails, inputs);
        if (inputs.empty()) { job.cancel(); requested.clear(); decoded.reset(); image_failure.clear(); }
        else {
            const auto started = job.request(document->id, document->revision, inputs);
            if (started) { requested = std::move(resources); image_failure.clear(); }
            else failure = started.error().message;
        }
        pending = job.is_busy();
        return failure;
    }
    void report_image_failures(ui::player_preview_view_t& player, ui::storyboard_canvas_view_t* canvas) {
        if (!decoded) return;
        for (const auto& item : decoded->items) {
            if (item) continue;
            const auto resource = item.error().context.resource;
            const auto failed = [&](const auto& sprite) { return sprite.resource_id == resource; };
            const auto message = "Storyboard image: " + item.error().cause.message;
            for (const auto& sprite : player.storyboard_sprites) if (failed(sprite)) report(sprite, message);
            if (canvas) for (const auto& sprite : canvas->sprites) if (failed(sprite)) report(sprite, message);
            if (std::ranges::any_of(player.storyboard_sprites, failed) ||
                std::ranges::any_of(player.visual_layers, failed)) append_image_status(player.status_text, message);
            if (canvas && std::ranges::any_of(canvas->sprites, failed)) append_image_status(canvas->status, message);
        }
    }
    void populate_thumbnails(std::span<ui::player_preview_visual_layer_view_t* const> thumbnails) const {
        for (auto* view : thumbnails) {
            if (!view->diagnostic_text.empty()) continue;
            if (std::ranges::find(unavailable_requests, view->resource_id) != unavailable_requests.end()) {
                view->diagnostic_text = "Thumbnail image source is unavailable.";
                continue;
            }
            populate_image(*view);
            if (!image_failure.empty()) view->diagnostic_text = image_failure;
            if (!decoded) continue;
            const auto failed = std::ranges::find_if(decoded->items, [&](const auto& item) {
                return !item && item.error().context.resource == view->resource_id;
            });
            if (failed != decoded->items.end()) view->diagnostic_text = failed->error().cause.message;
        }
    }
#endif
};

storyboard_scene_presenter_t::storyboard_scene_presenter_t() : impl_(std::make_unique<impl_t>()) {}
storyboard_scene_presenter_t::~storyboard_scene_presenter_t() = default;
storyboard_scene_presenter_t::storyboard_scene_presenter_t(storyboard_scene_presenter_t&&) noexcept = default;
auto storyboard_scene_presenter_t::operator=(storyboard_scene_presenter_t&&) noexcept -> storyboard_scene_presenter_t& = default;

auto storyboard_scene_presenter_t::begin(const song_document_t& document, std::uint64_t session,
    const std::filesystem::path& directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    , visual::image_job_t& job
#endif
    , std::shared_ptr<const osbx_edit_preview_t> preview) -> bool {
    auto& state = *impl_;
    state.diagnostics.clear();
    for (auto& source : state.sources) source.reset_frame();
    const auto stamp = document_stamp_t::from(document);
    const auto prepared = osbx_lua_prepared_scope_t::generation();
    const bool binding_changed = state.document != &document || state.stamp != stamp ||
        state.session != session || state.prepared != prepared || state.directory != directory;
    if (binding_changed) {
        state.sources.clear();
        state.preview_owner.reset();
        state.document = &document; state.stamp = stamp; state.session = session; state.prepared = prepared; state.directory = directory;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        state.images.clear(); state.requested.clear(); state.unavailable_requests.clear();
        state.decoded.reset(); state.content = 0; state.image_failure.clear();
        job.cancel();
#endif
    }
    if (preview && (preview->capture != stamp || preview->document.id != document.id ||
        preview->prepared_generation != prepared)) preview.reset();
    state.bind_preview(std::move(preview));
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    state.poll_images(job);
    state.pending = job.is_busy();
#endif
    return binding_changed;
}
auto storyboard_scene_presenter_t::refill(std::vector<ui::player_preview_storyboard_sprite_view_t>& output,
    std::optional<storyboard_id_t> selected, storyboard_time_t time, std::size_t cap,
    storyboard_conditional_layer_t conditional_layer) -> diagnosed_t<bool> {
    output.clear();
    auto& state = *impl_;
    diagnosed_t<bool> result{.value = true};
    if (!state.document || !time.is_valid()) return {.diagnostics = {error(diagnostic_code_t::invalid_binding, "Storyboard scene is unbound or has an invalid clock.")}};
    cap = std::min(cap, ui::maximum_preview_storyboard_instances);
    for (const auto& storyboard : state.document->storyboards) {
        if (selected && storyboard.id != *selected) continue;
        auto& source = state.source(storyboard.id);
        const auto frame = source.refill(time, cap - output.size(), conditional_layer);
        if (!frame.ok()) { output.clear(); return {.diagnostics = frame.diagnostics}; }
        if (!std::ranges::all_of(frame.value, &storyboard_draw_instance_t::is_valid)) {
            output.clear();
            return {.diagnostics = {error(diagnostic_code_t::invalid_resolution,
                "Storyboard frame contains an invalid or nonfinite draw instance.")}};
        }
        for (const auto& instance : frame.value) {
            auto diagnostics = state.sprite(source, instance, output);
            result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
        }
    }
    std::ranges::stable_sort(output, {}, &ui::player_preview_storyboard_sprite_view_t::layer_order);
    return result;
}
auto storyboard_scene_presenter_t::scene(storyboard_id_t id) const -> const osbx_document_t* {
    const auto found = std::ranges::find(impl_->sources, id, &storyboard_source_t::id);
    return found == impl_->sources.end() ? nullptr : found->scene();
}
auto storyboard_scene_presenter_t::has_preview(storyboard_id_t id) const -> bool {
    return impl_->preview_owner && impl_->preview_owner->storyboard == id;
}
auto storyboard_scene_presenter_t::op_space(storyboard_id_t id, std::string_view node, std::string_view op,
    storyboard_time_t time) const -> result_t<osbx_op_space_t, failure_code_t> {
    const auto source = std::ranges::find(impl_->sources, id, &storyboard_source_t::id);
    if (source == impl_->sources.end()) return std::unexpected(failure_code_t::unavailable);
    return source->op_space(node, op, time);
}
auto storyboard_scene_presenter_t::node_state(storyboard_id_t id, std::string_view node) const -> const osbx_node_frame_t* {
    const auto found = std::ranges::find(impl_->sources, id, &storyboard_source_t::id);
    return found == impl_->sources.end() ? nullptr : found->node_state(node);
}
auto storyboard_scene_presenter_t::node_diagnostic(storyboard_id_t id, std::string_view node) const -> std::string_view {
    const auto found = std::ranges::find_if(impl_->diagnostics, [&](const auto& item) {
        return item.storyboard == id && item.node == node;
    });
    return found == impl_->diagnostics.end() ? std::string_view{} : found->message;
}
void storyboard_scene_presenter_t::populate_image(ui::player_preview_visual_layer_view_t& view) const {
    impl_->populate_image(view);
}
auto storyboard_scene_presenter_t::prepare_image_thumbnail(ui::player_preview_visual_layer_view_t& view,
    storyboard_id_t board, std::string_view image) -> result_t<void, error_t<failure_code_t>> {
    view = {.static_image = true};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    auto& state = *impl_;
    if (!state.document)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_ready, "Storyboard image catalog is unbound."});
    const auto source = std::ranges::find(state.document->storyboards, board, &storyboard_document_t::id);
    if (source == state.document->storyboards.end() || !source->osbx)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found, "Storyboard image table is unavailable."});
    const auto authored = std::ranges::find(source->osbx->images, image, &osbx_image_t::id);
    if (authored == source->osbx->images.end())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found, "Storyboard image no longer exists."});
    view.resource_id = state.document->storyboard_resource(authored->file);
    if (!view.resource_id) {
        const auto resource = state.images.resolve(*state.document, authored->file, state.directory, true);
        if (!resource) return std::unexpected(resource.error());
        view.resource_id = *resource;
    }
    state.populate_image(view);
    return {};
#else
    static_cast<void>(board); static_cast<void>(image);
    return std::unexpected(error_t<failure_code_t>{failure_code_t::unsupported, "Image decoding is unavailable in this build."});
#endif
}
void storyboard_scene_presenter_t::finish_images(ui::player_preview_view_t& player, ui::storyboard_canvas_view_t* canvas
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    , visual::image_job_t& job
#endif
    , std::span<ui::player_preview_visual_layer_view_t* const> thumbnails) {
    auto& state = *impl_;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    const auto failure = state.request_images(player.visual_layers, player.storyboard_sprites,
        canvas ? std::span{canvas->sprites} : std::span<ui::player_preview_storyboard_sprite_view_t>{}, job, thumbnails);
    if (!failure.empty()) {
        if (player.visible) append_image_status(player.status_text, failure);
        if (canvas && canvas->available) append_image_status(canvas->status, failure);
    }
#endif
    for (auto& layer : player.visual_layers) if (layer.static_image) state.populate_image(layer);
    for (auto& sprite : player.storyboard_sprites) state.populate_image(sprite);
    if (canvas) for (auto& sprite : canvas->sprites) state.populate_image(sprite);
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    state.populate_thumbnails(thumbnails);
    if (player.visible) append_image_status(player.status_text, state.image_failure);
    if (canvas && canvas->available) append_image_status(canvas->status, state.image_failure);
    state.report_image_failures(player, canvas);
#else
    static_cast<void>(thumbnails);
#endif
}
} // namespace wormedit::sys
