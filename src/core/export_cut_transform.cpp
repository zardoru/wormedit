#include "export_cut_transform_internal.h"
#include "chart_position_storage.h"
#include "chart_object_ids.h"
#include "id_ceiling.h"
#include "checked_math.h"
#include "timing_arithmetic.h"
#include <wormedit/core/rational.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/metadata_inheritance.h>
#include <wormedit/core/export_cut_frames.h>
#include <wormedit/core/storyboard_preview.h>
#include "text_util.h"
#include "filename_reservations.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <map>

namespace wormedit {
namespace export_cut_detail {
using timing_arithmetic::add_exact;
using timing_arithmetic::subtract_exact;

auto less(signed_rational_t a, signed_rational_t b) -> bool {
    return timing_arithmetic::compare(a, b).value_or(0) < 0;
}
auto transform_context_t::reserve_work(std::size_t count) -> bool {
    if (count > default_maximum_chart_objects - std::min(work_units, default_maximum_chart_objects)) {
        report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
            "Export-cut transformation exceeds the bounded chart work capacity.");
        return false;
    }
    work_units += count;
    return true;
}
auto transform_context_t::next_object() -> std::optional<object_id_t> {
    if (generated_objects >= default_maximum_chart_objects) return {};
    ++generated_objects;
    if (!object_ceiling) {
        const auto first = id_ceiling_t<object_id_t>::from(source).next();
        if (!first) return {};
        object_ceiling = first->value - 1;
    }
    if (object_ceiling == std::numeric_limits<std::uint64_t>::max()) return {};
    return object_id_t{++object_ceiling};
}
void transform_context_t::report(diagnostic_severity_t severity, diagnostic_code_t code,
                                std::string message, std::optional<object_id_t> object) {
    diagnostics.push_back({.severity = severity, .code = code, .message = std::move(message),
                           .location = {.chart = chart, .object = object}});
}
void transform_context_t::report_cut_omission(object_id_t object, bool crosses_boundary) {
    auto& reported = crosses_boundary ? reported_crossing_omission : reported_contained_omission;
    if (reported) return;
    reported = true;
    report(crosses_boundary ? diagnostic_severity_t::warning : diagnostic_severity_t::info,
        diagnostic_code_t::dropped_feature, crosses_boundary
            ? "Objects crossing export-cut boundaries were omitted."
            : "Objects wholly inside export cuts were omitted.", object);
}
auto exact_source_duration(std::uint32_t rate, std::optional<std::uint64_t> frames)
    -> std::optional<signed_rational_t> {
    if (!rate || !frames || *frames > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return {};
    return signed_rational_t{static_cast<std::int64_t>(*frames), rate};
}
void index_sampled_resources(const song_document_t& song, std::unordered_set<std::uint64_t>& resources) {
    for (const auto* chart : song.read_charts()) for (const auto& measure : chart->measures) {
        for (const auto& note : measure.notes) if (note.sound) resources.insert(note.sound->value);
        for (const auto& event : measure.bgm_events) if (event.sound) resources.insert(event.sound->value);
    }
}
void transform_context_t::ensure_media_index() const {
    if (media_indexed) return;
    media_indexed = true;
    std::unordered_map<std::uint64_t, const export_cut_source_t*> decoded;
    for (const auto& evidence : sources) decoded.emplace(evidence.asset.value, &evidence);
    for (const auto& asset : source.audio_assets) {
        const auto found = decoded.find(asset.id.value);
        const auto* evidence = found == decoded.end() ? nullptr : found->second;
        const auto duration = evidence ? exact_source_duration(evidence->native_sample_rate, evidence->native_frame_count)
            : exact_source_duration(asset.stream.sample_rate, asset.stream.frame_count);
        const auto path = evidence && !evidence->source_path.empty() ? evidence->source_path : asset.source_path;
        const auto [asset_entry, asset_inserted] = asset_durations.emplace(asset.id.value, duration);
        if (!asset_inserted) asset_entry->second.reset();
        const auto [custom, inserted] = custom_durations.emplace(path.lexically_normal().generic_string(), duration);
        if (!inserted) custom->second.reset();
    }
    index_sampled_resources(source, sampled_resources);
    for (const auto& resource : source.resources) {
        const auto asset = asset_durations.find(resource.asset.value);
        const auto duration = asset == asset_durations.end() ? std::optional<signed_rational_t>{} : asset->second;
        const auto [entry, inserted] = resource_durations.emplace(resource.id.value, duration);
        if (!inserted) entry->second.reset();
    }
}
auto transform_context_t::is_sampled(resource_id_t resource) const -> bool {
    ensure_media_index();
    return sampled_resources.contains(resource.value);
}
auto transform_context_t::asset_duration(asset_id_t asset) const -> std::optional<signed_rational_t> {
    ensure_media_index();
    const auto found = asset_durations.find(asset.value);
    return found == asset_durations.end() ? std::nullopt : found->second;
}
auto transform_context_t::duration(resource_id_t resource) const -> std::optional<signed_rational_t> {
    ensure_media_index();
    const auto found = resource_durations.find(resource.value);
    return found == resource_durations.end() ? std::nullopt : found->second;
}
auto transform_context_t::custom_sound_duration(std::string_view filename) const -> std::optional<signed_rational_t> {
    ensure_media_index();
    const auto found = custom_durations.find(std::filesystem::path{filename}.lexically_normal().generic_string());
    return found == custom_durations.end() ? std::nullopt : found->second;
}

struct row_interval_t { std::uint64_t begin{}, end{}; };
struct chart_cut_t {
    transform_context_t& context;
    const chart_document_t& source;
    measure_index_t measures;
    timing_index_t timing;
    std::vector<row_interval_t> cuts;
    std::vector<std::uint64_t> removed_prefix;
    chart_document_t result;
    measure_index_t destinations;
    std::optional<timing_index_t> speed_clock;

    auto source_time(local_position_t p) const -> std::optional<signed_rational_t> {
        const auto query = timing.query_seconds(measures, p);
        return query ? std::optional{query->value} : std::nullopt;
    }
    void index_cuts() {
        removed_prefix = {0};
        for (const auto cut : cuts) removed_prefix.push_back(removed_prefix.back() + cut.end - cut.begin);
    }
    auto removed_before(std::uint64_t row) const -> std::uint64_t {
        const auto next = std::lower_bound(cuts.begin(), cuts.end(), row,
            [](const auto& cut, auto value) { return cut.end <= value; });
        const auto index = static_cast<std::size_t>(next - cuts.begin());
        const auto partial = next != cuts.end() && row > next->begin ? row - next->begin : 0;
        return removed_prefix[index] + partial;
    }
    auto removed(std::uint64_t begin, std::uint64_t end) const -> std::uint64_t {
        return removed_before(end) - removed_before(begin);
    }
    auto row_of(local_position_t p) const -> std::optional<std::uint64_t> {
        return measures.row_of(p, measure_chart_end_policy_t::allow);
    }
    auto map(local_position_t p, bool end = false) const -> std::optional<local_position_t> {
        const auto row = row_of(p);
        if (!row) return {};
        return destinations.position_at(*row - removed(0, *row),
            end ? measure_chart_end_policy_t::allow : measure_chart_end_policy_t::reject);
    }
    auto wholly_removed(signed_rational_t begin, signed_rational_t end) const -> bool {
        if (!less(begin, end)) return context.timeline.contains_seconds(begin);
        const auto a = context.timeline.collapsed_seconds(begin);
        const auto b = context.timeline.collapsed_seconds(end);
        return a && b && timing_arithmetic::compare(*a, *b) == 0;
    }
    auto intersects(signed_rational_t begin, signed_rational_t end) const -> bool {
        const auto intervals = context.timeline.intervals();
        const auto next = std::lower_bound(intervals.begin(), intervals.end(), begin,
            [](const auto& cut, auto value) { return !less(value, {cut.end_milliseconds, 1000}); });
        return next != intervals.end() && less({next->begin_milliseconds, 1000}, end);
    }
    auto keep_span(object_id_t id, signed_rational_t begin, signed_rational_t end,
                   bool preserve_crossing = false) -> bool {
        if (wholly_removed(begin, end)) {
            context.report_cut_omission(id, false);
            return false;
        }
        if (!preserve_crossing && intersects(begin, end)) {
            context.report_cut_omission(id, true);
            return false;
        }
        return true;
    }
    auto custom_sound_end(const note_t& note, signed_rational_t begin) -> std::optional<signed_rational_t> {
        if (!note.osu_hitsample || note.osu_hitsample->filename.empty()) return begin;
        const auto duration = context.custom_sound_duration(note.osu_hitsample->filename);
        const auto end = duration ? add_exact(begin, *duration) : std::nullopt;
        if (!end) context.report(diagnostic_severity_t::error, diagnostic_code_t::dangling_asset,
            "Export cuts need a proven duration for this custom osu hitsample.", note.id);
        return end;
    }
    template<class Event>
    auto keep_sound(const Event& event) -> bool {
        const auto begin = source_time(event.position);
        if (!begin) return false;
        if (!context.timeline.intervals().empty() && !less(*begin,
            {context.timeline.intervals().back().end_milliseconds, 1000})) return true;
        auto end = event.end ? source_time(*event.end) : begin;
        if constexpr (std::is_same_v<Event, note_t>) {
            // Holds retain the body visible on Compose's collapsed axis. Their
            // authored endpoints map to the cut seams just like visible rows.
            if (event.end) return end && keep_span(event.id, *begin, *end, true);
        }
        if (event.sound && !event.sound_channel_event) {
            const auto duration = context.duration(*event.sound);
            if (!duration) {
                context.report(diagnostic_severity_t::error, diagnostic_code_t::dangling_asset,
                    "Export cuts need a proven duration for this sounding object.", event.id);
                return true;
            }
            const auto sound_end = add_exact(*begin, *duration);
            if (!sound_end) return false;
            if (!end || less(*end, *sound_end)) end = sound_end;
        }
        if constexpr (std::is_same_v<Event, note_t>) {
            const auto custom = custom_sound_end(event, *begin);
            if (!custom) return true;
            if (!end || less(*end, *custom)) end = custom;
        }
        return end && keep_span(event.id, *begin, *end);
    }
    auto prepare_measures() -> bool {
        result = source;
        result.measures.clear();
        for (std::size_t i = 0; i < source.measures.size(); ++i) {
            const auto& entry = measures.entries[i];
            const auto retained = entry.row_capacity - removed(entry.absolute_begin_row, entry.absolute_begin_row + entry.row_capacity);
            if (!retained) continue;
            auto measure = source.measures[i];
            measure.definition.length = rows_to_fraction(retained, source.divisions_per_standard_measure);
            if (retained != entry.row_capacity) measure.definition.meter.reset();
            measure.notes.clear();
            measure.bgm_events.clear();
            result.measures.push_back(std::move(measure));
        }
        const auto built = measure_index_t::from(result);
        if (!built || built->total_rows == 0) {
            context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_measure,
                "Export cuts remove the entire materialized chart.");
            return false;
        }
        destinations = *built;
        return true;
    }
    auto rewrite_objects() -> bool {
        for (const auto& measure : source.measures) {
            for (auto note : measure.notes) {
                if (!keep_sound(note)) continue;
                const auto destination = map(note.position);
                if (!destination) return false;
                note.position = *destination;
                if (note.end) note.end = map(*note.end, true);
                const auto bucket = std::ranges::find(result.measures, destination->measure, &measure_chunk_t::id);
                bucket->notes.push_back(std::move(note));
            }
            for (auto event : measure.bgm_events) {
                if (!keep_sound(event)) continue;
                const auto destination = map(event.position);
                if (!destination) return false;
                event.position = *destination;
                if (event.end) event.end = map(*event.end, true);
                const auto bucket = std::ranges::find(result.measures, destination->measure, &measure_chunk_t::id);
                bucket->bgm_events.push_back(std::move(event));
            }
        }
        return true;
    }
    template<class Events>
    void rewrite_states(Events& events) {
        std::stable_sort(events.begin(), events.end(), [&](const auto& a, const auto& b) {
            return row_of(a.position).value_or(0) < row_of(b.position).value_or(0);
        });
        Events mapped;
        for (auto event : events) {
            const auto position = map(event.position);
            if (!position) continue;
            event.position = *position;
            if (!mapped.empty() && mapped.back().position == event.position) mapped.back() = std::move(event);
            else mapped.push_back(std::move(event));
        }
        events = std::move(mapped);
    }
    auto speed_row(signed_rational_t seconds) const -> std::optional<signed_rational_t> {
        return timing.row_at(seconds, chart_end_policy_t::clamp_to_end,
            {.warp_side = timing_warp_inverse_side_t::entry}).absolute_row;
    }
    auto speed_seam_matches(std::optional<local_position_t> position, signed_rational_t source_seconds) const -> bool {
        if (!position || !speed_clock) return false;
        const auto clock = speed_clock->query_seconds(destinations, *position);
        const auto expected = context.timeline.collapsed_seconds(source_seconds);
        return clock && !clock->metadata.inside_warp && expected &&
            timing_arithmetic::compare(clock->value, *expected) == 0;
    }
    auto speed_piece(const speed_event_t& source_event, const speed_curve_segment_t& segment,
                     signed_rational_t begin, signed_rational_t end, bool retain_id,
                     std::optional<signed_rational_t> exact_begin = {},
                     std::optional<signed_rational_t> exact_end = {}) -> bool {
        if (!context.reserve_work()) return false;
        const auto begin_row = exact_begin ? exact_begin : speed_row(begin);
        const auto end_row = exact_end ? exact_end : speed_row(end);
        if (!begin_row || !end_row || begin_row->numerator < 0 ||
            begin_row->numerator % begin_row->denominator) return false;
        const auto row = static_cast<std::uint64_t>(begin_row->numerator) / begin_row->denominator;
        const auto position = destinations.position_at(row - removed_before(row));
        if (!speed_seam_matches(position, begin)) {
            context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
                "This SPEED transition needs a carry inside a warp or stop phase where native timing cannot apply it.", source_event.id);
            return false;
        }
        const auto initial = timing_arithmetic::speed_value(segment, *begin_row, begin,
            source.divisions_per_standard_measure);
        const auto target = timing_arithmetic::speed_value(segment, *end_row, end,
            source.divisions_per_standard_measure);
        const auto rows = subtract_exact(*end_row, *begin_row);
        const auto duration = segment.transition_is_seconds ? subtract_exact(end, begin)
            : rows ? timing_arithmetic::multiply(*rows, {4, source.divisions_per_standard_measure}) : std::nullopt;
        const auto initial_id = context.next_object();
        const auto target_id = retain_id ? std::optional{source_event.id} : context.next_object();
        if (!position || !initial || !target || !duration || duration->numerator < 0 ||
            !initial_id || !target_id) return false;
        auto event = source_event;
        event.id = *initial_id;
        event.position = *position;
        event.multiplier = *initial;
        event.transition_beats = {};
        result.speeds.push_back(event);
        event.id = *target_id;
        event.multiplier = *target;
        event.transition_beats = {static_cast<std::uint64_t>(duration->numerator), duration->denominator};
        result.speeds.push_back(std::move(event));
        return result.speeds.size() <= 200'000;
    }
    auto carry_speed(speed_event_t event) -> bool {
        const auto position = map(event.position);
        if (!position) return true;
        event.position = *position;
        event.transition_beats = {};
        result.speeds.push_back(std::move(event));
        return true;
    }
    auto split_seconds_speed(const speed_event_t& event, const speed_curve_segment_t& segment,
                     const scroll_speed_index_t& curves) -> bool {
        const auto span = scroll_speed_transition_span_t::from(curves, timing, event.id);
        if (!span) return false;
        const auto initial_count = result.speeds.size();
        auto begin = span->value.begin_seconds;
        const auto end = span->value.end_seconds;
        const auto piece = [&](signed_rational_t a, signed_rational_t b) {
            const auto start_row = a == span->value.begin_seconds ? std::optional{span->value.begin_row} : speed_row(a);
            const auto end_row = b == span->value.end_seconds ? std::optional{span->value.end_row} : speed_row(b);
            return speed_piece(event, segment, a, b, result.speeds.size() == initial_count, start_row, end_row);
        };
        const auto intervals = context.timeline.intervals();
        const auto first_cut = std::lower_bound(intervals.begin(), intervals.end(), begin,
            [](const auto& cut, auto value) { return !less(value, {cut.end_milliseconds, 1000}); });
        for (auto current = first_cut; current != intervals.end() && less({current->begin_milliseconds, 1000}, end); ++current) {
            if (!context.reserve_work()) return false;
            const auto cut = *current;
            const signed_rational_t cut_begin{cut.begin_milliseconds, 1000};
            const signed_rational_t cut_end{cut.end_milliseconds, 1000};
            if (!less(begin, cut_end) || !less(cut_begin, end)) continue;
            if (less(begin, cut_begin) && !piece(begin, cut_begin)) return false;
            begin = less(cut_end, end) ? cut_end : end;
        }
        if (less(begin, end) && !piece(begin, end)) return false;
        if (result.speeds.size() == initial_count) return carry_speed(event);
        return true;
    }
    auto row_seconds(signed_rational_t row) const -> std::optional<signed_rational_t> {
        if (row.numerator < 0 || !row.denominator) return {};
        const auto numerator = static_cast<std::uint64_t>(row.numerator);
        const auto query = timing.query_seconds(exact_chart_row_t{numerator / row.denominator,
            {numerator % row.denominator, row.denominator}});
        return query ? std::optional{query->value} : std::nullopt;
    }
    auto beat_piece(const speed_event_t& event, const speed_curve_segment_t& segment,
                    signed_rational_t begin, signed_rational_t end, bool retain_id) -> bool {
        const auto a = row_seconds(begin);
        const auto b = row_seconds(end);
        return a && b && speed_piece(event, segment, *a, *b, retain_id, begin, end);
    }
    auto split_beat_speed(const speed_event_t& event, const speed_curve_segment_t& segment,
                          const scroll_speed_index_t& curves) -> bool {
        const auto span = scroll_speed_transition_span_t::from(curves, timing, event.id);
        if (!span) return false;
        auto begin = span->value.begin_row;
        const auto end = span->value.end_row;
        const auto initial_count = result.speeds.size();
        const auto first = std::lower_bound(cuts.begin(), cuts.end(), begin,
            [](const auto& cut, auto value) { return !less(value, {static_cast<std::int64_t>(cut.end), 1}); });
        for (auto cut = first; cut != cuts.end() && less({static_cast<std::int64_t>(cut->begin), 1}, end); ++cut) {
            if (!context.reserve_work()) return false;
            const signed_rational_t cut_begin{static_cast<std::int64_t>(cut->begin), 1};
            const signed_rational_t cut_end{static_cast<std::int64_t>(cut->end), 1};
            if (less(begin, cut_begin) && !beat_piece(event, segment, begin, cut_begin,
                    result.speeds.size() == initial_count)) return false;
            begin = less(cut_end, end) ? cut_end : end;
        }
        if (less(begin, end) && !beat_piece(event, segment, begin, end,
                result.speeds.size() == initial_count)) return false;
        return result.speeds.size() != initial_count || carry_speed(event);
    }
    auto split_speed(const speed_event_t& event, const speed_curve_segment_t& segment,
                     const scroll_speed_index_t& curves) -> bool {
        if (segment.initial_multiplier == segment.target_multiplier) return carry_speed(event);
        return segment.transition_is_seconds ? split_seconds_speed(event, segment, curves)
            : split_beat_speed(event, segment, curves);
    }
    auto rewrite_speeds() -> bool {
        auto output_clock = timing_index_t::from(result);
        if (!output_clock.value) return false;
        speed_clock = std::move(*output_clock.value);
        const auto curves = scroll_speed_index_t::from(source, timing);
        if (!curves.value) return false;
        std::unordered_map<std::uint64_t, const speed_curve_segment_t*> segments;
        for (const auto& segment : curves.value->speed_segments) segments.emplace(segment.source_event.value, &segment);
        result.speeds.clear();
        auto ordered = source.speeds;
        std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) {
            return row_of(a.position).value_or(0) < row_of(b.position).value_or(0);
        });
        for (auto event : ordered) {
            if (event.transition_beats.numerator) {
                const auto segment = segments.find(event.id.value);
                if (segment == segments.end() || !split_speed(event, *segment->second, *curves.value)) return false;
            } else {
                const auto position = map(event.position);
                if (!position) continue;
                event.position = *position;
                result.speeds.push_back(std::move(event));
            }
        }
        std::stable_sort(result.speeds.begin(), result.speeds.end(), [&](const auto& a, const auto& b) {
            return destinations.row_of(a.position).value_or(0) < destinations.row_of(b.position).value_or(0);
        });
        return true;
    }
    void rewrite_pauses() {
        std::map<std::pair<std::uint64_t, pause_kind_t>, pause_event_t> grouped;
        for (const auto& pause : source.pauses) {
            const auto row = row_of(pause.position);
            if (row) grouped[{*row, pause.kind}] = pause;
        }
        result.pauses.clear();
        for (const auto& [key, source_pause] : grouped) {
            const auto boundary = std::ranges::lower_bound(timing.rows, key.first, {}, &timing_row_state_t::absolute_row);
            if (boundary == timing.rows.end() || boundary->absolute_row != key.first) continue;
            const auto begin = context.timeline.collapsed_seconds(key.second == pause_kind_t::delay
                ? boundary->before_delays_seconds : boundary->event_seconds);
            const auto end = context.timeline.collapsed_seconds(key.second == pause_kind_t::delay
                ? boundary->event_seconds : boundary->after_stops_seconds);
            const auto duration = begin && end ? subtract_exact(*end, *begin) : std::nullopt;
            const auto position = map(source_pause.position);
            if (!duration || duration->numerator <= 0 || !position) continue;
            auto pause = source_pause;
            pause.position = *position;
            pause.duration = *duration;
            pause.source_unit = pause_unit_t::seconds;
            result.pauses.push_back(std::move(pause));
        }
    }
    void rewrite_warps() {
        result.warps.clear();
        for (auto warp : source.warps) {
            const auto begin = map(warp.span.begin);
            const auto end = map(warp.span.end, true);
            if (!begin || !end || *begin == *end) continue;
            warp.span = {*begin, *end};
            result.warps.push_back(std::move(warp));
        }
    }
    auto music_asset() const -> std::optional<asset_id_t> {
        const auto& override = source.metadata.overrides.value(inherited_metadata_field_t::music);
        std::optional<asset_id_t> music;
        if (override) {
            for (const auto& asset : context.source.audio_assets)
                if (asset.source_path.lexically_normal() == std::filesystem::path{*override}.lexically_normal()) {
                    if (music) return {};
                    music = asset.id;
                }
        } else {
            for (const auto& resource : context.source.resources)
                if (resource.has_role(resource_role_t::music)) {
                    if (music && *music != resource.asset) return {};
                    music = resource.asset;
                }
        }
        return music;
    }
    auto anchor_rate() const -> std::optional<std::uint32_t> {
        const auto music = music_asset();
        if (!music) return {};
        const auto evidence = std::ranges::find(context.sources, *music, &export_cut_source_t::asset);
        if (evidence != context.sources.end()) return evidence->native_sample_rate;
        const auto* asset = context.source.find_audio_asset(*music);
        return asset ? std::optional{asset->stream.sample_rate} : std::nullopt;
    }
    auto rewrite_anchors() -> bool {
        if (source.timing_anchors.empty()) return true;
        const auto rate = anchor_rate();
        if (!rate || !*rate) {
            context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
                "Export cuts need one proven source music clock for timing anchors.");
            return false;
        }
        result.timing_anchors.clear();
        for (auto anchor : source.timing_anchors) {
            if (anchor.source_sample_frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
            const signed_rational_t source_seconds{static_cast<std::int64_t>(anchor.source_sample_frame), *rate};
            const auto time = source_time(anchor.position);
            const auto position = map(anchor.position);
            if (!time || context.timeline.contains_seconds(*time) ||
                context.timeline.contains_seconds(source_seconds) || !position) continue;
            const auto mapped = context.timeline.collapsed_seconds(source_seconds);
            const auto frame = mapped ? sample_frame_at(*mapped, export_cut_render_sample_rate) : sample_frame_result_t{};
            if (!frame.frame) return false;
            anchor.position = *position;
            anchor.source_sample_frame = *frame.frame;
            result.timing_anchors.push_back(std::move(anchor));
        }
        return true;
    }
    void rewrite_bookmarks() {
        result.bookmarks.clear();
        for (auto bookmark : source.bookmarks) {
            const auto time = bookmark.position ? source_time(*bookmark.position)
                : std::optional{signed_rational_t{bookmark.audio_milliseconds.value_or(0), 1000}};
            if (!time || context.timeline.contains_seconds(*time)) continue;
            if (bookmark.position) bookmark.position = map(*bookmark.position);
            if (bookmark.audio_milliseconds)
                bookmark.audio_milliseconds = context.timeline.collapsed_milliseconds(*bookmark.audio_milliseconds);
            result.bookmarks.push_back(std::move(bookmark));
        }
    }
    void rewrite_sections() {
        result.sections.clear();
        for (auto section : source.sections) {
            if (section.beat_span) {
                const auto begin = map(section.beat_span->begin);
                const auto end = map(section.beat_span->end, true);
                if (!begin || !end || *begin == *end) continue;
                section.beat_span = {*begin, *end};
            }
            if (section.audio_millisecond_span) {
                auto& [begin, end] = *section.audio_millisecond_span;
                begin = context.timeline.collapsed_milliseconds(begin);
                end = context.timeline.collapsed_milliseconds(end);
                if (begin >= end) continue;
            }
            result.sections.push_back(std::move(section));
        }
    }
    auto visual_ends() const -> std::unordered_map<std::uint64_t, signed_rational_t> {
        const auto end_query = timing.query_seconds(timing.total_rows, timing_boundary_side_t::before_delays);
        auto song_end = end_query ? end_query->value : signed_rational_t{};
        for (const auto& resource : context.source.resources) {
            if (!resource.has_role(resource_role_t::music)) continue;
            const auto value = context.duration(resource.id);
            if (value && less(song_end, *value)) song_end = *value;
        }
        const auto selected_music = music_asset();
        const auto selected_duration = selected_music ? context.asset_duration(*selected_music) : std::nullopt;
        if (selected_duration && less(song_end, *selected_duration)) song_end = *selected_duration;
        std::vector<const bga_event_t*> ordered;
        for (const auto& event : source.bga_events) ordered.push_back(&event);
        std::stable_sort(ordered.begin(), ordered.end(), [&](const auto* a, const auto* b) {
            return row_of(a->position).value_or(0) < row_of(b->position).value_or(0);
        });
        std::map<visual_event_kind_t, signed_rational_t> next;
        std::unordered_map<std::uint64_t, signed_rational_t> ends;
        for (auto i = ordered.rbegin(); i != ordered.rend(); ++i) {
            const auto begin = source_time((*i)->position);
            if (!begin) continue;
            const auto found = next.find((*i)->kind);
            ends.emplace((*i)->id.value, found == next.end() ? song_end : found->second);
            next[(*i)->kind] = *begin;
        }
        return ends;
    }
    void rewrite_visuals() {
        result.bga_events.clear();
        const auto ends = visual_ends();
        for (auto event : source.bga_events) {
            const auto begin = source_time(event.position);
            const auto end = ends.find(event.id.value);
            if (!begin || end == ends.end()) continue;
            if (!keep_span(event.id, *begin, end->second)) continue;
            const auto position = map(event.position);
            if (!position) continue;
            event.position = *position;
            result.bga_events.push_back(std::move(event));
        }
    }
    auto clock_matches(std::uint64_t row, signed_rational_t time, timing_boundary_side_t side,
                       const timing_index_t& after) -> bool {
        if (context.timeline.contains_seconds(time)) return true;
        const auto expected = context.timeline.collapsed_seconds(time);
        const auto mapped = row - removed(0, row);
        const auto actual = after.query_seconds(mapped, side);
        if (expected && actual && timing_arithmetic::compare(*expected, actual->value) == 0) return true;
        context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
            "This export cut cannot preserve the exact clock across a pause or warp boundary.");
        return false;
    }
    auto verify_clock() -> bool {
        const auto after = timing_index_t::from(result);
        if (!after.value) return false;
        for (const auto& boundary : timing.rows) {
            if (!clock_matches(boundary.absolute_row, boundary.before_delays_seconds,
                               timing_boundary_side_t::before_delays, *after.value) ||
                !clock_matches(boundary.absolute_row, boundary.event_seconds,
                               timing_boundary_side_t::event, *after.value) ||
                !clock_matches(boundary.absolute_row, boundary.after_stops_seconds,
                               timing_boundary_side_t::after_stops, *after.value)) return false;
        }
        return true;
    }

};

