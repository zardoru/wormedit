#include <wormedit/core/command.h>
#include <wormedit/core/timing.h>

#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <ranges>
#include <utility>

namespace wormedit {
auto document_stamp_t::from(std::uint64_t revision) noexcept -> document_stamp_t {
    return {.project = {}, .revision = revision};
}

auto document_stamp_t::from(const song_document_t& document) noexcept -> document_stamp_t {
    return {.project = document.id, .revision = document.revision};
}

auto document_stamp_t::is_revision_current(std::uint64_t current_revision) const noexcept -> bool {
    return revision == current_revision;
}

auto document_stamp_t::is_current(const song_document_t& document,
    document_identity_policy_t policy) const noexcept -> bool {
    return is_revision_current(document.revision) &&
           (policy == document_identity_policy_t::revision_only || project == document.id);
}

auto chart_stamp_t::from(const song_document_t& document, chart_id_t chart) noexcept -> chart_stamp_t {
    return {.document = document_stamp_t::from(document), .chart = chart};
}

auto chart_stamp_t::is_identity_current(const song_document_t& current,
                                          chart_id_t selected) const noexcept -> bool {
    return document.project == current.id && chart == selected;
}

auto chart_stamp_t::is_current(chart_id_t current_chart,
                                std::uint64_t current_revision) const noexcept -> bool {
    return chart == current_chart && document.is_revision_current(current_revision);
}

auto chart_stamp_t::is_current(const song_document_t& current, chart_id_t selected,
    document_identity_policy_t policy) const noexcept -> bool {
    return document.is_current(current, policy) && chart == selected;
}

auto stale_action_message(std::string_view label) -> std::string {
    constexpr std::string_view suffix =
        " ignored because the project, chart or document revision changed.";
    std::string message;
    message.reserve(label.size() + suffix.size());
    message.append(label);
    message.append(suffix);
    return message;
}

void reject(document_command_result_t& result, diagnostic_code_t code, std::string message,
            std::optional<object_id_t> object) {
    result.diagnostics.push_back(error(code, std::move(message), object));
}

namespace {

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

auto is_quantized_stem_audio_valid(const song_document_t& song, const chart_document_t& before,
                                const chart_document_t& chart,
                                diagnostic_list_t& diagnostics) -> bool {
    for (std::size_t i = 0; i < chart.sound_channels.size(); ++i) {
        const auto& channel = chart.sound_channels[i];
        const auto& previous = before.sound_channels[i];
        const auto unchanged = std::ranges::equal(channel.events, previous.events, [](const auto& a, const auto& b) {
            return a.position == b.position && a.end == b.end;
        });
        if (unchanged) continue;
        const auto asset = song.find_audio_asset(channel.source_clip.source);
        if (asset == nullptr || asset->stream.sample_rate == 0) continue;
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

auto document_history_t::from(song_document_t document, std::size_t maximum_undo_entries,
                              bool initially_saved) -> document_history_t {
    if (document.revision == 0) {
        document.revision = 1;
    }
    document.reset_chart_indices();
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

auto document_history_t::indices_storage(const chart_document_t& chart) const
    -> const std::shared_ptr<const chart_indices_t>& {
    auto& slot = chart.indices_slot.value_;
    if (!slot) slot = std::make_shared<const chart_indices_t>(chart_indices_t::from(chart));
    return slot;
}

auto document_history_t::indices(chart_id_t id) const -> const chart_indices_t* {
    const auto* chart = document.find_chart(id);
    return chart ? indices_storage(*chart).get() : nullptr;
}

auto document_history_t::indices_snapshot(chart_id_t id) const
    -> std::shared_ptr<const chart_indices_t> {
    const auto* chart = document.find_chart(id);
    return chart ? indices_storage(*chart) : std::shared_ptr<const chart_indices_t>{};
}

auto is_document_dirty(const document_history_t& history) noexcept -> bool {
    return !history.saved_revision.has_value() ||
           *history.saved_revision != history.document.revision;
}

void mark_document_saved(document_history_t& history) noexcept {
    history.saved_revision = history.document.revision;
}

static auto commit_validated_document(document_history_t& history, song_document_t&& proposed,
                             std::string label, document_command_options_t options)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (history.next_revision == std::numeric_limits<std::uint64_t>::max()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "document revision sequence is exhausted"));
        return result;
    }

    // Direct publication and chart replacement can carry slots from an older snapshot.
    proposed.reset_chart_indices();
    proposed.revision = history.next_revision;
    if (!options.preserve_retained_source_flags) mark_retained_sources_changed(proposed);
    sync_virtual_tails(proposed);
    result.diagnostics = validate_document(proposed);
    if (diagnostic_queries::has_errors(result.diagnostics)) {
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

auto commit_document_command(document_history_t& history, song_document_t proposed,
    std::string label, document_command_options_t options) -> document_command_result_t {
    return commit_validated_document(history, std::move(proposed), std::move(label), options);
}

document_command_transaction_t::document_command_transaction_t(document_history_t& history)
    : history_(history), outcome_{.revision = history.document.revision} {}

document_command_transaction_t::document_command_transaction_t(document_history_t& history,
    const document_stamp_t& stamp, std::string_view label, document_identity_policy_t policy,
    diagnostic_code_t stale_code)
    : document_command_transaction_t(history) {
    if (!stamp.is_current(source_document(), policy)) {
        outcome_ = rejection(source_document().revision, stale_code, stale_action_message(label));
        finished_ = true;
    }
}

document_command_transaction_t::document_command_transaction_t(document_history_t& history,
    const chart_stamp_t& stamp, std::string_view label, document_identity_policy_t policy,
    diagnostic_code_t stale_code)
    // The proposal owns its target chart; its missing-target validation follows this guard.
    : document_command_transaction_t(history, stamp.document, label, policy, stale_code) {}

auto document_command_transaction_t::is_ready() const noexcept -> bool {
    return !finished_ && !diagnostic_queries::has_errors(outcome_.diagnostics);
}

auto document_command_transaction_t::has_copy() const noexcept -> bool {
    return document_.has_value();
}

auto document_command_transaction_t::current_document() const noexcept -> const song_document_t& {
    return document_ ? *document_ : history_.document;
}

auto document_command_transaction_t::source_document() const noexcept -> const song_document_t& {
    return history_.document;
}

auto document_command_transaction_t::outcome() noexcept -> document_command_result_t& {
    return outcome_;
}

auto document_command_transaction_t::outcome() const noexcept -> const document_command_result_t& {
    return outcome_;
}

auto document_command_transaction_t::source_chart(chart_id_t id) const -> const chart_document_t* {
    const auto found = history_.document.find_chart(id);
    return found == nullptr ? nullptr : &*found;
}

auto document_command_transaction_t::document() -> song_document_t& {
    if (!document_) {
        document_.emplace(history_.document);
        document_->reset_chart_indices();
    }
    return *document_;
}

void document_command_transaction_t::adopt_document(song_document_t&& document) {
    document_.emplace(std::move(document));
    document_->reset_chart_indices();
}

auto document_command_transaction_t::chart(chart_id_t id) -> chart_document_t* {
    if (!source_chart(id)) return nullptr;
    const auto found = document().find_chart(id);
    return found == nullptr ? nullptr : &*found;
}

auto document_command_transaction_t::rejection(std::uint64_t revision, diagnostic_code_t code,
    std::string message, std::optional<object_id_t> object) -> document_command_result_t {
    document_command_result_t result{.revision = revision};
    wormedit::reject(result, code, std::move(message), object);
    return result;
}

auto document_command_transaction_t::rejection(std::uint64_t revision,
    diagnostic_list_t diagnostics) -> document_command_result_t {
    return {.revision = revision, .diagnostics = std::move(diagnostics)};
}

auto document_command_transaction_t::finish_without_commit() -> document_command_result_t {
    finished_ = true;
    document_.reset();
    return std::move(outcome_);
}

auto document_command_transaction_t::finish_with(document_command_result_t result)
    -> document_command_result_t {
    if (finished_) return result;
    outcome_ = std::move(result);
    return finish_without_commit();
}

auto document_command_transaction_t::reject(diagnostic_code_t code, std::string message,
    std::optional<object_id_t> object) -> document_command_result_t {
    wormedit::reject(outcome_, code, std::move(message), object);
    finished_ = true;
    return std::move(outcome_);
}

auto document_command_transaction_t::commit(std::string label, document_command_options_t options)
    -> document_command_result_t {
    if (!is_ready() || !document_) return std::move(outcome_);
    finished_ = true;
    auto working_document = std::move(*document_);
    return commit_validated_document(history_, std::move(working_document), std::move(label), options);
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
    const auto chart = history.document.find_chart(chart_id);
    if (chart == nullptr) {
        result.command.diagnostics.push_back(error(
            diagnostic_code_t::invalid_id, "quantization command refers to an unknown chart"));
        return result;
    }

    auto proposal = quantization_plan_t::from(*chart, request);
    result.payload.resolution_moves = std::move(proposal.value.resolution_moves);
    result.payload.quantized_moves = std::move(proposal.value.quantized_moves);
    result.payload.promoted_from = proposal.value.promoted_from;
    result.payload.promoted_to = proposal.value.promoted_to;
    if (!proposal.value.chart) {
        result.command.diagnostics = std::move(proposal.diagnostics);
        return result;
    }

    if (request.selection && !is_quantized_stem_audio_valid(history.document, *chart, *proposal.value.chart,
                                                        result.command.diagnostics)) return result;
    auto transaction = document_command_transaction_t(history);
    auto& proposed_document = transaction.document();
    const auto proposed_chart = proposed_document.find_chart(chart_id);
    *proposed_chart = std::move(*proposal.value.chart);
    result.command =
        transaction.commit("Quantize chart");
    return result;
}

auto apply_redivision_command(document_history_t& history,
                              chart_id_t chart_id, std::uint64_t new_resolution)
    -> redivision_command_result_t {
    redivision_command_result_t result;
    result.command.revision = history.document.revision;
    const auto chart = history.document.find_chart(chart_id);
    if (chart == nullptr) {
        result.command.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "redivision command refers to an unknown chart"));
        return result;
    }
    if (chart->divisions_per_standard_measure == new_resolution) {
        return result;
    }

    auto proposal = redivision_plan_t::from(*chart, new_resolution);
    if (!proposal.value.chart) {
        result.command.diagnostics = std::move(proposal.diagnostics);
        return result;
    }
    result.payload.moves = std::move(proposal.value.moves);

    auto transaction = document_command_transaction_t(history);
    auto& proposed_document = transaction.document();
    const auto proposed_chart = proposed_document.find_chart(chart_id);
    *proposed_chart = std::move(*proposal.value.chart);
    result.command =
        transaction.commit("Change chart resolution");
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
