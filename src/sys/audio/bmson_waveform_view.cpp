#include "sys/project/workspace_view.h"
#include "sys/audio/bmson_waveform_view.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/audio/stem_waveform_window.h"

#include <wormedit/audio/channel_waveform.h>
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <unordered_set>
#include <ranges>
#include <cmath>
#include <functional>

namespace wormedit::sys {

struct bmson_projection_cache_t {
    struct basis_t {
        const compose_scene_cache_t* owner{};
        const frontend_workspace_view_t* target{};
        const song_document_t* document{};
        const chart_document_t* chart{};
        chart_stamp_t stamp;
        std::uint64_t source_generation{}, timing_fingerprint{}, row_clock{}, epoch{};
        double scroll{}, height{}, zoom{};
        measure_id_t cursor;
        std::uint64_t selected_bgm{}, selected_note{};
        std::optional<asset_id_t> transport_asset, channel_asset;
        std::shared_ptr<const audio::waveform_analysis_result_t> transport_analysis, channel_analysis;
        std::shared_ptr<const export_cut_axis_t> axis;
        bool visible{}, scene{}, timing{}, stems{}, provided_visible{};
        auto operator==(const basis_t&) const -> bool = default;
    };
    struct analysis_t {
        asset_id_t asset;
        std::shared_ptr<const audio::waveform_analysis_result_t> analysis;
    };
    std::optional<basis_t> basis;
    std::vector<audio::decoded_source_binding_t> bindings;
    std::vector<const sound_channel_t*> visible;
    std::vector<analysis_t> analyses;
    std::array<std::size_t, 3> published_sizes{};
    object_id_t selected_source_lane;
};

namespace {

auto projection_ready(const bmson_waveform_projection_context_t& context) -> bool {
    return context.flattened.view.compose.visible &&
           context.flattened.view.compose.selected_bmson_bgm_event_id.value != 0 &&
           context.flattened.view.compose.scene_available && context.workspace.selected_chart &&
           context.interaction_state != nullptr && context.cache.timing.marker_timing_index;
}

auto stored_source_matches(const audio_asset_t& asset,
                           const audio::decoded_source_binding_t& binding) -> bool {
    return (asset.stream.sample_rate == 0 ||
            asset.stream.sample_rate == binding.source_sample_rate) &&
           (!asset.stream.frame_count || asset.stream.frame_count == binding.source_frame_count);
}

auto analysis_matches_binding(
    const std::shared_ptr<const audio::waveform_analysis_result_t>& analysis,
    const audio::decoded_source_binding_t& binding) -> bool {
    return analysis && analysis->waveform && binding.source_sample_rate != 0 &&
           binding.output_sample_rate == analysis->waveform->sample_rate &&
           binding.output_frame_count == analysis->waveform->total_frames;
}

// Where one channel's projection lives: the selected channel's fields in the scene cache, or one
// stem's own entry.
struct channel_projection_target_t {
    envelope_projection_ring_t& ring;
    const std::optional<sound_slice_result_t>& slices;
};

struct channel_ring_context_t {
    const bmson_waveform_projection_context_t& application;
    const audio::waveform_t& waveform;
    const sound_channel_t& channel;
    audio::channel_waveform_source_geometry_t source;
    double scroll{}, height{}, zoom{};
    channel_projection_target_t target;
    const audio::display_row_table_t* table{};
};
auto channel_ring_slot(std::int64_t row, std::size_t count) -> std::size_t {
    const auto remainder = row % static_cast<std::int64_t>(count);
    return static_cast<std::size_t>(remainder < 0 ? remainder + static_cast<std::int64_t>(count) : remainder);
}
void build_channel_ring_range_view(const channel_ring_context_t& context, std::int64_t begin,
                                std::size_t count) {
    auto& cache = context.application.cache;
    auto& ring = context.target.ring;
    const auto projected_data = audio::aligned_waveform_view_result_t::from(context.waveform, context.channel,
        context.table ? *context.table : cache.waveform.waveform_row_table,
        {.viewport_scroll_pixels = static_cast<double>(begin),
         .viewport_height_pixels = static_cast<double>(count),
         .pixels_per_standard_measure = context.zoom,
         .maximum_rows = audio::maximum_aligned_waveform_rows},
        *context.target.slices, context.source);
    ring.value().projected_rows += count;
    ++ring.value().projection_calls;
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = begin + static_cast<std::int64_t>(index);
        ring.value().rows[channel_ring_slot(world, ring.value().rows.size())] = {world, {}, false, false};
    }
    for (const auto& row : projected_data.rows) {
        const auto world = begin + static_cast<std::int64_t>(std::floor(row.y_pixels));
        if (world < begin || world >= begin + static_cast<std::int64_t>(count)) continue;
        ring.value().rows[channel_ring_slot(world, ring.value().rows.size())] = {world,
            {.y_pixels = static_cast<double>(world) + .5,
             .absolute_peak = {std::clamp(row.absolute_peak[0], 0.0f, 1.0f),
                               std::clamp(row.absolute_peak[1], 0.0f, 1.0f)},
             .rms = {std::clamp(row.rms[0], 0.0f, 1.0f), std::clamp(row.rms[1], 0.0f, 1.0f)},
             .sample_point = false, .samples = {}}, true, false};
    }
}
auto populate_channel_ring(const channel_ring_context_t& context, bool downscroll,
                           std::vector<wormedit::ui::compose_waveform_row_view_t>& rows) -> bool {
    if (!std::isfinite(context.scroll) || std::abs(context.scroll) > 1e12 ||
        !std::isfinite(context.height) || context.height <= 0 ||
        context.height > audio::maximum_aligned_waveform_rows - 2) return false;
    auto& ring = context.target.ring;
    const auto count = static_cast<std::size_t>(std::ceil(context.height)) + 2;
    const auto revision = context.application.history.document.revision;
    const auto project = context.application.history.document.id.value;
    const auto chart = context.application.workspace.selected_chart->value;
    const auto channel = context.channel.id.value;
    const auto geometry = envelope_projection_geometry_t::from(
        {.document = {.project = wormedit::song_id_t{project}, .revision = revision},
         .chart = wormedit::chart_id_t{chart}},
        channel, context.application.audition_source_generation, context.zoom, false, count);
    static_cast<void>(ring.get_or_build(envelope_projection_lookup_t{context.waveform.key, &context.waveform, geometry},
        [count](auto& data) { data.rows.assign(count, {}); },
        [count](const auto& data) { return data.rows.size() == count; }));
    ring.value().projected_rows = ring.value().reused_rows = ring.value().projection_calls = 0;
    const auto first = static_cast<std::int64_t>(std::floor(context.scroll)) - 1;
    std::size_t index = 0;
    while (index < count) {
        const auto world = first + static_cast<std::int64_t>(index);
        if (ring.value().rows[channel_ring_slot(world, count)].world_row == world) {
            ++ring.value().reused_rows; ++index; continue;
        }
        const auto begin = index++;
        while (index < count && ring.value().rows[channel_ring_slot(first + static_cast<std::int64_t>(index), count)].world_row !=
                                    first + static_cast<std::int64_t>(index)) ++index;
        build_channel_ring_range_view(context, first + static_cast<std::int64_t>(begin), index - begin);
    }
    rows.reserve(count);
    for (std::size_t row = 0; row < count; ++row) {
        const auto& stored = ring.value().rows[channel_ring_slot(first + static_cast<std::int64_t>(row), count)];
        if (!stored.present) continue;
        auto value = stored.value;
        value.y_pixels = compose_source_y(value.y_pixels - std::floor(context.scroll), context.height, downscroll);
        if (value.y_pixels < 0 || value.y_pixels > context.height) continue;
        rows.push_back(value);
    }
    return true;
}

enum class channel_projection_failure_t { cache_empty, retry };
auto build_retained_channel_rows(const channel_ring_context_t& context, bool downscroll,
                                 channel_projection_failure_t failure,
                                 compose_channel_projection_data_t& data) -> bool {
    data.rows.clear();
    if (!context.target.slices || !*context.target.slices) return true;
    return populate_channel_ring(context, downscroll, data.rows) ||
           failure == channel_projection_failure_t::cache_empty;
}
void build_retained_channel_slices(const chart_document_t& chart, const sound_channel_t& channel,
                                   audio::channel_waveform_source_geometry_t source,
                                   compose_channel_projection_t& projection,
                                   std::optional<sound_slice_result_t>& slices) {
    slices = derive_sound_channel_slices(chart, channel, source.sample_rate, source.total_frames);
    projection.invalidate(true);
}

// The BGM lane explicitly linked to this channel, retained by the per-revision index.
auto indexed_stem_lane(const stem_index_t& index, const chart_document_t& chart,
                       const sound_channel_t& channel) -> std::uint64_t {
    const auto* lane = stem_lane_for_channel(chart, channel.id);
    if (!lane) return 0;
    const auto entry = std::ranges::find(index.value().channels, channel.id.value,
                                       &stem_index_channel_t::id);
    return entry != index.value().channels.end() && entry->source_lane == lane->id.value
               ? entry->source_lane : lane->id.value;
}

auto stem_ready(const bmson_waveform_projection_context_t& context) -> bool {
    return context.flattened.view.compose.visible && context.flattened.view.compose.scene_available &&
           context.workspace.selected_chart && context.interaction_state != nullptr &&
           context.cache.timing.marker_timing_index && context.stem_analysis;
}

// The analysis of a stem's audio: the transport's or the selected channel's when it is theirs.
auto stem_analysis_for(const bmson_waveform_projection_context_t& context, asset_id_t asset)
    -> std::shared_ptr<const audio::waveform_analysis_result_t> {
    if (asset == context.workspace.transport.audio_asset && context.transport_waveform.asset == asset)
        return context.transport_waveform.analysis;
    if (context.channel_waveform.asset == asset) return context.channel_waveform.analysis;
    return context.stem_analysis ? context.stem_analysis(asset) : nullptr;
}

// Retain evidence already requested by projection; do not query producers twice.
void retain_bmson_analysis(const bmson_waveform_projection_context_t& context, asset_id_t asset,
    const std::shared_ptr<const audio::waveform_analysis_result_t>& analysis) {
    auto& retained = *context.cache.stems.rows;
    const auto found = std::ranges::find(retained.analyses, asset,
        &bmson_projection_cache_t::analysis_t::asset);
    if (found == retained.analyses.end()) retained.analyses.push_back({asset, analysis});
    else found->analysis = analysis;
}

// Retained slice-point event → the note or BGM marker that stands for it.
void rebuild_point_refs(const chart_document_t& chart, const sound_channel_t& channel,
                        compose_scene_cache_t::stem_waveform_cache_t::point_refs_t& refs) {
    refs.clear();
    std::unordered_set<std::uint64_t> events;
    for (const auto& event : channel.events) events.insert(event.id.value);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (note.sound_channel_event && events.contains(note.sound_channel_event->value))
                refs[note.sound_channel_event->value] = {note.id.value, note.lane.value, true};
        for (const auto& bgm : measure.bgm_events)
            if (bgm.sound_channel_event && events.contains(bgm.sound_channel_event->value))
                refs[bgm.sound_channel_event->value] = {bgm.id.value, bgm.source_lane.value, false};
    }
}

