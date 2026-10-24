#include "sys/bms_program_runtime.h"
#include <wormedit/core/numeric_text.h>

#include <algorithm>
#include <array>
#include <set>
#include <type_traits>
#include <utility>

namespace wormedit::sys {
namespace {
using kind_t = ui::bms_program_node_kind_t;
using operation_t = ui::bms_program_operation_t;
auto kind(const bms_program_node_t& node) -> kind_t {
    static constexpr std::array kinds{kind_t::root, kind_t::random, kind_t::if_arm, kind_t::else_if,
        kind_t::else_arm, kind_t::switch_scope, kind_t::case_arm, kind_t::set_random,
        kind_t::set_switch, kind_t::chart, kind_t::text, kind_t::bare};
    static_assert(kinds.size() == std::variant_size_v<bms_program_node_value_t>);
    if (const auto* arm = std::get_if<bms_case_scope_t>(&node.value); arm && !arm->value) return kind_t::default_arm;
    return kinds[node.value.index()];
}
auto parameter(const bms_program_node_t& node) -> std::optional<std::string> {
    return std::visit([](const auto& value) -> std::optional<std::string> {
        using value_t = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<value_t, bms_random_scope_t>) return std::to_string(value.range);
        else if constexpr (std::is_same_v<value_t, bms_case_scope_t>) return value.value ? std::to_string(*value.value) : "DEF";
        else if constexpr (std::is_same_v<value_t, bms_if_scope_t> || std::is_same_v<value_t, bms_else_if_scope_t> ||
            std::is_same_v<value_t, bms_switch_scope_t> || std::is_same_v<value_t, bms_set_random_t> ||
            std::is_same_v<value_t, bms_set_switch_t>) return std::to_string(value.value);
        else return {};
    }, node.value);
}
auto label(const bms_program_node_t& node) -> std::string {
    auto result = std::string{ui::bms_program_kinds[static_cast<std::size_t>(kind(node))].label};
    const auto value = parameter(node);
    if (value && *value != "DEF") result += " " + *value;
    return result;
}
auto reject(const document_history_t& history, std::string message) -> document_command_result_t {
    return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_binding, std::move(message));
}
auto unchanged(const document_history_t& history) -> document_command_result_t {
    return {.revision = history.document.revision};
}
void append_rows(ui::bms_program_view_t& view, const bms_program_document_t& owner) {
    const auto& program = owner.prepared().program;
    std::set<branch_id_t> active, unreachable;
    const auto playback = owner.build_projection();
    if (playback.value) {
        const auto choice = std::ranges::find(playback.value->selections(), view.selected, &branch_selection_t::branch);
        if (choice != playback.value->selections().end()) view.playback_choice = choice->selected_value;
        for (auto chart : playback.value->active_charts()) {
            active.insert(chart);
            for (auto parent = program.parent(chart); parent; parent = program.parent(*parent)) active.insert(*parent);
        }
    }
    for (const auto& node : program.nodes())
        if (program.is_unreachable_random_arm(node.id)) unreachable.insert(node.id);
    struct entry_t { branch_id_t id; std::size_t depth; std::string path; };
    std::vector<entry_t> pending{{.id = program.root(), .depth = 0}};
    while (!pending.empty()) {
        auto entry = std::move(pending.back()); pending.pop_back();
        const auto& node = *program.find(entry.id);
        auto title = label(node);
        if (std::holds_alternative<bms_chart_content_t>(node.value)) {
            const auto& siblings = program.find(*program.parent(node.id))->children;
            const auto position = std::ranges::find(siblings, node.id);
            const auto ordinal = std::count_if(siblings.begin(), position, [&](branch_id_t sibling) {
                return std::holds_alternative<bms_chart_content_t>(program.find(sibling)->value);
            });
            title += " " + std::to_string(ordinal + 1);
        }
        entry.path += entry.path.empty() ? title : " / " + title;
        view.rows.push_back({.node = node.id, .label = title, .path = entry.path, .depth = entry.depth, .kind = kind(node),
            .selected = node.id == view.selected, .playback_active = active.contains(node.id),
            .unreachable = unreachable.contains(node.id)});
        if (node.id == view.selected) view.target_path = entry.path;
        for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
            pending.push_back({.id = *child, .depth = entry.depth + 1, .path = entry.path});
    }
}
auto placement(const bms_program_t& program, branch_id_t selected, bool continuation)
    -> std::pair<branch_id_t, std::optional<branch_id_t>> {
    const auto& node = *program.find(selected);
    if (!node.is_scope() || continuation) return {program.parent(selected).value_or(branch_id_t{}), selected};
    return {selected, node.children.empty() ? std::nullopt : std::optional{node.children.back()}};
}
auto insert_value(const ui::bms_program_action_t& action)
    -> std::optional<bms_program_insert_value_t> {
    const auto value = parse_integer<std::int64_t>(action.text.c_str()).value_or(1);
    const auto previous = action.after.value_or(branch_id_t{});
    switch (action.kind) {
    case kind_t::random: return bms_random_scope_t{.range = value};
    case kind_t::if_arm: return bms_if_scope_t{.value = value};
    case kind_t::else_if: return bms_else_if_scope_t{.previous_arm = previous, .value = value};
    case kind_t::else_arm: return bms_else_scope_t{.previous_arm = previous};
    case kind_t::switch_scope: return bms_switch_scope_t{.value = value};
    case kind_t::case_arm: return bms_case_scope_t{.value = value};
    case kind_t::default_arm: return bms_case_scope_t{};
    case kind_t::set_random: return bms_set_random_t{.value = value};
    case kind_t::set_switch: return bms_set_switch_t{.value = value};
    case kind_t::chart: return bms_chart_content_t{};
    case kind_t::text: return bms_text_t{.bytes = "; comment\n", .encoding = text_encoding_t::utf8};
    case kind_t::root: case kind_t::bare: return {};
    }
    return {};
}
}

