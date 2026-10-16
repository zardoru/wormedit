#pragma once

#include <wormedit/core/quantize.h>
#include <wormedit/core/result.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <variant>
#include <vector>

namespace wormedit {

// Controller defaults and incomplete command defaults have distinct existing contracts.
enum class chart_edit_value_default_policy_t { controller, command };

template <typename Value> struct chart_edit_value_traits_t {
    using id_type = object_id_t;
    using delete_options_type = std::monostate;
};

template <typename Value> struct insert_edit_t {
    chart_id_t chart;
    Value value;
    auto operator==(const insert_edit_t&) const -> bool = default;

    template <class Context>
        requires requires(Context& context, const insert_edit_t& edit) {
            {
                context.validate(edit)
            } -> std::same_as<std::optional<typename Context::outcome_type>>;
        }
    [[nodiscard]] auto validate(Context& context) const
        -> std::optional<typename Context::outcome_type> {
        return context.validate(*this);
    }
    template <class Context>
        requires requires(Context& context, const insert_edit_t& edit) {
            { context.perform(edit) } -> std::same_as<typename Context::outcome_type>;
        }
    [[nodiscard]] auto perform(Context& context) const -> typename Context::outcome_type {
        return context.perform(*this);
    }
};

template <typename Value> struct update_edit_t {
    chart_id_t chart;
    typename chart_edit_value_traits_t<Value>::id_type id;
    Value value;
    auto operator==(const update_edit_t&) const -> bool = default;

    template <class Context>
        requires requires(Context& context, const update_edit_t& edit) {
            {
                context.validate(edit)
            } -> std::same_as<std::optional<typename Context::outcome_type>>;
        }
    [[nodiscard]] auto validate(Context& context) const
        -> std::optional<typename Context::outcome_type> {
        return context.validate(*this);
    }
    template <class Context>
        requires requires(Context& context, const update_edit_t& edit) {
            { context.perform(edit) } -> std::same_as<typename Context::outcome_type>;
        }
    [[nodiscard]] auto perform(Context& context) const -> typename Context::outcome_type {
        return context.perform(*this);
    }
};

template <typename Value> struct delete_edit_t {
    chart_id_t chart;
    typename chart_edit_value_traits_t<Value>::id_type id;
    [[no_unique_address]] typename chart_edit_value_traits_t<Value>::delete_options_type options{};
    auto operator==(const delete_edit_t&) const -> bool = default;

    template <class Context>
        requires requires(Context& context, const delete_edit_t& edit) {
            {
                context.validate(edit)
            } -> std::same_as<std::optional<typename Context::outcome_type>>;
        }
    [[nodiscard]] auto validate(Context& context) const
        -> std::optional<typename Context::outcome_type> {
        return context.validate(*this);
    }
    template <class Context>
        requires requires(Context& context, const delete_edit_t& edit) {
            { context.perform(edit) } -> std::same_as<typename Context::outcome_type>;
        }
    [[nodiscard]] auto perform(Context& context) const -> typename Context::outcome_type {
        return context.perform(*this);
    }
};

template <typename Value>
using chart_edit_t = std::variant<insert_edit_t<Value>, update_edit_t<Value>, delete_edit_t<Value>>;

// Positions and scalar settings are independent values; no field-by-field translation.
template <typename Settings> struct positioned_edit_value_t {
    local_position_t position;
    Settings settings{make_default_settings()};

    [[nodiscard]] static auto from(local_position_t position, const Settings& settings)
        -> positioned_edit_value_t {
        return {.position = position, .settings = settings};
    }
    [[nodiscard]] static auto from(position_span_t span, std::optional<warp_provenance_t> encoding)
        -> positioned_edit_value_t
        requires requires { Settings::from(span.end, encoding); }
    {
        return from(span.begin, Settings::from(span.end, encoding));
    }
    [[nodiscard]] auto span() const -> position_span_t
        requires requires(const Settings& value) { value.end; }
    {
        return {.begin = position, .end = settings.end};
    }
    [[nodiscard]] auto source_encoding() const -> std::optional<warp_provenance_t>
        requires requires(const Settings& value) {
            value.source_encoding;
            value.is_source_encoding_replaced;
        }
    {
        return settings.is_source_encoding_replaced ? std::optional{settings.source_encoding}
                                                    : std::nullopt;
    }
    auto operator==(const positioned_edit_value_t&) const -> bool = default;

  private:
    static auto make_default_settings() -> Settings {
        if constexpr (requires {
                          Settings::with_defaults(chart_edit_value_default_policy_t::command);
                      })
            return Settings::with_defaults(chart_edit_value_default_policy_t::command);
        else
            return {};
    }
};

// Older revision-only captures keep that contract when adopting a shared stamp.
enum class document_identity_policy_t { project_and_revision, revision_only };

struct document_stamp_t {
    song_id_t project;
    std::uint64_t revision{};

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
    [[nodiscard]] auto is_identity_current(const song_document_t& document,
                                            chart_id_t selected) const noexcept -> bool;
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