struct stem_split_context_t {
    const chart_document_t& chart;
    compose_scene_cache_t::stem_waveform_cache_t& entry;
    double scroll{}, height{}, zoom{};
    bool downscroll{};
    const export_cut_axis_t* cut_axis{};
};

auto slice_lane_ref(const stem_split_context_t& context, const derived_sound_slice_t& slice)
    -> std::optional<compose_scene_cache_t::stem_waveform_cache_t::point_ref_t> {
    for (const auto event : slice.events) {
        const auto found = context.entry.refs.value().find(event.value);
        if (found != context.entry.refs.value().end()) return found->second;
    }
    return std::nullopt;
}

// Lists the stem's cut lines and keeps its rows for the BGM lane. The slice points come from
// notes in the field as well as BGM markers, but all of them are drawn in the stem's own lane.
void split_stem_by_lane(const stem_split_context_t& context) {
    auto& entry = context.entry;
    entry.projection.view.value().pieces.clear();
    entry.projection.view.value().cuts.clear();
    if (!entry.slices.value() || !*entry.slices.value() || context.chart.divisions_per_standard_measure == 0 ||
        entry.source_lane == 0) return;
    const auto& slices = entry.slices.value()->slices;
    const auto base = std::floor(context.scroll);
    for (const auto& slice : slices) {
        const auto ref = slice_lane_ref(context, slice);
        if (!ref) continue;
        if (context.cut_axis && context.cut_axis->contains_row(slice.absolute_row)) continue;
        const auto row = context.cut_axis ? context.cut_axis->collapsed_row(slice.absolute_row) : slice.absolute_row;
        const auto world = static_cast<double>(row) * context.zoom /
                           static_cast<double>(context.chart.divisions_per_standard_measure);
        const auto y = compose_source_y(world - base, context.height, context.downscroll);
        if (y >= 0.0 && y <= context.height)
            entry.projection.view.value().cuts.push_back({.y_pixels = y, .bgm_lane_id = object_id_t{entry.source_lane}, .restart = slice.restart,
                                  .object_is_note = ref->playable, .object_id = object_id_t{ref->object}});
    }
    // The selected channel's waveform is drawn by its own path; everything else goes in its lane.
    if (!entry.is_selected && !entry.projection.view.value().rows.empty())
        entry.projection.view.value().pieces.push_back({.lane = entry.source_lane, .playable = false, .rows = entry.projection.view.value().rows});
}

