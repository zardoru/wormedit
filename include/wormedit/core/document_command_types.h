#pragma once

#include <wormedit/core/diagnostic.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit {
struct song_document_t;
struct document_history_t;
class document_command_transaction_t;

// Session selection and captured requests use the same stable semantic key.
struct bms_program_target_key_t {
    chart_id_t difficulty;
    branch_id_t chart;
    auto operator==(const bms_program_target_key_t&) const -> bool = default;
};

// Older revision-only captures keep that contract when adopting a shared stamp.
enum class document_identity_policy_t { project_and_revision, revision_only };

struct document_stamp_t {
    song_id_t project;
    std::uint64_t revision{};
    std::optional<bms_program_target_key_t> bms_target;

    [[nodiscard]] static auto from(const document_history_t& history) noexcept -> document_stamp_t;

    // Revision-only captures have no project identity; callers use the clock query.
    [[nodiscard]] static auto from(std::uint64_t revision) noexcept -> document_stamp_t;
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
    [[nodiscard]] static auto from(const document_history_t& history, chart_id_t chart) noexcept
        -> chart_stamp_t;
    [[nodiscard]] auto is_identity_current(const song_document_t& document,
                                            chart_id_t selected) const noexcept -> bool;
    [[nodiscard]] auto is_current(chart_id_t current_chart,
                                  std::uint64_t current_revision) const noexcept -> bool;
    [[nodiscard]] auto is_current(const song_document_t& current, chart_id_t selected,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision) const noexcept -> bool;
    auto operator==(const chart_stamp_t&) const -> bool = default;
};

[[nodiscard]] auto stale_action_message(std::string_view label) -> std::string;

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

template <class Payload> struct command_result_with_t {
    document_command_result_t command;
    Payload payload{};

    [[nodiscard]] static auto from(document_command_result_t command, Payload payload)
        -> command_result_with_t {
        return {.command = std::move(command), .payload = std::move(payload)};
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        if constexpr (requires { payload.is_success(); })
            return static_cast<bool>(command) && payload.is_success();
        return static_cast<bool>(command);
    }
};

struct document_command_options_t {
    // Metadata-only commands may preserve existing retained-source flags.
    // This never enables passthrough for a document that was already edited.
    bool preserve_retained_source_flags{false};
    auto operator==(const document_command_options_t&) const -> bool = default;
};

// Domain operations share execution state; chart identity, limits and completion belong
// to the transaction. The operation itself supplies validation, mutation and effect.
// A staged document edit describes publication; it never publishes by itself.
enum class command_noop_policy_t { reject, accept_without_diagnostics };

struct document_edit_effect_t {
    bool changed{};
    std::string command_label;
    document_command_options_t options{};
    auto operator==(const document_edit_effect_t&) const -> bool = default;
};

} // namespace wormedit
