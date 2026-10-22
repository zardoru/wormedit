#pragma once

#include <wormedit/core/quantize.h>
#include <wormedit/core/result.h>
#include <wormedit/core/selection_history.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    std::uint64_t sequence{};
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
    editor_selection_history_t selections;
    std::uint64_t next_history_sequence{1};

    [[nodiscard]] auto can_undo() const -> bool { return !undo.empty() || !selections.undo.empty(); }
    [[nodiscard]] auto can_redo() const -> bool { return !redo.empty() || !selections.redo.empty(); }

  private:
    [[nodiscard]] auto indices_storage(const chart_document_t& chart) const
        -> const std::shared_ptr<const chart_indices_t>&;
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

// One typed chart edit handled by an operation, as document_command_transaction_t::stage
// runs it: current-size caps first, then the edit. A void edit records its effect in the
// operation's context; otherwise it returns one with changed and command_label.
template <class Operation, class Edit>
concept chart_edit_handler_for =
    requires(Operation& operation, chart_document_t& chart, const Edit& edit) {
        { edit.chart } -> std::convertible_to<chart_id_t>;
        { operation.event_caps(chart, edit) } -> std::ranges::input_range;
        requires std::same_as<std::ranges::range_value_t<decltype(operation.event_caps(chart, edit))>,
                              chart_event_cap_t>;
        operation(chart, edit);
        requires std::is_void_v<decltype(operation(chart, edit))> || requires {
            { operation(chart, edit).changed } -> std::convertible_to<bool>;
            operation(chart, edit).command_label;
        };
    };

template <class Operation, class Action> inline constexpr bool handles_every_chart_edit_v = false;
template <class Operation, class... Edits>
inline constexpr bool handles_every_chart_edit_v<Operation, std::variant<Edits...>> =
    (chart_edit_handler_for<Operation, Edits> && ...);

// The contract for transaction.stage<Operation>/execute<Operation>(action, limits). The
// transaction owns limit rejection, chart lookup, cap checks and completion; the operation
// owns its domain validation, mutation and effect. Optional hooks, checked when declared:
//   static constexpr bool requires_unique_chart  reject charts whose ID appears twice
//   bool prepare(chart_document_t&)                 domain setup after the cap checks
//   bool postvalidate(chart_document_t&, song_document_t&)  after a changed edit
template <class Operation, class Action, class Limits>
concept chart_edit_operation =
    std::derived_from<Operation, chart_edit_context_t<Limits>> &&
    requires(const Limits& limits) {
        { Operation::has_valid_limits(limits) } -> std::convertible_to<bool>;
        { Operation::invalid_limits_message } -> std::convertible_to<std::string_view>;
        { Operation::unknown_chart_message } -> std::convertible_to<std::string_view>;
    } &&
    handles_every_chart_edit_v<Operation, Action> &&
    (!requires { Operation::requires_unique_chart; } ||
     std::same_as<std::remove_cv_t<decltype(Operation::requires_unique_chart)>, bool>) &&
    (!requires(Operation& operation, chart_document_t& chart) { operation.prepare(chart); } ||
     requires(Operation& operation, chart_document_t& chart) {
         { operation.prepare(chart) } -> std::convertible_to<bool>;
     }) &&
    (!requires(Operation& operation, chart_document_t& chart, song_document_t& document) {
        operation.postvalidate(chart, document);
    } || requires(Operation& operation, chart_document_t& chart, song_document_t& document) {
        { operation.postvalidate(chart, document) } -> std::convertible_to<bool>;
    });

// A synchronous command owns its working document only after the first edit.
// Stamped construction creates a rejected transaction when the capture is stale.
class document_command_transaction_t {
  public:
    explicit document_command_transaction_t(document_history_t& history);
    // Runs the ordinary command boundary on an isolated document, retaining no Undo.
    // Domain callbacks may collect reports; the live document is never published.
    // Returned applied/revision describe the candidate.
    [[nodiscard]] static auto preview(const song_document_t&,
        const std::function<document_command_result_t(document_history_t&)>&)
        -> document_command_result_t;

    document_command_transaction_t(document_history_t& history, const document_stamp_t& stamp,
        std::string_view label,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision,
        diagnostic_code_t stale_code = diagnostic_code_t::invalid_id);
    document_command_transaction_t(document_history_t& history, const chart_stamp_t& stamp,
        std::string_view label,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision,
        diagnostic_code_t stale_code = diagnostic_code_t::invalid_id);
    [[nodiscard]] auto is_ready() const noexcept -> bool;
    [[nodiscard]] auto has_copy() const noexcept -> bool;
    // The current transaction document, including an already prepared snapshot.
    [[nodiscard]] auto current_document() const noexcept -> const song_document_t&;
    [[nodiscard]] auto source_document() const noexcept -> const song_document_t&;
    [[nodiscard]] auto outcome() noexcept -> document_command_result_t&;
    [[nodiscard]] auto outcome() const noexcept -> const document_command_result_t&;
    [[nodiscard]] auto source_chart(chart_id_t id) const -> const chart_document_t*;
    auto document() -> song_document_t&;
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
    // Run a chart edit in this transaction. Nested edits share its one lazy copy.
    // Rejection poisons publication but retains storage until callbacks unwind.
    template <class Edit>
    [[nodiscard]] auto stage(Edit edit) -> std::invoke_result_t<Edit&, song_document_t&> {
        using result_type = std::invoke_result_t<Edit&, song_document_t&>;
        if (!is_ready())
            return result_type{.diagnostics = outcome_.diagnostics};
        auto staged = edit(document());
        if (!staged.value || !staged.ok()) {
            if (!finished_)
                outcome_.diagnostics.insert(outcome_.diagnostics.end(), staged.diagnostics.begin(),
                                            staged.diagnostics.end());
            finished_ = true;
        }
        if (!is_ready())
            return result_type{.diagnostics = outcome_.diagnostics};
        return staged;
    }

