#include "sys/audio/audio_slicer_runtime.h"
#include "sys/commands/document_edit_feedback.h"
#include <wormedit/core/numeric_text.h>
#include <chrono>
#include <algorithm>
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/slice_render.h>
#endif

namespace wormedit::sys {
auto build_audio_slicer_plan(const ui::audio_slicer_settings_t& settings, std::uint32_t rate,
    std::uint64_t frames) -> result_t<audio_slice_plan_t, std::string> {
    const auto bpm = parse_exact_decimal(settings.bpm);
    const auto offset = parse_exact_decimal(settings.offset);
    std::uint32_t division{};
    if (!bpm || bpm->numerator <= 0) return std::unexpected("Enter a positive BPM.");
    if (!offset) return std::unexpected("Enter an exact offset in seconds.");
    if (!parse_integer(settings.division, division)) return std::unexpected("Enter an integer division (1–1024).");
    auto planned = audio_slice_plan_t::from({
        .beats_per_minute = {static_cast<std::uint64_t>(bpm->numerator), bpm->denominator},
        .division = division, .offset_seconds = *offset, .sample_rate = rate,
        .source_frames = frames, .maximum_slices = maximum_bulk_sound_import});
    if (!planned.ok()) return std::unexpected(planned.diagnostics.front().message);
    return std::move(planned.value);
}
void refill_audio_slicer_view(ui::audio_slicer_view_t& view, const ui::audio_slicer_settings_t& settings,
    std::uint32_t rate, std::uint64_t frames, bool can_slice) {
    view.cut_frames.clear();
    view.can_slice = false;
    if (!rate || !frames) { view.message = "Select an available sound to slice."; return; }
    const auto plan = build_audio_slicer_plan(settings, rate, frames);
    if (!plan) { view.message = plan.error(); return; }
    for (const auto& range : plan->ranges) if (range.begin_frame) view.cut_frames.push_back(range.begin_frame);
    view.can_slice = can_slice;
    view.message = std::to_string(plan->ranges.size()) + " slices";
    if (!can_slice) view.message += " · save the project and wait for file jobs to finish";
}
audio_slicer_files_t::~audio_slicer_files_t() {
    if (retained || directory.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace {
auto create_slice_directory(const audio_slicer_request_t& request)
    -> result_t<std::filesystem::path, error_t<failure_code_t>> {
    const auto parent = request.project_directory / request.relative_directory;
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, error.message()});
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto directory = parent / ("slices-" + std::to_string(nonce) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(directory, error)) return directory;
        if (error) return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, error.message()});
    }
    return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "Cannot create a unique slice directory."});
}
}
#endif
auto prepare_audio_slices(const audio_slicer_request_t& request, std::stop_token stop)
    -> audio_slicer_job_t::work_outcome_t {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    audio::decoder_t decoder;
    std::string error;
    if (!decoder.open(request.source, &error))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed, error});
    const auto plan = build_audio_slicer_plan(request.settings, audio::output_sample_rate, decoder.info()->output_frame_count);
    if (!plan) return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, plan.error()});
    auto directory = create_slice_directory(request);
    if (!directory) return std::unexpected(directory.error());
    auto files = std::make_shared<audio_slicer_files_t>();
    files->directory = *directory;
    const auto rendered = audio::rendered_audio_slices_t::from(decoder, plan->ranges,
        {.directory = *directory, .base_name = request.source.stem().string(), .stop = stop,
         .progress = [&request](std::size_t done, std::size_t total) {
             if (request.progress) { request.progress->total.store(total); request.progress->completed.store(done); }
         }, .maximum_slices = maximum_bulk_sound_import});
    if (!rendered) return std::unexpected(rendered.error());
    files->import.chart = request.chart;
    for (std::size_t i = 0; i < rendered->files.size(); ++i) {
        const auto& file = rendered->files[i];
        const auto frames = plan->ranges[i].end_frame - plan->ranges[i].begin_frame;
        files->import.entries.push_back({.asset = {.source_path = file,
            .role = resource_role_t::sample, .name = file.stem().string(),
            .allocate_bms_identifier = request.allocate_bms_ids,
            .audio_stream = audio_stream_info_t{.sample_rate = audio::output_sample_rate,
                .channels = 2, .frame_count = frames, .seekable = true}}, .kind = sound_kind_t::keysound});
    }
    return files;