void build_stem_channel_projection(const channel_ring_context_t& projection,
                                    const stem_split_context_t& split,
                                    compose_channel_projection_data_t& data) {
    static_cast<void>(build_retained_channel_rows(projection, split.downscroll,
                                                 channel_projection_failure_t::cache_empty, data));
    split_stem_by_lane(split);
}

// Projects one stem whose audio is decoded; false leaves it as a placeholder.
auto build_stem_view(const bmson_waveform_projection_context_t& context, const chart_document_t& chart,
                  const sound_channel_t& channel, compose_scene_cache_t::stem_waveform_cache_t& entry) -> bool {
    const auto asset = channel.source_clip.source;
    const auto binding = context.audition_sources.binding(context.history.document, asset);
    const auto stored = context.history.document.find_audio_asset(asset);
    const auto analysis = stem_analysis_for(context, asset);
    retain_bmson_analysis(context, asset, analysis);
    if (!binding || stored == nullptr ||
        !stored_source_matches(*stored, *binding) || !analysis_matches_binding(analysis, *binding)) return false;
    const audio::channel_waveform_source_geometry_t source{
        .sample_rate = binding->source_sample_rate, .total_frames = binding->source_frame_count};
    const auto& waveform = *analysis->waveform;
    const auto revision = context.history.document.revision;
    static_cast<void>(entry.slices.get_or_build(
        std::tuple{static_cast<const void*>(&waveform), context.audition_source_generation, revision},
        std::bind_front(build_retained_channel_slices, std::cref(chart), std::cref(channel), source,
                        std::ref(entry.projection)), std::identity{}));
    static_cast<void>(entry.refs.get_or_build(revision,
        std::bind_front(rebuild_point_refs, std::cref(chart), std::cref(channel)),
        [](const auto& refs) { return !refs.empty(); }));
    const auto scroll = context.interaction_state->viewport_scroll_pixels;
    const auto height = context.flattened.view.compose.viewport_height_pixels;
    const auto zoom = context.flattened.view.compose.pixels_per_standard_measure;
    const auto downscroll = compose_field_downscroll(chart);
    const channel_ring_context_t projection{context, waveform, channel, source, scroll, height, zoom,
                                            {entry.projection.ring, entry.slices.value()}};
    const stem_split_context_t split{chart, entry, scroll, height, zoom, downscroll, context.interaction_state->cut_axis.get()};
    static_cast<void>(entry.projection.view.get_or_build(std::tuple{scroll, height, zoom},
        std::bind_front(build_stem_channel_projection, std::cref(projection), std::cref(split))));
    return true;
}

