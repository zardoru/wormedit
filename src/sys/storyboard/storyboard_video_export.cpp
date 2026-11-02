#include "sys/storyboard/storyboard_video_export.h"
#include "core/checked_math.h"
#include "core/text_util.h"
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/storyboard_preview.h>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>

namespace wormedit::sys {
namespace {
using status_t = result_t<void, error_t<failure_code_t>>;
auto board(const storyboard_video_request_t& request) -> const storyboard_document_t* {
    const auto found = std::ranges::find(request.document.storyboards, request.identity.storyboard, &storyboard_document_t::id);
    return found == request.document.storyboards.end() ? nullptr : &*found;
}
auto failure(std::string message, failure_code_t code = failure_code_t::invalid_request) -> std::unexpected<error_t<failure_code_t>> {
    return std::unexpected(error_t{code, std::move(message)});
}
#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_AUDIO_DECODER)
auto destination(const storyboard_video_request_t& request) -> result_t<std::filesystem::path, error_t<failure_code_t>> {
    if (!text_util::is_valid_bounded_text(request.settings.filename, 4096)) return failure("Choose a valid output filename.");
    auto path = text_util::path_from_utf8(request.settings.filename);
    if (path.empty() || (!path.is_absolute() && request.paths.project.empty()))
        return failure("Save the project or choose an absolute destination with Browse.");
    if (!path.is_absolute()) path = request.paths.project / path;
    std::error_code error;
    path = std::filesystem::absolute(path, error).lexically_normal();
    if (error) return failure(error.message(), failure_code_t::io_failed);
    const auto expected = request.settings.profile == media::video_profile_t::mp4_h264_aac ? ".mp4" : ".webm";
    if (text_util::ascii_lowercase(path.extension().string()) != expected) return failure("Filename extension must match the selected profile.");
    const auto matches = [&](const std::filesystem::path& source) {
        const auto asset = source.is_absolute() ? source : request.paths.project / source;
        return std::filesystem::equivalent(asset, path, error) || asset.lexically_normal() == path;
    };
    for (const auto& asset : request.document.audio_assets) if (matches(asset.source_path)) return failure("Video cannot overwrite a project asset.");
    for (const auto& asset : request.document.visual_assets) if (matches(asset.source_path)) return failure("Video cannot overwrite a project asset.");
    error.clear();
    if (!request.probe && std::filesystem::exists(path, error) && !request.settings.replace_existing)
        return failure("Output exists. Confirm replacement or choose another filename.", failure_code_t::conflict);
    if (error) return failure(error.message(), failure_code_t::io_failed);
    return path;
}
#endif
}

namespace {
class video_blocker_writer_t {
    std::vector<std::string>& values_;
    std::size_t used_{};
    auto next() -> std::string& {
        if (used_ == values_.size()) values_.emplace_back();
        auto& value = values_[used_++];
        value.clear();
        return value;
    }
  public:
    explicit video_blocker_writer_t(std::vector<std::string>& values) : values_(values) {}
    void append(std::string_view message) { next().append(message); }
    void append_link(std::string_view node, std::string_view target) {
        next().append(node).append("/").append(target).append(
            ": prepare the current Lua link successfully before exporting.");
    }
    void finish() { values_.resize(used_); }
};
void collect_media_blockers(const osbx_document_t& scene, video_blocker_writer_t& result) {
    bool animation = false, sample = false;
    for (const auto& node : scene.nodes) {
        animation = animation || node.kind == osbx_node_kind_t::animation;
        sample = sample || node.kind == osbx_node_kind_t::sample;
        if (animation && sample) break;
    }
    if (animation) result.append("OSBX Animation video export is unavailable: numbered-frame video assets are not implemented.");
    if (sample) result.append("OSBX Sample video export is unavailable: storyboard sample audio mixing is not implemented.");
}
void collect_video_blockers(const storyboard_video_request_t& request, video_blocker_writer_t& result) {
    const auto* source = board(request);
    if (!source) { result.append("Choose a storyboard."); return; }
    if (!request.document.read_chart(request.chart)) result.append("Choose a chart for the song clock and audio mix.");
    if (!source->osbx) return;
    if (!has_osbx_lua_links(*source->osbx)) { collect_media_blockers(*source->osbx, result); return; }
    if (!request.prepared) { result.append("Lua links have no successful prepared result."); return; }
    osbx_lua_prepared_scope_t scope{*request.prepared, request.prepared_generation};
    const auto* prepared = osbx_lua_prepared_scope_t::prepared(*source->osbx, request.identity.stamp,
        source->id, request.prepared_generation);
    if (!prepared) { result.append("Lua preparation is missing or stale for this storyboard revision."); return; }
    for (const auto& link : prepared->links)
        if (link.is_last_good) result.append_link(link.node_id, link.target);
    for (const auto& diagnostic : prepared->diagnostics) result.append(diagnostic.message);
    if (!prepared->complete || !prepared->scene) result.append("One or more Lua links have no complete prepared scene.");
    if (prepared->scene) collect_media_blockers(*prepared->scene, result);
}
void refill_video_blockers(const storyboard_video_request_t* request,
    std::vector<std::string>& values, bool unavailable = false) {
    video_blocker_writer_t result{values};
    if (request) collect_video_blockers(*request, result);
    if (unavailable) result.append("Video export unavailable: image/audio decoding and the optional video encoder are required.");
    result.finish();
}
}
auto storyboard_video_blockers(const storyboard_video_request_t& request) -> std::vector<std::string> {
    std::vector<std::string> result;
    refill_video_blockers(&request, result);
    return result;
}

#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace {
class video_scene_t {
    const storyboard_video_request_t& request_;
    const storyboard_document_t& source_;
    std::optional<osbx_evaluation_index_t> osbx_;
    std::optional<storyboard_preview_index_t> osb_;
    std::map<std::string_view, storyboard_draw_instance_t> templates_;
    std::vector<storyboard_draw_instance_t> draws_;
    std::vector<storyboard_raster_image_t> images_;
    visual::image_cache_t cache_;
    frame_arena_t arena_;
    std::optional<storyboard_raster_t> raster_;
    auto load_images(std::span<const std::string> paths) -> status_t {
        std::vector<storyboard_raster_image_source_t> sources;
        std::uint64_t id = 1;
        for (const auto& path : paths) {
            const auto resolved = visual::resolved_visual_asset_t::from_project_image(text_util::path_from_utf8(path), resource_id_t{id++}, request_.paths.project);
            if (!resolved) return std::unexpected(resolved.error());
            sources.push_back({.path = path, .source = *resolved});
        }
        auto loaded = storyboard_raster_images_t::from(request_.document.id, request_.document.revision, sources, cache_);
        if (!loaded.value) return failure(loaded.diagnostics.empty() ? "Image cache budget exceeded." : loaded.diagnostics.front().message);
        images_ = std::move(loaded.value->bindings);
        return {};
    }
    auto prepare_osbx(std::set<std::string>& paths) -> status_t {
        auto index = osbx_evaluation_index_t::from(*source_.osbx);
        if (!index.value) return failure(index.diagnostics.empty() ? "Invalid OSBX scene." : index.diagnostics.front().message);
        osbx_ = std::move(*index.value);
        const auto& scene = osbx_->prepared_owner ? *osbx_->prepared_owner : *source_.osbx;
        for (const auto i : osbx_->draw_order) {
            const auto& node = *osbx_->nodes[i].source;
            if (node.kind != osbx_node_kind_t::sprite) continue;
            const auto image = std::ranges::find(scene.images, *node.image, &osbx_image_t::id);
            if (image == scene.images.end()) return failure("Scene refers to a missing image.");
            auto draw = storyboard_draw_instance_t::from(scene, node, *image);
            draw.stable_id = i + 1;
            templates_.emplace(node.id, draw);
            paths.insert(image->file);
        }
        draws_.reserve(templates_.size());
        return {};
    }
    auto evaluate(storyboard_time_t time) -> result_t<std::span<const storyboard_draw_instance_t>, error_t<failure_code_t>> {
        arena_.reset();
        if (osb_) {
            const auto frame = evaluate_storyboard_frame(*source_.model, *osb_, time, arena_, {.conditional_layer = request_.settings.layers});
            if (!frame.is_success()) return failure(frame.diagnostic ? frame.diagnostic->message : "OSB frame storage exhausted.", failure_code_t::render_failed);
            return frame.instances;
        }
        const auto frame = evaluate_osbx_frame(*osbx_, time, arena_, {.conditional_layer = request_.settings.layers});
        if (!frame.is_success()) return failure(frame.diagnostics.empty() ? "OSBX frame failed." : std::string{frame.diagnostics.front().message}, failure_code_t::render_failed);
        draws_.clear();
        for (const auto& draw : frame.instances) {
            const auto source = templates_.find(draw.id);
            if (source == templates_.end()) return failure("Evaluated sprite has no image template.");
            draws_.push_back(source->second.with_evaluation(draw));
        }
        return std::span<const storyboard_draw_instance_t>{draws_};
    }
  public:
    explicit video_scene_t(const storyboard_video_request_t& request) : request_(request), source_(*board(request)) {}
    auto prepare(const media::video_encoder_options_t& options) -> status_t {
        std::set<std::string> paths;
        if (source_.osbx) {
            if (auto result = prepare_osbx(paths); !result) return result;
        } else if (source_.model) {
            auto index = storyboard_preview_index_t::from(*source_.model);
            if (!index.value) return failure("Invalid OSB storyboard.");
            osb_ = std::move(*index.value);
            for (const auto& sprite : source_.model->sprites) {
                if (sprite.animation) return failure("Animated OSB assets are not supported by the current preview renderer.", failure_code_t::unsupported);
                paths.insert(sprite.path);
            }
        } else return failure("Storyboard has no scene.");
        if (paths.size() > storyboard_raster_options_t::maximum_supported_instances) return failure("Image count exceeds the frame budget.");
        const std::vector<std::string> ordered{paths.begin(), paths.end()};
        if (auto result = load_images(ordered); !result) return result;
        auto raster = storyboard_raster_t::from({.width = options.width, .height = options.height, .threads = options.threads, .frame_slots = 2});
        if (!raster) return std::unexpected(raster.error());
        raster_ = std::move(*raster);
        return {};
    }
    auto frame(std::uint64_t, storyboard_time_t time, storyboard_conditional_layer_t, std::stop_token stop)
        -> result_t<storyboard_raster_frame_t, error_t<failure_code_t>> {
        if (stop.stop_requested()) return failure("Video export cancelled.", failure_code_t::cancelled);
        auto draw = evaluate(time);
        if (!draw) return std::unexpected(draw.error());
        return raster_->render(*draw, images_, arena_, request_.settings.layers);
    }
};

struct video_plan_t { media::video_encoder_options_t options; std::uint64_t source_end{}; };
auto frame_options(const storyboard_video_request_t& request, const std::filesystem::path& path,
    std::uint64_t song_end) -> result_t<video_plan_t, error_t<failure_code_t>> {
    std::uint64_t begin{}, end = song_end;
    if (request.settings.selection) {
        if (!request.selection || !request.selection->is_playable()) return failure("Select keys at two different timeline times.");
        const auto a = sample_frame_at({request.selection->begin_ms, 1000}, audio::output_sample_rate);
        const auto b = sample_frame_at({request.selection->end_ms, 1000}, audio::output_sample_rate);
        if (!a.frame || !b.frame) return failure("Selected timestamp exceeds the native audio clock.");
        begin = *a.frame; end = *b.frame;
    }
    const auto fps = request.settings.fps;
    if ((fps != 30 && fps != 60) || end <= begin) return failure("Choose a nonempty range and 30 or 60 fps.");
    const auto step = audio::output_sample_rate / fps;
    // Absolute ms edges quantize once to native sample frames. The terminal
    // duration rounds outward by <one video frame, retaining the same origin.
    const auto count = (end - begin) / step + ((end - begin) % step != 0 ? 1 : 0);
    media::video_encoder_options_t result{.destination = path, .profile = request.settings.profile,
        .width = request.settings.width, .height = request.settings.height, .clock = {.numerator = fps},
        .frame_count = count, .begin_audio_frame = begin, .conditional_layer = request.settings.layers,
        .replace_existing = request.settings.replace_existing};
    if (!result.is_valid()) return failure("Selected frame range exceeds the encoder limits.");
    return video_plan_t{.options = std::move(result), .source_end = end};
}
auto estimate(const media::video_encoder_options_t& options, bool audio) -> result_t<storyboard_video_result_t, error_t<failure_code_t>> {
    const auto bit_rate = options.estimated_video_bit_rate() +
        (audio ? media::video_encoder_options_t::audio_target_bit_rate : 0U);
    const auto bits = checked_math::checked_multiply(options.frame_count, bit_rate);
    if (!bits) return failure("Estimated output size overflow.", failure_code_t::arithmetic_overflow);
    const auto bytes = *bits / options.clock.numerator / 8;
    std::uint64_t reserved{};
    if (!checked_math::checked_add(bytes, bytes / 4 + 64U * 1024U * 1024U, reserved)) return failure("Disk estimate overflow.");
    std::error_code error;
    const auto disk = std::filesystem::space(options.destination.parent_path(), error);
    if (error) return failure("Cannot check destination free space: " + error.message(), failure_code_t::io_failed);
    if (disk.available < reserved) return failure("Insufficient free disk space for the estimated output and safety reserve.", failure_code_t::capacity_exceeded);
    if (bytes > options.maximum_output_bytes) return failure("Estimated output exceeds the bounded encoder file limit.", failure_code_t::capacity_exceeded);
    // Canonical VID1 example measurements, not codec throughput or guarantees.
    const auto measured = options.height == 2160 ? 1.489 : 5.465;
    return storyboard_video_result_t{.destination = options.destination, .total = options.frame_count,
        .estimated_bytes = bytes, .free_bytes = disk.available,
        .estimated_seconds = static_cast<double>(options.frame_count) / measured};
}
auto inspect_capabilities(const storyboard_video_request_t& request, const media::video_encoder_options_t& options,
    storyboard_video_result_t result, std::stop_token stop) -> storyboard_video_job_t::work_outcome_t {
    for (const auto& [width, height] : std::array{std::pair{640U,480U}, std::pair{1280U,720U}, std::pair{1920U,1080U}, std::pair{3840U,2160U}}) {
        if (stop.stop_requested()) return failure("Encoder check cancelled.", failure_code_t::cancelled);
        auto selected = options; selected.width = width; selected.height = height;
        const auto checked = media::video_encoder_t::probe(selected, request.settings.audio != ui::storyboard_video_audio_t::none, stop);
        result.capabilities.push_back({.width = width, .height = height, .available = checked.has_value(),
            .reason = checked ? std::string{} : checked.error().message});
    }
    return result;
}
}
#endif

