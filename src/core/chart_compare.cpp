#include <wormedit/core/rational.h>
#include <wormedit/core/chart_compare.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <ranges>
#include <span>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace wormedit {
auto chart_compare_difference_row_t::from(rational_t position,
                                          std::vector<chart_compare_pane_row_t> panes)
    -> chart_compare_difference_row_t {
    return {.musical_position = position, .panes = std::move(panes)};
}

namespace {



// Exact comparison through continued-fraction quotients avoids cross-product
// overflow and remains portable to compilers without a 128-bit integer type.
auto compare_fraction(rational_t left, rational_t right) -> int {
    bool reverse{};
    for (;;) {
        const auto left_quotient = left.numerator / left.denominator;
        const auto right_quotient = right.numerator / right.denominator;
        if (left_quotient != right_quotient) {
            const auto comparison = left_quotient < right_quotient ? -1 : 1;
            return reverse ? -comparison : comparison;
        }
        left.numerator %= left.denominator;
        right.numerator %= right.denominator;
        if (left.numerator == 0 || right.numerator == 0) {
            if (left.numerator == right.numerator) {
                return 0;
            }
            const auto comparison = left.numerator == 0 ? -1 : 1;
            return reverse ? -comparison : comparison;
        }
        std::swap(left.numerator, left.denominator);
        std::swap(right.numerator, right.denominator);
        reverse = !reverse;
    }
}

struct rational_less_t {
    auto operator()(rational_t left, rational_t right) const -> bool {
        return compare_fraction(left, right) < 0;
    }
};

auto musical_position(std::uint64_t absolute_row, std::uint64_t divisions) -> rational_t {
    return reduce_fraction({absolute_row, divisions});
}

auto signature_less(const chart_compare_lane_signature_t& left,
                    const chart_compare_lane_signature_t& right) -> bool {
    return std::tie(left.stage, left.role, left.display_order) <
           std::tie(right.stage, right.role, right.display_order);
}

auto signature_less(const chart_compare_layer_signature_t& left,
                    const chart_compare_layer_signature_t& right) -> bool {
    return std::tie(left.name, left.color.red, left.color.green, left.color.blue,
                    left.color.alpha) < std::tie(right.name, right.color.red, right.color.green,
                                                 right.color.blue, right.color.alpha);
}

auto signature_less(const chart_compare_note_signature_t& left,
                    const chart_compare_note_signature_t& right) -> bool {
    const auto left_prefix = std::tie(left.lane.stage, left.lane.role, left.lane.display_order,
                                      left.kind, left.part, left.sound);
    const auto right_prefix = std::tie(right.lane.stage, right.lane.role, right.lane.display_order,
                                       right.kind, right.part, right.sound);
    if (left_prefix != right_prefix) {
        return left_prefix < right_prefix;
    }
    if (left.semantic_layers != right.semantic_layers) {
        return std::ranges::lexicographical_compare(
            left.semantic_layers, right.semantic_layers,
            [](const auto& left_layer, const auto& right_layer) {
                return signature_less(left_layer, right_layer);
            });
    }
    return left.label < right.label;
}

struct positioned_layout_t {
    rational_t position;
    std::uint64_t absolute_row{};
    const lane_layout_t* layout{};
    std::vector<chart_compare_lane_signature_t> signature;
};

struct positioned_note_t {
    rational_t position;
    chart_compare_note_signature_t signature;
};

struct chart_projection_t {
    const chart_document_t* chart{};
    measure_index_t measures;
    std::vector<positioned_layout_t> layouts;
    std::vector<positioned_note_t> notes;
};

auto active_layout_at(const chart_projection_t& projection, rational_t position)
    -> const positioned_layout_t* {
    const auto after = std::ranges::upper_bound(
        projection.layouts, position,
        [](rational_t value, const rational_t& boundary) {
            return compare_fraction(value, boundary) < 0;
        },
        &positioned_layout_t::position);
    return after == projection.layouts.begin() ? nullptr : &*std::prev(after);
}

auto lane_signature_at(const chart_projection_t& projection, rational_t position, lane_id_t lane)
    -> std::optional<chart_compare_lane_signature_t> {
    const auto* positioned = active_layout_at(projection, position);
    if (positioned == nullptr) {
        return std::nullopt;
    }
    const auto found = positioned->layout->find(lane);
    if (found == nullptr) {
        return std::nullopt;
    }
    return chart_compare_lane_signature_t{
        .role = found->role, .stage = found->stage, .display_order = found->display_order};
}

auto note_signatures_at(const chart_projection_t& projection, rational_t position)
    -> std::span<const positioned_note_t> {
    const auto begin = std::ranges::lower_bound(projection.notes, position, rational_less_t{},
                                                &positioned_note_t::position);
    const auto end = std::ranges::upper_bound(begin, projection.notes.end(), position,
                                              rational_less_t{}, &positioned_note_t::position);
    if (begin == end) {
        return {};
    }
    return {&*begin, static_cast<std::size_t>(std::distance(begin, end))};
}

auto local_position_at(const chart_projection_t& projection, rational_t position)
    -> std::optional<local_position_t> {
    if (position.denominator == 0 ||
        projection.chart->divisions_per_standard_measure % position.denominator != 0) {
        return std::nullopt;
    }
    const auto multiplier = projection.chart->divisions_per_standard_measure / position.denominator;
    if (position.numerator != 0 &&
        multiplier > std::numeric_limits<std::uint64_t>::max() / position.numerator) {
        return std::nullopt;
    }
    const auto row = position.numerator * multiplier;
    if (row >= projection.measures.total_rows) {
        return std::nullopt;
    }
    const auto after = std::ranges::upper_bound(projection.measures.entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == projection.measures.entries.begin()) {
        return std::nullopt;
    }
    const auto measure = std::prev(after);
    if (row >= measure->absolute_begin_row + measure->row_capacity) {
        return std::nullopt;
    }
    return local_position_t{.measure = measure->id, .row = row - measure->absolute_begin_row};
}

struct pane_signature_t {
    bool in_range{};
    std::span<const chart_compare_lane_signature_t> layout;
    std::span<const positioned_note_t> notes;
};

auto equal(const pane_signature_t& left, const pane_signature_t& right) -> bool {
    return left.in_range == right.in_range && std::ranges::equal(left.layout, right.layout) &&
           std::ranges::equal(left.notes, right.notes, {}, &positioned_note_t::signature,
                              &positioned_note_t::signature);
}

struct chart_compare_work_t {
    const song_document_t& song;
    const chart_compare_request_t& request;
    chart_compare_result_t& result;
    std::unordered_set<std::uint64_t> selected_ids;
    std::vector<chart_projection_t> projections;
    std::vector<rational_t> candidates;
    std::size_t endpoint_count{};
    std::size_t semantic_layer_assignment_count{};
    std::size_t signature_text_bytes{};
    std::size_t layout_count{};
    std::size_t lane_count{};
};

auto fail_compare(chart_compare_work_t& work, diagnostic_code_t code, std::string message,
                  std::optional<object_id_t> object = std::nullopt) -> bool {
    work.result.diagnostics.push_back(error(code, std::move(message), object));
    return false;
}

auto append_projection_layouts(chart_compare_work_t& work, chart_projection_t& projection)
    -> bool {
    const auto& request = work.request;
    const auto& chart = *projection.chart;
    if (chart.lane_layouts.size() > request.maximum_layout_segments - work.layout_count) {
        return fail_compare(work, diagnostic_code_t::invalid_resolution,
                            "chart comparison layout count exceeds its configured cap");
    }
    work.layout_count += chart.lane_layouts.size();
    projection.layouts.reserve(chart.lane_layouts.size());
    for (const auto& segment : chart.lane_layouts) {
        if (segment.layout.lanes.size() > request.maximum_lane_definitions - work.lane_count) {
            return fail_compare(work, diagnostic_code_t::invalid_resolution,
                                "chart comparison lane definition count exceeds its configured cap",
                                segment.id);
        }
        work.lane_count += segment.layout.lanes.size();
        std::unordered_set<std::uint64_t> lane_ids;
        lane_ids.reserve(segment.layout.lanes.size());
        for (const auto& lane : segment.layout.lanes) {
            if (!lane.id || !lane_ids.insert(lane.id.value).second) {
                return fail_compare(work, diagnostic_code_t::duplicate_id,
                                    "chart comparison layout lane IDs must be present and unique",
                                    segment.id);
            }
        }
        const auto row = absolute_row(projection.measures, segment.position);
        if (!row) {
            return fail_compare(work, diagnostic_code_t::row_out_of_range,
                                "chart comparison layout starts outside chart geometry",
                                segment.id);
        }
        const auto position = musical_position(*row, chart.divisions_per_standard_measure);
        projection.layouts.push_back(
            {.position = position,
             .absolute_row = *row,
             .layout = &segment.layout,
             .signature = {}});
        work.candidates.push_back(position);
    }
    std::ranges::stable_sort(projection.layouts, [](const auto& left, const auto& right) {
        return compare_fraction(left.position, right.position) < 0;
    });
    for (std::size_t index = 1; index < projection.layouts.size(); ++index) {
        if (compare_fraction(projection.layouts[index - 1].position,
                             projection.layouts[index].position) == 0) {
            return fail_compare(work, diagnostic_code_t::invalid_measure,
                                "chart comparison layouts cannot share an exact musical position");
        }
    }
    for (auto& layout : projection.layouts) {
        layout.signature.reserve(layout.layout->lanes.size());
        for (const auto& lane : layout.layout->lanes) {
            layout.signature.push_back(
                {.role = lane.role, .stage = lane.stage, .display_order = lane.display_order});
        }
        std::ranges::sort(layout.signature, [](const auto& left, const auto& right) {
            return signature_less(left, right);
        });
    }
    return true;
}

auto append_chart_projection(chart_compare_work_t& work, chart_id_t chart_id) -> bool {
    if (!chart_id || !work.selected_ids.insert(chart_id.value).second) {
        return fail_compare(work, diagnostic_code_t::duplicate_id,
                            "chart comparison IDs must be present and unique");
    }
    const auto chart = work.song.find_chart(chart_id);
    if (chart == nullptr) {
        return fail_compare(work, diagnostic_code_t::invalid_id,
                            "chart comparison refers to a chart outside the document");
    }
    if (chart->divisions_per_standard_measure == 0) {
        return fail_compare(work, diagnostic_code_t::invalid_resolution,
                            "chart comparison cannot use a chart with zero resolution");
    }
    const auto built = build_measure_index(*chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) {
        work.result.diagnostics.insert(work.result.diagnostics.end(), diagnostics->begin(),
                                       diagnostics->end());
        return false;
    }
    chart_projection_t projection{.chart = &*chart,
                                  .measures = std::get<measure_index_t>(built),
                                  .layouts = {},
                                  .notes = {}};
    work.candidates.push_back(
        musical_position(projection.measures.total_rows, chart->divisions_per_standard_measure));
    if (!append_projection_layouts(work, projection)) {
        return false;
    }
    work.projections.push_back(std::move(projection));
    return true;
}

auto append_semantic_layers(chart_compare_work_t& work, const chart_projection_t& projection,
                            const note_t& note, chart_compare_note_signature_t& signature)
    -> bool {
    if (!work.request.include_semantic_layers) {
        return true;
    }
    if (note.semantic_layers.size() > work.request.maximum_semantic_layer_assignments -
                                          work.semantic_layer_assignment_count) {
        return fail_compare(work, diagnostic_code_t::invalid_resolution,
                            "chart comparison semantic-layer assignments exceed their cap", note.id);
    }
    work.semantic_layer_assignment_count += note.semantic_layers.size();
    signature.semantic_layers.reserve(note.semantic_layers.size());
    for (const auto layer_id : note.semantic_layers) {
        const auto layer = std::ranges::find(projection.chart->semantic_layers, layer_id,
                                             &semantic_layer_t::id);
        if (layer == projection.chart->semantic_layers.end()) {
            return fail_compare(work, diagnostic_code_t::invalid_id,
                                "chart comparison note refers to an unknown semantic layer",
                                note.id);
        }
        if (layer->name.size() > work.request.maximum_signature_text_bytes -
                                     work.signature_text_bytes) {
            return fail_compare(work, diagnostic_code_t::invalid_resolution,
                                "chart comparison signature text exceeds its byte cap", note.id);
        }
        work.signature_text_bytes += layer->name.size();
        signature.semantic_layers.push_back({.name = layer->name, .color = layer->color});
    }
    std::ranges::sort(signature.semantic_layers, [](const auto& left, const auto& right) {
        return signature_less(left, right);
    });
    return true;
}

auto append_note_label(chart_compare_work_t& work, const note_t& note,
                       chart_compare_note_signature_t& signature) -> bool {
    if (!work.request.include_labels) {
        return true;
    }
    const auto bytes = note.label ? note.label->size() : 0;
    if (bytes > work.request.maximum_signature_text_bytes - work.signature_text_bytes) {
        return fail_compare(work, diagnostic_code_t::invalid_resolution,
                            "chart comparison signature text exceeds its byte cap", note.id);
    }
    work.signature_text_bytes += bytes;
    signature.label = note.label;
    return true;
}

auto append_note_endpoint(chart_compare_work_t& work, chart_projection_t& projection,
                          const note_t& note, local_position_t local,
                          chart_compare_note_part_t part) -> bool {
    const auto row = absolute_row(projection.measures, local);
    if (!row) {
        return fail_compare(work, diagnostic_code_t::row_out_of_range,
                            "chart comparison note endpoint is outside chart geometry", note.id);
    }
    const auto position = musical_position(*row,
                                           projection.chart->divisions_per_standard_measure);
    const auto lane = lane_signature_at(projection, position, note.lane);
    if (!lane) {
        return fail_compare(work, diagnostic_code_t::unknown_lane,
                            "chart comparison note has no lane at its endpoint", note.id);
    }
    chart_compare_note_signature_t signature{.lane = *lane,
                                             .kind = note.kind,
                                             .part = part,
                                             .sound = std::nullopt,
                                             .semantic_layers = {},
                                             .label = std::nullopt};
    if (work.request.include_sound_assignments) {
        signature.sound = note.sound;
    }
    if (!append_semantic_layers(work, projection, note, signature) ||
        !append_note_label(work, note, signature)) {
        return false;
    }
    projection.notes.push_back({.position = position, .signature = std::move(signature)});
    work.candidates.push_back(position);
    return true;
}

auto append_note(chart_compare_work_t& work, chart_projection_t& projection,
                 const measure_chunk_t& measure, const note_t& note) -> bool {
    if (!note.id || note.position.measure != measure.id ||
        (note.kind != note_kind_t::hold && note.end.has_value())) {
        return fail_compare(work, diagnostic_code_t::invalid_hold,
                            "chart comparison note has invalid identity, storage, or span shape",
                            note.id);
    }
    const auto required = note.end.has_value() && note.kind == note_kind_t::hold ? 2U : 1U;
    if (required > work.request.maximum_input_note_endpoints - work.endpoint_count) {
        return fail_compare(work, diagnostic_code_t::invalid_resolution,
                            "chart comparison note endpoints exceed their configured cap");
    }
    work.endpoint_count += required;
    if (note.kind != note_kind_t::hold) {
        return append_note_endpoint(work, projection, note, note.position,
                                    chart_compare_note_part_t::point);
    }
    const auto begin = absolute_row(projection.measures, note.position);
    const auto end = note.end ? absolute_row(projection.measures, *note.end)
                              : std::optional<std::uint64_t>{};
    if (!begin || !end || *end <= *begin) {
        return fail_compare(work, diagnostic_code_t::invalid_hold,
                            "chart comparison hold must have an in-range tail after its head",
                            note.id);
    }
    return append_note_endpoint(work, projection, note, note.position,
                                chart_compare_note_part_t::hold_head) &&
           append_note_endpoint(work, projection, note, *note.end,
                                chart_compare_note_part_t::hold_tail);
}

auto build_projection_notes(chart_compare_work_t& work, chart_projection_t& projection) -> bool {
    for (const auto& measure : projection.chart->measures) {
        for (const auto& note : measure.notes) {
            if (!append_note(work, projection, measure, note)) {
                return false;
            }
        }
    }
    std::ranges::sort(projection.notes, [](const auto& left, const auto& right) {
        const auto order = compare_fraction(left.position, right.position);
        return order != 0 ? order < 0 : signature_less(left.signature, right.signature);
    });
    return true;
}

auto pane_signatures_at(const chart_compare_work_t& work, rational_t position)
    -> std::vector<pane_signature_t> {
    std::vector<pane_signature_t> signatures;
    signatures.reserve(work.projections.size());
    for (const auto& projection : work.projections) {
        const auto chart_end = musical_position(projection.measures.total_rows,
                                                projection.chart->divisions_per_standard_measure);
        pane_signature_t signature{.in_range = compare_fraction(position, chart_end) < 0,
                                   .layout = {},
                                   .notes = {}};
        if (work.request.compare_layouts && signature.in_range) {
            if (const auto* layout = active_layout_at(projection, position); layout != nullptr) {
                signature.layout = layout->signature;
            }
        }
        signature.notes = note_signatures_at(projection, position);
        signatures.push_back(std::move(signature));
    }
    return signatures;
}

auto build_difference_panes(const chart_compare_work_t& work, rational_t position,
                         const std::vector<pane_signature_t>& signatures)
    -> std::vector<chart_compare_pane_row_t> {
    std::vector<chart_compare_pane_row_t> panes;
    panes.reserve(work.projections.size());
    for (std::size_t index = 0; index < work.projections.size(); ++index) {
        const auto differs = work.request.mode == chart_compare_mode_t::baseline_first
                                 ? index == 0 || !equal(signatures[index], signatures.front())
                                 : std::ranges::any_of(signatures, [&](const auto& other) {
                                       return !equal(signatures[index], other);
                                   });
        chart_compare_pane_row_t pane{
            .chart = work.projections[index].chart->id,
            .local_position = local_position_at(work.projections[index], position),
            .active_layout = {signatures[index].layout.begin(), signatures[index].layout.end()},
            .notes = {},
            .differs = differs};
        pane.notes.reserve(signatures[index].notes.size());
        for (const auto& note : signatures[index].notes) {
            pane.notes.push_back(note.signature);
        }
        panes.push_back(std::move(pane));
    }
    return panes;
}

[[maybe_unused]] auto make_difference_row(const chart_compare_work_t& work, rational_t position,
                                          const std::vector<pane_signature_t>& signatures)
    -> chart_compare_difference_row_t {
    return chart_compare_difference_row_t::from(position,
                                                build_difference_panes(work, position, signatures));
}

auto append_difference_rows(chart_compare_work_t& work) -> bool {
    std::ranges::sort(work.candidates, rational_less_t{});
    work.candidates.erase(
        std::ranges::unique(work.candidates, [](rational_t left, rational_t right) {
            return compare_fraction(left, right) == 0;
        }).begin(),
        work.candidates.end());
    for (const auto position : work.candidates) {
        const auto signatures = pane_signatures_at(work, position);
        if (std::ranges::all_of(signatures, [&](const auto& item) {
                return equal(item, signatures.front());
            })) {
            continue;
        }
        if (work.result.differing_rows.size() >= work.request.maximum_difference_rows) {
            work.result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "chart comparison differences exceed their configured cap"));
            work.result.differing_rows.clear();
            return false;
        }
        work.result.differing_rows.push_back(chart_compare_difference_row_t::from(position, build_difference_panes(work, position, signatures)));
    }
    return true;
}

} // namespace

