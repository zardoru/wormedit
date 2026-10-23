#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/document_index.h>
#include "lane_occupancy.h"
#include "id_ceiling.h"

#include <algorithm>
#include <limits>
#include <set>

namespace wormedit {
namespace {
auto find_program(const song_document_t& document, chart_id_t difficulty)
    -> const bms_program_document_t* {
    const bms_program_document_t* result{};
    for (const auto& program : document.prepared_bms_programs) {
        if (!program || program->difficulty() != difficulty) continue;
        if (result) return nullptr;
        result = program.get();
    }
    return result;
}

auto resolve_target(const song_document_t& document, chart_id_t difficulty,
    std::optional<branch_id_t> selected)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    const auto* owner = find_program(document, difficulty);
    if (!difficulty || !owner || document.find_chart(difficulty))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
            "Scoped difficulty is absent or ambiguous."});
    const auto& program = owner->prepared().program;
    const auto node = selected.value_or(program.root());
    const auto chart = program.target_chart(node);
    if (!chart)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
            "Selected scope or Chart is unavailable."});
    const auto scope = program.parent(*chart);
    const auto projection = owner->build_projection();
    if (!projection.value || !projection.ok())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::materialize_failed,
            "Scoped difficulty cannot be evaluated."});
    if (std::ranges::find(projection.value->active_charts(), *chart) ==
        projection.value->active_charts().end())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable,
            "Selected Chart is inactive in this evaluation."});
    return bms_program_edit_target_t{.stamp = document_stamp_t::from(document),
        .difficulty = difficulty, .scope = *scope, .chart = *chart};
}

auto maximum_program_id(const song_document_t& document) -> std::uint64_t {
    id_ceiling_t<object_id_t> ceiling;
    for (const auto& owner : document.prepared_bms_programs) {
        if (!owner) continue;
        (void)id_declarations_t<object_id_t>::visit(owner->prepared(), ceiling);
    }
    return ceiling.maximum;
}

auto endpoint_owners(const bms_program_document_t& owner,
    std::span<const bms_program_owner_t> contributors) -> std::vector<bms_program_owner_t> {
    std::vector<bms_program_owner_t> endpoints;
    for (const auto contributor : contributors) {
        const auto* node = owner.prepared().program.find(contributor.chart);
        const auto& items = std::get<bms_chart_content_t>(node->value).items;
        const auto item = std::ranges::find(items, contributor.item, &bms_content_item_t::id);
        if (item != items.end() && std::holds_alternative<bms_hold_endpoint_t>(item->value))
            endpoints.push_back(contributor);
    }
    return endpoints;
}

// Declarations also contribute to a projected hold, but are independently
// editable. Only its authored endpoints determine whether a hold spans owners.
auto cross_owner_endpoints(const bms_program_document_t& owner)
    -> std::set<object_id_t> {
    std::set<object_id_t> result;
    const auto projection = owner.build_projection();
    if (!projection.value) return result;
    for (const auto& measure : projection.value->song().charts.front().measures) {
        for (const auto& note : measure.notes) {
            if (!note.end) continue;
            const auto endpoints = endpoint_owners(owner, projection.value->owners(note.id.value));
            if (endpoints.size() == 2 && endpoints.front().chart != endpoints.back().chart)
                for (const auto endpoint : endpoints) result.insert(endpoint.item);
        }
    }
    return result;
}

auto has_unchanged_item(const bms_program_document_t* before,
    const bms_program_document_t& proposed, const bms_program_projection_diagnostic_t& diagnostic) -> bool {
    if (!before) return false;
    const auto* source = before->prepared().program.find(diagnostic.node);
    const auto* destination = proposed.prepared().program.find(diagnostic.node);
    if (!source || !destination) return false;
    const auto* old_content = std::get_if<bms_chart_content_t>(&source->value);
    const auto* new_content = std::get_if<bms_chart_content_t>(&destination->value);
    if (!old_content || !new_content) return false;
    const auto old_item = std::ranges::find(old_content->items, diagnostic.item, &bms_content_item_t::id);
    const auto new_item = std::ranges::find(new_content->items, diagnostic.item, &bms_content_item_t::id);
    return old_item != old_content->items.end() && new_item != new_content->items.end() && *old_item == *new_item;
}

auto has_existing_missing_reference(const bms_program_document_t* before,
    const bms_program_document_t& proposed, const bms_program_projection_diagnostic_t& diagnostic,
    std::span<const bms_program_projection_diagnostic_t> previous_diagnostics) -> bool {
    if (!has_unchanged_item(before, proposed, diagnostic)) return false;
    return std::ranges::find(previous_diagnostics, diagnostic) != previous_diagnostics.end();
}

