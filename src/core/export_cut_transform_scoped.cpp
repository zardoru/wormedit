#include "export_cut_transform_internal.h"
#include "id_ceiling.h"
#include "bms_native_content_internal.h"
#include <tuple>
#include <algorithm>
#include <map>
#include <set>

namespace wormedit::export_cut_detail {
namespace {
using item_index_t = std::map<object_id_t, bms_content_item_t>;
using item_updates_t = std::map<object_id_t, std::optional<bms_content_item_t>>;

auto content_index(const bms_program_t& program) -> item_index_t {
    item_index_t result;
    for (const auto& node : program.nodes())
        if (const auto* chart = std::get_if<bms_chart_content_t>(&node.value))
            for (const auto& item : chart->items) result.emplace(item.id, item);
    return result;
}
auto normalized_content(bms_content_value_t value) -> bms_content_value_t {
    if (auto* native = std::get_if<bms_native_content_t>(&value)) {
        auto event = native->payload();
        clear_native_source_provenance(event);
        const auto normalized = bms_native_content_t::from(std::move(event),
            {native->positions().begin(), native->positions().end()}, native->resource_reference());
        if (normalized) value = *normalized;
    }
    return value;
}
auto geometry_of(const chart_document_t& chart) -> std::vector<std::pair<measure_id_t, measure_definition_t>> {
    std::vector<std::pair<measure_id_t, measure_definition_t>> result;
    for (const auto& measure : chart.measures) result.emplace_back(measure.id, measure.definition);
    return result;
}
auto active_items(const bms_program_t& program, std::span<const branch_id_t> active)
    -> std::vector<object_id_t> {
    const std::set<branch_id_t> charts(active.begin(), active.end());
    std::vector<object_id_t> result;
    for (const auto& node : program.nodes()) {
        if (!charts.contains(node.id)) continue;
        if (const auto* chart = std::get_if<bms_chart_content_t>(&node.value))
            for (const auto& item : chart->items)
                if (!std::holds_alternative<bms_measure_length_t>(item.value)) result.push_back(item.id);
    }
    return result;
}
auto same_clock(const timing_index_t& a, const timing_index_t& b) -> bool {
    return std::ranges::equal(a.rows, b.rows, [](const auto& left, const auto& right) {
        return std::tie(left.absolute_row, left.before_delays_seconds, left.event_seconds, left.after_stops_seconds) ==
            std::tie(right.absolute_row, right.before_delays_seconds, right.event_seconds, right.after_stops_seconds);
    });
}
auto same_curves(const chart_document_t& a, const chart_document_t& b,
                 const timing_index_t& first, const timing_index_t& second) -> bool {
    const auto left = scroll_speed_index_t::from(a, first);
    const auto right = scroll_speed_index_t::from(b, second);
    if (!left.value || !right.value) return false;
    const auto scrolls = std::ranges::equal(left.value->scroll_segments, right.value->scroll_segments,
        [](const auto& x, const auto& y) {
            return std::tie(x.begin_row, x.end_row, x.multiplier) == std::tie(y.begin_row, y.end_row, y.multiplier);
        });
    return scrolls && std::ranges::equal(left.value->speed_segments, right.value->speed_segments,
        [](const auto& x, const auto& y) {
            return std::tie(x.begin_row, x.end_row, x.begin_seconds, x.initial_multiplier, x.target_multiplier,
                x.transition_duration, x.transition_is_seconds) ==
                std::tie(y.begin_row, y.end_row, y.begin_seconds, y.initial_multiplier, y.target_multiplier,
                y.transition_duration, y.transition_is_seconds);
        });
}
auto same_semantics(const chart_document_t& a, const chart_document_t& b) -> bool {
    if (geometry_of(a) != geometry_of(b) || a.divisions_per_standard_measure != b.divisions_per_standard_measure ||
        a.beat_zero_audio_offset_seconds != b.beat_zero_audio_offset_seconds || a.lane_layouts.size() != b.lane_layouts.size()) return false;
    const auto first = timing_index_t::from(a);
    const auto second = timing_index_t::from(b);
    if (!first.value || !second.value || !same_clock(*first.value, *second.value) ||
        !same_curves(a, b, *first.value, *second.value)) return false;
    const auto left = normalized_native_event_values(a);
    const auto right = normalized_native_event_values(b);
    return left && right && *left == *right;
}
struct scoped_cut_t {
    const bms_program_document_t& source;
    transform_context_t& context;
    item_index_t original;
    item_updates_t updates;
    std::optional<bms_prepared_program_t> geometry;
    std::optional<std::vector<std::pair<measure_id_t, measure_definition_t>>> measures;
    std::optional<std::vector<bms_content_value_t>> additions;
    std::set<branch_id_t> observed_charts;

