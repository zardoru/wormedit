#include "bmson_sound_channel_editor_internal.h"
#include "interaction_edit_commit.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/note_to_bgm.h>

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit {
using interaction_detail::chart_note_ids;

namespace {

struct editor_delete_ids_t {
    std::unordered_set<std::uint64_t> notes;
    std::unordered_set<std::uint64_t> bgm_events;
    // Chart-level objects that are not on a measure; IDs are unique within each
    // family.
    std::unordered_set<std::uint64_t> bookmarks;
    std::unordered_set<std::uint64_t> sections;
    std::unordered_set<std::uint64_t> bga_events;
    // Measures whose starting bar line is removed by merging them into the measure before.
    std::unordered_set<std::uint64_t> bar_lines;

    [[nodiscard]] auto total() const -> std::size_t {
        return notes.size() + bgm_events.size() + bookmarks.size() + sections.size() +
               bga_events.size() + bar_lines.size();
    }
    [[nodiscard]] auto family(editor_selection_object_kind_t kind)
        -> std::unordered_set<std::uint64_t>* {
        switch (kind) {
        case editor_selection_object_kind_t::note:
            return &notes;
        case editor_selection_object_kind_t::bgm_event:
            return &bgm_events;
        case editor_selection_object_kind_t::bookmark:
            return &bookmarks;
        case editor_selection_object_kind_t::section:
            return &sections;
        case editor_selection_object_kind_t::bga_event:
            return &bga_events;
        case editor_selection_object_kind_t::bar_line:
            return &bar_lines;
        default:
            return nullptr;
        }
    }
};

auto bms_branch_source_orders(const chart_document_t& chart)
    -> std::unordered_set<std::size_t> {
    std::unordered_set<std::size_t> orders;
    if (!chart.bms_control_tree)
        return orders;
    const auto visit = [&](const auto& self, const bms_control_node_t& node,
                           bool inside_branch) -> void {
        if (inside_branch)
            orders.insert(node.source_order);
        const auto boundary = node.kind == bms_control_kind_t::random ||
                              node.kind == bms_control_kind_t::switch_block ||
                              node.kind == bms_control_kind_t::if_group ||
                              node.kind == bms_control_kind_t::if_branch ||
                              node.kind == bms_control_kind_t::else_if_branch ||
                              node.kind == bms_control_kind_t::else_branch ||
                              node.kind == bms_control_kind_t::case_branch ||
                              node.kind == bms_control_kind_t::default_branch;
        for (const auto& child : node.children)
            self(self, child, inside_branch || boundary);
    };
    visit(visit, *chart.bms_control_tree, false);
    return orders;
}


auto collect_delete_object_ids(const std::vector<editor_selection_object_t>& objects)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t> {
    editor_delete_ids_t ids;
    for (const auto object : objects) {
        auto* family = ids.family(object.kind);
        if (family == nullptr) {
            return diagnostic_list_t{error(diagnostic_code_t::unsupported_feature,
                                           "Delete does not support this selected object type",
                                           object.id)};
        }
        if (!object.id || !family->insert(object.id.value).second) {
            return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                                           "Delete selection contains a duplicate or empty identity",
                                           object.id)};
        }
    }
    return ids;
}

auto collect_delete_ids(const editor_interaction_state_t& state)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t> {
    return collect_delete_object_ids(editor_selection_targets(state));
}

auto is_note_under_edit_cursor(const note_t& note, const editor_interaction_state_t& state)
    -> bool {
    return note.position == state.cursor && note.lane == state.active_lane;
}

void resolve_delete_cursor_fallback(const chart_document_t& chart,
                                    const editor_interaction_state_t& state,
                                    editor_delete_ids_t& ids) {
    if (ids.total() != 0)
        return;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (is_note_under_edit_cursor(note, state))
                ids.notes.insert(note.id.value);
}

