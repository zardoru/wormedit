#include "sys/preview_audio_export.h"
#include "core/text_util.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/audio_source_registration.h"
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/hitsound_resolver.h>
#include <wormedit/audio/offline_mix.h>
#include <wormedit/audio/ogg_encoder.h>
#endif
#include <algorithm>
#include <cmath>

namespace wormedit::sys {
namespace {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto is_preview_asset_destination(const preview_audio_export_request_t& request,
    const std::filesystem::path& destination) -> bool {
    const auto matches = [&](const std::filesystem::path& source) {
        if (source.empty()) return false;
        std::error_code error;
        const auto path = std::filesystem::absolute(source.is_absolute() ? source : request.asset_base / source, error);
        if (!error && path.lexically_normal() == destination.lexically_normal()) return true;
        return std::filesystem::equivalent(path, destination, error) && !error;
    };
    for (const auto& asset : request.document.audio_assets) if (matches(asset.source_path)) return true;
    for (const auto& asset : request.document.visual_assets) if (matches(asset.source_path)) return true;
    return false;
}

auto prepare_preview_audio_destination(const preview_audio_export_request_t& request)
    -> result_t<std::filesystem::path, error_t<failure_code_t>> {
    const auto& settings = request.settings;
    if (!text_util::is_valid_bounded_text(settings.filename, 4096))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Invalid preview filename."});
    auto destination = std::filesystem::path{settings.filename};
    if (destination.empty() || (!destination.is_absolute() && request.asset_base.empty()))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Choose an output path or save the project first."});
    if (destination.extension().empty()) destination += ".ogg";
    if (text_util::ascii_lowercase(destination.extension().string()) != ".ogg")
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Preview audio must use an .ogg filename."});
    if (!destination.is_absolute()) destination = request.asset_base / destination;
    std::error_code error;
    destination = std::filesystem::absolute(destination, error).lexically_normal();
    if (error) return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, error.message()});
    if (is_preview_asset_destination(request, destination))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "Preview export cannot overwrite a project asset."});
    if (std::filesystem::exists(destination, error) && !settings.replace_existing)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "The preview file already exists; enable replacement or choose another name."});
    if (error) return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, error.message()});
    return destination;
}

struct preview_audio_mix_t {
    audio::decoded_source_registry_t sources;
    preview_schedule_t schedule;
    audio::preview_bindings_resolution_t bindings;
    std::uint64_t begin{}, end{};

    auto include_music(const song_document_t& document) -> result_t<void, error_t<failure_code_t>> {
        for (const auto& resource : document.resources) {
            if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end()) continue;
            const auto source = sources.open_source_binding(resource.asset);
            if (!source) return std::unexpected(source.error());
            bindings.append_music(resource.id, *source, schedule);
            break;
        }
        return {};
    }

    auto prepare(const preview_audio_export_request_t& request) -> result_t<void, error_t<failure_code_t>> {
        const auto* chart = request.document.find_chart(chart_id_t{request.settings.chart});
        if (!chart) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Preview chart is unavailable."});
        const auto timing = timing_index_t::from(*chart, {.allow_bounded_clock = true});
        if (!timing.value) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Preview timing is invalid."});
        const auto a = timing.value->seconds_at(request.settings.begin_row);
        const auto b = timing.value->seconds_at(request.settings.end_row);
        const auto first = a.seconds ? sample_frame_at(*a.seconds, audio::output_sample_rate).frame : std::nullopt;
        const auto last = b.seconds ? sample_frame_at(*b.seconds, audio::output_sample_rate).frame : std::nullopt;
        if (!first || !last || *last <= *first)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Select a nonempty materialized audio interval."});
        begin = *first;
        end = *last;
        if (request.settings.duration_seconds > 0) {
            const auto frames = static_cast<std::uint64_t>(std::llround(request.settings.duration_seconds * audio::output_sample_rate));
            end = begin + std::min(frames, end - begin);
        }
        (void)register_audition_sources(request.document, request.asset_base, 1, sources);
        auto built = preview_schedule_t::from(*chart, *timing.value,
            {.sample_rate = audio::output_sample_rate, .include_metronome = false});
        if (!built.value) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Preview schedule is invalid."});
        schedule = std::move(*built.value);
        audio::hitsound_resolver_t hitsounds;
        hitsounds.environment = {.map_directory = request.asset_base, .skin_directory = request.skin_directory,
            .bundle_directory = request.bundle_directory};
        const auto extras = hitsounds.bindings(request.document, *chart, sources, 1);
        auto resolved = audio::preview_bindings_resolution_t::from(request.document, chart->id, schedule,
            sources.bindings(), {.hitsounds = extras});
        if (!resolved.diagnostics.empty())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, resolved.diagnostics.front().message});
        bindings = std::move(resolved.value);
        return include_music(request.document);
    }
};
#endif
}

