#include <wormedit/core/quantize.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {

constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

auto error(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> void {
    diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .object = object,
    });
}

auto checked_multiply(std::uint64_t left, std::uint64_t right, std::uint64_t& result) -> bool {
    if (left != 0 && right > maximum / left) {
        return false;
    }
    result = left * right;
    return true;
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left == 0 || right == 0) {
        return std::nullopt;
    }
    const auto reduced = left / std::gcd(left, right);
    std::uint64_t result{};
    if (!checked_multiply(reduced, right, result)) {
        return std::nullopt;
    }
    return result;
}

auto required_authored_resolution(const chart_document_t& chart, std::uint64_t divisions)
    -> std::optional<std::uint64_t> {
    auto required = chart.divisions_per_standard_measure;
    for (const auto& measure : chart.measures) {
        const auto numerator = measure.definition.length.numerator;
        const auto denominator = measure.definition.length.denominator;
        if (numerator == 0 || denominator == 0) {
            return std::nullopt;
        }

        // Compute (denominator * divisions) / gcd(numerator,
        // denominator * divisions) without forming the potentially overflowing
        // product. After cancelling the denominator, only the divisions term
        // can share another factor with the reduced numerator.
        const auto denominator_common = std::gcd(numerator, denominator);
        const auto reduced_denominator = denominator / denominator_common;
        const auto reduced_numerator = numerator / denominator_common;
        const auto divisions_common = std::gcd(reduced_numerator, divisions);
        const auto reduced_divisions = divisions / divisions_common;
        std::uint64_t factor{};
        if (!checked_multiply(reduced_denominator, reduced_divisions, factor)) {
            return std::nullopt;
        }
        const auto combined = checked_lcm(required, factor);
        if (!combined.has_value()) {
            return std::nullopt;
        }
        required = *combined;
    }
    return required;
}

auto required_resolution(const chart_document_t& chart, const quantize_request_t& request)
    -> std::optional<std::uint64_t> {
    if (request.space == quantization_space_t::standard_measure) {
        return checked_lcm(chart.divisions_per_standard_measure, request.divisions);
    }
    return required_authored_resolution(chart, request.divisions);
}

auto position_for_absolute_row(const measure_index_t& index, std::uint64_t row)
    -> std::optional<local_position_t> {
    if (row >= index.total_rows) {
        return std::nullopt;
    }
    const auto entry =
        std::ranges::find_if(std::views::reverse(index.entries), [row](const auto& candidate) {
            return candidate.absolute_begin_row <= row;
        });
    if (entry == index.entries.rend()) {
        return std::nullopt;
    }
    return local_position_t{
        .measure = entry->id,
        .row = row - entry->absolute_begin_row,
    };
}

auto nearest_multiple(std::uint64_t value, std::uint64_t step, quantization_tie_t tie,
                      diagnostic_list_t& diagnostics, object_id_t object)
    -> std::optional<std::uint64_t> {
    const auto remainder = value % step;
    if (remainder == 0) {
        return value;
    }
    const auto distance_to_upper = step - remainder;
    if (remainder < distance_to_upper) {
        return value - remainder;
    }
    if (remainder > distance_to_upper) {
        if (distance_to_upper > maximum - value) {
            error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "quantized row overflows the supported range", object);
            return std::nullopt;
        }
        return value + distance_to_upper;
    }
    if (tie == quantization_tie_t::reject) {
        error(diagnostics, diagnostic_code_t::collision,
              "object lies exactly halfway between quantization rows", object);
        return std::nullopt;
    }
    if (tie == quantization_tie_t::earlier) {
        return value - remainder;
    }
    if (distance_to_upper > maximum - value) {
        error(diagnostics, diagnostic_code_t::invalid_resolution,
              "quantized row overflows the supported range", object);
        return std::nullopt;
    }
    return value + distance_to_upper;
}