void bms_program_runtime_t::load_draft(const bms_program_t& program, std::uint64_t revision) {
    draft_revision_ = revision;
    dirty_ = false;
    parameter_.clear(); text_.clear(); preview_.clear();
    if (const auto* node = program.find(selected_)) {
        parameter_ = parameter(*node).value_or("");
        if (const auto* text = std::get_if<bms_text_t>(&node->value)) {
            const auto decoded = validate_bms_program_text(*text);
            text_ = decoded ? *decoded : text->bytes;
        }
    }
    pending_ = std::monostate{};
}
void bms_program_runtime_t::select(document_history_t& history, const bms_program_t& program, branch_id_t node) {
    selected_ = node;
    content_chart_ = program.target_chart(node);
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty_, .chart = content_chart_.value_or(branch_id_t{})};
    load_draft(program, history.document.revision);
    feedback_.clear();
}
void bms_program_runtime_t::synchronize(document_history_t& history, std::optional<chart_id_t> difficulty) {
    const auto* owner = difficulty ? history.document.find_bms_program(*difficulty) : nullptr;
    if (!owner) {
        if (difficulty_) history.bms_edit_target.reset();
        *this = {};
        return;
    }
    const auto& program = owner->prepared().program;
    if (project_ != history.document.id || difficulty_ != *difficulty) {
        *this = {};
        project_ = history.document.id; difficulty_ = *difficulty;
        select(history, program, program.root());
    } else if (!program.find(selected_)) {
        const auto target = history.bms_edit_target;
        select(history, program, target && target->difficulty == difficulty_ ? target->chart : branch_id_t{});
    } else if (draft_revision_ != history.document.revision && !dirty_) {
        load_draft(program, history.document.revision);
        feedback_.clear();
    }
    content_chart_ = program.target_chart(selected_);
    history.bms_edit_target = bms_program_target_key_t{.difficulty = difficulty_,
        .chart = content_chart_.value_or(branch_id_t{})};
}
void bms_program_runtime_t::build_tree_cache(const bms_program_document_t& owner, std::uint64_t revision) const {
    const auto key = std::pair{revision, selected_};
    if (tree_key_ == key) return;
    tree_view_.rows.clear(); tree_view_.messages.clear(); tree_view_.target_path.clear();
    tree_view_.selected = selected_; tree_view_.playback_choice.reset();
    append_rows(tree_view_, owner);
    const auto choice = std::ranges::find(owner.evaluation().selections, selected_, &branch_selection_t::branch);
    if (choice != owner.evaluation().selections.end()) tree_view_.playback_choice = choice->selected_value;
    if (content_chart_) {
        const auto projection = owner.build_projection(content_chart_);
        for (const auto& diagnostic : projection.diagnostics) tree_view_.messages.push_back({.text = diagnostic.message,
            .warning = diagnostic.severity == diagnostic_severity_t::warning, .error = diagnostic.severity == diagnostic_severity_t::error});
    }
    tree_key_ = key;
}
void bms_program_runtime_t::refill(ui::bms_program_view_t& view, const document_history_t& history) const {
    view.available = false;
    view.stale_draft = view.has_parameter = view.has_text = view.can_preview = view.can_delete = view.can_unwrap = false;
    view.unconditional_conversion = false;
    view.rows.clear(); view.messages.clear(); view.confirmation.clear(); view.confirmation_operation.reset(); view.target_path.clear();
    view.parameter.clear(); view.text.clear(); view.preview.clear(); view.content_chart.reset(); view.playback_choice.reset();
    view.project = {}; view.difficulty = {}; view.revision = 0; view.selected = {};
    const auto* owner = history.document.find_bms_program(difficulty_);
    if (!owner || history.document.id != project_) return;
    view.available = true; view.project = project_; view.difficulty = difficulty_;
    view.revision = history.document.revision; view.selected = selected_; view.content_chart = content_chart_;
    view.parameter = parameter_; view.text = text_; view.preview = preview_; view.stale_draft = draft_revision_ != view.revision;
    build_tree_cache(*owner, view.revision);
    view.rows = tree_view_.rows; view.target_path = tree_view_.target_path; view.playback_choice = tree_view_.playback_choice;
    const auto* node = owner->prepared().program.find(selected_);
    if (node) {
        view.has_parameter = parameter(*node).has_value();
        view.has_text = std::holds_alternative<bms_text_t>(node->value);
        view.can_preview = std::holds_alternative<bms_random_scope_t>(node->value) || std::holds_alternative<bms_switch_scope_t>(node->value);
        view.can_delete = node->id != owner->prepared().program.root();
        view.can_unwrap = view.can_delete && node->is_scope();
    }
    for (const auto& diagnostic : feedback_) view.messages.push_back({.text = diagnostic.message,
        .warning = diagnostic.severity == diagnostic_severity_t::warning, .error = diagnostic.severity == diagnostic_severity_t::error});
    view.messages.insert(view.messages.end(), tree_view_.messages.begin(), tree_view_.messages.end());
    if (const auto* proposal = std::get_if<bms_program_delete_proposal_t>(&pending_)) {
        const auto& summary = proposal->payload.summary();
        view.confirmation_operation = operation_t::confirm_delete;
        view.confirmation = "Delete entire subtree: " + std::to_string(summary.nodes.size()) + " nodes, " +
            std::to_string(summary.charts) + " Charts, " + std::to_string(summary.content_items) + " items, " +
            std::to_string(summary.texts) + " Text nodes, " + std::to_string(summary.scopes) + " scopes, " +
            std::to_string(summary.settings) + " fixed settings. Shared resources remain.";
    }
    if (const auto* proposal = std::get_if<bms_program_unwrap_proposal_t>(&pending_)) {
        const auto& report = proposal->payload.report();
        view.unconditional_conversion = !report.unconditional_branches.empty() || report.removed_skips;
        view.confirmation_operation = view.unconditional_conversion ? operation_t::confirm_unconditional : operation_t::confirm_promotion;
        view.confirmation = "Remove this scope and promote its ordered contents. " + std::to_string(report.unconditional_branches.size()) +
            " dependent branches become always-applying; " + std::to_string(report.removed_skips) + " selector-specific SKIPs are removed.";
    }
}
auto bms_program_runtime_t::save(document_history_t& history) -> document_command_result_t {
    if (draft_revision_ != history.document.revision) return reject(history, "Draft is stale. Reload the node before applying it.");
    const auto& program = history.document.find_bms_program(difficulty_)->prepared().program;
    const auto* node = program.find(selected_);
    const auto stamp = chart_stamp_t::from(history, difficulty_);
    if (const auto* original = std::get_if<bms_text_t>(&node->value)) {
        const auto decoded = validate_bms_program_text(*original);
        if (decoded && *decoded == text_) return unchanged(history);
        auto text = bms_text_t{.bytes = text_, .encoding = text_encoding_t::utf8};
        return apply_bms_program_text_command(history, {.stamp = stamp, .node = selected_, .text = std::move(text)});
    }
    const auto value = parse_integer<std::int64_t>(parameter_.c_str());
    if (!value && !(std::holds_alternative<bms_case_scope_t>(node->value) && parameter_ == "DEF"))
        return reject(history, "Enter an integer parameter, or DEF for the default case.");
    if (std::holds_alternative<bms_random_scope_t>(node->value))
        return apply_bms_program_random_range_command(history, {.stamp = stamp, .node = selected_, .range = *value});
    return apply_bms_program_parameter_command(history, {.stamp = stamp, .node = selected_, .value = value});
}
auto bms_program_runtime_t::prepare(document_history_t& history, bool unwrap) -> document_command_result_t {
    pending_ = std::monostate{};
    if (unwrap) {
        auto proposal = bms_program_unwrap_edit_t::propose(history.document, difficulty_, selected_);
        if (proposal.value) pending_ = std::move(*proposal.value);
        return {.revision = history.document.revision, .diagnostics = std::move(proposal.diagnostics)};
    }
    auto proposal = bms_program_delete_edit_t::propose(history.document, difficulty_, selected_);
    if (proposal.value) pending_ = std::move(*proposal.value);
    return {.revision = history.document.revision, .diagnostics = std::move(proposal.diagnostics)};
}
auto bms_program_runtime_t::confirm(document_history_t& history, operation_t operation) -> document_command_result_t {
    if (const auto* proposal = std::get_if<bms_program_delete_proposal_t>(&pending_);
        proposal && operation == operation_t::confirm_delete)
        return apply_proposal(history, *proposal, bms_program_delete_decision_t::confirm);
    if (const auto* proposal = std::get_if<bms_program_unwrap_proposal_t>(&pending_)) {
        if (operation == operation_t::confirm_promotion)
            return apply_proposal(history, *proposal, bms_program_unwrap_decision_t::confirm_promotion);
        if (operation == operation_t::confirm_unconditional)
            return apply_proposal(history, *proposal, bms_program_unwrap_decision_t::confirm_unconditional_conversion);
    }
    return reject(history, "Confirmation does not match the prepared Delete or Unwrap operation.");
}
auto bms_program_runtime_t::edit(document_history_t& history, const ui::bms_program_action_t& action)
    -> document_command_result_t {
    const auto& program = history.document.find_bms_program(difficulty_)->prepared().program;
    const auto stamp = chart_stamp_t::from(history, difficulty_);
    switch (action.operation) {
    case operation_t::reload: load_draft(program, history.document.revision); return unchanged(history);
    case operation_t::draft_parameter: parameter_ = action.text; dirty_ = true; return unchanged(history);
    case operation_t::draft_text: text_ = action.text; dirty_ = true; return unchanged(history);
    case operation_t::draft_preview: preview_ = action.text; return unchanged(history);
    case operation_t::save: return save(history);
    case operation_t::prepare_delete: return prepare(history, false);
    case operation_t::prepare_unwrap: return prepare(history, true);
    case operation_t::confirm_delete: case operation_t::confirm_promotion: case operation_t::confirm_unconditional:
        return confirm(history, action.operation);
    case operation_t::cancel: pending_ = std::monostate{}; return unchanged(history);
    case operation_t::set_preview: case operation_t::clear_preview: {
        const auto value = parse_integer<std::int64_t>(action.text.c_str());
        if (action.operation == operation_t::set_preview && !value) return reject(history, "Enter an integer playback choice.");
        return apply_bms_program_preview_command(history, {.stamp = stamp, .node = selected_,
            .value = action.operation == operation_t::clear_preview ? std::nullopt : value});
    }
    case operation_t::add: {
        const auto [parent, after] = placement(program, selected_, action.kind == kind_t::else_if || action.kind == kind_t::else_arm);
        auto insertion = action;
        insertion.after = after;
        const auto value = insert_value(insertion);
        if (!value) return reject(history, "Choose a supported node kind.");
        return apply_bms_program_insert_command(history, {.stamp = stamp, .parent = parent, .after = after, .value = *value});
    }
    case operation_t::move: {
        const auto* destination = program.find(action.node);
        if (!destination) return reject(history, "Move destination no longer exists.");
        const auto [parent, after] = placement(program, action.node, false);
        auto proposal = bms_program_move_edit_t::propose(history.document, difficulty_,
            {.node = selected_, .parent = parent, .after = after});
        if (!proposal.value) return {.revision = history.document.revision, .diagnostics = std::move(proposal.diagnostics)};
        return apply_proposal(history, *proposal.value);
    }
    case operation_t::select: return unchanged(history);
    }
    return reject(history, "Unknown scope operation.");
}
auto bms_program_runtime_t::apply(document_history_t& history, editor_interaction_state_t& state,
    const ui::bms_program_action_t& action) -> document_command_result_t {
    if (action.project != project_ || history.document.id != project_ || action.difficulty != difficulty_ ||
        action.revision != history.document.revision || action.selected != selected_)
        return reject(history, stale_action_message("BMS scope action"));
    const auto* owner = history.document.find_bms_program(difficulty_);
    if (!owner) return reject(history, "Scoped difficulty is no longer available.");
    if (action.operation == operation_t::select) {
        if (!owner->prepared().program.find(action.node)) return reject(history, "Selected node no longer exists.");
        if (selected_ != action.node) {
            select(history, owner->prepared().program, action.node);
            state.clear_authoring_selection(); history.selections = {};
        }
        return unchanged(history);
    }
    if (!owner->prepared().program.find(selected_)) return reject(history, "Choose an existing node first.");
    auto result = edit(history, action);
    if (result.applied) {
        if (action.operation == operation_t::save) dirty_ = false;
        synchronize(history, difficulty_);
        state.clear_authoring_selection(); history.selections = {};
    } else if (action.operation == operation_t::save && !diagnostic_queries::has_errors(result.diagnostics))
        load_draft(owner->prepared().program, history.document.revision);
    feedback_ = result.diagnostics;
    return result;
}
}
