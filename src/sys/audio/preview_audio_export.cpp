#include "sys/audio/preview_audio_export.h"
#include "core/text_util.h"
#include "sys/audio/offline_preview_mix.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/audio/audio_source_registration.h"
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/hitsound_resolver.h>
#include <wormedit/audio/offline_mix.h>
#include <wormedit/audio/ogg_encoder.h>
#include <wormedit/formats/export_cut_audio.h>
#include <wormedit/formats/conversion.h>
#if defined(WORMEDIT_HAS_LUA)
#include "scripting/lua_curve.h"
#include <wormedit/core/sv_recipe.h>
#endif
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

struct preview_audio_interval_t {
    std::uint64_t begin{}, end{};
    static auto from(const preview_audio_export_request_t& request)
        -> result_t<preview_audio_interval_t, error_t<failure_code_t>> {
        preview_audio_interval_t result;
        const auto* chart = request.document.read_chart(chart_id_t{request.settings.chart});
        if (!chart) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Preview chart is unavailable."});
        const auto timing = timing_index_t::from(*chart, {.allow_bounded_clock = true});
        if (!timing.value) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Preview timing is invalid."});
        const auto a = timing.value->seconds_at(request.settings.begin_row);
        const auto b = timing.value->seconds_at(request.settings.end_row);
        const auto first = a.seconds ? sample_frame_at(*a.seconds, audio::output_sample_rate).frame : std::nullopt;
        const auto last = b.seconds ? sample_frame_at(*b.seconds, audio::output_sample_rate).frame : std::nullopt;
        if (!first || !last || *last <= *first)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Select a nonempty materialized audio interval."});
        result.begin = *first;
        result.end = *last;
        if (request.settings.duration_seconds > 0) {
            const auto frames = static_cast<std::uint64_t>(std::llround(request.settings.duration_seconds * audio::output_sample_rate));
            result.end = result.begin + std::min(frames, result.end - result.begin);
        }
        return result;
    }
};


auto validate_cut_preview(const preview_audio_export_request_t& request, std::stop_token stop)
    -> result_t<void, error_t<failure_code_t>> {
    if (request.document.export_cuts.empty()) return {};
    const auto sources = prepare_export_cut_sources(request.document, request.asset_base, stop);
    if (!sources.ok()) return std::unexpected(error_t<failure_code_t>{
        stop.stop_requested() ? failure_code_t::cancelled : failure_code_t::invalid_request,
        sources.diagnostics.front().message});
    conversion_request_t conversion{.target = format_family_t::native_project,
        .chart = chart_id_t{request.settings.chart}, .loss_policy = loss_policy_t::warn};
    conversion.export_cut_sources = sources.value;
    if (const auto failure = prepare_conversion_slice_sources(request.document, conversion, request.asset_base, stop))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, *failure});
#if defined(WORMEDIT_HAS_LUA)
    scripting::lua_curve_backend_t scripts{{.cancellation = stop}};
    sv_recipe_evaluation_scope_t script_scope{scripts};
#endif
    const auto diagnostics = diagnose_conversion_preparation(request.document, conversion, {.stop = stop});
    if (diagnostic_queries::has_errors(diagnostics)) {
        const auto failure = std::ranges::find(diagnostics, diagnostic_severity_t::error,
            &conversion_diagnostic_t::severity);
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
            failure == diagnostics.end() ? "The selected preview cannot preserve the export-cut clock." : failure->message});
    }
    return {};
}

auto finish_cut_preview(std::vector<float>& pcm, const preview_audio_export_request_t& request,
    std::uint64_t source_begin) -> result_t<void, error_t<failure_code_t>> {
    if (request.document.export_cuts.empty()) return {};
    const auto timeline = export_cut_timeline_t::from(request.document.export_cuts);
    if (!timeline.ok()) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
        timeline.diagnostics.front().message});
    auto cut = cut_preview_audio(pcm, source_begin, timeline.value, audio::output_sample_rate);
    if (!cut.ok()) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
        cut.diagnostics.front().message});
    if (cut.value.empty()) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
        "Export cuts remove the complete selected preview interval."});
    pcm = std::move(cut.value);
    audio::apply_offline_mix_fade(pcm, request.settings.fade_out_ms);
    return {};
}
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
    if (auto valid = validate_cut_preview(request, stop); !valid) return std::unexpected(std::move(valid.error()));
    const auto interval = preview_audio_interval_t::from(request);
    if (!interval) return std::unexpected(interval.error());
    auto prepared = offline_preview_mix_t::from(request.document, chart_id_t{settings.chart},
        {.project = request.asset_base, .skin = request.skin_directory, .bundle = request.bundle_directory});
    if (!prepared) return std::unexpected(prepared.error());
    auto mixed = (*prepared)->render(interval->begin, interval->end,
        {.fade_out_ms = request.document.export_cuts.empty() ? settings.fade_out_ms : 0}, stop);
    if (!mixed) return std::unexpected(std::move(mixed.error()));
    if (auto cut = finish_cut_preview(mixed->interleaved_stereo, request, interval->begin); !cut)
        return std::unexpected(std::move(cut.error()));
    const auto output_frames = mixed->interleaved_stereo.size() / audio::output_channel_count;
    if (request.progress) request.progress->phase.store(job_phase_t::reading);
    auto encoded = audio::encode_ogg_vorbis(mixed->interleaved_stereo,
        {.quality = settings.quality, .progress = request.progress.get()}, stop);
    if (!encoded) return std::unexpected(std::move(encoded.error()));
    if (request.progress) { request.progress->phase.store(job_phase_t::publishing); request.progress->completed.store(0); request.progress->total.store(1); }
    auto file = prepared_atomic_file_t::from(*destination, *encoded, 16U * 1024U * 1024U,
        [&](const std::filesystem::path& temporary) -> atomic_file_save_outcome_t {
            auto verified = audio::verify_ogg_vorbis(temporary, output_frames, stop);
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
    const document_stamp_t stamp{.project = song_id_t{action.project}, .revision = action.revision};
    if (is_busy() || !stamp.is_current(document) || !document.read_chart(chart_id_t{action.chart}))
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