#else
    static_cast<void>(request); static_cast<void>(stop);
    return std::unexpected(error_t<failure_code_t>{failure_code_t::unsupported, "Audio decoding is unavailable."});
#endif
}
runtime::audio_slicer_t::audio_slicer_t(task_scheduler_t& scheduler, notification_sink_t notifications)
    : notifications_(std::move(notifications)), job_(scheduler, 1, "Slicing could not start.",
        "Slicing capacity exceeded.", "Slicing returned no result.", async_job_destruction_policy_t::release) {}
auto runtime::audio_slicer_t::start(const song_document_t& document, const ui::audio_slicer_action_t& action,
    const std::filesystem::path& base) -> async_job_start_outcome_t {
    const chart_stamp_t stamp{{action.project, action.revision}, action.chart};
    const auto* chart = document.find_chart(action.chart);
    const auto resource = std::ranges::find(document.resources, action.resource, &resource_entry_t::id);
    const auto* asset = document.find_audio_asset(action.asset);
    if (is_busy() || !stamp.is_current(document, action.chart) || !chart || resource == document.resources.end() || !asset ||
        resource->asset != action.asset || base.empty())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict, "Save the project and select a current sound before slicing."});
    auto progress = std::make_shared<job_progress_t>();
    auto started = job_.request([&] {
        return audio_slicer_request_t{.settings = action.settings, .source = base / asset->source_path,
            .project_directory = base, .relative_directory = document.settings.audio_directory(),
            .chart = action.chart, .allocate_bms_ids = chart->source_family == format_family_t::bms,
            .progress = progress};
    }, stamp, prepare_audio_slices);
    if (started) { progress_ = std::move(progress); cancelling_ = false; }
    return started;
}
void runtime::audio_slicer_t::poll(document_history_t& history, workspace_controller_t& workspace) {
    auto completed = job_.poll();
    if (!completed) return;
    progress_.reset();
    std::string message;
    auto severity = notification_severity_t::warning;
    if (std::exchange(cancelling_, false)) message = "Audio slicing cancelled.";
    else if (!*completed) message = completed->error().cause.message;
    else if (!completed->value().identity.is_current(history.document, workspace.selected_chart.value_or(chart_id_t{})) ||
             workspace.selected_chart != completed->value().identity.chart) message = "Slices discarded because the document or chart changed.";
    else {
        const auto& files = *completed->value().payload;
        const auto result = apply_bulk_sound_import(history, files->import);
        files->retained = result.applied;
        if (result.applied) {
            const auto feedback = rebind_document_edit(workspace, history,
                {.success = "Slices imported.", .rebind_failure = "Slices imported, but workspace rebinding failed.",
                 .is_message_diagnostic = false, .source = notification_source_t::import_});
            message = feedback.message_text();
            severity = feedback.severity;
        }
        else message = result.diagnostics.empty() ? "Slice import was rejected." : result.diagnostics.front().message;
    }
    notifications_.post(severity, notification_source_t::import_, message);
}
void runtime::audio_slicer_t::cancel() {
    if (!is_busy()) return;
    cancelling_ = true;
    job_.cancel(async_job_cancel_policy_t::retain_completion);
}
void runtime::audio_slicer_t::populate(ui::audio_slicer_view_t& view,
    const ui::audio_slicer_settings_t& settings, std::uint32_t rate, std::uint64_t frames, bool available) {
    const auto geometry = std::pair{rate, frames};
    if (settings != settings_ || geometry_ != geometry || available_ != available) {
        refill_audio_slicer_view(cached_view_, settings, rate, frames, available);
        settings_ = settings;
        geometry_ = geometry;
        available_ = available;
    }
    view.cut_frames.assign(cached_view_.cut_frames.begin(), cached_view_.cut_frames.end());
    view.message = cached_view_.message;
    view.can_slice = cached_view_.can_slice;
}
auto runtime::audio_slicer_t::view() const -> ui::file_job_view_t {
    return {.active = is_busy(), .cancelling = cancelling_, .completed = progress_ ? progress_->completed.load() : 0,
        .total = progress_ ? progress_->total.load() : 0, .label = "Slicing audio"};
}
}