auto stem_source_bindings(const bmson_waveform_projection_context_t& context)
    -> std::span<const audio::decoded_source_binding_t> {
    auto& bindings = context.cache.stems.source_bindings;
    if (!context.visible_stems)
        context.audition_sources.bindings(context.history.document, bindings);
    return bindings;
}

// Select the visible channels into retained scratch. The selected channel is published
// separately when it is outside this window.
auto visible_stems(const bmson_waveform_projection_context_t& context, const chart_document_t& chart)
    -> std::span<const sound_channel_t* const> {
    if (context.visible_stems) return *context.visible_stems;
    const bool cache_matches = context.cache.stamp().chart == chart.id &&
                               context.cache.stamp().document.revision == context.history.document.revision;
    const auto* timing = cache_matches && context.cache.timing.marker_timing_index
                             ? &*context.cache.timing.marker_timing_index : nullptr;
    const auto* indices = context.history.read_chart(chart.id) == &chart
        ? context.history.indices(chart.id) : nullptr;
    auto& visible = context.cache.stems.visible_channels;
    indexed_visible_stem_channels(context.cache.stems.stem_index,
        context.history.document, chart, context.interaction_state->cursor.measure,
        stem_window_measures(context.flattened.view.compose.viewport_height_pixels,
                             context.flattened.view.compose.pixels_per_standard_measure),
        stem_source_bindings(context), visible,
        stem_geometry_indices_t{.timing = timing, .measures = indices ? &indices->measures : nullptr});
    return visible;
}

// Cache-owned projection rows remain stable while the frame borrows them.
void publish_stem(wormedit::ui::compose_view_t& view, const sound_channel_t& channel,
                  const compose_scene_cache_t::stem_waveform_cache_t& entry) {
    if (entry.pending) {
        if (!entry.is_selected && entry.source_lane != 0)
            view.stem_waveforms.push_back({.stem_key = channel.id.value,
                .source_lane_id = object_id_t{entry.source_lane}, .pending = true});
        return;
    }
    for (const auto& piece : entry.projection.view.value().pieces)
        if (!piece.rows.empty())
            view.stem_waveforms.push_back({.stem_key = channel.id.value,
                .source_lane_id = object_id_t{piece.lane}, .rows = piece.rows});
    const auto& cuts = entry.projection.view.value().cuts;
    view.stem_cuts.insert(view.stem_cuts.end(), cuts.begin(), cuts.end());
}

void populate_stem(const bmson_waveform_projection_context_t& context,
                   const chart_document_t& chart, const sound_channel_t& channel, bool selected) {
    if (!stem_lane_for_channel(chart, channel.id)) return;
    auto& entry = context.cache.stems.stem_for(channel.id);
    if (entry.is_selected != selected) entry.projection.view.invalidate();
    entry.is_selected = selected;
    entry.source_lane = indexed_stem_lane(context.cache.stems.stem_index, chart, channel);
    entry.pending = !build_stem_view(context, chart, channel, entry);
    publish_stem(context.flattened.view.compose, channel, entry);
}

// Near-cursor stems and the selected channel retain independent audio projection caches.
void populate_stem_waveforms(const bmson_waveform_projection_context_t& context) {
    auto& cache = context.cache;
    auto& view = context.flattened.view.compose;
    view.stem_waveforms.clear();
    view.stem_cuts.clear();
    if (!stem_ready(context)) return;
    const auto chart = context.history.read_chart(*context.workspace.selected_chart);
    if (chart == nullptr || chart->source_family != format_family_t::bmson) {
        cache.stems.stem_waveforms.clear();
        return;
    }
    const auto selected = view.selected_bmson_bgm_event_id.value != 0
        ? selected_bmson_waveform_source(cache, *chart, context.history.document.revision,
              object_id_t{view.selected_bmson_bgm_event_id.value}) : std::nullopt;
    const auto visible = visible_stems(context, *chart);
    cache.stems.stem_waveforms.reserve(visible.size() + (selected ? 1 : 0));
    std::erase_if(cache.stems.stem_waveforms, [&](const auto& entry) {
        return (!selected || selected->channel != entry.channel) &&
            std::ranges::none_of(visible, [&entry](const auto* channel) {
                return channel && channel->id == entry.channel;
            });
    });
    bool selected_visible = false;
    for (const auto* channel : visible) {
        if (!channel) continue;
        const bool is_selected = selected && selected->channel == channel->id;
        selected_visible = selected_visible || is_selected;
        populate_stem(context, *chart, *channel, is_selected);
    }
    if (!selected || selected_visible) return;
    const auto found = std::ranges::find(chart->sound_channels, selected->channel, &sound_channel_t::id);
    if (found != chart->sound_channels.end()) populate_stem(context, *chart, *found, true);
}

