#include <wormedit/core/document.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {

constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

auto add_checked(std::uint64_t left, std::uint64_t right, std::uint64_t& result) -> bool {
    if (right > maximum - left) {
        return false;
    }
    result = left + right;
    return true;
}

auto add_diagnostic(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message,
                    std::optional<object_id_t> object = std::nullopt) -> void {
    diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .object = object,
    });
}

template <typename id_t>
auto require_id(diagnostic_list_t& diagnostics, id_t id, std::string_view description) -> bool {
    if (id) {
        return true;
    }
    add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                   std::string(description) + " has the reserved zero ID");
    return false;
}

template <typename range_t, typename projection_t>
auto check_unique_ids(diagnostic_list_t& diagnostics, const range_t& values,
                      projection_t projection, std::string_view description) -> void {
    std::unordered_set<std::uint64_t> ids;
    for (const auto& value : values) {
        const auto id = projection(value);
        if (!require_id(diagnostics, id, description)) {
            continue;
        }
        if (!ids.insert(id.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           std::string(description) + " ID is duplicated");
        }
    }
}

auto measure_error_message(measure_error_t error) -> std::string {
    switch (error) {
    case measure_error_t::none:
        return "measure is valid";
    case measure_error_t::empty:
        return "measure length is empty";
    case measure_error_t::invalid_syntax:
        return "measure length syntax is invalid";
    case measure_error_t::non_positive:
        return "measure length is not positive";
    case measure_error_t::zero_denominator:
        return "measure length has a zero denominator";
    case measure_error_t::overflow:
        return "measure geometry overflows the supported range";
    case measure_error_t::zero_resolution:
        return "chart resolution is zero";
    case measure_error_t::non_integral_capacity:
        return "measure length cannot be represented at the chart resolution";
    case measure_error_t::meter_mismatch:
        return "authored meter does not match measure length";
    }
    return "measure is invalid";
}

auto contains_resource(const std::unordered_set<std::uint64_t>& resources, resource_id_t id)
    -> bool {
    return id && resources.contains(id.value);
}

auto position_row_or_diagnostic(const measure_index_t& index, local_position_t position,
                                diagnostic_list_t& diagnostics,
                                std::optional<object_id_t> object = std::nullopt)
    -> std::optional<std::uint64_t> {
    const auto entry = std::ranges::find_if(
        index.entries, [&](const auto& candidate) { return candidate.id == position.measure; });
    if (entry == index.entries.end()) {
        add_diagnostic(diagnostics, diagnostic_code_t::unknown_measure,
                       "object position refers to an unknown measure", object);
        return std::nullopt;
    }
    if (position.row >= entry->row_capacity) {
        add_diagnostic(diagnostics, diagnostic_code_t::row_out_of_range,
                       "object row lies outside its measure", object);
        return std::nullopt;
    }
    std::uint64_t row{};
    if (!add_checked(entry->absolute_begin_row, position.row, row)) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                       "absolute object row overflows the supported range", object);
        return std::nullopt;
    }
    return row;
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto* selected = static_cast<const lane_layout_t*>(nullptr);
    auto selected_row = std::uint64_t{};
    const auto event_row = absolute_row(index, position);
    if (!event_row.has_value()) {
        return nullptr;
    }
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

auto lane_is_active(const chart_document_t& chart, const measure_index_t& index,
                    local_position_t position, lane_id_t lane) -> bool {
    const auto* layout = effective_layout(chart, index, position);
    return layout != nullptr && std::ranges::any_of(layout->lanes, [lane](const auto& candidate) {
               return candidate.id == lane;
           });
}

auto combined_layout(const chart_document_t& chart) -> lane_layout_t {
    lane_layout_t result;
    std::unordered_set<std::uint64_t> inserted;
    for (const auto& segment : chart.lane_layouts) {
        result.split_stage = result.split_stage || segment.layout.split_stage;
        for (const auto& lane : segment.layout.lanes) {
            if (lane.id && inserted.insert(lane.id.value).second) {
                result.lanes.push_back(lane);
            }
        }
    }
    return result;
}

auto validate_object_id(diagnostic_list_t& diagnostics,
                        std::unordered_set<std::uint64_t>& object_ids, object_id_t id,
                        std::string_view description) -> void {
    if (!require_id(diagnostics, id, description)) {
        return;
    }
    if (!object_ids.insert(id.value).second) {
        add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                       std::string(description) + " reuses an object ID", id);
    }
}