auto cut_row(const timing_index_t& timing, std::int64_t milliseconds, timing_warp_inverse_side_t side,
             transform_context_t& context) -> std::optional<signed_rational_t> {
    const signed_rational_t seconds{milliseconds, 1000};
    const auto row = timing.row_at(seconds, chart_end_policy_t::clamp_to_end, {.warp_side = side});
    if (!row.absolute_row) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_resolution,
            "Export-cut boundary cannot be mapped to the chart's exact clock.");
        return {};
    }
    return row.absolute_row;
}
auto cut_grid(const chart_document_t& chart, const timing_index_t& timing, transform_context_t& context)
    -> std::optional<std::uint64_t> {
    auto factor = std::uint64_t{1};
    for (const auto cut : context.timeline.intervals()) {
        const auto begin = cut_row(timing, cut.begin_milliseconds, timing_warp_inverse_side_t::entry, context);
        const auto end = cut_row(timing, cut.end_milliseconds, timing_warp_inverse_side_t::entry, context);
        if (!begin || !end) return {};
        const auto first = checked_math::checked_lcm(factor, begin->denominator);
        const auto next = first ? checked_math::checked_lcm(*first, end->denominator) : std::nullopt;
        if (!next || !checked_math::checked_multiply(chart.divisions_per_standard_measure, *next)) {
            context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_resolution,
                "Export cuts exceed the chart's bounded exact row-grid resolution.");
            return {};
        }
        factor = *next;
    }
    return checked_math::checked_multiply(chart.divisions_per_standard_measure, factor);
}
auto chart_intervals(const timing_index_t& timing, transform_context_t& context)
    -> std::optional<std::vector<row_interval_t>> {
    std::vector<row_interval_t> rows;
    for (const auto cut : context.timeline.intervals()) {
        const auto begin = cut_row(timing, cut.begin_milliseconds, timing_warp_inverse_side_t::entry, context);
        const auto end = cut_row(timing, cut.end_milliseconds, timing_warp_inverse_side_t::entry, context);
        if (!begin || !end || begin->numerator < 0 || end->numerator < 0 ||
            begin->numerator % begin->denominator || end->numerator % end->denominator) return {};
        row_interval_t interval{static_cast<std::uint64_t>(begin->numerator) / begin->denominator,
                                static_cast<std::uint64_t>(end->numerator) / end->denominator};
        if (interval.begin == interval.end) continue;
        if (!rows.empty() && interval.begin <= rows.back().end) rows.back().end = std::max(interval.end, rows.back().end);
        else rows.push_back(interval);
    }
    return rows;
}
void rewrite_preview(std::optional<song_preview_t>& preview, const export_cut_timeline_t& timeline) {
    if (!preview) return;
    const auto end = preview->end_milliseconds();
    if (!end && timeline.contains(preview->start_milliseconds)) { preview.reset(); return; }
    preview->start_milliseconds = timeline.collapsed_milliseconds(preview->start_milliseconds);
    if (end) {
        const auto length = timeline.collapsed_milliseconds(*end) - preview->start_milliseconds;
        if (length > 0) preview->length_milliseconds = length;
        else preview.reset();
    }
}
auto reserve_baked_ids(const chart_document_t& original, chart_document_t& baked,
                       transform_context_t& context) -> bool {
    std::set<object_id_t> authored;
    for (const auto& event : original.scrolls) authored.insert(event.id);
    for (const auto& event : original.speeds) authored.insert(event.id);
    const auto reserve = [&](auto& events) {
        for (auto& event : events) {
            if (authored.contains(event.id)) continue;
            const auto allocated = context.next_object();
            if (!allocated) return false;
            event.id = *allocated;
        }
        return true;
    };
    return reserve(baked.scrolls) && reserve(baked.speeds);
}
auto transform_chart(const chart_document_t& original, transform_context_t& context)
    -> std::optional<chart_document_t> {
    if (!context.reserve_work(context.timeline.intervals().size() * 2 + original.measures.size() + original.speeds.size())) return {};
    auto baked = bake_sv_recipes_for_export(original);
    for (const auto& diagnostic : baked.diagnostics)
        context.report(diagnostic.severity, diagnostic.code, diagnostic.message, diagnostic.object);
    if (!baked.value || !baked.ok() || !reserve_baked_ids(original, *baked.value, context)) return {};
    const auto clock = timing_index_t::from(*baked.value);
    if (!clock.value) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_rational,
            "Export cuts require valid exact chart timing.");
        return {};
    }
    const auto grid = cut_grid(*baked.value, *clock.value, context);
    if (!grid) return {};
    const auto refined = redivision_plan_t::from(*baked.value, *grid);
    if (!refined.value.chart || !refined.ok()) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_resolution,
            "The export-cut grid cannot preserve all chart positions exactly.");
        return {};
    }
    const auto& chart = *refined.value.chart;
    const auto measures = measure_index_t::from(chart);
    const auto timing = timing_index_t::from(chart);
    if (!measures || !timing.value) return {};
    const auto intervals = chart_intervals(*timing.value, context);
    if (!intervals) return {};
    chart_cut_t edit{.context = context, .source = chart, .measures = *measures,
        .timing = *timing.value, .cuts = *intervals};
    edit.index_cuts();
    if (!edit.prepare_measures() || !edit.rewrite_objects()) return {};
    edit.rewrite_states(edit.result.bpms);
    edit.rewrite_states(edit.result.scrolls);
    edit.rewrite_states(edit.result.volumes);
    edit.rewrite_states(edit.result.sample_sets);
    edit.rewrite_states(edit.result.kiai);
    edit.rewrite_states(edit.result.lane_layouts);
    edit.rewrite_pauses();
    edit.rewrite_warps();
    edit.rewrite_bookmarks();
    edit.rewrite_sections();
    edit.rewrite_visuals();
    // Slice preparation must close BMSON source ranges before their clocks change.
    if (!chart.sound_channels.empty()) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
            "Retained sound channels require exact source-slice preparation before applying export cuts.");
        return {};
    }
    if (!edit.rewrite_anchors()) return {};
    const auto offset = context.timeline.collapsed_seconds(chart.beat_zero_audio_offset_seconds);
    if (!offset) return {};
    edit.result.beat_zero_audio_offset_seconds = *offset;
    rewrite_preview(edit.result.metadata.preview_override, context.timeline);
    if (chart.source_family == format_family_t::osu_mania)
        edit.result.metadata.format_group_for(format_family_t::osu_mania).ordered_fields.emplace_back(
            "osu.import.explicit_source_origin", "1");
    edit.result.bms_measure_length_projections.clear();
    edit.result.bms_bpm_projections.clear();
    edit.result.bms_pause_projections.clear();
    edit.result.bms_control_tree.reset();
    edit.result.timing_links = {};
    edit.result.indices_slot = {};
    if (!edit.rewrite_speeds()) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
            "Export cuts cannot preserve this SPEED transition exactly.");
        return {};
    }
    if (!edit.verify_clock()) return {};
    return std::move(edit.result);
}