void copy_packed_display_clock(const audio::display_row_table_t& base, audio::display_row_table_t& table,
                                double end, double seconds, double scale,
                                audio::display_row_table_t::clock_data_t& clock) {
    clock = base.clock.value();
    table.viewport = base.viewport;
    clock.segments.push_back({static_cast<double>(base.clock.value().total_rows), end, seconds, scale});
    clock.boundaries.push_back({});
}
void build_packed_display_rows(const audio::display_row_table_t& table,
                                audio::aligned_waveform_request_t request,
                                audio::display_row_table_t::row_data_t& data) {
    data.first_world_pixel = static_cast<std::int64_t>(std::floor(request.viewport_scroll_pixels)) - 1;
    data.rows.clear();
    const auto count = static_cast<std::size_t>(std::ceil(request.viewport_height_pixels)) + 2;
    const auto end = static_cast<double>(table.clock.value().total_rows);
    const auto rows_per_pixel = static_cast<double>(table.clock.value().divisions) / request.pixels_per_standard_measure;
    for (std::size_t index = 0; index < count; ++index) {
        const auto world = data.first_world_pixel + static_cast<std::int64_t>(index);
        const auto source = [&](double row) { return table.cut_axis ? static_cast<double>(table.cut_axis->source_boundary_row(row)) : row; };
        const auto begin = std::clamp(source(static_cast<double>(world) * rows_per_pixel), 0.0, end);
        const auto next = std::clamp(source((static_cast<double>(world) + 1) * rows_per_pixel), 0.0, end);
        const auto first = table.seconds_at(begin, timing_boundary_side_t::before_delays);
        const auto last = table.seconds_at(next, timing_boundary_side_t::before_delays);
        data.rows.push_back({world, begin, next, first.value_or(0), last.value_or(0),
                             first && last && begin < next && *first <= *last});
    }
}

// Only packed display pieces extend the last tempo beyond authored geometry.
// The exact timing index and the existing shared stem table are untouched.
auto packed_display_table(const bmson_waveform_projection_context_t& context)
    -> const audio::display_row_table_t* {
    auto& cache = context.cache;
    const auto& base = cache.waveform.waveform_row_table;
    const auto& view = context.flattened.view.compose;
    if (!base.viewport.has_value() || !cache.timing.marker_timing_index) return nullptr;
    const auto end = std::ceil((context.interaction_state->viewport_scroll_pixels +
        view.viewport_height_pixels + 2) * static_cast<double>(base.clock.value().divisions) /
        view.pixels_per_standard_measure);
    if (!std::isfinite(end) || end >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) return nullptr;
    if (end <= static_cast<double>(base.clock.value().total_rows)) return &base;
    const auto& timing = *cache.timing.marker_timing_index;
    const auto seconds = timing.seconds_at_approx(timing.total_rows, chart_end_policy_t::reject, timing_boundary_side_t::after_stops);
    const auto scale = timing.tail_seconds_per_row();
    if (!seconds || !scale) return nullptr;
    auto& table = cache.stems.packed_waveform_row_table;
    table.cut_axis = base.cut_axis;
    table.cut_frames = base.cut_frames;
    auto& clock = table.clock.get_or_build(
        audio::display_row_table_t::clock_key_t{std::in_place_index<1>, base.clock.value().generation},
        std::bind_front(copy_packed_display_clock, std::cref(base), std::ref(table), end, *seconds, *scale),
        [&table](const auto&) { return table.viewport.has_value(); });
    clock.total_rows = static_cast<std::uint64_t>(end);
    clock.segments.back().end_row = end;
    const auto finish = *seconds + (end - static_cast<double>(base.clock.value().total_rows)) * *scale;
    if (!std::isfinite(finish)) return nullptr;
    clock.boundaries.back() = {end, finish, finish, finish};
    const audio::aligned_waveform_request_t request{
        .viewport_scroll_pixels = context.interaction_state->viewport_scroll_pixels,
        .viewport_height_pixels = view.viewport_height_pixels,
        .pixels_per_standard_measure = view.pixels_per_standard_measure,
        .cut_axis = context.interaction_state->cut_axis};
    static_cast<void>(table.viewport.get_or_build(
        std::tuple{request.viewport_scroll_pixels, request.viewport_height_pixels, request.pixels_per_standard_measure},
        std::bind_front(build_packed_display_rows, std::cref(table), request),
        [](const auto&) { return false; }));
    return &table;
}