auto quantized_position(const measure_index_t& index, std::uint64_t resolution,
                        local_position_t position, const quantize_request_t& request,
                        diagnostic_list_t& diagnostics, object_id_t object)
    -> std::optional<local_position_t> {
    const auto absolute = absolute_row(index, position);
    if (!absolute.has_value()) {
        error(diagnostics, diagnostic_code_t::row_out_of_range,
              "object position is outside the chart geometry", object);
        return std::nullopt;
    }

    std::uint64_t coordinate{};
    std::uint64_t step{};
    std::uint64_t base{};
    if (request.space == quantization_space_t::standard_measure) {
        coordinate = *absolute;
        step = resolution / request.divisions;
    } else {
        const auto entry = std::ranges::find_if(
            index.entries, [&](const auto& candidate) { return candidate.id == position.measure; });
        if (entry == index.entries.end() || entry->row_capacity % request.divisions != 0) {
            error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "authored measure cannot be divided by the requested grid", object);
            return std::nullopt;
        }
        coordinate = position.row;
        step = entry->row_capacity / request.divisions;
        base = entry->absolute_begin_row;
    }
    if (step == 0) {
        error(diagnostics, diagnostic_code_t::invalid_resolution,
              "quantization grid is finer than the chart resolution", object);
        return std::nullopt;
    }
    const auto snapped = nearest_multiple(coordinate, step, request.tie, diagnostics, object);
    if (!snapped.has_value() || *snapped > maximum - base) {
        return std::nullopt;
    }
    const auto result = position_for_absolute_row(index, base + *snapped);
    if (!result.has_value()) {
        error(diagnostics, diagnostic_code_t::row_out_of_range,
              "quantization would move the object beyond the final measure", object);
    }
    return result;
}

auto note_collision_key(const measure_index_t& index, const note_t& note) -> std::string {
    const auto row = absolute_row(index, note.position).value_or(maximum);
    return std::to_string(row) + ":" + std::to_string(note.lane.value);
}

auto validate_note_intervals_and_collisions(const chart_document_t& before,
                                            const chart_document_t& after,
                                            const measure_index_t& index,
                                            quantization_collision_t collision_policy,
                                            diagnostic_list_t& diagnostics) -> void {
    std::unordered_map<std::uint64_t, std::string> original_keys;
    for (const auto& measure : before.measures) {
        for (const auto& note : measure.notes) {
            original_keys.emplace(note.id.value, note_collision_key(index, note));
        }
    }

    std::unordered_map<std::string, object_id_t> occupied;
    for (const auto& measure : after.measures) {
        for (const auto& note : measure.notes) {
            if (note.end.has_value()) {
                const auto begin = absolute_row(index, note.position);
                const auto end = absolute_row(index, *note.end);
                if (!begin.has_value() || !end.has_value() || *end <= *begin) {
                    error(diagnostics, diagnostic_code_t::invalid_hold,
                          "quantization would reverse or collapse a hold", note.id);
                }
            }
            if (collision_policy == quantization_collision_t::allow) {
                continue;
            }
            const auto key = note_collision_key(index, note);
            const auto [entry, inserted] = occupied.emplace(key, note.id);
            if (!inserted && entry->second != note.id &&
                original_keys[entry->second.value] != original_keys[note.id.value]) {
                error(diagnostics, diagnostic_code_t::collision,
                      "quantization would collide notes in one lane", note.id);
            }
        }
    }
}

auto validate_musical_spans(const chart_document_t& chart, const measure_index_t& index,
                            diagnostic_list_t& diagnostics) -> void {
    const auto validate_span = [&](local_position_t begin_position, local_position_t end_position,
                                   object_id_t object, std::string_view description) {
        const auto begin = absolute_row(index, begin_position);
        const auto end = absolute_row(index, end_position);
        if (!begin.has_value() || !end.has_value() || *end <= *begin) {
            error(diagnostics, diagnostic_code_t::invalid_hold,
                  std::string("quantization would reverse or collapse ") + std::string(description),
                  object);
        }
    };
    for (const auto& warp : chart.warps) {
        validate_span(warp.span.begin, warp.span.end, warp.id, "a warp");
    }
    for (const auto& section : chart.sections) {
        if (section.beat_span.has_value()) {
            validate_span(section.beat_span->begin, section.beat_span->end, section.id,
                          "a beat-anchored section");
        }
    }
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (event.end.has_value()) {
                validate_span(event.position, *event.end, event.id, "a sound-channel hold");
            }
        }
    }
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto event_row = absolute_row(index, position);
    if (!event_row.has_value()) {
        return nullptr;
    }
    const lane_layout_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto segment_row = absolute_row(index, segment.position);
        if (segment_row.has_value() && *segment_row <= *event_row &&
            (selected == nullptr || *segment_row >= selected_row)) {
            selected = &segment.layout;
            selected_row = *segment_row;
        }
    }
    return selected;
}