auto validate_chart(const chart_document_t& chart,
                    const std::unordered_set<std::uint64_t>& resources,
                    const std::unordered_set<std::uint64_t>& assets, diagnostic_list_t& diagnostics)
    -> void {
    if (chart.divisions_per_standard_measure == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_resolution,
                       "chart divisions per standard measure must be positive");
    }
    if (chart.beat_zero_audio_offset_seconds.denominator == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                       "beat-zero audio offset has a zero denominator");
    }

    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        const auto& index_diagnostics = std::get<diagnostic_list_t>(built_index);
        diagnostics.insert(diagnostics.end(), index_diagnostics.begin(), index_diagnostics.end());
        return;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    std::unordered_set<std::uint64_t> object_ids;

    check_unique_ids(
        diagnostics, chart.semantic_layers, [](const auto& layer) { return layer.id; },
        "semantic layer");
    check_unique_ids(
        diagnostics, chart.bgm_source_lanes, [](const auto& lane) { return lane.id; },
        "BGM source lane");

    std::unordered_set<std::uint64_t> layer_ids;
    for (const auto& layer : chart.semantic_layers) {
        if (layer.id) {
            layer_ids.insert(layer.id.value);
        }
    }
    std::unordered_set<std::uint64_t> bgm_lane_ids;
    for (const auto& lane : chart.bgm_source_lanes) {
        if (lane.id) {
            bgm_lane_ids.insert(lane.id.value);
        }
        validate_object_id(diagnostics, object_ids, lane.id, "BGM source lane");
    }

    std::unordered_set<std::uint64_t> layout_rows;
    for (const auto& segment : chart.lane_layouts) {
        validate_object_id(diagnostics, object_ids, segment.id, "lane layout segment");
        const auto row =
            position_row_or_diagnostic(index, segment.position, diagnostics, segment.id);
        if (row.has_value() && !layout_rows.insert(*row).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           "multiple lane layouts start at the same position", segment.id);
        }
        check_unique_ids(
            diagnostics, segment.layout.lanes, [](const auto& lane) { return lane.id; },
            "layout lane");
    }
    const auto profile_layout = combined_layout(chart);
    const auto profile_diagnostics = validate_input_profile(profile_layout, chart.input_profile);
    diagnostics.insert(diagnostics.end(), profile_diagnostics.begin(), profile_diagnostics.end());

    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            validate_object_id(diagnostics, object_ids, note.id, "note");
            const auto start =
                position_row_or_diagnostic(index, note.position, diagnostics, note.id);
            if (note.position.measure != measure.id) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                               "note is stored outside the measure named by its position", note.id);
            }
            if (!lane_is_active(chart, index, note.position, note.lane)) {
                add_diagnostic(diagnostics, diagnostic_code_t::unknown_lane,
                               "note refers to a lane absent from its active layout", note.id);
            }
            if (note.sound.has_value() && !contains_resource(resources, *note.sound)) {
                add_diagnostic(diagnostics, diagnostic_code_t::dangling_resource,
                               "note refers to an unknown sound resource", note.id);
            }
            for (const auto layer : note.semantic_layers) {
                if (!layer || !layer_ids.contains(layer.value)) {
                    add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                                   "note refers to an unknown semantic layer", note.id);
                }
            }
            const auto requires_end =
                note.kind == note_kind_t::hold || note.kind == note_kind_t::roll;
            if (requires_end != note.end.has_value()) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                               "hold/roll notes require an end and other note kinds prohibit one",
                               note.id);
            } else if (note.end.has_value()) {
                const auto end = position_row_or_diagnostic(index, *note.end, diagnostics, note.id);
                if (start.has_value() && end.has_value() && *end <= *start) {
                    add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                                   "hold/roll end must be after its start", note.id);
                }
                if (!lane_is_active(chart, index, *note.end, note.lane)) {
                    add_diagnostic(diagnostics, diagnostic_code_t::unknown_lane,
                                   "hold/roll lane is absent from the layout at its end", note.id);
                }
            }
        }
        for (const auto& bgm : measure.bgm_events) {
            validate_object_id(diagnostics, object_ids, bgm.id, "BGM event");
            position_row_or_diagnostic(index, bgm.position, diagnostics, bgm.id);
            if (bgm.position.measure != measure.id) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                               "BGM event is stored outside the measure named by its position",
                               bgm.id);
            }
            if (!bgm_lane_ids.contains(bgm.source_lane.value)) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                               "BGM event refers to an unknown source lane", bgm.id);
            }
            if (!contains_resource(resources, bgm.sound)) {
                add_diagnostic(diagnostics, diagnostic_code_t::dangling_resource,
                               "BGM event refers to an unknown sound resource", bgm.id);
            }
        }
    }

    const auto validate_positioned = [&](const auto& values, std::string_view description) {
        for (const auto& value : values) {
            validate_object_id(diagnostics, object_ids, value.id, description);
            position_row_or_diagnostic(index, value.position, diagnostics, value.id);
        }
    };
    validate_positioned(chart.bpms, "BPM event");
    validate_positioned(chart.pauses, "pause event");
    validate_positioned(chart.scrolls, "scroll event");
    validate_positioned(chart.speeds, "speed event");
    validate_positioned(chart.kiai, "kiai event");
    validate_positioned(chart.timing_anchors, "timing anchor");
    validate_positioned(chart.bga_events, "BGA event");

    for (const auto& bpm : chart.bpms) {
        if (bpm.beats_per_minute.denominator == 0 || bpm.beats_per_minute.numerator <= 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                           "BPM must be a positive rational", bpm.id);
        }
    }
    for (const auto& pause : chart.pauses) {
        if (pause.duration.denominator == 0 || pause.duration.numerator < 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                           "pause duration must be a non-negative rational", pause.id);
        }
    }
    for (const auto& scroll : chart.scrolls) {
        if (scroll.multiplier.denominator == 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                           "scroll multiplier has a zero denominator", scroll.id);
        }
    }
    for (const auto& speed : chart.speeds) {
        if (speed.multiplier.denominator == 0 || speed.transition_beats.denominator == 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                           "speed event contains an invalid rational", speed.id);
        }
    }
    for (const auto& warp : chart.warps) {
        validate_object_id(diagnostics, object_ids, warp.id, "warp event");
        const auto begin = position_row_or_diagnostic(index, warp.span.begin, diagnostics, warp.id);
        const auto end = position_row_or_diagnostic(index, warp.span.end, diagnostics, warp.id);
        if (begin.has_value() && end.has_value() && *end <= *begin) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                           "warp end must be after its start", warp.id);
        }
    }
    for (const auto& bga : chart.bga_events) {
        if (!contains_resource(resources, bga.resource)) {
            add_diagnostic(diagnostics, diagnostic_code_t::dangling_resource,
                           "BGA event refers to an unknown resource", bga.id);
        }
    }
    for (const auto& bookmark : chart.bookmarks) {
        validate_object_id(diagnostics, object_ids, bookmark.id, "bookmark");
        const auto valid_beat = bookmark.anchor == marker_anchor_t::beat &&
                                bookmark.position.has_value() &&
                                !bookmark.audio_milliseconds.has_value();
        const auto valid_audio = bookmark.anchor == marker_anchor_t::audio_time &&
                                 !bookmark.position.has_value() &&
                                 bookmark.audio_milliseconds.has_value();
        if (!valid_beat && !valid_audio) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "bookmark must carry exactly the value selected by its anchor kind",
                           bookmark.id);
        }
        if (bookmark.position.has_value()) {
            position_row_or_diagnostic(index, *bookmark.position, diagnostics, bookmark.id);
        }
    }
    for (const auto& section : chart.sections) {
        validate_object_id(diagnostics, object_ids, section.id, "section");
        const auto valid_beat = section.anchor == marker_anchor_t::beat &&
                                section.beat_span.has_value() &&
                                !section.audio_millisecond_span.has_value();
        const auto valid_audio = section.anchor == marker_anchor_t::audio_time &&
                                 !section.beat_span.has_value() &&
                                 section.audio_millisecond_span.has_value();
        if (!valid_beat && !valid_audio) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "section must carry exactly the span selected by its anchor kind",
                           section.id);
        }
        if (section.beat_span.has_value()) {
            const auto begin = position_row_or_diagnostic(index, section.beat_span->begin,
                                                          diagnostics, section.id);
            const auto end =
                position_row_or_diagnostic(index, section.beat_span->end, diagnostics, section.id);
            if (begin.has_value() && end.has_value() && *end <= *begin) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                               "beat-anchored section end must be after its start", section.id);
            }
        }
        if (section.audio_millisecond_span.has_value() &&
            section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                           "audio-anchored section end must be after its start", section.id);
        }
    }
    for (const auto& channel : chart.sound_channels) {
        validate_object_id(diagnostics, object_ids, channel.id, "sound channel");
        if (!channel.source_clip.source || !assets.contains(channel.source_clip.source.value)) {
            add_diagnostic(diagnostics, diagnostic_code_t::dangling_asset,
                           "sound channel clip refers to an unknown audio asset", channel.id);
        }
        if (channel.source_clip.end_frame.has_value() &&
            *channel.source_clip.end_frame <= channel.source_clip.begin_frame) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                           "sound channel clip end must be after its start", channel.id);
        }
        for (const auto& event : channel.events) {
            validate_object_id(diagnostics, object_ids, event.id, "sound channel event");
            const auto begin =
                position_row_or_diagnostic(index, event.position, diagnostics, event.id);
            if (!lane_is_active(chart, index, event.position, event.lane)) {
                add_diagnostic(diagnostics, diagnostic_code_t::unknown_lane,
                               "sound channel event refers to a lane absent from its active layout",
                               event.id);
            }
            if (event.end.has_value()) {
                const auto end =
                    position_row_or_diagnostic(index, *event.end, diagnostics, event.id);
                if (begin.has_value() && end.has_value() && *end <= *begin) {
                    add_diagnostic(diagnostics, diagnostic_code_t::invalid_hold,
                                   "sound channel event end must be after its start", event.id);
                }
            }
        }
    }
}

} // namespace

