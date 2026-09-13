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
namespace {

auto error(diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto normalize(rational_t value) -> rational_t {
    const auto divisor = std::gcd(value.numerator, value.denominator);
    return {value.numerator / divisor, value.denominator / divisor};
}

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
    return normalize({absolute_row, divisions});
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
    const auto found = std::ranges::find(positioned->layout->lanes, lane, &lane_definition_t::id);
    if (found == positioned->layout->lanes.end()) {
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

} // namespace

auto chart_compare_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto compare_chart_difficulties(const song_document_t& song, const chart_compare_request_t& request)
    -> chart_compare_result_t {
    chart_compare_result_t result{.document_revision = song.revision, .charts = request.charts};
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

    std::unordered_set<std::uint64_t> selected_ids;
    selected_ids.reserve(request.charts.size());
    std::vector<chart_projection_t> projections;
    projections.reserve(request.charts.size());
    std::vector<rational_t> candidates;
    std::size_t endpoint_count{};
    std::size_t semantic_layer_assignment_count{};
    std::size_t signature_text_bytes{};
    std::size_t layout_count{};
    std::size_t lane_count{};

    for (const auto chart_id : request.charts) {
        if (!chart_id || !selected_ids.insert(chart_id.value).second) {
            result.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                               "chart comparison IDs must be present and unique"));
            return result;
        }
        const auto chart = std::ranges::find(song.charts, chart_id, &chart_document_t::id);
        if (chart == song.charts.end()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id,
                      "chart comparison refers to a chart outside the document"));
            return result;
        }
        if (chart->divisions_per_standard_measure == 0) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "chart comparison cannot use a chart with zero resolution"));
            return result;
        }
        const auto built = build_measure_index(*chart);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) {
            result.diagnostics.insert(result.diagnostics.end(), diagnostics->begin(),
                                      diagnostics->end());
            return result;
        }
        chart_projection_t projection{.chart = &*chart,
                                      .measures = std::get<measure_index_t>(built)};
        candidates.push_back(musical_position(projection.measures.total_rows,
                                              chart->divisions_per_standard_measure));
        if (chart->lane_layouts.size() > request.maximum_layout_segments - layout_count) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "chart comparison layout count exceeds its configured cap"));
            return result;
        }
        layout_count += chart->lane_layouts.size();
        projection.layouts.reserve(chart->lane_layouts.size());
        for (const auto& segment : chart->lane_layouts) {
            if (segment.layout.lanes.size() > request.maximum_lane_definitions - lane_count) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_resolution,
                          "chart comparison lane definition count exceeds its configured cap",
                          segment.id));
                return result;
            }
            lane_count += segment.layout.lanes.size();
            std::unordered_set<std::uint64_t> lane_ids;
            lane_ids.reserve(segment.layout.lanes.size());
            for (const auto& lane : segment.layout.lanes) {
                if (!lane.id || !lane_ids.insert(lane.id.value).second) {
                    result.diagnostics.push_back(error(
                        diagnostic_code_t::duplicate_id,
                        "chart comparison layout lane IDs must be present and unique", segment.id));
                    return result;
                }
            }
            const auto row = absolute_row(projection.measures, segment.position);
            if (!row) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::row_out_of_range,
                          "chart comparison layout starts outside chart geometry", segment.id));
                return result;
            }
            const auto position = musical_position(*row, chart->divisions_per_standard_measure);
            projection.layouts.push_back(
                {.position = position, .absolute_row = *row, .layout = &segment.layout});
            candidates.push_back(position);
        }
        std::ranges::stable_sort(projection.layouts, [](const auto& left, const auto& right) {
            return compare_fraction(left.position, right.position) < 0;
        });
        for (std::size_t index = 1; index < projection.layouts.size(); ++index) {
            if (compare_fraction(projection.layouts[index - 1].position,
                                 projection.layouts[index].position) == 0) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_measure,
                          "chart comparison layouts cannot share an exact musical position"));
                return result;
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
        projections.push_back(std::move(projection));
    }

    for (auto& projection : projections) {
        for (const auto& measure : projection.chart->measures) {
            for (const auto& note : measure.notes) {
                if (!note.id || note.position.measure != measure.id ||
                    (note.kind != note_kind_t::hold && note.end.has_value())) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "chart comparison note has invalid identity, storage, or span shape",
                              note.id));
                    return result;
                }
                const auto required =
                    note.end.has_value() && note.kind == note_kind_t::hold ? 2U : 1U;
                if (required > request.maximum_input_note_endpoints - endpoint_count) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_resolution,
                              "chart comparison note endpoints exceed their configured cap"));
                    return result;
                }
                endpoint_count += required;
                const auto add_endpoint = [&](local_position_t local,
                                              chart_compare_note_part_t part) -> bool {
                    const auto row = absolute_row(projection.measures, local);
                    if (!row) {
                        result.diagnostics.push_back(error(
                            diagnostic_code_t::row_out_of_range,
                            "chart comparison note endpoint is outside chart geometry", note.id));
                        return false;
                    }
                    const auto position =
                        musical_position(*row, projection.chart->divisions_per_standard_measure);
                    const auto lane = lane_signature_at(projection, position, note.lane);
                    if (!lane) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::unknown_lane,
                                  "chart comparison note has no lane at its endpoint", note.id));
                        return false;
                    }
                    chart_compare_note_signature_t signature{
                        .lane = *lane, .kind = note.kind, .part = part};
                    if (request.include_sound_assignments) {
                        signature.sound = note.sound;
                    }
                    if (request.include_semantic_layers) {
                        if (note.semantic_layers.size() >
                            request.maximum_semantic_layer_assignments -
                                semantic_layer_assignment_count) {
                            result.diagnostics.push_back(error(
                                diagnostic_code_t::invalid_resolution,
                                "chart comparison semantic-layer assignments exceed their cap",
                                note.id));
                            return false;
                        }
                        semantic_layer_assignment_count += note.semantic_layers.size();
                        signature.semantic_layers.reserve(note.semantic_layers.size());
                        for (const auto layer_id : note.semantic_layers) {
                            const auto layer = std::ranges::find(projection.chart->semantic_layers,
                                                                 layer_id, &semantic_layer_t::id);
                            if (layer == projection.chart->semantic_layers.end()) {
                                result.diagnostics.push_back(error(
                                    diagnostic_code_t::invalid_id,
                                    "chart comparison note refers to an unknown semantic layer",
                                    note.id));
                                return false;
                            }
                            if (layer->name.size() >
                                request.maximum_signature_text_bytes - signature_text_bytes) {
                                result.diagnostics.push_back(
                                    error(diagnostic_code_t::invalid_resolution,
                                          "chart comparison signature text exceeds its byte cap",
                                          note.id));
                                return false;
                            }
                            signature_text_bytes += layer->name.size();
                            signature.semantic_layers.push_back(
                                {.name = layer->name, .color = layer->color});
                        }
                        std::ranges::sort(signature.semantic_layers,
                                          [](const auto& left, const auto& right) {
                                              return signature_less(left, right);
                                          });
                    }
                    if (request.include_labels) {
                        const auto label_bytes = note.label.has_value() ? note.label->size() : 0;
                        if (label_bytes >
                            request.maximum_signature_text_bytes - signature_text_bytes) {
                            result.diagnostics.push_back(error(
                                diagnostic_code_t::invalid_resolution,
                                "chart comparison signature text exceeds its byte cap", note.id));
                            return false;
                        }
                        signature_text_bytes += label_bytes;
                        signature.label = note.label;
                    }
                    projection.notes.push_back(
                        {.position = position, .signature = std::move(signature)});
                    candidates.push_back(position);
                    return true;
                };
                if (note.kind == note_kind_t::hold) {
                    const auto begin = absolute_row(projection.measures, note.position);
                    const auto end = note.end ? absolute_row(projection.measures, *note.end)
                                              : std::optional<std::uint64_t>{};
                    if (!begin || !end || *end <= *begin) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::invalid_hold,
                                  "chart comparison hold must have an in-range tail after its head",
                                  note.id));
                        return result;
                    }
                    if (!add_endpoint(note.position, chart_compare_note_part_t::hold_head) ||
                        !add_endpoint(*note.end, chart_compare_note_part_t::hold_tail)) {
                        return result;
                    }
                } else if (!add_endpoint(note.position, chart_compare_note_part_t::point)) {
                    return result;
                }
            }
        }
        std::ranges::sort(projection.notes, [](const auto& left, const auto& right) {
            const auto position_order = compare_fraction(left.position, right.position);
            return position_order != 0 ? position_order < 0
                                       : signature_less(left.signature, right.signature);
        });
    }

    std::ranges::sort(candidates, rational_less_t{});
    candidates.erase(std::ranges::unique(candidates,
                                         [](rational_t left, rational_t right) {
                                             return compare_fraction(left, right) == 0;
                                         })
                         .begin(),
                     candidates.end());
    for (const auto position : candidates) {
        std::vector<pane_signature_t> signatures;
        signatures.reserve(projections.size());
        for (const auto& projection : projections) {
            const auto chart_end = musical_position(
                projection.measures.total_rows, projection.chart->divisions_per_standard_measure);
            pane_signature_t signature{
                .in_range = compare_fraction(position, chart_end) < 0,
            };
            if (request.compare_layouts && signature.in_range) {
                if (const auto* layout = active_layout_at(projection, position);
                    layout != nullptr) {
                    signature.layout = layout->signature;
                }
            }
            signature.notes = note_signatures_at(projection, position);
            signatures.push_back(std::move(signature));
        }
        const auto all_equal = std::ranges::all_of(signatures, [&](const auto& signature) {
            return equal(signature, signatures.front());
        });
        if (all_equal) {
            continue;
        }
        if (result.differing_rows.size() >= request.maximum_difference_rows) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "chart comparison differences exceed their configured cap"));
            result.differing_rows.clear();
            return result;
        }
        chart_compare_difference_row_t row{.musical_position = position};
        row.panes.reserve(projections.size());
        for (std::size_t index = 0; index < projections.size(); ++index) {
            bool differs{};
            if (request.mode == chart_compare_mode_t::baseline_first) {
                differs = index == 0 ? true : !equal(signatures[index], signatures.front());
            } else {
                differs = std::ranges::any_of(signatures, [&](const auto& other) {
                    return !equal(signatures[index], other);
                });
            }
            chart_compare_pane_row_t pane{
                .chart = projections[index].chart->id,
                .local_position = local_position_at(projections[index], position),
                .active_layout = {signatures[index].layout.begin(), signatures[index].layout.end()},
                .differs = differs,
            };
            pane.notes.reserve(signatures[index].notes.size());
            for (const auto& note : signatures[index].notes) {
                pane.notes.push_back(note.signature);
            }
            row.panes.push_back(std::move(pane));
        }
        result.differing_rows.push_back(std::move(row));
    }
    return result;
}

} // namespace wormedit