auto has_unchanged_note_geometry(const note_t& note, const chart_document_t& chart,
    const note_t* old, const chart_document_t* previous) -> bool {
    if (!previous) return false;
    if (!old || old->lane != note.lane || old->position.measure != note.position.measure ||
        old->end.has_value() != note.end.has_value()) return false;
    if (rows_to_fraction(old->position.row, previous->divisions_per_standard_measure) !=
        rows_to_fraction(note.position.row, chart.divisions_per_standard_measure)) return false;
    return !note.end || (old->end->measure == note.end->measure &&
        rows_to_fraction(old->end->row, previous->divisions_per_standard_measure) ==
        rows_to_fraction(note.end->row, chart.divisions_per_standard_measure));
}

auto new_note_collision(const bms_program_projection_t& projection,
    const bms_program_projection_t* before) -> std::optional<object_id_t> {
    const auto& chart = projection.song().charts.front();
    const auto index = measure_index_t::from(chart);
    if (!index) return std::nullopt; // The shared document validator reports geometry failures.
    const auto* previous_chart = before ? &before->song().charts.front() : nullptr;
    document_id_index_t<note_t> old_notes;
    if (previous_chart)
        for (const auto& measure : previous_chart->measures)
            for (const auto& note : measure.notes) old_notes.add(note);
    std::vector<lane_occupant_t> intervals;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (auto interval = lane_occupant_t::from(note, *index)) {
                interval->proposed = !has_unchanged_note_geometry(note, chart, old_notes.find(note.id), previous_chart);
                intervals.push_back(*interval);
            }
        }
    }
    const auto occupancy = lane_occupancy_t::from(std::move(intervals));
    const auto* collision = occupancy.first_collision(lane_occupancy_pairs_t::proposed);
    return collision ? std::optional{collision->note} : std::nullopt;
}

struct content_editor_t {
    const bms_program_edit_target_t& target;
    std::vector<bms_content_item_t>& items;
    const std::set<object_id_t>& cross_owner;
    std::uint64_t reserved_through;
    diagnostic_list_t diagnostics;
    bool changed{};

    auto find(bms_program_owner_t owner) -> bms_content_item_t* {
        if (owner.scope != target.scope || owner.chart != target.chart || cross_owner.contains(owner.item)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                "Edit does not target the object's complete semantic owner.", owner.item));
            return nullptr;
        }
        const auto found = std::ranges::find(items, owner.item, &bms_content_item_t::id);
        if (found != items.end()) return &*found;
        diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "Scoped object no longer exists.", owner.item));
        return nullptr;
    }
    void operator()(const bms_program_content_insert_t& edit) {
        if (edit.item.id.value <= reserved_through ||
            std::ranges::find(items, edit.item.id, &bms_content_item_t::id) != items.end()) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                "New scoped content requires an unused history-safe identity.", edit.item.id));
            return;
        }
        items.push_back(edit.item);
        changed = true;
    }
    void operator()(const bms_program_content_update_t& edit) {
        auto* item = find(edit.owner);
        if (!item || item->value == edit.value) return;
        item->value = edit.value;
        changed = true;
    }
    void operator()(const bms_program_content_delete_t& edit) {
        if (!find(edit.owner)) return;
        std::erase_if(items, [&](const auto& item) { return item.id == edit.owner.item; });
        changed = true;
    }
};

auto selector_values(const bms_program_t& program, const bms_program_node_t& node,
    std::size_t maximum) -> result_t<std::vector<std::int64_t>, error_t<failure_code_t>> {
    std::vector<std::int64_t> values;
    if (const auto* random = std::get_if<bms_random_scope_t>(&node.value)) {
        if (static_cast<std::uint64_t>(random->range) > maximum)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
                "Scoped edit exceeds its bounded evaluation budget."});
        for (std::int64_t value = 1; value <= random->range; ++value) values.push_back(value);
    } else if (std::holds_alternative<bms_switch_scope_t>(node.value)) {
        bool has_default{};
        for (const auto child : node.children) {
            const auto* arm = std::get_if<bms_case_scope_t>(&program.find(child)->value);
            if (!arm) continue;
            if (arm->value) values.push_back(*arm->value);
            else has_default = true;
        }
        if (has_default) {
            const std::set occupied(values.begin(), values.end());
            std::int64_t fallback{};
            while (occupied.contains(fallback)) ++fallback;
            values.push_back(fallback);
        }
    }
    return values;
}