auto run_storyboard_video(const storyboard_video_request_t& request, std::stop_token stop)
    -> storyboard_video_job_t::work_outcome_t {
#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (!media::video_encoder_t::is_available()) return failure("Video export unavailable: this build has no video encoder.", failure_code_t::unavailable);
    const auto blockers = storyboard_video_blockers(request);
    if (!blockers.empty()) return failure(blockers.front());
    const auto path = destination(request);
    if (!path) return std::unexpected(path.error());
    auto mix = offline_preview_mix_t::from(request.document, request.chart, request.paths,
        request.settings.audio == ui::storyboard_video_audio_t::song_and_samples,
        request.settings.audio != ui::storyboard_video_audio_t::none);
    if (!mix) return std::unexpected(mix.error());
    if (stop.stop_requested()) return failure("Video export cancelled.", failure_code_t::cancelled);
    const auto plan = frame_options(request, *path, (*mix)->end_frame());
    if (!plan) return std::unexpected(plan.error());
    const auto& options = plan->options;
    auto result = estimate(options, request.settings.audio != ui::storyboard_video_audio_t::none);
    if (!result) return std::unexpected(result.error());
    if (request.probe) return inspect_capabilities(request, options, std::move(*result), stop);
    const std::vector<osbx_lua_prepared_scene_t> empty;
    osbx_lua_prepared_scope_t scope{request.prepared ? std::span<const osbx_lua_prepared_scene_t>{*request.prepared} : std::span<const osbx_lua_prepared_scene_t>{empty}, request.prepared_generation};
    video_scene_t scene{request};
    if (auto ready = scene.prepare(options); !ready) return std::unexpected(ready.error());
    media::video_encoder_sources_t sources{.frame = [&](std::uint64_t index, storyboard_time_t time,
        storyboard_conditional_layer_t choice, std::stop_token token) { return scene.frame(index, time, choice, token); }};
    if (request.settings.audio != ui::storyboard_video_audio_t::none) {
        sources.audio = [&](const audio::offline_mix_sink_t& sink, std::stop_token token) {
            auto rendered = (*mix)->render_to(options.begin_audio_frame, plan->source_end, sink, token);
            if (!rendered) return rendered;
            const auto end = options.begin_audio_frame + *options.input_audio_frames();
            const std::array<float, 2940> silence{}; // At most one 30fps stereo frame.
            return end == plan->source_end ? status_t{} : sink(plan->source_end,
                std::span<const float>{silence}.first(static_cast<std::size_t>(end - plan->source_end) * audio::output_channel_count));
        };
    }
    auto encoded = media::video_encoder_t::encode(options, sources, stop, [&](media::video_encoder_progress_t progress) {
        if (request.progress) { request.progress->total.store(progress.total_frames); request.progress->completed.store(progress.completed_frames); }
    });
    if (!encoded) return std::unexpected(encoded.error());
    result->encoded = std::move(*encoded);
    return std::move(*result);
#else
    (void)request; (void)stop;
    return failure("Video export unavailable: this build needs image/audio decoding and the optional encoder.", failure_code_t::unavailable);
#endif
}