auto conversion_report_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto build_measure_index(const chart_document_t& chart)
    -> std::variant<measure_index_t, diagnostic_list_t> {
    diagnostic_list_t diagnostics;
    measure_index_t index;
    index.entries.reserve(chart.measures.size());
    std::unordered_set<std::uint64_t> measure_ids;

    if (chart.divisions_per_standard_measure == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_resolution,
                       "chart divisions per standard measure must be positive");
    }
    for (const auto& measure : chart.measures) {
        if (!require_id(diagnostics, measure.id, "measure")) {
            continue;
        }
        if (!measure_ids.insert(measure.id.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "measure ID is duplicated");
            continue;
        }
        const auto validation =
            validate_measure(chart.divisions_per_standard_measure, measure.definition);
        if (!validation) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           measure_error_message(validation.error));
            continue;
        }
        index.entries.push_back({
            .id = measure.id,
            .absolute_begin_row = index.total_rows,
            .row_capacity = *validation.row_capacity,
        });
        std::uint64_t total{};
        if (!add_checked(index.total_rows, *validation.row_capacity, total)) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           "measure prefix rows overflow the supported range");
        } else {
            index.total_rows = total;
        }
    }

    if (!diagnostics.empty()) {
        return diagnostics;
    }
    return index;
}

auto absolute_row(const measure_index_t& index, local_position_t position)
    -> std::optional<std::uint64_t> {
    const auto entry = std::ranges::find_if(
        index.entries, [&](const auto& candidate) { return candidate.id == position.measure; });
    if (entry == index.entries.end() || position.row >= entry->row_capacity) {
        return std::nullopt;
    }
    std::uint64_t result{};
    if (!add_checked(entry->absolute_begin_row, position.row, result)) {
        return std::nullopt;
    }
    return result;
}

