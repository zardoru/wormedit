#include <wormedit/core/command.h>

#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
    };
}

void trim_undo_history(document_history_t& history) {
    if (history.undo.size() <= history.maximum_undo_entries) {
        return;
    }
    const auto excess = history.undo.size() - history.maximum_undo_entries;
    history.undo.erase(history.undo.begin(),
                       history.undo.begin() + static_cast<std::ptrdiff_t>(excess));
}

void mark_retained_sources_changed(song_document_t& document) {
    for (auto& group : document.metadata.format_groups) {
        group.encoding.unchanged_since_import = false;
    }
    for (auto& chart : document.charts) {
        for (auto& group : chart.metadata.format_groups) {
            group.encoding.unchanged_since_import = false;
        }
    }
}

auto quantized_stem_audio_valid(const song_document_t& song, const chart_document_t& before,
                                const chart_document_t& chart,
                                diagnostic_list_t& diagnostics) -> bool {
    for (std::size_t i = 0; i < chart.sound_channels.size(); ++i) {
        const auto& channel = chart.sound_channels[i];
        const auto& previous = before.sound_channels[i];
        const auto unchanged = std::ranges::equal(channel.events, previous.events, [](const auto& a, const auto& b) {
            return a.position == b.position && a.end == b.end;
        });
        if (unchanged) continue;
        const auto asset = std::ranges::find(song.audio_assets, channel.source_clip.source,
                                             &audio_asset_t::id);
        if (asset == song.audio_assets.end() || asset->stream.sample_rate == 0) continue;
        if (!derive_sound_channel_slices(chart, channel, asset->stream.sample_rate,
                                         asset->stream.frame_count)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                "quantizing these points would leave the stem with unusable slices"));
            return false;
        }
    }
    return true;
}

} // namespace

auto make_document_history(song_document_t document, std::size_t maximum_undo_entries,
                           bool initially_saved) -> document_history_t {
    if (document.revision == 0) {
        document.revision = 1;
    }
    sync_virtual_tails(document);
    const auto current_revision = document.revision;
    const auto next_revision = current_revision == std::numeric_limits<std::uint64_t>::max()
                                   ? current_revision
                                   : current_revision + 1;
    return {
        .document = std::move(document),
        .next_revision = next_revision,
        .saved_revision =
            initially_saved ? std::optional<std::uint64_t>{current_revision} : std::nullopt,
        .maximum_undo_entries = maximum_undo_entries,
    };
}

auto is_document_dirty(const document_history_t& history) noexcept -> bool {
    return !history.saved_revision.has_value() ||
           *history.saved_revision != history.document.revision;
}

void mark_document_saved(document_history_t& history) noexcept {
    history.saved_revision = history.document.revision;
}

auto commit_document_command(document_history_t& history, song_document_t proposed,
                             std::string label, document_command_options_t options)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (history.next_revision == std::numeric_limits<std::uint64_t>::max()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "document revision sequence is exhausted"));
        return result;
    }

    proposed.revision = history.next_revision;
    if (!options.preserve_retained_source_flags) mark_retained_sources_changed(proposed);
    sync_virtual_tails(proposed);
    result.diagnostics = validate_document(proposed);
    if (std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == diagnostic_severity_t::error;
        })) {
        return result;
    }

    document_command_record_t record{
        .label = std::move(label),
        .before = history.document,
        .after = std::move(proposed),
    };
    history.document = record.after;
    history.undo.push_back(std::move(record));
    trim_undo_history(history);
    history.redo.clear();
    ++history.next_revision;
    result.applied = true;
    result.revision = history.document.revision;
    return result;
}

void merge_recent_undo_records(document_history_t& history, std::size_t count, std::string label) {
    if (count < 2 || history.undo.size() < count) return;
    const auto first = history.undo.end() - static_cast<std::ptrdiff_t>(count);
    document_command_record_t merged{.label = std::move(label), .before = std::move(first->before),
                                     .after = std::move(history.undo.back().after)};
    history.undo.erase(first, history.undo.end());
    history.undo.push_back(std::move(merged));
}

auto apply_quantize_command(document_history_t& history, chart_id_t chart_id,
                            const quantize_request_t& request) -> quantize_command_result_t {
    quantize_command_result_t result;
    result.command.revision = history.document.revision;
    const auto chart = std::ranges::find_if(
        history.document.charts, [chart_id](const auto& item) { return item.id == chart_id; });
    if (chart == history.document.charts.end()) {
        result.command.diagnostics.push_back(error(
            diagnostic_code_t::invalid_id, "quantization command refers to an unknown chart"));
        return result;
    }

    auto proposal = quantize_chart_exact(*chart, request);
    result.resolution_moves = std::move(proposal.resolution_moves);
    result.quantized_moves = std::move(proposal.quantized_moves);
    result.promoted_from = proposal.promoted_from;
    result.promoted_to = proposal.promoted_to;
    if (!proposal) {
        result.command.diagnostics = std::move(proposal.diagnostics);
        return result;
    }

    if (request.selection && !quantized_stem_audio_valid(history.document, *chart, *proposal.chart,
                                                        result.command.diagnostics)) return result;
    auto proposed_document = history.document;
    const auto proposed_chart = std::ranges::find_if(
        proposed_document.charts, [chart_id](const auto& item) { return item.id == chart_id; });
    *proposed_chart = std::move(*proposal.chart);
    result.command =
        commit_document_command(history, std::move(proposed_document), "Quantize chart");
    return result;
}

auto apply_redivision_command(document_history_t& history, std::uint64_t expected_revision,
                              chart_id_t chart_id, std::uint64_t new_resolution)
    -> redivision_command_result_t {
    redivision_command_result_t result;
    result.command.revision = history.document.revision;
    if (history.document.revision != expected_revision) {
        result.command.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "redivision command targets a stale revision"));
        return result;
    }
    const auto chart = std::ranges::find_if(
        history.document.charts, [chart_id](const auto& item) { return item.id == chart_id; });
    if (chart == history.document.charts.end()) {
        result.command.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "redivision command refers to an unknown chart"));
        return result;
    }
    if (chart->divisions_per_standard_measure == new_resolution) {
        return result;
    }

    auto proposal = redivide_chart_exact(*chart, new_resolution);
    if (!proposal) {
        result.command.diagnostics = std::move(proposal.diagnostics);
        return result;
    }
    result.moves = std::move(proposal.moves);

    auto proposed_document = history.document;
    const auto proposed_chart = std::ranges::find_if(
        proposed_document.charts, [chart_id](const auto& item) { return item.id == chart_id; });
    *proposed_chart = std::move(*proposal.chart);
    result.command =
        commit_document_command(history, std::move(proposed_document), "Change chart resolution");
    return result;
}

auto undo_document_command(document_history_t& history) -> bool {
    if (history.undo.empty()) {
        return false;
    }
    auto record = std::move(history.undo.back());
    history.undo.pop_back();
    history.document = record.before;
    history.redo.push_back(std::move(record));
    return true;
}

auto redo_document_command(document_history_t& history) -> bool {
    if (history.redo.empty()) {
        return false;
    }
    auto record = std::move(history.redo.back());
    history.redo.pop_back();
    history.document = record.after;
    history.undo.push_back(std::move(record));
    trim_undo_history(history);
    return true;
}

} // namespace wormedit