auto evaluation_candidates(const bms_program_document_t& owner, std::size_t maximum)
    -> result_t<std::vector<bms_program_evaluation_t>, error_t<failure_code_t>> {
    std::vector<bms_program_evaluation_t> candidates(1);
    if (!maximum) return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
        "Scoped publication needs an evaluation budget."});
    for (const auto& node : owner.prepared().program.nodes()) {
        auto values = selector_values(owner.prepared().program, node, maximum);
        if (!values) return std::unexpected(values.error());
        if (values->empty()) continue;
        if (values->size() > maximum / candidates.size())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
                "Scoped edit exceeds its bounded evaluation budget."});
        std::vector<bms_program_evaluation_t> expanded;
        for (const auto& candidate : candidates) {
            for (const auto value : *values) {
                auto evaluation = candidate;
                evaluation.selections.push_back({node.id, value});
                expanded.push_back(std::move(evaluation));
            }
        }
        candidates = std::move(expanded);
    }
    return candidates;
}

// Reuse the evaluator's reachability checks instead of reimplementing IF/SWITCH
// interpretation. Content edits cannot change control flow or selector settings.
auto reachable_evaluation(const bms_program_document_t& owner, bms_program_evaluation_t evaluation)
    -> bms_program_evaluation_t {
    const auto projection = bms_program_projection_t::from(owner.prepared(), evaluation);
    for (const auto& diagnostic : projection.diagnostics) {
        if (diagnostic.code != failure_code_t::unavailable) continue;
        std::erase_if(evaluation.selections,
            [&](const auto& selection) { return selection.branch == diagnostic.node; });
    }
    return evaluation;
}

auto validate_evaluation(const bms_program_document_t* previous,
    const bms_program_document_t& owner, bms_program_evaluation_t evaluation) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    const auto projection = bms_program_projection_t::from(owner.prepared(), evaluation);
    if (!projection.value) {
        for (const auto& diagnostic : projection.diagnostics)
            diagnostics.push_back(error(diagnostic_code_t::invalid_binding, diagnostic.message, diagnostic.item));
        return diagnostics;
    }
    const auto old = previous ? bms_program_projection_t::from(previous->prepared(), evaluation) :
        attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>{};
    const auto* before = old.value ? &*old.value : nullptr;
    if (const auto collision = new_note_collision(*projection.value, before))
        diagnostics.push_back(error(diagnostic_code_t::collision,
            "Scoped edit would overlap playable notes in an affected evaluation.", *collision));
    for (const auto& diagnostic : projection.diagnostics) {
        if (diagnostic.code != failure_code_t::not_found || diagnostic.is_error()) continue;
        if (has_existing_missing_reference(previous, owner, diagnostic, old.diagnostics)) continue;
        diagnostics.push_back(error(diagnostic_code_t::dangling_resource,
            "Scoped edit would introduce an unresolved resource or timing reference.", diagnostic.item));
    }
    auto validation = validate_document(projection.value->song());
    diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    return diagnostics;
}
} // namespace

auto bms_program_document_t::build_projection() const
    -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t> {
    return bms_program_projection_t::from(prepared_, evaluation_);
}

auto bms_program_document_t::from(chart_id_t difficulty, bms_prepared_program_t prepared,
    bms_program_evaluation_t evaluation)
    -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t> {
    if (!difficulty) return {.diagnostics = {{.code = failure_code_t::invalid_request,
        .message = "Prepared difficulty requires a stable identity."}}};
    auto candidate = std::shared_ptr<const bms_program_document_t>(
        new bms_program_document_t(difficulty, std::move(prepared), std::move(evaluation)));
    auto projection = candidate->build_projection();
    if (!projection.value || !projection.ok()) return {.diagnostics = std::move(projection.diagnostics)};
    return {.value = std::move(candidate), .diagnostics = std::move(projection.diagnostics)};
}

auto bms_program_edit_target_t::from(const song_document_t& document,
    chart_id_t difficulty, std::optional<branch_id_t> selected)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    return resolve_target(document, difficulty, selected);
}

auto bms_program_edit_target_t::from(const song_document_t& document,
    chart_id_t difficulty, object_id_t existing_item)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    const auto* owner = find_program(document, difficulty);
    if (!existing_item || !owner)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
            "Existing scoped item or difficulty is unavailable."});
    if (cross_owner_endpoints(*owner).contains(existing_item))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict,
            "Existing hold spans multiple semantic owners."});
    for (const auto& node : owner->prepared().program.nodes()) {
        const auto* content = std::get_if<bms_chart_content_t>(&node.value);
        if (!content) continue;
        if (std::ranges::find(content->items, existing_item, &bms_content_item_t::id) != content->items.end())
            return resolve_target(document, difficulty, node.id);
    }
    return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
        "Existing scoped item no longer exists."});
}

