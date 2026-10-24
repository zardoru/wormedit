#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_program_edit.h>

#include <algorithm>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {
using authored_program_t = result_t<bms_program_t, error_t<failure_code_t>>;
template<class Edit>
auto stage_program(document_command_transaction_t& transaction, chart_id_t difficulty, Edit edit,
    std::string label, std::optional<bms_program_evaluation_t> evaluation_override = {}) -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        const auto* owner = song.find_bms_program(difficulty);
        if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.")}};
        auto program = edit(owner->prepared().program);
        if (!program) return {.diagnostics = {error(diagnostic_code_t::invalid_id, program.error().message)}};
        if (owner->prepared().program == *program && (!evaluation_override || *evaluation_override == owner->evaluation()))
            return {.value = document_edit_effect_t{}};
        auto prepared = owner->prepared();
        prepared.program = std::move(*program);
        auto evaluation = evaluation_override ? std::move(*evaluation_override) :
            owner->evaluation().without_invalid_choices(prepared.program);
        auto updated = bms_program_document_t::from(difficulty, std::move(prepared), std::move(evaluation));
        if (!updated.value || !updated.ok()) {
            const auto message = updated.diagnostics.empty() ? "The authored program cannot be projected." :
                updated.diagnostics.front().message;
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, message)}};
        }
        const auto found = std::ranges::find_if(song.prepared_bms_programs,
            [&](const auto& item) { return item->difficulty() == difficulty; });
        *found = std::move(*updated.value);
        return {.value = document_edit_effect_t{.changed = true, .command_label = std::move(label)}};
    });
}
template<class Edit>
auto stage_program_proposal(proposal_edit_scope_t<chart_stamp_t>& scope, Edit edit, std::string label)
    -> attempt_t<std::string> {
    auto staged = stage_program(scope.transaction(), scope.captured_stamp().chart, std::move(edit), std::move(label));
    if (!staged.value || !staged.ok()) return {.diagnostics = std::move(staged.diagnostics)};
    return {.value = staged.value->changed ? std::optional{std::move(staged.value->command_label)} : std::nullopt,
        .diagnostics = std::move(staged.diagnostics)};
}
template<class Proposal, class Inspect, class... Arguments>
auto prepare_validated_program_proposal(const song_document_t& song, Proposal proposal,
    Inspect inspect, Arguments... arguments) -> attempt_t<Proposal> {
    const auto checked = document_command_transaction_t::preview(song, [&](document_history_t& preview) {
        auto result = apply_proposal(preview, proposal, arguments...);
        if (result.applied) inspect(proposal.payload, preview.document);
        return result;
    });
    if (!checked.applied) return {.diagnostics = checked.diagnostics};
    return {.value = std::move(proposal), .diagnostics = checked.diagnostics};
}
auto validate_insert_value(const bms_program_insert_value_t& value) -> std::optional<std::string> {
    if (const auto* chart = std::get_if<bms_chart_content_t>(&value); chart && !chart->items.empty())
        return "A new Chart must be empty; content uses the scoped content editor.";
    if (const auto* arm = std::get_if<bms_case_scope_t>(&value); arm && !arm->skips.empty())
        return "New cases start without SKIP; author behavior with a stable case anchor.";
    if (const auto* text = std::get_if<bms_text_t>(&value)) {
        const auto checked = validate_bms_program_text(*text);
        if (!checked) return checked.error().message;
    }
    return std::nullopt;
}
} // namespace