auto packed_slices(const packed_waveform_piece_t& piece) -> sound_slice_result_t {
    sound_slice_result_t result;
    result.slices.push_back({piece.row, piece.clip.begin_frame, piece.clip.end_frame, true, {piece.event}});
    if (piece.next_row)
        result.slices.push_back({*piece.next_row, 0, 0, true, {}});
    return result;
}
auto packed_slice_matches(const packed_waveform_piece_t& piece,
                          const std::optional<sound_slice_result_t>& slices) -> bool {
    if (!slices || slices->slices.size() != (piece.next_row ? 2U : 1U)) return false;
    const auto& first = slices->slices.front();
    return first.absolute_row == piece.row && first.source_begin_frame == piece.clip.begin_frame &&
        first.source_end_frame == piece.clip.end_frame &&
        (!piece.next_row || slices->slices.back().absolute_row == *piece.next_row);
}
void build_retained_packed_slices(const packed_waveform_piece_t& piece,
                                  compose_channel_projection_t& projection,
                                  std::optional<sound_slice_result_t>& slices) {
    slices = packed_slices(piece);
    projection.invalidate(false);
}
auto build_packed_piece_view(const bmson_waveform_projection_context_t& context,
    const packed_waveform_piece_t& piece, compose_scene_cache_t::stem_waveform_cache_t& entry,
    const audio::display_row_table_t& table, bool downscroll) -> bool {
    const auto binding = context.audition_sources.binding(context.history.document, piece.clip.source);
    const auto analysis = stem_analysis_for(context, piece.clip.source);
    retain_bmson_analysis(context, piece.clip.source, analysis);
    const auto stored = context.history.document.find_audio_asset(piece.clip.source);
    if (!binding || stored == nullptr ||
        !stored_source_matches(*stored, *binding) || !analysis_matches_binding(analysis, *binding) ||
        analysis->waveform->key.source != binding->source) return false;
    const auto& waveform = *analysis->waveform;
    const auto revision = context.history.document.revision;
    static_cast<void>(entry.slices.get_or_build(
        std::tuple{static_cast<const void*>(&waveform), context.audition_source_generation, revision},
        std::bind_front(build_retained_packed_slices, std::cref(piece), std::ref(entry.projection)),
        std::bind_front(packed_slice_matches, std::cref(piece))));
    const auto& view = context.flattened.view.compose;
    const auto scroll = context.interaction_state->viewport_scroll_pixels;
    const auto key = std::tuple{scroll, view.viewport_height_pixels, view.pixels_per_standard_measure};
    static_cast<void>(entry.projection.view.get_or_build(key,
        [&context, &piece, &entry, &table, &binding, &waveform, &view, scroll, downscroll](auto& data) {
            const sound_channel_t channel{.id = piece.event, .source_clip = piece.clip};
            const audio::channel_waveform_source_geometry_t source{
                binding->source_sample_rate, binding->source_frame_count};
            const channel_ring_context_t projection{context, waveform, channel, source,
                scroll, view.viewport_height_pixels, view.pixels_per_standard_measure,
                {entry.projection.ring, entry.slices.value()}, &table};
            return build_retained_channel_rows(projection, downscroll,
                                               channel_projection_failure_t::retry, data);
        }));
    return entry.projection.view.is_bound_to(key);
}

auto packed_pending_band(const packed_waveform_piece_t& piece,
    const audio::display_row_table_t& table, bool downscroll) -> std::pair<double, double> {
    double low = table.height(), high = 0;
    for (const auto& row : table.viewport.value().rows) {
        if (!row.valid || row.begin_seconds >= piece.end_seconds || row.end_seconds <= piece.begin_seconds) continue;
        const auto y = static_cast<double>(row.world_pixel) - std::floor(table.scroll());
        low = std::min(low, y); high = std::max(high, y + 1);
    }
    if (low >= high) return {0, 0};
    const auto begin = compose_source_y(std::clamp(low, 0.0, table.height()), table.height(), downscroll);
    const auto end = compose_source_y(std::clamp(high, 0.0, table.height()), table.height(), downscroll);
    return {std::min(begin, end), std::max(begin, end)};
}
void populate_packed_waveforms(const bmson_waveform_projection_context_t& context) {
    auto& cache = context.cache;
    auto& view = context.flattened.view.compose;
    if (!view.visible || !view.scene_available || !context.workspace.selected_chart ||
        !context.interaction_state || !cache.timing.marker_timing_index) return;
    const auto chart = context.history.read_chart(*context.workspace.selected_chart);
    if (chart == nullptr || chart->source_family != format_family_t::bmson) return;
    const auto& measures = context.history.indices(chart->id)->measures;
    if (!measures) return;
    prepare_packed_waveform_index(cache.stems.packed_waveform_index, context.history.document,
        *chart, stem_source_bindings(context), *cache.timing.marker_timing_index,
        &*measures);
    const auto window = packed_window_seconds(*cache.timing.marker_timing_index,
        context.interaction_state->viewport_scroll_pixels, view.viewport_height_pixels,
        view.pixels_per_standard_measure);
    if (!window) return;
    auto& pieces = cache.stems.visible_packed_pieces;
    visible_packed_waveform_pieces(cache.stems.packed_waveform_index, window->first, window->second,
                                 pieces, cache.stems.visible_packed_indices);
    std::erase_if(cache.stems.packed_waveforms, [&](const auto& entry) {
        return std::ranges::none_of(pieces, [&entry](const auto* piece) { return piece->event == entry.channel; });
    });
    cache.stems.packed_waveforms.reserve(pieces.size());
    if (pieces.empty()) return;
    const auto* table = packed_display_table(context);
    if (!table) return;
    const auto downscroll = compose_field_downscroll(*chart);
    for (const auto* piece : pieces) {
        auto* entry = &cache.stems.packed_for(piece->event);
        const auto ready = build_packed_piece_view(context, *piece, *entry, *table, downscroll);
        const auto band = packed_pending_band(*piece, *table, downscroll);
        if ((!ready && band.first < band.second) || (ready && !entry->projection.view.value().rows.empty()))
            view.stem_waveforms.push_back({.stem_key = piece->event.value,
                .source_lane_id = piece->lane,
                .rows = ready ? std::span<const ui::compose_waveform_row_view_t>{entry->projection.view.value().rows}
                              : std::span<const ui::compose_waveform_row_view_t>{}, .pending = !ready,
                .piece_kind = ui::compose_waveform_piece_kind_t::packed_event, .bounded = true,
                .begin_y_pixels = band.first, .end_y_pixels = band.second});
    }
}

} // namespace