auto validate_input_profile(const lane_layout_t& layout, const input_profile_t& profile)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (profile.stage_count == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_stage,
                       "input profile stage count must be positive");
    }

    std::unordered_map<std::uint64_t, stage_group_t> lane_stages;
    for (const auto& lane : layout.lanes) {
        if (lane.id) {
            lane_stages.emplace(lane.id.value, lane.stage);
        }
    }
    std::unordered_set<std::uint64_t> bound_lanes;
    std::unordered_set<std::string> active_keys;
    for (const auto& binding : profile.bindings) {
        if (binding.key == physical_key_t::unknown || !binding.lane) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_binding,
                           "input binding must contain a physical key and lane ID");
            continue;
        }
        const auto lane = lane_stages.find(binding.lane.value);
        if (lane == lane_stages.end()) {
            add_diagnostic(diagnostics, diagnostic_code_t::unknown_lane,
                           "input binding refers to a lane absent from the layout");
        }
        if (!bound_lanes.insert(binding.lane.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_binding,
                           "a lane has more than one default physical binding");
        }
        if (profile.requires_focused_stage != binding.focused_stage.has_value()) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_stage,
                           "focused-stage binding does not match the profile focus policy");
            continue;
        }
        if (binding.focused_stage.has_value() && *binding.focused_stage >= profile.stage_count) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_stage,
                           "input binding focused stage is out of range");
            continue;
        }
        if (profile.requires_focused_stage && lane != lane_stages.end()) {
            const auto expected_stage = lane->second == stage_group_t::right ? 1U : 0U;
            if (*binding.focused_stage != expected_stage) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_stage,
                               "input binding lane does not belong to its focused stage");
            }
        }
        const auto stage = binding.focused_stage.value_or(0);
        const auto key =
            std::to_string(static_cast<int>(binding.key)) + ":" + std::to_string(stage);
        if (!active_keys.insert(key).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_binding,
                           "physical key collides within an active input stage");
        }
    }
    return diagnostics;
}