    auto reject(std::string message, std::optional<object_id_t> object = {}) -> bool {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
            std::move(message), object);
        return false;
    }
    auto record_geometry(const bms_prepared_program_t& candidate, const chart_document_t& after) -> bool {
        const auto next = geometry_of(after);
        if (geometry && (*measures != next ||
            geometry->settings.divisions_per_standard_measure != candidate.settings.divisions_per_standard_measure ||
            geometry->lane_profile != candidate.lane_profile))
            return reject("Conditional timing or measure layouts map an export cut to incompatible shared geometry.");
        if (!geometry) { geometry = candidate; measures = next; }
        return true;
    }
    auto record_items(const item_index_t& candidate, std::span<const branch_id_t> active) -> bool {
        for (const auto id : active_items(source.prepared().program, active)) {
            const auto found = candidate.find(id);
            auto value = found == candidate.end() ? std::optional<bms_content_item_t>{} : found->second;
            if (value) value->value = normalized_content(std::move(value->value));
            const auto [previous, inserted] = updates.emplace(id, value);
            if (!inserted && previous->second != value)
                return reject("Conditional timing maps this shared authored object differently across export outcomes.", id);
        }
        return true;
    }
    auto record_additions(const bms_prepared_program_t& candidate) -> bool {
        std::vector<bms_content_value_t> values;
        for (const auto& node : candidate.program.nodes()) {
            const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            for (const auto& item : chart->items) {
                if (original.contains(item.id) || std::holds_alternative<bms_measure_length_t>(item.value)) continue;
                values.push_back(normalized_content(item.value));
            }
        }
        if (additions && *additions != values)
            return reject("Conditional timing requires different generated carry events across export outcomes.");
        if (!additions) additions = std::move(values);
        return true;
    }
    auto project(const bms_program_projection_t& projection, const bms_program_document_t& evaluated_owner) -> bool {
        const auto charts = projection.song().read_charts();
        if (charts.size() != 1) return reject("Scoped export cuts require one complete projected difficulty.");
        const auto& before = *charts.front();
        if (!context.reserve_work(id_ceiling_t<object_id_t>::from(before).count)) return false;
        transform_context_t local{.source = projection.song(), .timeline = context.timeline,
            .sources = context.sources, .diagnostics = context.diagnostics, .chart = context.chart,
            .object_ceiling = context.object_ceiling, .generated_objects = context.generated_objects,
            .work_units = context.work_units};
        auto after = transform_chart(before, local);
        context.object_ceiling = local.object_ceiling;
        context.generated_objects = local.generated_objects;
        context.work_units = local.work_units;
        if (!after) return false;
        if (after->lane_layouts.size() > 1)
            return reject("This scoped export cut needs multiple native lane layouts that cannot share one BMS profile.");
        const auto first = context.next_object();
        if (!first) return reject("Scoped export-cut identities exceed the bounded object capacity.");
        auto rebased = evaluated_owner.build_rebased_program(before, *after, *first);
        if (!rebased) {
            for (const auto& diagnostic : rebased.error())
                context.report(diagnostic.severity, diagnostic.code, diagnostic.message, diagnostic.object);
            return false;
        }
        context.object_ceiling = std::max(context.object_ceiling, id_ceiling_t<object_id_t>::from(*rebased).maximum);
        if (!record_geometry(*rebased, *after) || !record_items(content_index(rebased->program), projection.active_charts()) ||
            !record_additions(*rebased)) return false;
        observed_charts.insert(projection.active_charts().begin(), projection.active_charts().end());
        return true;
    }
    auto evaluate(const bms_program_evaluation_t& evaluation, std::optional<branch_id_t> authoring = {}) -> bool {
        auto projection = authoring ? bms_program_projection_t::for_authoring(source.prepared(), *authoring, evaluation)
            : bms_program_projection_t::from(source.prepared(), evaluation);
        if (!projection.value || !projection.ok())
            return reject("An authored scoped outcome has no complete exact timing view for export cuts.");
        const auto evaluated = bms_program_document_t::from(source.difficulty(), source.prepared(), evaluation);
        if (!evaluated.value || !evaluated.ok()) return reject("Scoped export choice cannot bind its authored resource catalog.");
        return project(*projection.value, **evaluated.value);
    }
    auto collect() -> bool {
        const auto evaluations = source.bounded_evaluations(256);
        if (!evaluations) return reject(evaluations.error().message);
        for (const auto& evaluation : *evaluations) if (!evaluate(evaluation)) return false;
        // Dead or authoring-only Charts still own content; never leave their source clocks untouched.
        for (const auto& node : source.prepared().program.nodes()) {
            if (!std::holds_alternative<bms_chart_content_t>(node.value) || observed_charts.contains(node.id)) continue;
            if (!evaluate(source.evaluation(), node.id)) return false;
        }
        return true;
    }
    auto update_chart(bms_chart_content_t& chart) -> bool {
        std::vector<bms_content_item_t> items;
        for (auto item : chart.items) {
            if (std::holds_alternative<bms_measure_length_t>(item.value)) continue;
            const auto found = updates.find(item.id);
            if (found != updates.end()) {
                if (!found->second) continue;
                item = *found->second;
            }
            if (auto* metadata = std::get_if<bms_metadata_declaration_t>(&item.value);
                metadata && metadata->field == bms_metadata_field_t::offset) {
                const auto* value = std::get_if<signed_rational_t>(&metadata->value);
                const auto mapped = value ? context.timeline.collapsed_seconds(*value) : std::nullopt;
                if (!mapped) return reject("The scoped OFFSET cannot be rebased exactly.", item.id);
                metadata->value = *mapped;
            }
            items.push_back(std::move(item));
        }
        chart.items = std::move(items);
        return true;
    }
    auto append_root(bms_chart_content_t& root) -> bool {
        auto retained = std::move(root.items);
        root.items.clear();
        for (const auto& [measure, definition] : *measures) {
            const auto id = context.next_object();
            if (!id) return reject("Scoped export-cut geometry exceeds the object capacity.");
            root.items.push_back({.id = *id, .value = bms_measure_length_t{
                .measure = measure, .length = definition.length, .meter = definition.meter}});
        }
        for (const auto& value : *additions) {
            const auto id = context.next_object();
            if (!id) return reject("Scoped export-cut carry events exceed the object capacity.");
            root.items.push_back({.id = *id, .value = value});
        }
        root.items.insert(root.items.end(), std::make_move_iterator(retained.begin()), std::make_move_iterator(retained.end()));
        return true;
    }
    auto verify_evaluation(const bms_prepared_program_t& merged, const bms_program_evaluation_t& evaluation,
                           std::optional<branch_id_t> authoring = {}) -> bool {
        const auto before = authoring ? bms_program_projection_t::for_authoring(source.prepared(), *authoring, evaluation)
            : bms_program_projection_t::from(source.prepared(), evaluation);
        const auto after = authoring ? bms_program_projection_t::for_authoring(merged, *authoring, evaluation)
            : bms_program_projection_t::from(merged, evaluation);
        if (!before.value || !before.ok() || !after.value || !after.ok())
            return reject("The merged scoped export is invalid in an authored outcome.");
        const auto originals = before.value->song().read_charts();
        const auto outputs = after.value->song().read_charts();
        if (originals.size() != 1 || outputs.size() != 1) return reject("Scoped export outcome has no unique difficulty.");
        if (!context.reserve_work(id_ceiling_t<object_id_t>::from(*originals.front()).count)) return false;
        transform_context_t local{.source = before.value->song(), .timeline = context.timeline,
            .sources = context.sources, .diagnostics = context.diagnostics, .chart = context.chart,
            .object_ceiling = context.object_ceiling, .generated_objects = context.generated_objects,
            .work_units = context.work_units};
        const auto expected = transform_chart(*originals.front(), local);
        context.object_ceiling = local.object_ceiling;
        context.generated_objects = local.generated_objects;
        context.work_units = local.work_units;
        return expected && (same_semantics(*expected, *outputs.front()) ||
            reject("The merged scoped export cannot preserve this outcome's exact objects and timing."));
    }
    auto verify(const bms_prepared_program_t& merged) -> bool {
        const auto evaluations = source.bounded_evaluations(256);
        if (!evaluations) return reject(evaluations.error().message);
        for (const auto& evaluation : *evaluations) if (!verify_evaluation(merged, evaluation)) return false;
        std::set<branch_id_t> reachable;
        for (const auto& evaluation : *evaluations) {
            const auto projection = bms_program_projection_t::from(source.prepared(), evaluation);
            if (!projection.value || !projection.ok()) return false;
            reachable.insert(projection.value->active_charts().begin(), projection.value->active_charts().end());
        }
        for (const auto& node : source.prepared().program.nodes()) {
            if (!std::holds_alternative<bms_chart_content_t>(node.value) || reachable.contains(node.id)) continue;
            if (!verify_evaluation(merged, source.evaluation(), node.id)) return false;
        }
        return true;
    }
    auto finish() -> std::shared_ptr<const bms_program_document_t> {
        if (!geometry || !measures || !additions) return {};
        std::vector<bms_program_node_t> nodes(source.prepared().program.nodes().begin(), source.prepared().program.nodes().end());
        const auto root = source.prepared().program.target_chart(source.prepared().program.root());
        if (!root) { reject("Scoped export requires an explicit Root Chart for shared geometry."); return {}; }
        for (auto& node : nodes) {
            auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            if (!update_chart(*chart)) return {};
            if (node.id == *root && !append_root(*chart)) return {};
        }
        const auto program = bms_program_t::from(std::move(nodes));
        if (!program) { reject(program.error().message); return {}; }
        auto prepared = *geometry;
        prepared.program = *program;
        prepared.settings.timing_links = {};
        prepared.source.original_bytes.reset();
        prepared.source.unchanged_since_import = false;
        if (!verify(prepared)) return {};
        auto result = bms_program_document_t::from(source.difficulty(), std::move(prepared), source.evaluation());
        if (!result.value || !result.ok()) { reject("Rebased scoped export content fails its exact semantic validation."); return {}; }
        return *result.value;
    }
};
}
auto transform_scoped(const bms_program_document_t& source, transform_context_t& context)
    -> std::shared_ptr<const bms_program_document_t> {
    if (!context.object_ceiling && !context.next_object()) return {};
    scoped_cut_t cut{.source = source, .context = context, .original = content_index(source.prepared().program)};
    return cut.collect() ? cut.finish() : nullptr;
}
} // namespace wormedit::export_cut_detail