auto validate_layout_effects(const chart_document_t& chart, const measure_index_t& index,
                             diagnostic_list_t& diagnostics) -> void {
    std::unordered_set<std::uint64_t> layout_rows;
    for (const auto& segment : chart.lane_layouts) {
        const auto row = absolute_row(index, segment.position);
        if (row.has_value() && !layout_rows.insert(*row).second) {
            error(diagnostics, diagnostic_code_t::collision,
                  "quantization would collide lane-layout boundaries", segment.id);
        }
    }

    const auto lane_is_active = [&](local_position_t position, lane_id_t lane) {
        const auto* layout = effective_layout(chart, index, position);
        return layout != nullptr &&
               std::ranges::any_of(layout->lanes,
                                   [lane](const auto& candidate) { return candidate.id == lane; });
    };
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!lane_is_active(note.position, note.lane) ||
                (note.end.has_value() && !lane_is_active(*note.end, note.lane))) {
                error(diagnostics, diagnostic_code_t::unknown_lane,
                      "quantization would move a note outside its active lane layout", note.id);
            }
        }
    }
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (!lane_is_active(event.position, event.lane) ||
                (event.end.has_value() && !lane_is_active(*event.end, event.lane))) {
                error(diagnostics, diagnostic_code_t::unknown_lane,
                      "quantization would move a sound event outside its active lane layout",
                      event.id);
            }
        }
    }
}

auto rehome_measure_objects(chart_document_t& chart, diagnostic_list_t& diagnostics) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> destinations;
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        destinations.emplace(chart.measures[index].id.value, index);
    }

    std::vector<std::vector<note_t>> notes(chart.measures.size());
    std::vector<std::vector<bgm_event_t>> bgm_events(chart.measures.size());
    bool valid = true;
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            const auto destination = destinations.find(note.position.measure.value);
            if (destination == destinations.end()) {
                error(diagnostics, diagnostic_code_t::unknown_measure,
                      "quantized note refers to an unknown measure", note.id);
                valid = false;
            } else {
                notes[destination->second].push_back(std::move(note));
            }
        }
        for (auto& bgm : measure.bgm_events) {
            const auto destination = destinations.find(bgm.position.measure.value);
            if (destination == destinations.end()) {
                error(diagnostics, diagnostic_code_t::unknown_measure,
                      "quantized BGM event refers to an unknown measure", bgm.id);
                valid = false;
            } else {
                bgm_events[destination->second].push_back(std::move(bgm));
            }
        }
    }
    if (!valid) {
        return false;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        chart.measures[index].notes = std::move(notes[index]);
        chart.measures[index].bgm_events = std::move(bgm_events[index]);
    }
    return true;
}

} // namespace