storyboard_video_export_t::storyboard_video_export_t(task_scheduler_t& scheduler,
    notification_sink_t notifications, storyboard_video_job_t::work_function_t work)
    : probe_(scheduler, 1, "Encoder check could not start.", "Encoder check is stopping.", "Encoder check returned no result.", async_job_destruction_policy_t::release),
      export_(scheduler, 1, "Video export could not start.", "Video export is stopping.", "Video export returned no result.", async_job_destruction_policy_t::release),
      notifications_(std::move(notifications)), work_(work) {}
void storyboard_video_export_t::remember() {
    if (!draft_) return;
    const auto project = draft_->document.id;
    std::erase_if(remembered_, [&](const remembered_t& item) { return item.project == project; });
    remembered_.push_back({.project = project, .settings = draft_->settings});
    if (remembered_.size() > 32) remembered_.pop_front();
}
void storyboard_video_export_t::open(storyboard_video_request_t request) {
    if (is_busy()) { open_ = true; return; }
    const auto remembered = std::ranges::find(remembered_, request.document.id, &remembered_t::project);
    if (remembered != remembered_.end()) request.settings = remembered->settings;
    draft_ = std::move(request);
    open_ = true;
    configure(draft_->settings);
}
void storyboard_video_export_t::configure(const ui::storyboard_video_settings_t& settings) {
    if (!draft_ || is_busy()) return;
    draft_->settings = settings;
    draft_->identity.generation = ++generation_;
    probe_.cancel();
    inspected_ = {};
    status_.clear();
    needs_probe_ = true;
    remember();
}
void storyboard_video_export_t::request_probe() {
    if (!draft_ || !needs_probe_ || probe_.is_busy()) return;
    const auto blockers = storyboard_video_blockers(*draft_);
    if (!blockers.empty()) { status_ = blockers.front(); needs_probe_ = false; return; }
    const auto started = probe_.request([this] {
        auto request = *draft_;
        request.probe = true;
        return request;
    }, draft_->identity, work_);
    if (started) needs_probe_ = false;
}
void storyboard_video_export_t::close() { open_ = false; probe_.cancel(); needs_probe_ = false; }
auto storyboard_video_export_t::start(storyboard_video_request_t request) -> async_job_start_outcome_t {
    const auto current = view();
    if (!draft_ || !current.can_start || request.document.id != draft_->document.id || request.identity.session != draft_->identity.session || request.identity.storyboard != draft_->identity.storyboard)
        return failure("Video selection is stale, busy, blocked, or still checking encoders.", failure_code_t::conflict);
    request.settings = draft_->settings;
    request.probe = false; // Start always publishes; capability work never does.
    request.identity.generation = generation_;
    request.progress = std::make_shared<job_progress_t>();
    request.progress->total.store(inspected_.total);
    const auto started = export_.request([&] { return request; }, request.identity, work_,
        task_cancel_result_policy_t::retain_running_result);
    if (started) {
        progress_ = request.progress;
        started_ = std::chrono::steady_clock::now();
        cancelling_ = false;
        open_ = false;
        status_ = "Rendering snapshot and encoding video.";
    }
    return started;
}
void storyboard_video_export_t::cancel() {
    if (!is_busy()) return;
    cancelling_ = true;
    export_.cancel(async_job_cancel_policy_t::retain_completion);
}
void storyboard_video_export_t::cancel_project(song_id_t project) {
    if (!draft_ || draft_->document.id != project) return;
    cancel(); close();
}
void storyboard_video_export_t::poll() {
    if (auto result = probe_.poll()) {
        if (*result && result->value().identity.generation == generation_) {
            inspected_ = *result->value().payload;
            status_.clear();
        } else if (!*result) status_ = result->error().cause.message;
    }
    if (open_) request_probe();
    auto result = export_.poll();
    if (!result) return;
    // Successful validated publication is authoritative, even if Cancel was
    // requested after the atomic commit point. Never stale-discard edits.
    if (*result && !result->value().payload->encoded) {
        status_ = "Video worker completed without a validated publication report.";
        notifications_.post(notification_severity_t::error, notification_source_t::export_, status_);
    } else if (*result) {
        const auto& exported = *result->value().payload;
        status_ = "Video exported: " + text_util::path_utf8(exported.destination);
        notifications_.post({.severity = notification_severity_t::success, .source = notification_source_t::export_,
            .message = status_, .target = notification_file_t{.path = exported.destination}});
    } else {
        const auto& error = result->error().cause;
        status_ = error.message;
        notifications_.post(error.code == failure_code_t::cancelled ? notification_severity_t::info : notification_severity_t::error,
            notification_source_t::export_, status_);
    }
    progress_.reset();
    cancelling_ = false;
}
auto storyboard_video_export_t::view() const -> ui::storyboard_video_export_view_t {
    ui::storyboard_video_export_view_t result;
    refill(result);
    return result;
}
void storyboard_video_export_t::refill(ui::storyboard_video_export_view_t& result) const {
    result.open = open_;
    result.available = media::video_encoder_t::is_available();
    result.probing = probe_.is_busy() || needs_probe_;
    result.running = is_busy();
    result.cancelling = cancelling_;
    result.generation = generation_;
    result.completed = progress_ ? progress_->completed.load() : 0;
    result.total = progress_ ? progress_->total.load() : inspected_.total;
    result.estimated_bytes = inspected_.estimated_bytes;
    result.free_bytes = inspected_.free_bytes;
    result.estimated_seconds = inspected_.estimated_seconds;
    result.capabilities = inspected_.capabilities;
    result.status = status_;
    if (draft_) result.settings = draft_->settings;
    else result.settings = ui::storyboard_video_settings_t{};
    result.selection = draft_ ? draft_->selection : std::nullopt;
    result.source_clock_with_cuts = draft_ && !draft_->document.export_cuts.empty();
    result.elapsed_seconds = 0;
    result.remaining_seconds = 0;
#if !defined(WORMEDIT_HAS_IMAGE_DECODER) || !defined(WORMEDIT_HAS_AUDIO_DECODER)
    result.available = false;
#endif
    refill_video_blockers(draft_ ? &*draft_ : nullptr, result.blockers, !result.available);
    const auto selected = std::ranges::find_if(result.capabilities, [&](const ui::storyboard_video_capability_t& item) {
        return item.width == result.settings.width && item.height == result.settings.height;
    });
    result.can_start = result.available && !result.running && !result.probing && result.blockers.empty() &&
        selected != result.capabilities.end() && selected->available;
    if (is_busy()) {
        result.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        if (result.completed) result.remaining_seconds = result.elapsed_seconds *
            static_cast<double>(result.total - std::min(result.total, result.completed)) / static_cast<double>(result.completed);
    }
}
auto storyboard_video_export_t::file_view() const -> ui::file_job_view_t {
    return {.active = is_busy(), .cancelling = cancelling_, .completed = progress_ ? progress_->completed.load() : 0,
        .total = progress_ ? progress_->total.load() : 0, .label = "Rendering and encoding storyboard video"};
}
} // namespace wormedit::sys