auto apply_bms_program_insert_command(document_history_t& history, bms_program_insert_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Add BMS node");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            if (const auto message = validate_insert_value(request.value))
                return std::unexpected(error_t{failure_code_t::invalid_request, *message});
            bms_program_node_t node{.value = std::visit([](const auto& value) -> bms_program_node_value_t { return value; }, request.value)};
            const auto first = transaction.next_bms_program_item_id(node.is_scope() ? 2 : 1);
            if (!first) return std::unexpected(error_t{failure_code_t::capacity_exceeded, "No fresh BMS node identities are available."});
            node.id = branch_id_t{first->value};
            const auto chart = node.is_scope() ? std::optional{branch_id_t{first->value + 1}} : std::nullopt;
            return program.with_node(request.parent, request.after, std::move(node), chart, request.limits);
        }, "Add BMS node"));
}
auto apply_bms_program_text_command(document_history_t& history, bms_program_text_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Edit BMS Text");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            const auto checked = validate_bms_program_text(request.text);
            if (!checked) return std::unexpected(checked.error());
            return program.with_text(request.node, std::move(request.text), request.limits);
        }, "Edit BMS Text"));
}
auto apply_bms_program_random_range_command(document_history_t& history, bms_program_random_range_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Change BMS RANDOM range");
    diagnostic_list_t notices;
    auto staged = stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            auto updated = program.with_random_range(request.node, request.range, request.limits);
            if (!updated || *updated == program) return updated;
            for (const auto arm : updated->unreachable_random_arms(request.node))
                notices.push_back({.severity = diagnostic_severity_t::warning, .code = diagnostic_code_t::invalid_binding,
                    .message = "IF/ELSEIF value is outside the RANDOM range; its body is retained.",
                    .object = object_id_t{arm.value}});
            const auto* owner = transaction.source_document().find_bms_program(request.stamp.chart);
            const auto& choices = owner->evaluation().selections;
            const auto selected = std::ranges::find(choices, request.node, &branch_selection_t::branch);
            if (selected != choices.end() && (selected->selected_value < 1 || selected->selected_value > request.range))
                notices.push_back({.severity = diagnostic_severity_t::warning, .code = diagnostic_code_t::invalid_binding,
                    .message = "Out-of-range RANDOM preview choice cleared without selecting a replacement.",
                    .object = object_id_t{request.node.value}});
            return updated;
        }, "Change BMS RANDOM range");
    auto result = transaction.finish_execution(std::move(staged));
    if (result.applied) result.diagnostics.insert(result.diagnostics.end(), notices.begin(), notices.end());
    return result;
}
auto apply_bms_program_parameter_command(document_history_t& history, bms_program_parameter_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Change BMS node parameter");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) { return program.with_parameter(request.node, request.value, request.limits); },
        "Change BMS node parameter"));
}
auto apply_bms_program_preview_command(document_history_t& history, bms_program_preview_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Change BMS playback choice");
    const auto* owner = transaction.source_document().find_bms_program(request.stamp.chart);
    if (!owner) return transaction.reject(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.");
    const auto* node = owner->prepared().program.find(request.node);
    if (!node || (!std::holds_alternative<bms_random_scope_t>(node->value) && !std::holds_alternative<bms_switch_scope_t>(node->value)))
        return transaction.reject(diagnostic_code_t::invalid_id, "Playback choices require an existing RANDOM or SWITCH node.");
    auto evaluation = owner->evaluation();
    const auto selected = std::ranges::find(evaluation.selections, request.node, &branch_selection_t::branch);
    if (selected == evaluation.selections.end()) {
        if (request.value) evaluation.selections.push_back({.branch = request.node, .selected_value = *request.value});
    } else if (request.value) selected->selected_value = *request.value;
    else evaluation.selections.erase(selected);
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [](const bms_program_t& program) -> authored_program_t { return program; },
        "Change BMS playback choice", std::move(evaluation)));
}
auto apply_bms_program_skip_command(document_history_t& history, bms_program_skip_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Add BMS SKIP");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            const auto* node = program.find(request.case_node);
            if (!node || !std::holds_alternative<bms_case_scope_t>(node->value))
                return std::unexpected(error_t{failure_code_t::not_found, "SKIP requires an existing CASE/DEF owner."});
            auto nodes = std::vector<bms_program_node_t>(program.nodes().begin(), program.nodes().end());
            auto& arm = std::get<bms_case_scope_t>(std::ranges::find(nodes, request.case_node, &bms_program_node_t::id)->value);
            if (std::ranges::find(arm.skips, request.skip) != arm.skips.end()) return program;
            arm.skips.push_back(std::move(request.skip));
            return bms_program_t::from(std::move(nodes), request.limits);
        }, "Add BMS SKIP"));
}
} // namespace wormedit

namespace wormedit {
auto bms_program_move_report_t::from(const bms_program_document_t& owner, branch_id_t subtree)
    -> std::optional<bms_program_move_report_t> {
    const auto& program = owner.prepared().program;
    const auto parent = program.parent(subtree);
    if (!parent) return std::nullopt;
    bms_program_move_report_t report{.source_parent = *parent};
    auto contents = bms_program_subtree_t::from(program, subtree);
    if (!contents) return std::nullopt;
    report.subtree = std::move(contents->nodes);
    for (const auto& node : program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        report.potentially_affected_charts.push_back(node.id);
        for (const auto& item : chart->items)
            if (std::holds_alternative<bms_resource_declaration_t>(item.value) ||
                std::holds_alternative<bms_timing_definition_t>(item.value) ||
                std::holds_alternative<bms_metadata_declaration_t>(item.value) ||
                std::holds_alternative<bms_measure_length_t>(item.value))
                report.potentially_affected_definitions.push_back(item.id);
    }
    const auto projection = owner.build_projection();
    if (!projection.value) return std::nullopt;
    const auto charts = projection.value->active_charts();
    report.before_active_charts.assign(charts.begin(), charts.end());
    return report;
}

auto bms_program_move_edit_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope) const -> attempt_t<std::string> {
    return stage_program_proposal(scope,
        [&](const bms_program_t& program) { return program.with_move(move, limits); }, "Move BMS subtree");
}
auto bms_program_move_edit_t::propose(const song_document_t& song, chart_id_t difficulty,
    bms_program_move_t move, bms_program_limits_t limits) -> attempt_t<bms_program_move_proposal_t> {
    auto proposal = bms_program_move_proposal_t::from(chart_stamp_t::from(song, difficulty),
        bms_program_move_edit_t{.move = move, .limits = limits});
    return prepare_validated_program_proposal(song, std::move(proposal),
        [&](bms_program_move_edit_t& payload, const song_document_t& after) {
            payload.report = *bms_program_move_report_t::from(*song.find_bms_program(difficulty), move.node);
            const auto projection = after.find_bms_program(difficulty)->build_projection();
            const auto charts = projection.value->active_charts();
            payload.report.after_active_charts.assign(charts.begin(), charts.end());
        });
}
} // namespace wormedit