    template <class Edit>
    [[nodiscard]] auto stage(chart_id_t id, std::string_view unknown_chart_message, Edit edit,
                             bool requires_unique_chart = false)
        -> std::invoke_result_t<Edit&, chart_document_t&, song_document_t&> {
        using result_type = std::invoke_result_t<Edit&, chart_document_t&, song_document_t&>;
        return stage([&](song_document_t& proposed) -> result_type {
            auto* target = proposed.find_chart(id);
            if (!id || !target ||
                (requires_unique_chart &&
                 std::ranges::count(proposed.charts, id, &chart_document_t::id) != 1)) {
                return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                              std::string(unknown_chart_message))}};
            }
            return edit(*target, proposed);
        });
    }

    template <class Operation, class Action, class Limits>
        requires chart_edit_operation<Operation, Action, Limits>
    [[nodiscard]] auto stage(const Action& action, const Limits& limits)
        -> attempt_t<document_edit_effect_t> {
        if (!is_ready())
            return {.diagnostics = outcome_.diagnostics};
        if (!Operation::has_valid_limits(limits)) {
            wormedit::reject(outcome_, diagnostic_code_t::invalid_resolution,
                             std::string(Operation::invalid_limits_message));
            finished_ = true;
            return {.diagnostics = outcome_.diagnostics};
        }
        return std::visit(edit_execution_t<Operation, Limits>{*this, limits}, action);
    }

    template <class Operation, class Action, class Limits>
        requires chart_edit_operation<Operation, Action, Limits>
    [[nodiscard]] auto execute(const Action& action, const Limits& limits)
        -> document_command_result_t {
        return finish_execution(stage<Operation>(action, limits));
    }

  private:
    // The std::visit visitor behind stage<Operation>: one call per edit alternative, through
    // the same chart-resolution/staging boundary as composite edits.
    template <class Operation, class Limits> struct edit_execution_t {
        document_command_transaction_t& transaction;
        const Limits& limits;

        template <class Edit>
        auto operator()(const Edit& edit) const -> attempt_t<document_edit_effect_t> {
            constexpr bool unique = [] {
                if constexpr (requires { Operation::requires_unique_chart; })
                    return Operation::requires_unique_chart;
                return false;
            }();
            return transaction.stage(
                edit.chart, Operation::unknown_chart_message,
                [&](chart_document_t& chart,
                    song_document_t& document) -> attempt_t<document_edit_effect_t> {
                    Operation operation{{transaction.outcome_, limits}};
                    for (const auto& cap : operation.event_caps(chart, edit)) {
                        if (cap.count > cap.maximum) {
                            wormedit::reject(transaction.outcome_,
                                             diagnostic_code_t::invalid_resolution,
                                             std::string(cap.message));
                            return {};
                        }
                    }
                    if constexpr (requires { operation.prepare(chart); }) {
                        if (!operation.prepare(chart))
                            return {};
                    }
                    if constexpr (std::is_void_v<decltype(operation(chart, edit))>) {
                        operation(chart, edit);
                        return transaction.stage_effect(operation, operation, chart, document);
                    } else {
                        auto effect = operation(chart, edit);
                        return transaction.stage_effect(operation, std::move(effect), chart,
                                                        document);
                    }
                },
                unique);
        }
    };

    template <class Operation, class Effect>
    [[nodiscard]] auto stage_effect(Operation& operation, Effect&& effect, chart_document_t& chart,
                                    song_document_t& document)
        -> attempt_t<document_edit_effect_t> {
        if (!outcome_.diagnostics.empty())
            return {};
        if (!effect.changed)
            return {.value = document_edit_effect_t{}};
        if constexpr (requires { operation.postvalidate(chart, document); }) {
            if (!operation.postvalidate(chart, document))
                return {};
        }
        return {.value = document_edit_effect_t{.changed = effect.changed,
                                                .command_label = std::move(effect.command_label)}};
    }

  public:
    [[nodiscard]] auto
    finish_execution(attempt_t<document_edit_effect_t> staged,
                     command_noop_policy_t noop_policy = command_noop_policy_t::reject)
        -> document_command_result_t;

    [[nodiscard]] auto commit(std::string label, document_command_options_t options = {})
        -> document_command_result_t;

  private:
    document_history_t& history_;
    std::optional<song_document_t> document_;
    document_command_result_t outcome_;
    bool finished_{};
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