auto validate_document(const song_document_t& song) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    require_id(diagnostics, song.id, "song");
    check_unique_ids(
        diagnostics, song.audio_assets, [](const auto& asset) { return asset.id; }, "audio asset");
    check_unique_ids(
        diagnostics, song.visual_assets, [](const auto& asset) { return asset.id; },
        "visual asset");
    check_unique_ids(
        diagnostics, song.resources, [](const auto& resource) { return resource.id; }, "resource");
    check_unique_ids(diagnostics, song.charts, [](const auto& chart) { return chart.id; }, "chart");
    check_unique_ids(
        diagnostics, song.midi_assets, [](const auto& asset) { return asset.id; }, "MIDI asset");
    check_unique_ids(
        diagnostics, song.generator_recipes, [](const auto& recipe) { return recipe.id; },
        "generator recipe");
    check_unique_ids(
        diagnostics, song.storyboards, [](const auto& storyboard) { return storyboard.id; },
        "storyboard");

    std::unordered_set<std::uint64_t> asset_ids;
    for (const auto& asset : song.audio_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "audio and visual assets share a duplicate ID");
        }
    }
    for (const auto& asset : song.visual_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "audio and visual assets share a duplicate ID");
        }
    }
    for (const auto& asset : song.midi_assets) {
        if (asset.id && !asset_ids.insert(asset.id.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "audio, visual, and MIDI assets share a duplicate ID");
        }
        if (asset.ticks_per_quarter == 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_resolution,
                           "MIDI asset ticks per quarter must be positive");
        }
    }
    std::unordered_set<std::uint64_t> resource_ids;
    for (const auto& resource : song.resources) {
        if (resource.id) {
            resource_ids.insert(resource.id.value);
        }
        if (!resource.asset || !asset_ids.contains(resource.asset.value)) {
            add_diagnostic(diagnostics, diagnostic_code_t::dangling_asset,
                           "resource refers to an unknown asset");
        }
    }
    for (const auto& chart : song.charts) {
        validate_chart(chart, resource_ids, asset_ids, diagnostics);
    }
    for (const auto& recipe : song.generator_recipes) {
        for (const auto asset : recipe.input_assets) {
            if (!asset || !asset_ids.contains(asset.value)) {
                add_diagnostic(diagnostics, diagnostic_code_t::dangling_asset,
                               "generator recipe refers to an unknown input asset");
            }
        }
    }
    std::unordered_set<std::uint64_t> chart_ids;
    for (const auto& chart : song.charts) {
        if (chart.id) {
            chart_ids.insert(chart.id.value);
        }
    }
    for (const auto& group : song.output_groups) {
        for (const auto chart : group.charts) {
            if (!chart || !chart_ids.contains(chart.value)) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                               "output group refers to an unknown chart");
            }
        }
    }
    return diagnostics;
}