auto chart_compare_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto compare_chart_difficulties(const song_document_t& song, const chart_compare_request_t& request)
    -> chart_compare_result_t {
    chart_compare_result_t result{.document_revision = song.revision,
                                  .charts = request.charts,
                                  .differing_rows = {},
                                  .diagnostics = {}};
    if (request.maximum_charts < 2 || request.maximum_input_note_endpoints == 0 ||
        request.maximum_semantic_layer_assignments == 0 ||
        request.maximum_signature_text_bytes == 0 || request.maximum_layout_segments == 0 ||
        request.maximum_lane_definitions == 0 || request.maximum_difference_rows == 0 ||
        request.charts.size() < 2 || request.charts.size() > request.maximum_charts) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "chart comparison requires two or more charts within nonzero configured limits"));
        return result;
    }

    chart_compare_work_t work{.song = song,
                              .request = request,
                              .result = result,
                              .selected_ids = {},
                              .projections = {},
                              .candidates = {},
                              .endpoint_count = 0,
                              .semantic_layer_assignment_count = 0,
                              .signature_text_bytes = 0,
                              .layout_count = 0,
                              .lane_count = 0};
    work.selected_ids.reserve(request.charts.size());
    work.projections.reserve(request.charts.size());
    for (const auto chart_id : request.charts) {
        if (!append_chart_projection(work, chart_id)) {
            return result;
        }
    }
    for (auto& projection : work.projections) {
        if (!build_projection_notes(work, projection)) {
            return result;
        }
    }
    (void)append_difference_rows(work);
    return result;
}

} // namespace wormedit