auto quantize_chart_exact(const chart_document_t& chart, const quantize_request_t& request)
    -> quantization_result_t {
    quantization_result_t result;
    if (request.divisions == 0 || chart.divisions_per_standard_measure == 0) {
        error(result.diagnostics, diagnostic_code_t::invalid_resolution,
              "quantization divisions and chart resolution must be positive");
        return result;
    }

    std::unordered_set<std::uint64_t> selected;
    for (const auto object : request.objects) {
        if (!object || !selected.insert(object.value).second) {
            error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "quantization object IDs must be nonzero and unique", object);
        }
    }
    if (!result.diagnostics.empty()) {
        return result;
    }

    const auto target_resolution = required_resolution(chart, request);
    if (!target_resolution.has_value()) {
        error(result.diagnostics, diagnostic_code_t::invalid_resolution,
              "quantization resolution promotion overflows the supported range");
        return result;
    }

    auto working = chart;
    if (*target_resolution != chart.divisions_per_standard_measure) {
        if (!request.allow_resolution_promotion) {
            error(result.diagnostics, diagnostic_code_t::invalid_resolution,
                  "requested grid requires explicit chart-resolution promotion");
            return result;
        }
        auto promotion = redivide_chart_exact(chart, *target_resolution);
        if (!promotion) {
            result.diagnostics = std::move(promotion.diagnostics);
            return result;
        }
        result.promoted_from = chart.divisions_per_standard_measure;
        result.promoted_to = *target_resolution;
        result.resolution_moves = std::move(promotion.moves);
        working = std::move(*promotion.chart);
    }

    const auto built_index = build_measure_index(working);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        result.diagnostics = std::get<diagnostic_list_t>(built_index);
        return result;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto before_quantization = working;
    std::unordered_set<std::uint64_t> matched;
    bool failed = false;
    const auto should_move = [&](object_id_t object) {
        if (selected.empty()) {
            return true;
        }
        if (selected.contains(object.value)) {
            matched.insert(object.value);
            return true;
        }
        return false;
    };
    const auto transform = [&](local_position_t& position, object_id_t object,
                               position_role_t role) {
        if (!should_move(object)) {
            return;
        }
        const auto before = position;
        const auto after = quantized_position(index, working.divisions_per_standard_measure,
                                              position, request, result.diagnostics, object);
        if (!after.has_value()) {
            failed = true;
            return;
        }
        position = *after;
        if (position != before) {
            result.quantized_moves.push_back({
                .object = object,
                .role = role,
                .before = before,
                .after = position,
            });
        }
    };
    const auto transform_positioned = [&](auto& values) {
        for (auto& value : values) {
            transform(value.position, value.id, position_role_t::point);
        }
    };

    for (auto& segment : working.lane_layouts) {
        transform(segment.position, segment.id, position_role_t::point);
    }
    for (auto& measure : working.measures) {
        for (auto& note : measure.notes) {
            transform(note.position, note.id, position_role_t::point);
            if (note.end.has_value() && request.holds == quantization_hold_t::both_endpoints) {
                transform(*note.end, note.id, position_role_t::note_end);
            }
        }
        for (auto& bgm : measure.bgm_events) {
            transform(bgm.position, bgm.id, position_role_t::point);
        }
    }
    transform_positioned(working.bpms);
    transform_positioned(working.pauses);
    transform_positioned(working.scrolls);
    transform_positioned(working.speeds);
    transform_positioned(working.kiai);
    transform_positioned(working.timing_anchors);
    transform_positioned(working.bga_events);
    for (auto& warp : working.warps) {
        transform(warp.span.begin, warp.id, position_role_t::span_begin);
        transform(warp.span.end, warp.id, position_role_t::span_end);
    }
    for (auto& bookmark : working.bookmarks) {
        if (bookmark.position.has_value()) {
            transform(*bookmark.position, bookmark.id, position_role_t::point);
        }
    }
    for (auto& section : working.sections) {
        if (section.beat_span.has_value()) {
            transform(section.beat_span->begin, section.id, position_role_t::span_begin);
            transform(section.beat_span->end, section.id, position_role_t::span_end);
        }
    }
    for (auto& channel : working.sound_channels) {
        for (auto& event : channel.events) {
            transform(event.position, event.id, position_role_t::point);
            if (event.end.has_value() && request.holds == quantization_hold_t::both_endpoints) {
                transform(*event.end, event.id, position_role_t::note_end);
            }
        }
    }
    if (!rehome_measure_objects(working, result.diagnostics)) {
        failed = true;
    }

    for (const auto requested : selected) {
        if (!matched.contains(requested)) {
            error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "quantization request refers to a missing or non-positional object",
                  object_id_t{requested});
            failed = true;
        }
    }
    validate_note_intervals_and_collisions(before_quantization, working, index, request.collisions,
                                           result.diagnostics);
    validate_musical_spans(working, index, result.diagnostics);
    validate_layout_effects(working, index, result.diagnostics);
    if (failed || !result.diagnostics.empty()) {
        return result;
    }
    result.chart = std::move(working);
    return result;
}

} // namespace wormedit