auto validate_delete_note_ids(const chart_document_t& chart,
                              const std::unordered_set<std::uint64_t>& ids)
    -> std::optional<diagnostic_t> {
    const auto available = chart_note_ids(chart);
    for (const auto id : ids) {
        if (!available.contains(id))
            return error(diagnostic_code_t::invalid_id, "selected note no longer exists",
                         object_id_t{id});
    }
    return std::nullopt;
}

auto inspect_delete_bgm_events(const chart_document_t& chart, const editor_delete_ids_t& ids)
    -> std::variant<std::optional<object_id_t>, diagnostic_list_t> {
    std::optional<object_id_t> linked_bmson_event;
    std::size_t found_bgm{};
    const auto branch_source_orders = ids.bgm_events.empty()
                                          ? std::unordered_set<std::size_t>{}
                                          : bms_branch_source_orders(chart);
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (!ids.bgm_events.contains(event.id.value))
                continue;
            ++found_bgm;
            if (event.sound_channel_event) {
                if (ids.total() != 1) {
                    return diagnostic_list_t{error(
                        diagnostic_code_t::unsupported_feature,
                        "delete linked BMSON markers one at a time", event.id)};
                }
                linked_bmson_event = event.id;
            } else if (event.provenance.family == format_family_t::bms &&
                       !event.provenance.source_spelling.empty() &&
                       branch_source_orders.contains(event.provenance.source_order)) {
                return diagnostic_list_t{error(
                    diagnostic_code_t::unsupported_feature,
                    "branch-owned BMS BGM events need a source-aware delete", event.id)};
            }
        }
    }
    if (found_bgm != ids.bgm_events.size()) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "selected BGM event no longer exists")};
    }
    return linked_bmson_event;
}

// Delete removes playable stem points from the keys without cutting them out of the audio stem.
// Stage this conversion alongside ordinary deletion, so a later guard cannot leave a partial edit.
auto stage_delete_stem_points(document_command_transaction_t& transaction,
                              const chart_document_t& chart,
                              const editor_interaction_state_t& state,
                              const editor_delete_ids_t& ids)
    -> std::variant<std::size_t, diagnostic_list_t> {
    if (chart.source_family != format_family_t::bmson)
        return std::size_t{0};
    std::vector<object_id_t> stems;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            if (ids.notes.contains(note.id.value) && note.sound_channel_event)
                stems.push_back(note.id);
        }
    if (stems.empty())
        return std::size_t{0};
    auto moved = stage_move_notes_to_bgm(transaction, state.chart, stems, std::nullopt);
    if (!moved.value || !moved.ok())
        return std::move(moved.diagnostics);
    return stems.size();
}

// Deleted bar lines merge their measures after the other objects are gone, so every remaining
// object keeps its row.
auto stage_delete_bar_lines(chart_document_t& chart, const editor_delete_ids_t& ids)
    -> std::variant<std::size_t, diagnostic_list_t> {
    if (ids.bar_lines.empty()) return std::size_t{0};
    std::vector<measure_id_t> measures;
    measures.reserve(ids.bar_lines.size());
    for (const auto id : ids.bar_lines) measures.push_back(measure_id_t{id});
    if (auto diagnostics = remove_bar_lines(chart, measures); !diagnostics.empty()) return diagnostics;
    return measures.size();
}

auto erase_editor_selection_objects(chart_document_t& chart,
                                    const editor_delete_ids_t& ids) -> std::size_t {
    std::size_t removed{};
    removed += std::erase_if(chart.bookmarks, [&](const auto& bookmark) {
        return ids.bookmarks.contains(bookmark.id.value);
    });
    removed += std::erase_if(chart.sections, [&](const auto& section) {
        return ids.sections.contains(section.id.value);
    });
    removed += std::erase_if(chart.bga_events, [&](const auto& event) {
        return ids.bga_events.contains(event.id.value);
    });
    for (auto& measure : chart.measures) {
        removed += std::erase_if(measure.notes, [&](const auto& note) {
            return ids.notes.contains(note.id.value);
        });
        removed += std::erase_if(measure.bgm_events, [&](const auto& event) {
            return ids.bgm_events.contains(event.id.value);
        });
    }
    return removed;
}