auto redivide_chart_exact(const chart_document_t& chart,
                          std::uint64_t target_divisions_per_standard_measure)
    -> redivision_result_t {
    redivision_result_t result;
    if (chart.divisions_per_standard_measure == 0 || target_divisions_per_standard_measure == 0) {
        add_diagnostic(result.diagnostics, diagnostic_code_t::invalid_resolution,
                       "source and target chart resolutions must be positive");
        return result;
    }

    const auto source_index_result = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(source_index_result)) {
        result.diagnostics = std::get<diagnostic_list_t>(source_index_result);
        return result;
    }
    const auto& source_index = std::get<measure_index_t>(source_index_result);

    for (const auto& measure : chart.measures) {
        const auto validation =
            validate_measure(target_divisions_per_standard_measure, measure.definition);
        if (!validation) {
            add_diagnostic(result.diagnostics, diagnostic_code_t::invalid_resolution,
                           "target resolution cannot represent measure " +
                               std::to_string(measure.id.value) + ": " +
                               measure_error_message(validation.error));
        }
    }
    if (!result.diagnostics.empty()) {
        return result;
    }

    auto transformed = chart;
    const auto common =
        std::gcd(chart.divisions_per_standard_measure, target_divisions_per_standard_measure);
    const auto denominator = chart.divisions_per_standard_measure / common;
    const auto numerator = target_divisions_per_standard_measure / common;
    bool failed = false;
    const auto transform_position = [&](local_position_t& position, object_id_t object,
                                        position_role_t role) {
        const auto before = position;
        if (!absolute_row(source_index, position).has_value()) {
            add_diagnostic(result.diagnostics, diagnostic_code_t::row_out_of_range,
                           "object position is outside the source measure geometry",
                           object ? std::optional<object_id_t>{object} : std::nullopt);
            failed = true;
            return;
        }
        if (position.row % denominator != 0) {
            add_diagnostic(result.diagnostics, diagnostic_code_t::row_out_of_range,
                           "object row is not exactly representable at the target resolution",
                           object ? std::optional<object_id_t>{object} : std::nullopt);
            failed = true;
            return;
        }
        const auto reduced_row = position.row / denominator;
        if (numerator != 0 && reduced_row > maximum / numerator) {
            add_diagnostic(result.diagnostics, diagnostic_code_t::invalid_resolution,
                           "object row overflows at the target resolution",
                           object ? std::optional<object_id_t>{object} : std::nullopt);
            failed = true;
            return;
        }
        position.row = reduced_row * numerator;
        if (position != before) {
            result.moves.push_back({
                .object = object,
                .role = role,
                .before = before,
                .after = position,
            });
        }
    };

    for (auto& segment : transformed.lane_layouts) {
        transform_position(segment.position, segment.id, position_role_t::point);
    }
    for (auto& measure : transformed.measures) {
        for (auto& note : measure.notes) {
            transform_position(note.position, note.id, position_role_t::point);
            if (note.end.has_value()) {
                transform_position(*note.end, note.id, position_role_t::note_end);
            }
        }
        for (auto& bgm : measure.bgm_events) {
            transform_position(bgm.position, bgm.id, position_role_t::point);
        }
    }
    const auto transform_positioned = [&](auto& values) {
        for (auto& value : values) {
            transform_position(value.position, value.id, position_role_t::point);
        }
    };
    transform_positioned(transformed.bpms);
    transform_positioned(transformed.pauses);
    transform_positioned(transformed.scrolls);
    transform_positioned(transformed.speeds);
    transform_positioned(transformed.kiai);
    transform_positioned(transformed.timing_anchors);
    transform_positioned(transformed.bga_events);
    for (auto& warp : transformed.warps) {
        transform_position(warp.span.begin, warp.id, position_role_t::span_begin);
        transform_position(warp.span.end, warp.id, position_role_t::span_end);
    }
    for (auto& bookmark : transformed.bookmarks) {
        if (bookmark.position.has_value()) {
            transform_position(*bookmark.position, bookmark.id, position_role_t::point);
        }
    }
    for (auto& section : transformed.sections) {
        if (section.beat_span.has_value()) {
            transform_position(section.beat_span->begin, section.id, position_role_t::span_begin);
            transform_position(section.beat_span->end, section.id, position_role_t::span_end);
        }
    }
    for (auto& channel : transformed.sound_channels) {
        for (auto& event : channel.events) {
            transform_position(event.position, event.id, position_role_t::point);
            if (event.end.has_value()) {
                transform_position(*event.end, event.id, position_role_t::note_end);
            }
        }
    }
    if (failed) {
        result.moves.clear();
        return result;
    }

    transformed.divisions_per_standard_measure = target_divisions_per_standard_measure;
    result.chart = std::move(transformed);
    return result;
}

} // namespace wormedit
