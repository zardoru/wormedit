#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/storyboard.h>
#include <wormedit/core/storyboard_preview.h>
#include <wormedit/core/osbx_lua_link.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
#include <memory>
#include <numbers>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <unordered_map>

namespace wormedit {
struct osbx_sample_index_t {
    storyboard_document_model_t model;
    storyboard_preview_index_t index;
};
namespace {

constexpr visual_affine_t identity{1, 0, 0, 1, 0, 0};

auto translation(double x, double y) -> visual_affine_t {
    return {1, 0, 0, 1, x, y};
}

auto bezier_component(double t, double p1, double p2) -> double {
    const auto u = 1 - t;
    return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
}

auto bezier_derivative(double t, double p1, double p2) -> double {
    const auto u = 1 - t;
    return 3 * u * u * p1 + 6 * u * t * (p2 - p1) + 3 * t * t * (1 - p2);
}

auto bezier_progress(const osbx_cubic_bezier_t& curve, double x) -> double {
    if (x == 0 || x == 1)
        return x;
    double low = 0, high = 1, t = x;
    // Safeguarded Newton, followed by bounded bisection, including flat tangents.
    for (int iteration = 0; iteration < 40; ++iteration) {
        const auto error = bezier_component(t, curve.x1, curve.x2) - x;
        if (std::abs(error) <= 1e-13)
            break;
        if (error < 0)
            low = t;
        else
            high = t;
        const auto derivative = bezier_derivative(t, curve.x1, curve.x2);
        const auto next = derivative > 1e-12 ? t - error / derivative : -1;
        t = iteration < 8 && next > low && next < high ? next : (low + high) / 2;
    }
    return bezier_component(t, curve.y1, curve.y2);
}

auto eased_progress(const osbx_easing_t& easing, double progress) -> double {
    if (std::holds_alternative<osbx_step_easing_t>(easing))
        return 0;
    if (const auto* osb = std::get_if<osbx_osb_easing_t>(&easing))
        return evaluate_storyboard_easing(osb->value, progress);
    if (const auto* curve = std::get_if<osbx_cubic_bezier_t>(&easing))
        return bezier_progress(*curve, progress);
    return progress;
}

template<class Value> auto key_time(const osbx_track_view_t<Value>& track, const osbx_key_t<Value>& key) -> storyboard_time_t {
    return track.clock ? *track.clock->map(key.time_ms) : storyboard_time_t{.milliseconds = key.time_ms};
}

template <class Value> auto upper_key(const osbx_track_view_t<Value>& track, storyboard_time_t time) {
    return std::upper_bound(track.keys.begin(), track.keys.end(), time, [&](auto value, const auto& key) {
        return osbx_clip_clock_t::is_before(value, key_time(track, key));
    });
}

auto sample_base(const osbx_track_view_t<std::vector<double>>& track, storyboard_time_t time)
    -> std::array<double, 6> {
    std::array<double, 6> result{};
    if (track.keys.empty()) {
        std::ranges::copy(*track.default_value, result.begin());
        return result;
    }
    const auto upper = upper_key(track, time);
    if (upper == track.keys.begin()) {
        std::ranges::copy(upper->value, result.begin());
        return result;
    }
    const auto& left = *std::prev(upper);
    std::ranges::copy(left.value, result.begin());
    if (upper == track.keys.end())
        return result;
    // In this bracket both differences are nonnegative and fit uint64, even
    // across the full signed clock range. Subtract before converting to double
    // so adjacent keys near INT64_MAX also work where long double is 64-bit.
    const auto a = key_time(track, left), b = key_time(track, *upper);
    const auto elapsed = static_cast<std::uint64_t>(time.milliseconds) - static_cast<std::uint64_t>(a.milliseconds);
    const auto duration = static_cast<std::uint64_t>(b.milliseconds) - static_cast<std::uint64_t>(a.milliseconds);
    const auto progress = (static_cast<double>(elapsed) + time.fraction() - a.fraction()) /
        (static_cast<double>(duration) + b.fraction() - a.fraction());
    const auto eased = eased_progress(left.easing, progress);
    for (std::size_t i = 0; i < left.value.size(); ++i)
        result[i] = std::lerp(left.value[i], upper->value[i], eased);
    return result;
}

template <class Value>
auto sample_discrete_base(const osbx_track_view_t<Value>& track, storyboard_time_t time) -> Value {
    if (track.keys.empty())
        return *track.default_value;
    const auto upper = upper_key(track, time);
    return upper == track.keys.begin() ? upper->value : std::prev(upper)->value;
}

struct sampling_context_t {
    const storyboard_trigger_scenario_t* scenario{};
    std::optional<failure_code_t> error;
};
template<class Value> struct program_selection_t {
    bool active{};
    std::span<const osbx_key_t<Value>> keys;
    storyboard_time_t query, start;
};
template<class Value> auto selected_program(std::span<const osbx_key_t<Value>> keys,
    storyboard_time_t query, const osbx_clip_clock_t& clock) -> result_t<program_selection_t<Value>, failure_code_t> {
    if (keys.empty()) return std::unexpected(failure_code_t::invalid_request);
    if (storyboard_time_t::is_before(query, {.milliseconds = keys.front().time_ms})) return program_selection_t<Value>{};
    const auto upper = std::upper_bound(keys.begin(), keys.end(), query,
        [](auto time, const auto& key) { return storyboard_time_t::is_before(time, {.milliseconds = key.time_ms}); });
    const auto start = clock.map(std::prev(upper)->time_ms);
    if (!start) return std::unexpected(start.error());
    return program_selection_t<Value>{.active = true, .keys = keys, .query = query, .start = *start};
}
template<class Value> auto loop_selection(const osbx_loop_t<Value>& loop, const osbx_clip_clock_t& clock,
    storyboard_time_t time) -> result_t<program_selection_t<Value>, failure_code_t> {
    const auto end = loop.end_time();
    const auto begin = clock.map(loop.start_ms);
    if (!end || !begin) return std::unexpected(failure_code_t::capacity_exceeded);
    if (storyboard_time_t::is_before(time, *begin)) return program_selection_t<Value>{};
    const auto mapped_end = clock.map(*end);
    if (!mapped_end) return std::unexpected(mapped_end.error());
    const auto period = static_cast<std::uint64_t>(loop.keys.back().time_ms);
    std::uint32_t iteration = loop.count - 1;
    storyboard_time_t query{.milliseconds = static_cast<std::int64_t>(period)};
    if (period && storyboard_time_t::is_before(time, *mapped_end)) {
        const auto local = clock.unmap(time);
        if (!local) return std::unexpected(local.error());
        const auto delta = static_cast<std::uint64_t>(local->milliseconds) - static_cast<std::uint64_t>(loop.start_ms);
        iteration = static_cast<std::uint32_t>(delta / period);
        query = {.milliseconds = static_cast<std::int64_t>(delta % period), .remainder = local->remainder, .denominator = local->denominator};
    }
    if (query.milliseconds < loop.keys.front().time_ms) {
        if (!iteration) return program_selection_t<Value>{};
        --iteration; query = {.milliseconds = static_cast<std::int64_t>(period)};
    }
    const auto start = loop.iteration_start(iteration);
    if (!start) return std::unexpected(start.error());
    const auto origin = clock.map(*start);
    if (!origin) return std::unexpected(origin.error());
    auto active_clock = clock; active_clock.origin = *origin;
    return selected_program(std::span{loop.keys}, query, active_clock);
}
auto map_program_bound(std::optional<std::int64_t> value, const osbx_clip_clock_t& clock)
    -> result_t<std::optional<storyboard_time_t>, failure_code_t> {
    if (!value) return std::optional<storyboard_time_t>{};
    const auto mapped = clock.map(*value);
    if (!mapped) return std::unexpected(mapped.error());
    return std::optional{*mapped};
}
template<class Value> auto trigger_selection(const osbx_trigger_t<Value>& trigger, const osbx_clip_clock_t& clock,
    storyboard_time_t time, const storyboard_trigger_scenario_t* scenario) -> result_t<program_selection_t<Value>, failure_code_t> {
    if (!scenario) return program_selection_t<Value>{};
    // Names/windows/groups were validated when the immutable index was built.
    // Only query-dependent arithmetic is checked here, never the name grammar.
    if (trigger.keys.empty()) return std::unexpected(failure_code_t::invalid_request);
    const auto begin = map_program_bound(trigger.trigger.start_time_ms, clock), end = map_program_bound(trigger.trigger.end_time_ms, clock);
    if (!begin || !end) return std::unexpected(failure_code_t::capacity_exceeded);
    auto deadline_clock = clock; deadline_clock.origin = time;
    const auto deadline = deadline_clock.map(-trigger.keys.front().time_ms);
    if (!deadline) return std::unexpected(deadline.error());
    const auto firing = scenario->latest(trigger.trigger.name, *begin, *end, *deadline);
    if (!firing) return program_selection_t<Value>{};
    auto active_clock = clock; active_clock.origin = *firing;
    const auto last = active_clock.map(trigger.keys.back().time_ms);
    if (!last) return std::unexpected(last.error());
    auto query = result_t<storyboard_time_t, failure_code_t>{storyboard_time_t{.milliseconds = trigger.keys.back().time_ms}};
    if (storyboard_time_t::is_before(time, *last)) query = active_clock.unmap(time);
    if (!query) return std::unexpected(query.error());
    return selected_program(std::span{trigger.keys}, *query, active_clock);
}
template<class Value> auto program_selection(const osbx_track_view_t<Value>& track, storyboard_time_t time,
    sampling_context_t& context) -> program_selection_t<Value> {
    program_selection_t<Value> selected;
    if (track.program.empty()) return selected;
    std::optional<storyboard_time_t> start;
    const auto upper = upper_key(track, time);
    if (upper != track.keys.begin()) start = key_time(track, *std::prev(upper));
    const auto clock = track.clock ? *track.clock : osbx_clip_clock_t{};
    for (const auto& block : track.program) {
        auto value = std::visit([&](const auto& data) -> result_t<program_selection_t<Value>, failure_code_t> {
            if constexpr (std::is_same_v<std::decay_t<decltype(data)>, osbx_loop_t<Value>>) return loop_selection(data, clock, time);
            else return trigger_selection(data, clock, time, context.scenario);
        }, block.data);
        if (!value) { context.error = value.error(); return {}; }
        if (value->active && (!start || !storyboard_time_t::is_before(value->start, *start))) {
            selected = *value; start = value->start;
        }
    }
    return selected;
}
auto sample(const osbx_track_view_t<std::vector<double>>& track, storyboard_time_t time, sampling_context_t& context)
    -> std::array<double, 6> {
    const auto selected = program_selection(track, time, context);
    if (!selected.active) return sample_base(track, time);
    return sample_base({.default_value = track.default_value, .keys = selected.keys}, selected.query);
}
template<class Value> auto sample_discrete(const osbx_track_view_t<Value>& track, storyboard_time_t time, sampling_context_t& context) -> Value {
    const auto selected = program_selection(track, time, context);
    if (!selected.active) return sample_discrete_base(track, time);
    return sample_discrete_base(osbx_track_view_t<Value>{.default_value = track.default_value, .keys = selected.keys}, selected.query);
}

auto compile_ops(std::span<const osbx_transform_op_t> ops, const osbx_clip_clock_t* clock = nullptr,
    const storyboard_time_t* begin = nullptr) -> std::vector<osbx_evaluation_op_t> {
    std::vector<osbx_evaluation_op_t> result;
    result.reserve(ops.size());
    for (const auto& op : ops)
        result.push_back({.id = op.id, .kind = op.kind,
            .track = {.default_value = &op.track.default_value, .keys = op.track.keys, .program = op.track.program, .clock = clock},
            .children = compile_ops(op.children, clock, begin), .begin = begin, .authored = !clock});
    return result;
}

auto evaluate_ops(std::span<const osbx_evaluation_op_t> ops, storyboard_time_t time, sampling_context_t& context) -> visual_affine_t;

auto evaluate_op(const osbx_evaluation_op_t& op, storyboard_time_t time, sampling_context_t& context) -> visual_affine_t {
    if (op.begin && osbx_clip_clock_t::is_before(time, *op.begin)) return identity;
    const auto v = sample(op.track, time, context);
    constexpr auto radians = std::numbers::pi / 180;
    switch (op.kind) {
    case osbx_transform_kind_t::translate:
        return translation(v[0], v[1]);
    case osbx_transform_kind_t::rotate: {
        const auto angle = v[0] * radians;
        const auto c = std::cos(angle), s = std::sin(angle);
        return {c, s, -s, c, 0, 0};
    }
    case osbx_transform_kind_t::scale:
        return {v[0], 0, 0, v[1], 0, 0};
    case osbx_transform_kind_t::skew:
        return {1, std::tan(v[1] * radians), std::tan(v[0] * radians), 1, 0, 0};
    case osbx_transform_kind_t::matrix:
        return {v};
    case osbx_transform_kind_t::pivot:
        return translation(v[0], v[1]) * evaluate_ops(op.children, time, context) *
               translation(-v[0], -v[1]);
    }
    return identity;
}

auto evaluate_ops(std::span<const osbx_evaluation_op_t> ops, storyboard_time_t time, sampling_context_t& context) -> visual_affine_t {
    auto result = identity;
    for (const auto& op : ops)
        result = evaluate_op(op, time, context) * result;
    return result;
}

auto contains_op(std::span<const osbx_evaluation_op_t> ops, std::string_view id) -> bool {
    return std::ranges::any_of(ops, [&](const auto& op) { return op.authored && (op.id == id || contains_op(op.children, id)); });
}
auto query_op_space(std::span<const osbx_evaluation_op_t> ops, std::string_view node,
    std::string_view id, storyboard_time_t time, visual_affine_t world_side, sampling_context_t& context)
    -> result_t<osbx_op_space_t, failure_code_t> {
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        if (it->authored && it->id == id) {
            osbx_op_space_t result{.node = node, .op = it->id, .kind = it->kind, .world_side = world_side,
                .values = sample(it->track, time, context), .component_count = it->track.default_value->size()};
            if (!world_side.is_finite() || !std::ranges::all_of(result.components(), [](double v) { return std::isfinite(v); }))
                return std::unexpected(failure_code_t::invalid_geometry);
            return result;
        }
        if (it->authored && it->kind == osbx_transform_kind_t::pivot && contains_op(it->children, id)) {
            const auto pivot = sample(it->track, time, context);
            return query_op_space(it->children, node, id, time, world_side * translation(pivot[0], pivot[1]), context);
        }
        world_side = world_side * evaluate_op(*it, time, context);
    }
    return std::unexpected(failure_code_t::not_found);
}

// Iterative parent traversal also handles hierarchies deeper than the C++ stack.
auto parent_order(const osbx_document_t& document) -> std::vector<std::size_t> {
    std::unordered_map<std::string_view, std::size_t> by_id;
    for (std::size_t i = 0; i < document.nodes.size(); ++i)
        by_id.emplace(document.nodes[i].id, i);
    std::vector<bool> visited(document.nodes.size());
    std::vector<std::size_t> result, path;
    result.reserve(document.nodes.size());
    for (std::size_t i = 0; i < document.nodes.size(); ++i) {
        path.clear();
        auto current = i;
        while (!visited[current]) {
            visited[current] = true;
            path.push_back(current);
            const auto& parent = document.nodes[current].parent;
            if (!parent)
                break;
            current = by_id.at(*parent);
        }
        result.insert(result.end(), path.rbegin(), path.rend());
    }
    return result;
}

auto evaluate_node(const osbx_evaluation_node_t& node, const osbx_node_frame_t& parent,
                   storyboard_time_t time, sampling_context_t& context) -> osbx_node_frame_t {
    osbx_node_frame_t state{
        .matrix = parent.matrix * evaluate_ops(node.ops, time, context),
        .opacity = parent.opacity * sample(node.opacity, time, context)[0],
        .visible = parent.visible && sample_discrete(node.visibility, time, context) &&
            (!node.source->lifetime || node.source->lifetime->contains(time)),
    };
    const auto color = sample(node.color, time, context);
    for (std::size_t i = 0; i < state.color.size(); ++i)
        state.color[i] = parent.color[i] * color[i];
    for (const auto& clip : node.clips) {
        if (osbx_clip_clock_t::is_before(time, clip.source->begin)) continue;
        state.opacity *= sample(clip.opacity, time, context)[0];
        const auto multiplier = sample(clip.color, time, context);
        for (std::size_t i = 0; i < state.color.size(); ++i) state.color[i] *= multiplier[i];
        state.visible = state.visible && sample_discrete(clip.visibility, time, context);
    }
    return state;
}

auto node_blend(const osbx_evaluation_node_t& node, storyboard_time_t time, sampling_context_t& context) -> osbx_blend_t {
    auto value = sample_discrete(node.blend, time, context);
    for (const auto& clip : node.clips)
        if (clip.blend && !osbx_clip_clock_t::is_before(time, clip.source->begin)) value = sample_discrete(*clip.blend, time, context);
    return value;
}

template <class T>
auto allocate_frame(frame_arena_t& arena, std::size_t count)
    -> result_t<std::span<T>, failure_code_t> {
    if (count == 0)
        return std::span<T>{};
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
        return std::unexpected(failure_code_t::capacity_exceeded);
    auto memory = arena.allocate(count * sizeof(T), alignof(T));
    if (!memory)
        return std::unexpected(memory.error());
    return std::span<T>{static_cast<T*>(*memory), count};
}

auto evaluate_node_state(const osbx_evaluation_node_t& node, const osbx_node_frame_t& parent,
    storyboard_time_t time, std::span<osbx_frame_diagnostic_t> diagnostics,
    std::size_t& diagnostic_count, osbx_frame_options_t options) -> osbx_node_frame_t {
    sampling_context_t context{.scenario = options.scenario};
    auto state = evaluate_node(node, parent, time, context);
    const auto& source = *node.source;
    if (context.error) {
        state.visible = false;
        std::construct_at(&diagnostics[diagnostic_count++], osbx_frame_diagnostic_t{.code = *context.error,
            .id = source.id, .message = "Temporal program clock/query failed"});
    } else if ((source.kind == osbx_node_kind_t::animation && !source.animation) ||
               (source.kind == osbx_node_kind_t::sample && (!source.sample || !node.sample_refusal.empty()))) {
        state.visible = false;
        std::construct_at(&diagnostics[diagnostic_count++], osbx_frame_diagnostic_t{
            .code = failure_code_t::unsupported, .id = source.id,
            .message = node.sample_refusal.empty() ? "Incomplete OSBX animation/sample metadata" : node.sample_refusal});
    } else if (!state.is_finite()) {
        state.visible = false;
        std::construct_at(&diagnostics[diagnostic_count++],
                          osbx_frame_diagnostic_t{
                              .code = failure_code_t::arithmetic_overflow,
                              .id = source.id,
                              .message = "OSBX evaluated transform or properties are non-finite; node skipped"});
    }
    return state;
}

auto draw_instance(const osbx_node_t& node, const osbx_node_frame_t& state,
    osbx_blend_t blend, storyboard_time_t time) -> osbx_draw_instance_t {
    const auto epoch = node.lifetime ? node.lifetime->begin_ms : 0;
    return {.id = node.id, .world = state.matrix, .opacity = state.opacity,
        .color = state.color, .blend = blend, .image = *node.image, .origin = node.origin,
        .frame_index = node.animation ? node.animation->frame_index_at(time, epoch) : std::nullopt};
}

void build_frame(const osbx_evaluation_index_t& index, storyboard_time_t time,
                 std::span<osbx_node_frame_t> states, std::span<osbx_draw_instance_t> instances,
                 std::span<osbx_frame_diagnostic_t> diagnostics, osbx_frame_effect_t& result, osbx_frame_options_t options) {
    std::size_t diagnostic_count = 0, instance_count = 0;
    for (std::size_t i = 0; i < index.nodes.size(); ++i) {
        const auto& node = index.nodes[i];
        const auto state = evaluate_node_state(node, node.parent ? states[*node.parent] : osbx_node_frame_t{},
            time, diagnostics, diagnostic_count, options);
        std::construct_at(&states[i], state);
    }
    for (const auto i : index.draw_order) {
        const auto& node = index.nodes[i];
        const auto& state = states[i];
        const auto layer = node.source->layer;
        const auto selected = (layer != osbx_layer_t::pass || options.conditional_layer == storyboard_conditional_layer_t::pass) &&
                              (layer != osbx_layer_t::fail || options.conditional_layer == storyboard_conditional_layer_t::fail);
        if (!state.visible || (!selected && !options.include_all_conditional_layers) ||
            (node.source->kind != osbx_node_kind_t::sprite && node.source->kind != osbx_node_kind_t::animation))
            continue;
        sampling_context_t context{.scenario = options.scenario};
        const auto blend = node_blend(node, time, context);
        if (context.error) {
            std::construct_at(&diagnostics[diagnostic_count++], osbx_frame_diagnostic_t{.code = *context.error,
                .id = node.source->id, .message = "Temporal blend clock/query failed"});
            continue;
        }
        std::construct_at(&instances[instance_count++], draw_instance(*node.source, state, blend, time));
    }
    result.instances = instances.first(instance_count);
    result.diagnostics = diagnostics.first(diagnostic_count);
    result.node_states = states;
}

} // namespace