auto is_storyboard_comment(std::string_view text) -> bool {
    const auto trimmed = text_util::trim(text, text_util::ascii_whitespace);
    return trimmed.empty() || trimmed.starts_with("//");
}
auto is_storyboard_content_known(const storyboard_document_model_t& model, transform_context_t& context) -> bool {
    std::vector<const std::vector<storyboard_node_t>*> pending;
    if (!context.reserve_work(model.sprites.size())) return false;
    for (const auto& sprite : model.sprites) pending.push_back(&sprite.nodes);
    while (!pending.empty()) {
        const auto* nodes = pending.back();
        pending.pop_back();
        if (!context.reserve_work(nodes->size())) return false;
        for (const auto& node : *nodes) {
            if (node.kind == storyboard_node_kind_t::retained_line && !is_storyboard_comment(node.retained_line)) return false;
            if (!node.children.empty()) pending.push_back(&node.children);
        }
    }
    return std::ranges::all_of(model.retained_top_level_lines, [](const auto& line) { return is_storyboard_comment(line.text); });
}
auto storyboard_omissions(const storyboard_preview_index_t& index,
    const storyboard_document_t& storyboard, transform_context_t& context)
    -> std::optional<std::set<std::uint64_t>> {
    std::set<std::uint64_t> omitted;
    for (const auto& sprite : index.sources) {
        if (!context.reserve_work()) return {};
        const auto begin = context.timeline.collapsed_milliseconds(sprite.begin_time_ms);
        const auto end = context.timeline.collapsed_milliseconds(sprite.end_time_ms);
        if (begin == end && (sprite.begin_time_ms < sprite.end_time_ms || context.timeline.contains(sprite.begin_time_ms))) {
            omitted.insert(sprite.stable_id);
            context.report_cut_omission(object_id_t{storyboard.id.value}, false);
        } else if (context.timeline.intersects({sprite.begin_time_ms, sprite.end_time_ms})) {
            omitted.insert(sprite.stable_id);
            context.report_cut_omission(object_id_t{storyboard.id.value}, true);
        }
    }
    return omitted;
}
auto rewrite_storyboard(storyboard_document_t& storyboard, transform_context_t& context) -> bool {
    const auto fail = [&] {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
            "Export cuts cannot rebase this storyboard's unmodeled timed payload safely.", object_id_t{storyboard.id.value});
        return false;
    };
    if (!storyboard.model) {
        return std::ranges::all_of(storyboard.retained_source_lines, is_storyboard_comment) || fail();
    }
    auto& model = *storyboard.model;
    if (!is_storyboard_content_known(model, context)) return fail();
    const auto index = storyboard_preview_index_t::from(model);
    if (!index.ok() || !index.value) return fail();
    const auto omitted = storyboard_omissions(*index.value, storyboard, context);
    if (!omitted) return false;
    std::erase_if(model.sprites, [&](const auto& sprite) { return omitted->contains(sprite.stable_id); });
    for (auto& sprite : model.sprites) for (auto& node : sprite.nodes) {
        if (node.kind == storyboard_node_kind_t::command) {
            node.command.start_time_ms = context.timeline.collapsed_milliseconds(node.command.start_time_ms);
            node.command.end_time_ms = context.timeline.collapsed_milliseconds(node.command.end_time_ms);
        } else if (node.kind == storyboard_node_kind_t::loop)
            node.loop_start_time_ms = context.timeline.collapsed_milliseconds(node.loop_start_time_ms);
    }
    storyboard.retained_source_lines.clear();
    return true;
}
auto rewrite_storyboards(song_document_t& song, transform_context_t& context) -> bool {
    for (auto& storyboard : song.storyboards) if (!rewrite_storyboard(storyboard, context)) return false;
    return true;
}

} // namespace export_cut_detail
namespace {
auto is_selected(chart_id_t id, std::span<const chart_id_t> charts) -> bool {
    return charts.empty() || std::ranges::find(charts, id) != charts.end();
}
auto music_assets(const song_document_t& song, std::span<const chart_id_t> charts,
                  export_cut_detail::transform_context_t& context) -> std::set<asset_id_t> {
    std::set<asset_id_t> result;
    for (const auto& resource : song.resources) if (resource.has_role(resource_role_t::music)) result.insert(resource.asset);
    for (const auto* chart : song.read_charts()) {
        if (!is_selected(chart->id, charts)) continue;
        if (!context.reserve_work(song.audio_assets.size())) return {};
        auto overrides = chart->metadata.overrides;
        overrides.value(inherited_metadata_field_t::preview).reset();
        const auto references = metadata_audio_references_t::from(song, overrides);
        for (const auto& diagnostic : references.diagnostics)
            context.report(diagnostic.severity, diagnostic.code, diagnostic.message, diagnostic.object);
        result.insert(references.value.assets.begin(), references.value.assets.end());
    }
    return result;
}
auto source_geometry(const song_document_t& song, asset_id_t id,
                     std::span<const export_cut_source_t> sources) -> std::optional<export_cut_source_t> {
    const auto decoded = std::ranges::find(sources, id, &export_cut_source_t::asset);
    if (decoded != sources.end()) return *decoded;
    const auto* asset = song.find_audio_asset(id);
    if (!asset || !asset->stream.sample_rate || !asset->stream.frame_count ||
        *asset->stream.frame_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return {};
    const auto output = sample_frame_at({static_cast<std::int64_t>(*asset->stream.frame_count), asset->stream.sample_rate}, 44100);
    if (!output.frame) return {};
    return export_cut_source_t{.asset = id, .native_sample_rate = asset->stream.sample_rate,
        .native_frame_count = *asset->stream.frame_count, .output_frame_count = *output.frame, .source_path = asset->source_path};
}
auto rebind_music_resources(export_cut_transform_t& result, asset_id_t old_asset, asset_id_t generated,
                            const std::string& filename, export_cut_detail::transform_context_t& context) -> bool {
    auto ids = id_ceiling_t<resource_id_t>::from(result.document);
    std::vector<resource_entry_t> additions;
    for (auto& resource : result.document.resources) {
        if (resource.asset != old_asset || !resource.has_role(resource_role_t::music)) continue;
        if (!context.is_sampled(resource.id)) {
            resource.asset = generated;
            resource.name = filename;
            continue;
        }
        const auto id = ids.next();
        if (!id) {
            context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_id,
                "No unused resource identity remains for cut music.");
            return false;
        }
        ids.reserve(*id);
        auto music = resource;
        music.id = *id;
        music.asset = generated;
        music.name = filename;
        music.roles = {resource_role_t::music};
        music.target_identities.clear();
        additions.push_back(std::move(music));
        std::erase(resource.roles, resource_role_t::music);
        if (resource.roles.empty()) resource.roles = {resource_role_t::sample};
    }
    result.document.resources.insert(result.document.resources.end(), additions.begin(), additions.end());
    return true;
}
auto refresh_scoped_music(export_cut_transform_t& result, const std::filesystem::path& old_path,
                          const std::string& filename, export_cut_detail::transform_context_t& context) -> bool {
    for (auto& owner : result.document.prepared_bms_programs) {
        if (!owner) continue;
        auto prepared = owner->prepared().with_resolved_catalog(result.document);
        auto& override = prepared.settings.overrides.value(inherited_metadata_field_t::music);
        if (override && std::filesystem::path{*override}.lexically_normal() == old_path.lexically_normal()) override = filename;
        const auto rebuilt = bms_program_document_t::from(owner->difficulty(), std::move(prepared), owner->evaluation());
        if (!rebuilt.value || !rebuilt.ok()) {
            context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_binding,
                "The scoped export cannot bind its generated cut music catalog.");
            return false;
        }
        owner = *rebuilt.value;
    }
    return true;
}
auto cut_music_filename(const export_cut_transform_t& result, asset_id_t id,
                        const export_cut_detail::transform_context_t& context) -> std::string {
    filename_reservations_t names;
    for (const auto& asset : result.document.audio_assets) names.reserve(asset.source_path);
    for (const auto& asset : result.document.visual_assets) names.reserve(asset.source_path);
    for (const auto& source : context.sources) if (!source.source_path.empty()) names.reserve(source.source_path);
    for (const auto& plan : result.audio_plans) names.reserve(plan.filename);
    const auto stem = "export-cut-music-" + std::to_string(id.value);
    std::size_t suffix{};
    return names.allocate([&] {
        const auto filename = stem + (suffix ? "-" + std::to_string(suffix) : "") + std::string{context.audio.extension()};
        ++suffix;
        return filename;
    }).generic_string();
}
auto append_cut_music(export_cut_transform_t& result, asset_id_t id,
                      export_cut_detail::transform_context_t& context) -> bool {
    if (!context.reserve_work(context.source.resources.size() + result.document.prepared_bms_programs.size())) return false;
    const auto geometry = source_geometry(context.source, id, context.sources);
    const auto* original = context.source.find_audio_asset(id);
    if (!geometry || !original || geometry->native_frame_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::dangling_asset,
            "Export cuts require proven decoded geometry for the song music.");
        return false;
    }
    const auto source_end = sample_frame_at({static_cast<std::int64_t>(geometry->native_frame_count),
        geometry->native_sample_rate}, export_cut_render_sample_rate);
    const auto frames = source_end.frame ? export_cut_frame_timeline_t::from(context.timeline,
        export_cut_render_sample_rate, *source_end.frame) : diagnosed_t<export_cut_frame_timeline_t>{};
    const auto count = source_end.frame ? frames.value.kept_frame_count(0, *source_end.frame) : 0;
    if (!source_end.frame || !frames.ok() || !count) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_resolution,
            "Export cuts remove the complete song music or exceed its exact frame clock.");
        return false;
    }
    const auto allocated = id_ceiling_t<asset_id_t>::from(result.document).next();
    if (!allocated) {
        context.report(diagnostic_severity_t::error, diagnostic_code_t::invalid_id,
            "No unused asset identity remains for cut music.");
        return false;
    }
    const auto filename = cut_music_filename(result, id, context);
    auto generated = *original;
    generated.id = *allocated;
    generated.source_path = filename;
    generated.waveform_cache_path.reset();
    generated.stream = {.sample_rate = export_cut_render_sample_rate, .channels = 2, .frame_count = count, .seekable = true};
    result.document.audio_assets.push_back(std::move(generated));
    if (!rebind_music_resources(result, id, *allocated, filename, context)) return false;
    for (auto& chart : result.document.charts) {
        auto& override = chart.metadata.overrides.value(inherited_metadata_field_t::music);
        if (override && std::filesystem::path{*override}.lexically_normal() == original->source_path.lexically_normal())
            override = filename;
    }
    if (!refresh_scoped_music(result, original->source_path, filename, context)) return false;
    auto encoding = context.audio;
    encoding.mp3_bitrate_kbps = encoding.bitrate_for(geometry->bitrate_bits_per_second);
    result.audio_plans.push_back({.asset = id, .filename = filename, .source = *geometry,
        .output_frames = count, .encoding = encoding});
    return true;
}
auto transform_selected_charts(song_document_t& document, std::span<const chart_id_t> charts,
    export_cut_detail::transform_context_t& context) -> bool {
    for (auto& chart : document.charts) {
        if (!is_selected(chart.id, charts)) continue;
        context.chart = chart.id;
        auto transformed = export_cut_detail::transform_chart(chart, context);
        if (!transformed) {
            if (!diagnostic_queries::has_errors(context.diagnostics)) context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
                "This chart cannot be represented exactly after applying export cuts.");
            return false;
        }
        chart = std::move(*transformed);
    }
    for (auto& owner : document.prepared_bms_programs) {
        if (!owner || !is_selected(owner->difficulty(), charts)) continue;
        context.chart = owner->difficulty();
        auto transformed = export_cut_detail::transform_scoped(*owner, context);
        if (!transformed) {
            if (!diagnostic_queries::has_errors(context.diagnostics)) context.report(diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
                "This chart cannot be represented exactly after applying export cuts.");
            return false;
        }
        owner = std::move(transformed);
    }
    return true;
}

}
auto export_cut_transform_t::from(const song_document_t& source, std::span<const chart_id_t> charts,
                                   std::span<const export_cut_source_t> sources, export_audio_settings_t audio)
    -> export_cut_transform_outcome_t {
    export_cut_transform_outcome_t outcome{};
    if (!audio.is_valid()) {
        outcome.diagnostics.push_back({.severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_rational, .message = "Invalid export audio encoding settings."});
        return outcome;
    }
    const auto built = export_cut_timeline_t::from(source.export_cuts);
    for (const auto& diagnostic : built.diagnostics)
        outcome.diagnostics.push_back({.severity = diagnostic.severity, .code = diagnostic.code,
            .message = diagnostic.message, .location = {.object = diagnostic.object}});
    if (!built.ok()) return outcome;
    export_cut_detail::transform_context_t context{.source = source, .timeline = built.value,
        .sources = sources, .diagnostics = outcome.diagnostics, .audio = audio};
    export_cut_transform_t prepared{.document = source};
    if (source.export_cuts.empty()) { outcome.value = std::move(prepared); return outcome; }
    if (!transform_selected_charts(prepared.document, charts, context)) return outcome;
    context.chart.reset();
    for (const auto asset : music_assets(source, charts, context)) if (!append_cut_music(prepared, asset, context)) return outcome;
    export_cut_detail::rewrite_preview(prepared.document.metadata.preview, built.value);
    if (!export_cut_detail::rewrite_storyboards(prepared.document, context)) return outcome;
    prepared.document.export_cuts.clear();
    prepared.document.chart_read_slot = {};
    if (!outcome.ok()) return outcome;
    outcome.value = std::move(prepared);
    return outcome;
}

} // namespace wormedit