auto prepare_compose_display_row_table(compose_scene_cache_t& cache,
    audio::aligned_waveform_request_t request, audio::display_row_table_identity_t identity) -> bool {
    if (!cache.timing.marker_timing_index) return false;
    const auto previous = cache.waveform.waveform_row_table.clock.value().generation;
    const auto previous_axis = cache.waveform.waveform_row_table.cut_axis;
    const auto prepared = (cache.waveform.waveform_row_table).prepare(
        *cache.timing.marker_timing_index, request, identity);
    if (previous_axis != request.cut_axis) {
        cache.waveform.overview.invalidate();
        cache.waveform.bgm_bar.rows.invalidate();
        cache.waveform.bgm_bar.marks.invalidate();
    }
    if (!prepared || previous_axis != request.cut_axis || previous != cache.waveform.waveform_row_table.clock.value().generation) {
        cache.waveform.waveform_envelope_ring.invalidate();
        cache.waveform.channel_projection.invalidate(false);
        cache.waveform.channel_waveform_slices.value().reset();
        for (auto& entry : cache.stems.packed_waveforms) entry.projection.invalidate(false);
        for (auto& entry : cache.stems.stem_waveforms) {
            entry.projection.invalidate(false);
            entry.slices.value().reset();
        }
    }
    return prepared;
}

void build_bmson_waveform_projection(const bmson_waveform_projection_context_t& context) {
    context.flattened.view.compose.channel_waveform_rows.clear();
    context.flattened.view.compose.channel_waveform_source_lane_id = {};
    populate_stem_waveforms(context);
    populate_packed_waveforms(context);
    if (!projection_ready(context))
        return;

    auto& flattened = context.flattened;
    auto& history = context.history;
    const auto& workspace = context.workspace;
    const auto* interaction_state = context.interaction_state;
    auto& cache = context.cache;

    const auto chart = history.read_chart(*workspace.selected_chart);
    if (chart == nullptr)
        return;

    const auto selection = selected_bmson_waveform_source(
        cache, *chart, history.document.revision,
        wormedit::object_id_t{flattened.view.compose.selected_bmson_bgm_event_id.value});
    const auto channel = selection ? std::ranges::find(chart->sound_channels, selection->channel,
                                                       &wormedit::sound_channel_t::id)
                                   : chart->sound_channels.end();
    if (!selection || channel == chart->sound_channels.end() ||
        !stem_lane_for_channel(*chart, channel->id))
        return;

    const auto asset = selection->asset;
    const auto binding = context.audition_sources.binding(context.history.document, asset);
    const auto stored_asset =
        history.document.find_audio_asset(asset);
    const auto& analysis =
        asset == workspace.transport.audio_asset && context.transport_waveform.asset == asset
            ? context.transport_waveform.analysis
        : context.channel_waveform.asset == asset
            ? context.channel_waveform.analysis
            : std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>{};
    if (!binding || stored_asset == nullptr ||
        !stored_source_matches(*stored_asset, *binding) ||
        !analysis_matches_binding(analysis, *binding))
        return;

    const auto& waveform = *analysis->waveform;
    const wormedit::audio::channel_waveform_source_geometry_t source{
        .sample_rate = binding->source_sample_rate, .total_frames = binding->source_frame_count};
    static_cast<void>(cache.waveform.channel_waveform_slices.get_or_build(
        std::tuple{channel->id, static_cast<const void*>(&waveform), context.audition_source_generation},
        std::bind_front(build_retained_channel_slices, std::cref(*chart), std::cref(*channel), source,
                        std::ref(cache.waveform.channel_projection)), std::identity{}));
    const auto scroll = interaction_state->viewport_scroll_pixels;
    const auto height = flattened.view.compose.viewport_height_pixels;
    const auto scale = flattened.view.compose.pixels_per_standard_measure;
    const channel_ring_context_t projection{context, waveform, *channel, source, scroll, height, scale,
        {cache.waveform.channel_projection.ring, cache.waveform.channel_waveform_slices.value()}};
    static_cast<void>(cache.waveform.channel_projection.view.get_or_build(std::tuple{scroll, height, scale},
        std::bind_front(build_retained_channel_rows, std::cref(projection), compose_field_downscroll(*chart),
                        channel_projection_failure_t::cache_empty)));

    if (!cache.waveform.channel_projection.view.value().rows.empty()) {
        const auto& rows = cache.waveform.channel_projection.view.value().rows;
        flattened.view.compose.channel_waveform_rows.assign(rows.begin(), rows.end());
        flattened.view.compose.channel_waveform_source_lane_id = selection->source_lane;
    }
}