auto osbx_node_frame_t::is_finite() const -> bool {
    const auto is_finite_value = [](double value) { return std::isfinite(value); };
    return is_finite_value(opacity) && matrix.is_finite() && std::ranges::all_of(color, is_finite_value);
}

auto osbx_frame_effect_t::node_state(const osbx_evaluation_index_t& index, std::string_view id) const
    -> const osbx_node_frame_t* {
    if (source_nodes != index.nodes.data() || arena_error || node_states.size() != index.nodes.size()) return nullptr;
    const auto found = std::ranges::find_if(index.nodes, [id](const auto& node) { return node.source->id == id; });
    if (found == index.nodes.end() || std::ranges::find(diagnostics, id, &osbx_frame_diagnostic_t::id) != diagnostics.end())
        return nullptr;
    const auto& state = node_states[static_cast<std::size_t>(found - index.nodes.begin())];
    return state.is_finite() ? &state : nullptr;
}

auto osbx_frame_effect_t::op_space(const osbx_evaluation_index_t& index, std::string_view node,
    std::string_view op, storyboard_time_t requested_time) const -> result_t<osbx_op_space_t, failure_code_t> {
    if (source_nodes != index.nodes.data() || time != requested_time || !requested_time.is_valid())
        return std::unexpected(failure_code_t::stale_generation);
    if (!is_success() || !node_state(index, node)) return std::unexpected(failure_code_t::unavailable);
    const auto found = std::ranges::find_if(index.nodes, [&](const auto& entry) { return entry.source->id == node; });
    const auto* parent = found->parent ? &node_states[*found->parent] : nullptr;
    sampling_context_t context{.scenario = scenario};
    auto result = query_op_space(found->ops, found->source->id, op, time, parent ? parent->matrix : identity, context);
    if (context.error) return std::unexpected(*context.error);
    return result;
}

