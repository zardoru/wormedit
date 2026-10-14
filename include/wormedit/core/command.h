#pragma once

#include <wormedit/core/quantize.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

// Older revision-only captures keep that contract when adopting a shared stamp.
enum class document_identity_policy_t { project_and_revision, revision_only };

struct document_stamp_t {
    song_id_t project;
    std::uint64_t revision{};

    [[nodiscard]] static auto from(const song_document_t& document) noexcept -> document_stamp_t;
    [[nodiscard]] auto is_revision_current(std::uint64_t current_revision) const noexcept -> bool;
    [[nodiscard]] auto is_current(const song_document_t& document,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision) const noexcept -> bool;
    auto operator==(const document_stamp_t&) const -> bool = default;
};

struct chart_stamp_t {
    document_stamp_t document;
    chart_id_t chart;

    [[nodiscard]] static auto from(const song_document_t& document, chart_id_t chart) noexcept
        -> chart_stamp_t;
    [[nodiscard]] auto is_current(chart_id_t current_chart,
                                  std::uint64_t current_revision) const noexcept -> bool;
    [[nodiscard]] auto is_current(const song_document_t& current, chart_id_t selected,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision) const noexcept -> bool;
    auto operator==(const chart_stamp_t&) const -> bool = default;
};

[[nodiscard]] auto stale_action_message(std::string_view label) -> std::string;

struct document_command_record_t {
    std::string label;
    song_document_t before;
    song_document_t after;
};

struct document_history_t {
    // Starts a bounded history; promotes a zero source revision to the first usable revision.
    [[nodiscard]] static auto from(song_document_t document,
                                  std::size_t maximum_undo_entries = 128,
                                  bool initially_saved = true) -> document_history_t;

    song_document_t document;
    std::vector<document_command_record_t> undo;
    std::vector<document_command_record_t> redo;
    std::uint64_t next_revision{1};
    std::optional<std::uint64_t> saved_revision;
    std::size_t maximum_undo_entries{128};
};

struct document_command_result_t {
    bool applied{};
    std::uint64_t revision{};
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return applied;
    }
};

void reject(document_command_result_t& result, diagnostic_code_t code, std::string message,
            std::optional<object_id_t> object = std::nullopt);

// Folds the newest `count` undo records into one so a multi-command gesture undoes in a single
// step. Does nothing for fewer than two records or when older ones were already trimmed away.
void merge_recent_undo_records(document_history_t& history, std::size_t count, std::string label);

template <class Payload> struct command_result_with_t {
    document_command_result_t command;
    Payload payload{};

    [[nodiscard]] explicit operator bool() const noexcept {
        if constexpr (requires { payload.is_success(); })
            return static_cast<bool>(command) && payload.is_success();
        return static_cast<bool>(command);
    }
};

struct quantize_command_payload_t {
    std::vector<redivision_move_t> resolution_moves;
    std::vector<redivision_move_t> quantized_moves;
    std::optional<std::uint64_t> promoted_from;
    std::optional<std::uint64_t> promoted_to;
};

using quantize_command_result_t = command_result_with_t<quantize_command_payload_t>;

struct redivision_command_payload_t {
    std::vector<redivision_move_t> moves;
};

using redivision_command_result_t = command_result_with_t<redivision_command_payload_t>;

// Compatibility for the sys/ui migration wave; new callers use document_history_t::from.
[[nodiscard]] auto make_document_history(song_document_t document,
                                         std::size_t maximum_undo_entries = 128,
                                         bool initially_saved = true) -> document_history_t;

[[nodiscard]] auto is_document_dirty(const document_history_t& history) noexcept -> bool;
void mark_document_saved(document_history_t& history) noexcept;

struct document_command_options_t {
    // Metadata-only commands may preserve existing retained-source flags.
    // This never enables passthrough for a document that was already edited.
    bool preserve_retained_source_flags{false};
};

// A synchronous command owns its working document only after the first edit.
// Captured proposals use the stamped factory; synchronous commands use from(history).
struct document_command_transaction_t {
    document_history_t& history;
    document_command_result_t outcome;
    std::optional<song_document_t> proposed;
    bool finished{};

    [[nodiscard]] static auto from(document_history_t& history)
        -> document_command_transaction_t;
    [[nodiscard]] static auto from(document_history_t& history, const document_stamp_t& stamp,
        std::string_view label,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision,
        diagnostic_code_t stale_code = diagnostic_code_t::invalid_id)
        -> document_command_transaction_t;
    [[nodiscard]] static auto from(document_history_t& history, const chart_stamp_t& stamp,
        std::string_view label,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision,
        diagnostic_code_t stale_code = diagnostic_code_t::invalid_id)
        -> document_command_transaction_t;
    [[nodiscard]] auto is_ready() const noexcept -> bool;
    [[nodiscard]] auto has_copy() const noexcept -> bool;
    [[nodiscard]] auto source_chart(chart_id_t id) const -> const chart_document_t*;
    auto copy_document() -> song_document_t&;
    // Staged editors hand over their existing snapshot without copying the live document.
    void adopt_document(song_document_t&& document);
    auto chart(chart_id_t id) -> chart_document_t*;
    // Returns the current outcome and discards the working snapshot without publication.
    [[nodiscard]] auto finish_without_commit() -> document_command_result_t;
    [[nodiscard]] auto reject(diagnostic_code_t code, std::string message,
                std::optional<object_id_t> object = std::nullopt) -> document_command_result_t;
    [[nodiscard]] static auto rejection(std::uint64_t revision, diagnostic_code_t code,
        std::string message, std::optional<object_id_t> object = std::nullopt)
        -> document_command_result_t;
    [[nodiscard]] static auto rejection(std::uint64_t revision, diagnostic_list_t diagnostics)
        -> document_command_result_t;
    [[nodiscard]] auto commit(std::string label, document_command_options_t options = {})
        -> document_command_result_t;
};



// Publishes a complete proposed document only when its graph validates. This
// is the common atomic boundary for future editor commands.
[[nodiscard]] auto commit_document_command(document_history_t& history,
                                           song_document_t proposed,
                                           std::string label,
                                           document_command_options_t options = {})
    -> document_command_result_t;

[[nodiscard]] auto apply_quantize_command(document_history_t& history,
                                          chart_id_t chart,
                                          const quantize_request_t& request)
    -> quantize_command_result_t;

// Exact global redivision of one chart as a single undoable command. Rounds
// nothing: any inexact position rejects the whole command and leaves history
// untouched. Redividing to the current resolution is a no-op without an entry.
[[nodiscard]] auto apply_redivision_command(document_history_t& history, chart_id_t chart,
                                            std::uint64_t new_resolution)
    -> redivision_command_result_t;

[[nodiscard]] auto undo_document_command(document_history_t& history) -> bool;
[[nodiscard]] auto redo_document_command(document_history_t& history) -> bool;

} // namespace wormedit