auto prepare_preview_audio_export(const preview_audio_export_request_t& request, std::stop_token stop)
    -> preview_audio_export_job_t::work_outcome_t {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    const auto& settings = request.settings;
    if (!std::isfinite(settings.duration_seconds) || settings.duration_seconds < 0 || settings.duration_seconds > 600 ||
        settings.fade_out_ms > 10000 || settings.begin_row >= settings.end_row)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Invalid preview duration, fade, or selection."});
    const auto destination = prepare_preview_audio_destination(request);
    if (!destination) return std::unexpected(destination.error());
    if (stop.stop_requested()) return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "Preview export cancelled."});
    preview_audio_mix_t prepared;
    if (auto setup = prepared.prepare(request); !setup) return std::unexpected(std::move(setup.error()));
    auto mixed = audio::render_offline_interval(prepared.sources, prepared.schedule, prepared.bindings.bindings,
        prepared.begin, prepared.end, {.fade_out_ms = settings.fade_out_ms}, stop);
    if (!mixed) return std::unexpected(std::move(mixed.error()));
    if (request.progress) request.progress->phase.store(job_phase_t::reading);
    auto encoded = audio::encode_ogg_vorbis(mixed->interleaved_stereo,
        {.quality = settings.quality, .progress = request.progress.get()}, stop);
    if (!encoded) return std::unexpected(std::move(encoded.error()));
    if (request.progress) { request.progress->phase.store(job_phase_t::publishing); request.progress->completed.store(0); request.progress->total.store(1); }
    auto file = prepared_atomic_file_t::from(*destination, *encoded, 16U * 1024U * 1024U,
        [&](const std::filesystem::path& temporary) -> atomic_file_save_outcome_t {
            auto verified = audio::verify_ogg_vorbis(temporary, prepared.end - prepared.begin, stop);
            if (!verified) return std::unexpected(verified.error().message);
            return {};
        }, stop);
    if (!file) return std::unexpected(error_t<failure_code_t>{stop.stop_requested() ? failure_code_t::cancelled : failure_code_t::io_failed, file.error()});
    if (request.progress) request.progress->completed.store(1);
    return preview_audio_prepared_t{.file = std::make_shared<prepared_atomic_file_t>(std::move(*file)),
        .destination = *destination, .replace_existing = settings.replace_existing};
#else
    static_cast<void>(request); static_cast<void>(stop);
    return std::unexpected(error_t<failure_code_t>{failure_code_t::unsupported, "This build has no preview audio encoder."});
#endif
}

runtime::preview_audio_export_t::preview_audio_export_t(task_scheduler_t& scheduler, notification_sink_t notifications)
    : notifications_(std::move(notifications)), preview_audio_job_(scheduler, 1,
        "Preview export could not start.", "Preview export capacity exceeded.", "Preview export returned no result.",
        async_job_destruction_policy_t::release) {}

auto runtime::preview_audio_export_t::start(const song_document_t& document,
    const ui::preview_audio_export_action_t& action, const std::filesystem::path& asset_base,
    const std::filesystem::path& skin, const std::filesystem::path& bundle) -> async_job_start_outcome_t {
    const document_stamp_t stamp{song_id_t{action.project}, action.revision};
    if (is_busy() || !stamp.is_current(document) || !document.find_chart(chart_id_t{action.chart}))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "Preview export is busy or its selection is stale."});
    auto progress = std::make_shared<job_progress_t>();
    auto started = preview_audio_job_.request([&] {
        return preview_audio_export_request_t{.document = document, .settings = action,
            .asset_base = asset_base, .skin_directory = skin, .bundle_directory = bundle, .progress = progress};
    }, {.document = stamp, .chart = chart_id_t{action.chart}}, prepare_preview_audio_export);
    if (started) { preview_audio_progress_ = std::move(progress); preview_audio_cancelling_ = false; }
    return started;
}

auto runtime::preview_audio_export_t::poll(const document_history_t& history,
    const workspace_controller_t& workspace) -> std::optional<std::string> {
    auto completed = preview_audio_job_.poll();
    if (!completed) return {};
    preview_audio_progress_.reset();
    const auto cancelled = std::exchange(preview_audio_cancelling_, false);
    std::string message;
    auto severity = notification_severity_t::error;
    if (cancelled) { message = "Preview export cancelled."; severity = notification_severity_t::info; }
    else if (!*completed) message = completed->error().cause.message;
    else if (!completed->value().identity.document.is_current(history.document) ||
             workspace.selected_chart != completed->value().identity.chart) {
        message = "Preview export discarded because the document or chart changed.";
        severity = notification_severity_t::warning;
    } else {
        const auto& prepared = *completed->value().payload;
        const auto published = prepared.file->publish(prepared.replace_existing);
        message = published ? "Preview audio exported: " + prepared.destination.string() : published.error();
        if (published) severity = notification_severity_t::success;
    }
    notifications_.post(severity, notification_source_t::export_, message);
    return message;
}
auto runtime::preview_audio_export_t::view() const -> ui::file_job_view_t {
    const auto* progress = preview_audio_progress_.get();
    return {.active = preview_audio_job_.is_busy(), .cancelling = preview_audio_cancelling_,
        .completed = progress ? progress->completed.load() : 0, .total = progress ? progress->total.load() : 0,
        .label = !progress || progress->phase.load() == job_phase_t::preparing ? "Mixing preview audio" :
            progress->phase.load() == job_phase_t::reading ? "Encoding preview audio" : "Verifying preview audio"};
}
void runtime::preview_audio_export_t::cancel() {
    if (!preview_audio_job_.is_busy()) return;
    preview_audio_cancelling_ = true;
    preview_audio_job_.cancel(async_job_cancel_policy_t::retain_completion);
}
}