namespace {

auto bmson_projection_basis(const bmson_waveform_projection_context_t& context)
    -> bmson_projection_cache_t::basis_t {
    const auto& view = context.flattened.view.compose;
    const auto* state = context.interaction_state;
    const auto* chart = context.workspace.selected_chart
        ? context.history.read_chart(*context.workspace.selected_chart) : nullptr;
    const auto& timing = context.cache.timing.marker_timing_index;
    return {.owner = &context.cache, .target = &context.flattened,
        .document = &context.history.document, .chart = chart,
        .stamp = chart ? chart_stamp_t::from(context.history.document, chart->id) : chart_stamp_t{},
        .source_generation = context.audition_source_generation,
        .timing_fingerprint = timing ? timing->source_fingerprint : 0,
        .row_clock = context.cache.waveform.waveform_row_table.clock.value().generation,
        .epoch = context.flattened.compose.bmson_rows_epoch,
        .scroll = state ? state->viewport_scroll_pixels : 0,
        .height = view.viewport_height_pixels, .zoom = view.pixels_per_standard_measure,
        .cursor = state ? state->cursor.measure : measure_id_t{},
        .selected_bgm = view.selected_bmson_bgm_event_id.value,
        .selected_note = view.selected_bmson_note_event_id.value,
        .transport_asset = context.transport_waveform.asset,
        .channel_asset = context.channel_waveform.asset,
        .transport_analysis = context.transport_waveform.analysis,
        .channel_analysis = context.channel_waveform.analysis,
        .axis = state ? state->cut_axis : nullptr,
        .visible = view.visible, .scene = view.scene_available, .timing = timing.has_value(),
        .stems = static_cast<bool>(context.stem_analysis),
        .provided_visible = context.visible_stems.has_value()};
}

auto bmson_published_sizes(const bmson_waveform_projection_context_t& context)
    -> std::array<std::size_t, 3> {
    const auto& view = context.flattened.view.compose;
    return {view.stem_waveforms.size(), view.stem_cuts.size(), view.channel_waveform_rows.size()};
}

auto can_reuse_bmson_rows(const bmson_waveform_projection_context_t& context,
                         const bmson_projection_cache_t& retained,
                         const bmson_projection_cache_t::basis_t& basis,
                         std::span<const audio::decoded_source_binding_t> bindings) -> bool {
    if (!retained.basis || *retained.basis != basis ||
        retained.published_sizes != bmson_published_sizes(context) ||
        !std::ranges::equal(retained.bindings, bindings)) return false;
    if (context.visible_stems && !std::ranges::equal(retained.visible, *context.visible_stems))
        return false;
    return std::ranges::all_of(retained.analyses, [&context](const auto& source) {
        return stem_analysis_for(context, source.asset) == source.analysis;
    });
}

void prepare_bmson_row_clock(const bmson_waveform_projection_context_t& context) {
    if (!context.cache.timing.marker_timing_index || !context.interaction_state) return;
    const auto& compose = context.flattened.view.compose;
    (void)prepare_compose_display_row_table(context.cache,
        {.viewport_scroll_pixels = context.interaction_state->viewport_scroll_pixels,
         .viewport_height_pixels = compose.viewport_height_pixels,
         .pixels_per_standard_measure = compose.pixels_per_standard_measure,
         .cut_axis = context.interaction_state->cut_axis},
        {compose.project_id.value, context.cache.stamp().chart.value, context.history.document.revision});
}

} // namespace

void populate_bmson_waveform_projection(const bmson_waveform_projection_context_t& context) {
    ++context.cache.stems.waveform_projection_calls;
    prepare_bmson_row_clock(context);
    auto& storage = context.cache.stems.rows;
    if (!storage) storage = std::make_shared<bmson_projection_cache_t>();
    const auto basis = bmson_projection_basis(context);
    const auto bindings = stem_source_bindings(context);
    if (can_reuse_bmson_rows(context, *storage, basis, bindings)) {
        context.flattened.view.compose.channel_waveform_source_lane_id = storage->selected_source_lane;
        return;
    }
    // Store source evidence before projection helpers reuse their scratch buffers.
    storage->bindings.assign(bindings.begin(), bindings.end());
    storage->visible.clear();
    if (context.visible_stems)
        storage->visible.assign(context.visible_stems->begin(), context.visible_stems->end());
    storage->analyses.clear();
    build_bmson_waveform_projection(context);
    storage->basis = basis;
    storage->published_sizes = bmson_published_sizes(context);
    storage->selected_source_lane = context.flattened.view.compose.channel_waveform_source_lane_id;
    ++context.cache.stems.row_refills;
}

} // namespace wormedit::sys

#endif