namespace wormedit {
auto bms_program_delete_edit_t::validate_capture(const chart_stamp_t& stamp) const -> diagnostic_list_t {
    if (stamp == stamp_) return {};
    return {error(diagnostic_code_t::invalid_id, "Deletion confirmation does not match its prepared project, revision and difficulty.")};
}
auto bms_program_delete_edit_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    bms_program_delete_decision_t decision) const -> attempt_t<std::string> {
    if (decision != bms_program_delete_decision_t::confirm) return {};
    return stage_program_proposal(scope,
        [&](const bms_program_t& program) { return program.without_subtree(summary_.root, limits_); }, "Delete BMS subtree");
}
auto bms_program_delete_edit_t::propose(const song_document_t& song, chart_id_t difficulty,
    branch_id_t node, bms_program_limits_t limits) -> attempt_t<bms_program_delete_proposal_t> {
    const auto* owner = song.find_bms_program(difficulty);
    if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.")}};
    auto summary = bms_program_subtree_t::from(owner->prepared().program, node);
    if (!summary) return {.diagnostics = {error(diagnostic_code_t::invalid_id, summary.error().message)}};
    const auto stamp = chart_stamp_t::from(song, difficulty);
    auto proposal = bms_program_delete_proposal_t::from(stamp,
        bms_program_delete_edit_t(stamp, std::move(*summary), limits));
    return prepare_validated_program_proposal(song, std::move(proposal),
        [](bms_program_delete_edit_t&, const song_document_t&) {}, bms_program_delete_decision_t::confirm);
}
} // namespace wormedit

namespace wormedit {
auto bms_program_unwrap_report_t::from(const bms_program_document_t& owner, branch_id_t node)
    -> std::optional<bms_program_unwrap_report_t> {
    const auto& program = owner.prepared().program;
    auto contents = bms_program_subtree_t::from(program, node);
    auto effects = bms_program_move_report_t::from(owner, node);
    if (!contents || !effects || !program.find(node)->is_scope()) return std::nullopt;
    bms_program_unwrap_report_t report{.contents = std::move(*contents), .effects = std::move(*effects),
        .unconditional_branches = program.unwrap_conversions(node)};
    auto changed = report.unconditional_branches;
    changed.push_back(node);
    for (const auto id : changed)
        if (const auto* arm = std::get_if<bms_case_scope_t>(&program.find(id)->value)) report.removed_skips += arm->skips.size();
    return report;
}
auto bms_program_unwrap_edit_t::validate_capture(const chart_stamp_t& stamp) const -> diagnostic_list_t {
    if (stamp == stamp_) return {};
    return {error(diagnostic_code_t::invalid_id, "Unwrap confirmation does not match its prepared project, revision and difficulty.")};
}
auto bms_program_unwrap_edit_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    bms_program_unwrap_decision_t decision) const -> attempt_t<std::string> {
    if (decision != bms_program_unwrap_decision_t::confirm_promotion &&
        decision != bms_program_unwrap_decision_t::confirm_unconditional_conversion) return {};
    if ((!report_.unconditional_branches.empty() || report_.removed_skips) &&
        decision != bms_program_unwrap_decision_t::confirm_unconditional_conversion)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
            "Unwrap requires explicit confirmation to make affected branches unconditional and remove their selector-specific SKIP behavior.")}};
    return stage_program_proposal(scope,
        [&](const bms_program_t& program) { return program.with_unwrap(report_.contents.root, limits_); }, "Unwrap BMS scope");
}
auto bms_program_unwrap_edit_t::propose(const song_document_t& song, chart_id_t difficulty,
    branch_id_t node, bms_program_limits_t limits) -> attempt_t<bms_program_unwrap_proposal_t> {
    const auto* owner = song.find_bms_program(difficulty);
    if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.")}};
    auto report = bms_program_unwrap_report_t::from(*owner, node);
    if (!report)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Unwrap requires an existing non-Root body scope.")}};
    const auto stamp = chart_stamp_t::from(song, difficulty);
    auto proposal = bms_program_unwrap_proposal_t::from(stamp,
        bms_program_unwrap_edit_t(stamp, std::move(*report), limits));
    return prepare_validated_program_proposal(song, std::move(proposal),
        [&](bms_program_unwrap_edit_t& payload, const song_document_t& after) {
            const auto projection = after.find_bms_program(difficulty)->build_projection();
            const auto charts = projection.value->active_charts();
            payload.report_.effects.after_active_charts.assign(charts.begin(), charts.end());
        }, bms_program_unwrap_decision_t::confirm_unconditional_conversion);
}
} // namespace wormedit