    // Main-loop-only lazy read. The returned value belongs to the committed snapshot.
    [[nodiscard]] auto indices(chart_id_t chart) const -> const chart_indices_t*;
    // Main-loop-only lazy read; the handle retains immutable indices through source replacement.
    [[nodiscard]] auto indices_snapshot(chart_id_t chart) const
        -> std::shared_ptr<const chart_indices_t>;

    song_document_t document;
    std::vector<document_command_record_t> undo;
    std::vector<document_command_record_t> redo;
    std::uint64_t next_revision{1};
    std::optional<std::uint64_t> saved_revision;
    std::size_t maximum_undo_entries{128};

  private:
    [[nodiscard]] auto indices_storage(const chart_document_t& chart) const
        -> const std::shared_ptr<const chart_indices_t>&;
};

struct document_command_effect_t {
    bool applied{};
    std::uint64_t revision{};
    auto operator==(const document_command_effect_t&) const -> bool = default;
};
using document_command_outcome_t = diagnosed_t<document_command_effect_t>;
struct document_command_result_t {
    [[nodiscard]] static auto from(document_command_outcome_t&& outcome) -> document_command_result_t;
    [[nodiscard]] static auto from(document_command_result_t&& result) -> document_command_outcome_t;
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

template <class Payload> struct command_payload_effect_t {
    document_command_effect_t command;
    Payload payload{};
    auto operator==(const command_payload_effect_t&) const -> bool requires std::equality_comparable<Payload> = default;
};
template <class Payload> using command_result_with_outcome_t = diagnosed_t<command_payload_effect_t<Payload>>;
template <class Payload> struct command_result_with_t {
    [[nodiscard]] static auto from(command_result_with_outcome_t<Payload>&& outcome) -> command_result_with_t {
        return {.command = document_command_result_t::from(document_command_outcome_t{
            .value = outcome.value.command, .diagnostics = std::move(outcome.diagnostics)}),
            .payload = std::move(outcome.value.payload)};
    }
    [[nodiscard]] static auto from(command_result_with_t&& result) -> command_result_with_outcome_t<Payload> {
        return {.value = {.command = {.applied = result.command.applied, .revision = result.command.revision},
            .payload = std::move(result.payload)}, .diagnostics = std::move(result.command.diagnostics)};
    }
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


[[nodiscard]] auto is_document_dirty(const document_history_t& history) noexcept -> bool;
void mark_document_saved(document_history_t& history) noexcept;

struct document_command_options_t {
    // Metadata-only commands may preserve existing retained-source flags.
    // This never enables passthrough for a document that was already edited.
    bool preserve_retained_source_flags{false};
};

// Domain operations share execution state; chart identity, limits and completion belong
// to the transaction. The operation itself supplies validation, mutation and effect.
template <class Limits> struct chart_edit_context_t {
    document_command_result_t& result;
    const Limits& limits;
    std::string command_label{};
    bool changed{};
};

struct chart_event_cap_t {
    std::size_t count;
    std::size_t maximum;
    std::string_view message;
    auto operator==(const chart_event_cap_t&) const -> bool = default;
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
    // The current transaction document, including an already prepared snapshot.
    [[nodiscard]] auto document() const noexcept -> const song_document_t&;
    [[nodiscard]] auto source_chart(chart_id_t id) const -> const chart_document_t*;
    auto copy_document() -> song_document_t&;
    // Staged editors hand over their existing snapshot without copying the live document.
    void adopt_document(song_document_t&& document);
    auto chart(chart_id_t id) -> chart_document_t*;
    // Returns the current outcome and discards the working snapshot without publication.
    [[nodiscard]] auto finish_without_commit() -> document_command_result_t;
    [[nodiscard]] auto finish_with(document_command_result_t result) -> document_command_result_t;
    [[nodiscard]] auto reject(diagnostic_code_t code, std::string message,
                std::optional<object_id_t> object = std::nullopt) -> document_command_result_t;
    [[nodiscard]] static auto rejection(std::uint64_t revision, diagnostic_code_t code,
        std::string message, std::optional<object_id_t> object = std::nullopt)
        -> document_command_result_t;
    [[nodiscard]] static auto rejection(std::uint64_t revision, diagnostic_list_t diagnostics)
        -> document_command_result_t;
    // Operations declare chart/cap policy and overload operator()(chart, edit).
    // This visitor is the single chart lookup, cap check and execution boundary.
    template <class Operation, class Limits> struct edit_execution_t {
        document_command_transaction_t& transaction;
        const Limits& limits;

        template <class Edit> auto operator()(const Edit& edit) const -> document_command_result_t {
            auto& proposed = transaction.copy_document();
            auto* chart = proposed.find_chart(edit.chart);
            bool invalid_chart = !edit.chart || chart == nullptr;
            if constexpr (requires { Operation::requires_unique_chart; }) {
                invalid_chart = invalid_chart || std::ranges::count(proposed.charts, edit.chart,
                                                                    &chart_document_t::id) != 1;
            }
            if (invalid_chart) {
                wormedit::reject(transaction.outcome, diagnostic_code_t::invalid_id,
                                 std::string(Operation::unknown_chart_message));
                return transaction.finish_without_commit();
            }
            Operation operation{{transaction.outcome, limits}};
            for (const auto& cap : operation.event_caps(*chart, edit)) {
                if (cap.count > cap.maximum) {
                    wormedit::reject(transaction.outcome, diagnostic_code_t::invalid_resolution,
                                     std::string(cap.message));
                    return transaction.finish_without_commit();
                }
            }
            if constexpr (requires { operation.prepare(*chart); }) {
                if (!operation.prepare(*chart))
                    return transaction.finish_without_commit();
            }
            if constexpr (std::is_void_v<decltype(operation(*chart, edit))>) {
                operation(*chart, edit);
                return transaction.finish_execution(operation, operation, *chart);
            } else {
                auto effect = operation(*chart, edit);
                return transaction.finish_execution(operation, std::move(effect), *chart);
            }
        }
    };

    template <class Operation, class Action, class Limits>
    [[nodiscard]] auto execute(const Action& action, const Limits& limits)
        -> document_command_result_t {
        if (!is_ready())
            return commit({});
        if (!Operation::has_valid_limits(limits)) {
            wormedit::reject(outcome, diagnostic_code_t::invalid_resolution,
                             std::string(Operation::invalid_limits_message));
            return finish_without_commit();
        }
        return std::visit(edit_execution_t<Operation, Limits>{*this, limits}, action);
    }

    template <class Operation, class Effect>
    [[nodiscard]] auto finish_execution(Operation& operation, Effect&& effect,
                                        chart_document_t& chart) -> document_command_result_t {
        if constexpr (requires { effect.has_value(); }) {
            if (!effect)
                return finish_without_commit();
            return finish_execution(operation, std::move(*effect), chart);
        } else {
            if (!outcome.diagnostics.empty() || !effect.changed)
                return finish_without_commit();
            if constexpr (requires { operation.postvalidate(chart, copy_document()); }) {
                if (!operation.postvalidate(chart, copy_document()))
                    return finish_without_commit();
            }
            return commit(std::move(effect.command_label));
        }
    }

    [[nodiscard]] auto commit_outcome(std::string label, document_command_options_t options = {})
        -> document_command_outcome_t;
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