auto bms_program_edit_target_t::validate(const song_document_t& document) const
    -> result_t<const bms_program_document_t*, error_t<failure_code_t>> {
    if (!stamp.is_current(document))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::stale_generation,
            stale_action_message("Scoped edit")});
    const auto current = resolve_target(document, difficulty, chart);
    if (!current) return std::unexpected(current.error());
    if (current->scope != scope)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::stale_generation,
            "Chart no longer belongs to the captured scope."});
    return find_program(document, difficulty);
}

auto document_command_transaction_t::next_bms_program_item_id(std::size_t count) const
    -> std::optional<object_id_t> {
    auto maximum = maximum_program_id(current_document());
    for (const auto& record : history_.undo) {
        maximum = std::max({maximum, maximum_program_id(record.before), maximum_program_id(record.after)});
    }
    for (const auto& record : history_.redo) {
        maximum = std::max({maximum, maximum_program_id(record.before), maximum_program_id(record.after)});
    }
    if (!count || count > std::numeric_limits<std::uint64_t>::max() - maximum) return std::nullopt;
    return object_id_t{maximum + 1};
}

auto document_command_transaction_t::stage(const bms_program_content_edit_batch_t& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto owner = edit.target.validate(document);
        if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, owner.error().message)}};
        const auto& source = (*owner)->prepared();
        auto nodes = std::vector<bms_program_node_t>(source.program.nodes().begin(), source.program.nodes().end());
        auto node = std::ranges::find(nodes, edit.target.chart, &bms_program_node_t::id);
        auto& items = std::get<bms_chart_content_t>(node->value).items;
        const auto cross_owner = cross_owner_endpoints(**owner);
        const auto next_id = next_bms_program_item_id();
        const auto maximum = next_id ? next_id->value - 1 : std::numeric_limits<std::uint64_t>::max();
        content_editor_t editor{.target = edit.target, .items = items,
            .cross_owner = cross_owner, .reserved_through = maximum};
        for (const auto& operation : edit.edits) std::visit(editor, operation);
        if (!editor.diagnostics.empty()) return {.diagnostics = std::move(editor.diagnostics)};
        if (!editor.changed) return {.value = document_edit_effect_t{}};
        auto program = bms_program_t::from(std::move(nodes));
        if (!program) return {.diagnostics = {error(diagnostic_code_t::invalid_id, program.error().message)}};
        if (*program == (*owner)->prepared().program) return {.value = document_edit_effect_t{}};
        auto prepared = bms_prepared_program_t{.program = std::move(*program),
            .measures = source.measures, .lane_profile = source.lane_profile,
            .bgm_columns = source.bgm_columns, .resources = source.resources,
            .audio_assets = source.audio_assets, .visual_assets = source.visual_assets};
        auto proposed = std::shared_ptr<const bms_program_document_t>(new bms_program_document_t(
            (*owner)->difficulty(), std::move(prepared), (*owner)->evaluation()));
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& value) { return value.get() == *owner; });
        *destination = std::move(proposed);
        return {.value = document_edit_effect_t{.changed = true, .command_label = edit.label}};
    });
}

auto validate_prepared_bms_programs(const song_document_t& document) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    std::set<chart_id_t> difficulties;
    for (const auto& owner : document.prepared_bms_programs) {
        if (!owner || !owner->difficulty() || !difficulties.insert(owner->difficulty()).second ||
            document.find_chart(owner->difficulty())) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                "Prepared difficulty must have one semantic owner and no writable chart mirror."));
            continue;
        }
        const auto projection = owner->build_projection();
        for (const auto& diagnostic : projection.diagnostics)
            diagnostics.push_back({.severity = diagnostic.severity,
                .code = diagnostic_code_t::invalid_binding, .message = diagnostic.message,
                .object = diagnostic.item ? std::optional{diagnostic.item} : std::nullopt});
        if (!projection.value) {
            if (projection.diagnostics.empty()) diagnostics.push_back(error(
                diagnostic_code_t::invalid_binding, "Prepared scoped projection failed."));
            continue;
        }
        auto validation = validate_document(projection.value->song());
        diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    }
    return diagnostics;
}

auto validate_bms_program_publication(const song_document_t& before,
    const song_document_t& proposed, std::size_t maximum_evaluations) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (const auto& owner : proposed.prepared_bms_programs) {
        if (!owner) continue; // The shared document validator rejects malformed ownership.
        const auto* previous = find_program(before, owner->difficulty());
        if (previous == owner.get()) continue;
        const auto evaluations = evaluation_candidates(*owner, maximum_evaluations);
        if (!evaluations) {
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, evaluations.error().message));
            continue;
        }
        for (const auto& evaluation : *evaluations) {
            auto validation = validate_evaluation(previous, *owner, reachable_evaluation(*owner, evaluation));
            diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
            if (diagnostic_queries::has_errors(diagnostics)) return diagnostics;
        }
    }
    return diagnostics;
}
} // namespace wormedit