auto stage_scoped_editor_delete(editor_edit_context_t& context,
    document_command_transaction_t& transaction) -> attempt_t<editor_edit_effect_t> {
    auto collected = collect_delete_ids(context.state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected)) return {.diagnostics = *diagnostics};
    auto ids = std::get<editor_delete_ids_t>(std::move(collected));
    if (!ids.bar_lines.empty()) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
        "Scoped deletion cannot remove shared measure geometry.")}};
    resolve_delete_cursor_fallback(context.scope.chart, context.state, ids);
    std::vector<object_id_t> objects;
    const auto append = [&](const auto& family) {
        for (const auto id : family) objects.push_back(object_id_t{id});
    };
    append(ids.notes); append(ids.bgm_events); append(ids.bookmarks); append(ids.sections); append(ids.bga_events);
    auto staged = transaction.stage_bms_native_objects(context.state.chart, objects, bms_existing_edit_kind_t::erase,
        [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
            const auto removed = erase_editor_selection_objects(chart, ids);
            if (removed != ids.total()) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                "Scoped deletion did not resolve every selected object.")}};
            return {.value = document_edit_effect_t{.changed = removed != 0, .command_label = "Delete chart objects"}};
        });
    if (!staged.value || !staged.ok()) return {.diagnostics = std::move(staged.diagnostics)};
    return {.value = editor_edit_effect_t{.label = "Delete chart objects",
        .has_document_changes = staged.value->changed, .selection = clear_editor_selection_t{}}};
}

} // namespace

auto stage_editor_delete(editor_edit_context_t& context,
                         document_command_transaction_t& transaction)
    -> attempt_t<editor_edit_effect_t> {
    if (transaction.has_bms_program(context.state.chart)) return stage_scoped_editor_delete(context, transaction);
    return transaction.stage(
        context.state.chart, "delete refers to an unknown chart",
        [&](chart_document_t& chart, song_document_t&) -> attempt_t<editor_edit_effect_t> {
            auto collected = collect_delete_ids(context.state);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            auto ids = std::get<editor_delete_ids_t>(std::move(collected));
            const auto selected = ids.total() != 0;
            resolve_delete_cursor_fallback(chart, context.state, ids);
            if (const auto invalid = validate_delete_note_ids(chart, ids.notes))
                return attempt_t<editor_edit_effect_t>{.diagnostics = {*invalid}};
            auto inspected = inspect_delete_bgm_events(chart, ids);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&inspected))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            const auto linked_event = std::get<std::optional<object_id_t>>(inspected);
            if (linked_event) {
                auto staged =
                    stage_delete_bmson_bgm_marker(transaction, context.state.chart, *linked_event);
                if (!staged.value || !staged.ok())
                    return {.diagnostics = std::move(staged.diagnostics)};
                return {.value =
                            editor_edit_effect_t{.label = std::move(staged.value->command_label),
                                                 .selection = clear_editor_selection_t{}}};
            }

            auto staged = stage_delete_stem_points(transaction, chart, context.state, ids);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&staged))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            auto removed = std::get<std::size_t>(staged);
            removed += erase_editor_selection_objects(chart, ids);
            auto merged = stage_delete_bar_lines(chart, ids);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&merged))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            removed += std::get<std::size_t>(merged);
            if (removed == 0)
                return {.value = editor_edit_effect_t{.has_document_changes = false}};
            if (removed != ids.total()) {
                return attempt_t<editor_edit_effect_t>{
                    .diagnostics = {error(diagnostic_code_t::invalid_id,
                                          "selected deletion did not resolve every object")}};
            }
            return {.value = editor_edit_effect_t{
                        .label = selected ? "Delete selected chart objects" : "Delete note",
                        .selection = clear_editor_selection_t{}}};
        });
}

auto delete_editor_selection(editor_edit_context_t& context) -> editor_interaction_result_t {
    return run_editor_edit(context, [&](document_command_transaction_t& transaction) {
        return stage_editor_delete(context, transaction);
    });
}

} // namespace wormedit