namespace {
template<class Value> auto clip_track(const osbx_track_t<Value>& track, const osbx_clip_clock_t& clock)
    -> osbx_track_view_t<Value> {
    return {.default_value = &track.default_value, .keys = track.keys, .program = track.program, .clock = &clock};
}
void append_compiled_clips(osbx_evaluation_index_t& index, const osbx_node_t& source,
    const osbx_document_t& document) {
    auto& node = index.nodes.back();
    const auto position = static_cast<std::size_t>(&source - document.nodes.data());
    const auto first = std::ranges::lower_bound(*index.compiled_owner, position, {}, &osbx_compiled_clip_t::node_index);
    const auto last = std::ranges::upper_bound(*index.compiled_owner, position, {}, &osbx_compiled_clip_t::node_index);
    for (auto current = first; current != last; ++current) {
        const auto& clip = *current;
        auto ops = compile_ops(clip.ops, &clip.clock, &clip.begin);
        node.ops.insert(node.ops.end(), std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
        osbx_evaluation_clip_t value{.source = &clip, .opacity = clip_track(clip.tracks.opacity, clip.clock),
            .color = clip_track(clip.tracks.color, clip.clock), .visibility = clip_track(clip.tracks.visibility, clip.clock)};
        if (clip.tracks.blend) value.blend = clip_track(*clip.tracks.blend, clip.clock);
        node.clips.push_back(value);
    }
}
auto clip_op_count(std::span<const osbx_transform_op_t> ops) -> std::size_t {
    std::size_t count = ops.size();
    for (const auto& op : ops) count += clip_op_count(op.children);
    return count;
}
} // namespace

auto osbx_evaluation_index_t::clip_frame_work() const -> std::size_t {
    std::size_t work{};
    for (const auto& node : nodes) {
        for (const auto& clip : node.clips) {
            // A size_t-bounded track needs at most 64 bracket comparisons,
            // endpoint maps, interpolation and activation/override queries.
            const auto count = clip_op_count(clip.source->ops) + 4;
            if (count > (std::numeric_limits<std::size_t>::max() - work) / 256)
                return std::numeric_limits<std::size_t>::max();
            work += count * 256;
        }
    }
    return work;
}

namespace {
auto op_program_count(std::span<const osbx_evaluation_op_t> ops) -> std::size_t {
    std::size_t count{};
    for (const auto& op : ops) count += op.track.program.size() + op_program_count(op.children);
    return count;
}
} // namespace

auto osbx_evaluation_index_t::program_frame_work() const -> std::size_t {
    std::size_t work{};
    const auto charge = [&](std::size_t count) {
        if (count > (SIZE_MAX - work) / 512) { work = SIZE_MAX; return; }
        work += count * 512; // Bounded exact arithmetic and binary key/firing queries.
    };
    for (const auto& node : nodes) {
        charge(op_program_count(node.ops) + node.opacity.program.size() + node.color.program.size() +
            node.visibility.program.size() + node.blend.program.size());
        for (const auto& clip : node.clips)
            charge(clip.opacity.program.size() + clip.color.program.size() + clip.visibility.program.size() +
                (clip.blend ? clip.blend->program.size() : 0));
    }
    return work;
}

namespace {
template<class T> auto is_inert_sample_track(const osbx_track_t<T>& track, const T& value) -> bool {
    return track.keys.empty() && track.program.empty() && track.default_value == value && !track.lua && !track.parameter;
}
auto sample_effect_refusal(const osbx_node_t& node) -> std::string_view {
    if (!node.ops.empty() || !node.uses.empty() || node.lifetime)
        return "Sample transforms, clips and lifetime gates cannot be represented by an instantaneous OSB cue";
    if (!is_inert_sample_track(node.tracks.opacity, std::vector<double>{1}) ||
        !is_inert_sample_track(node.tracks.color, std::vector<double>{1,1,1,1}) ||
        !is_inert_sample_track(node.tracks.visibility, true) ||
        !is_inert_sample_track(node.tracks.blend, osbx_blend_t::normal))
        return "Sample property tracks or temporal programs are unsupported; no cue was scheduled";
    return {};
}
auto compile_sample_index(osbx_evaluation_index_t& index, const osbx_document_t& document,
    osbx_validation_options_t options) -> bool {
    if (std::ranges::none_of(index.nodes, [](const auto& node) { return node.source->sample.has_value(); })) return true;
    const auto cost = osbx_clip_expansion_cost_t::from(document, options);
    if (!cost.value) return false;
    if (cost.value->storage > options.maximum_expanded_storage_bytes) return false;
    auto remaining = options.maximum_expanded_storage_bytes - cost.value->storage;
    if (sizeof(osbx_sample_index_t) > remaining) return false;
    remaining -= sizeof(osbx_sample_index_t);
    for (const auto& node : index.nodes) {
        if (!node.source->sample || !node.sample_refusal.empty()) continue;
        const auto path_bytes = node.source->sample->path.size();
        if (path_bytes > remaining / 3) return false;
        remaining -= path_bytes * 3; // Owned model, captured cue and construction/move headroom.
        const auto bytes = sizeof(storyboard_sample_t) * 3 +
            sizeof(storyboard_event_t) * 3 + sizeof(storyboard_preview_sample_source_t);
        if (bytes > remaining) return false;
        remaining -= bytes;
    }
    auto owner = std::make_shared<osbx_sample_index_t>();
    for (const auto i : index.draw_order) {
        const auto& node = index.nodes[i];
        if (!node.source->sample || !node.sample_refusal.empty()) continue;
        const auto& sample = *node.source->sample;
        owner->model.samples.push_back({.stable_id = i + 1, .source_order = owner->model.samples.size() + 1,
            .time_ms = sample.time_ms, .layer = node.source->layer, .path = sample.path, .volume = sample.volume});
    }
    auto built = storyboard_preview_index_t::from(owner->model);
    if (!built.value) return false;
    owner->index = std::move(*built.value);
    index.sample_owner = std::move(owner);
    return true;
}
} // namespace

auto osbx_evaluation_index_t::sample_schedule() const -> const storyboard_schedule_t& {
    static const storyboard_schedule_t empty;
    return sample_owner ? sample_owner->index.schedule : empty;
}
auto osbx_evaluation_index_t::sample_for(const storyboard_event_t& event,
    storyboard_conditional_layer_t choice) const -> const storyboard_sample_t* {
    return sample_owner ? sample_owner->index.sample_for(sample_owner->model, event, choice) : nullptr;
}

auto osbx_evaluation_index_t::from(const osbx_document_t& source,
                                   osbx_validation_options_t options)
    -> attempt_t<osbx_evaluation_index_t, std::vector<osbx_diagnostic_t>> {
    auto validation = validate_osbx_document(source, options);
    if (!validation.value) return {.diagnostics = std::move(validation.diagnostics)};
    auto prepared = osbx_lua_prepared_scope_t::find(source);
    if (has_osbx_lua_links(source) && !prepared)
        return {.diagnostics = osbx_lua_prepared_scope_t::failure(source)};
    const auto& document = prepared ? *prepared : source;
    if (prepared) validation = validate_osbx_document(document, options);
    if (!validation.value)
        return {.diagnostics = std::move(validation.diagnostics)};
    auto compiled = compile_osbx_clips(document, options);
    if (!compiled.value) return {.diagnostics = std::move(compiled.diagnostics)};
    osbx_evaluation_index_t index;
    index.compiled_owner = std::make_shared<const std::vector<osbx_compiled_clip_t>>(std::move(*compiled.value));
    index.prepared_owner = std::move(prepared);
    std::unordered_map<std::string_view, std::size_t> positions;
    const auto order = parent_order(document);
    index.nodes.reserve(order.size());
    for (const auto i : order) {
        const auto& node = document.nodes[i];
        const auto parent = node.parent ? std::optional{positions.at(*node.parent)} : std::nullopt;
        positions.emplace(node.id, index.nodes.size());
        index.nodes.push_back(
            {.source = &node,
             .parent = parent,
             .ops = compile_ops(node.ops),
             .opacity = osbx_track_view_t<std::vector<double>>::from(node.tracks.opacity),
             .color = osbx_track_view_t<std::vector<double>>::from(node.tracks.color),
             .visibility = osbx_track_view_t<bool>::from(node.tracks.visibility),
             .blend = osbx_track_view_t<osbx_blend_t>::from(node.tracks.blend)});
        append_compiled_clips(index, node, document);
        auto& added = index.nodes.back();
        added.sample_refusal = sample_effect_refusal(node);
        if (parent && !index.nodes[*parent].sample_refusal.empty())
            added.sample_refusal = index.nodes[*parent].sample_refusal;
    }
    index.draw_order.resize(order.size());
    std::iota(index.draw_order.begin(), index.draw_order.end(), std::size_t{0});
    std::ranges::sort(index.draw_order, {}, [&](std::size_t i) {
        const auto& node = *index.nodes[i].source;
        return std::tuple{node.layer, node.order, order[i]};
    });
    if (!compile_sample_index(index, document, options))
        return {.diagnostics = {{.code = osbx_diagnostic_code_t::clip_expansion_limit,
            .message = "OSBX Sample schedule exceeds shared storage bounds"}}};
    return {.value = std::move(index)};
}

auto sample_osbx_numeric_track(const osbx_track_t<>& track, std::int64_t time_ms, const storyboard_trigger_scenario_t* scenario) -> std::vector<double> {
    return sample_osbx_numeric_track(track, storyboard_time_t{.milliseconds = time_ms}, scenario);
}

auto sample_osbx_numeric_track(const osbx_track_t<>& track, storyboard_time_t time, const storyboard_trigger_scenario_t* scenario) -> std::vector<double> {
    if (!time.is_valid()) return {};
    sampling_context_t context{.scenario = scenario};
    const auto value = sample(osbx_track_view_t<std::vector<double>>::from(track), time, context);
    if (context.error) return {};
    return {value.begin(), value.begin() + static_cast<std::ptrdiff_t>(track.default_value.size())};
}

auto sample_osbx_blend_track(const osbx_track_t<osbx_blend_t>& track, storyboard_time_t time, const storyboard_trigger_scenario_t* scenario)
    -> result_t<osbx_blend_t, failure_code_t> {
    if (!time.is_valid()) return std::unexpected(failure_code_t::invalid_request);
    sampling_context_t context{.scenario = scenario};
    const auto value = sample_discrete(osbx_track_view_t<osbx_blend_t>::from(track), time, context);
    if (context.error) return std::unexpected(*context.error);
    return value;
}

auto sample_osbx_visibility_track(const osbx_track_t<bool>& track, storyboard_time_t time, const storyboard_trigger_scenario_t* scenario)
    -> result_t<bool, failure_code_t> {
    if (!time.is_valid()) return std::unexpected(failure_code_t::invalid_request);
    sampling_context_t context{.scenario = scenario};
    const auto value = sample_discrete(osbx_track_view_t<bool>::from(track), time, context);
    if (context.error) return std::unexpected(*context.error);
    return value;
}

auto evaluate_osbx_frame(const osbx_evaluation_index_t& index, std::int64_t time_ms,
                         frame_arena_t& arena, osbx_frame_options_t options) -> osbx_frame_effect_t {
    return evaluate_osbx_frame(index, storyboard_time_t{.milliseconds = time_ms}, arena, options);
}

auto evaluate_osbx_frame(const osbx_evaluation_index_t& index, storyboard_time_t time,
    frame_arena_t& arena, osbx_frame_options_t options) -> osbx_frame_effect_t {
    arena.reset();
    if (!time.is_valid()) return {.arena_error = failure_code_t::invalid_request};
    const auto count = index.nodes.size();
    auto states = allocate_frame<osbx_node_frame_t>(arena, count);
    if (!states)
        return {.arena_error = states.error()};
    auto instances = allocate_frame<osbx_draw_instance_t>(arena, count);
    if (!instances)
        return {.arena_error = instances.error()};
    auto diagnostics = allocate_frame<osbx_frame_diagnostic_t>(arena, count);
    if (!diagnostics)
        return {.arena_error = diagnostics.error()};
    osbx_frame_effect_t result{.source_nodes = index.nodes.data(), .time = time, .scenario = options.scenario};
    build_frame(index, time, *states, *instances, *diagnostics, result, options);
    return result;
}

} // namespace wormedit
