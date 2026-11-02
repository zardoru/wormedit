#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/keyframe_simplify.h>
#include <wormedit/core/osbx_export_metric.h>
#include <wormedit/core/storyboard_preview.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <unordered_map>
#include <type_traits>

namespace wormedit {
namespace {
using numeric_track_t = osbx_track_t<>;
using values_t = std::array<double, 3>;
constexpr std::array opcodes{storyboard_opcode_t::move, storyboard_opcode_t::rotate,
                             storyboard_opcode_t::vector_scale, storyboard_opcode_t::fade,
                             storyboard_opcode_t::color};
constexpr std::array policies{simplify_position, simplify_rotation, simplify_scale,
                              simplify_opacity, simplify_color};
constexpr std::array<std::uint8_t, 5> arities{2, 1, 2, 1, 3};
constexpr double radians = std::numbers::pi / 180;
constexpr std::string_view unsupported_node_kind = "OSBX-10 requires complete Animation/Sample metadata";
constexpr std::string_view no_equivalent =
    ": no .osb equivalent: remove the op, or export video / StepMania instead";

template <class T> auto is_constant(const osbx_track_t<T>& track) -> bool {
    return track.program.empty() && (track.keys.empty() || std::ranges::all_of(track.keys, [&](const auto& key) {
        return key.value == track.keys.front().value;
    }));
}

auto forbidden_op(std::span<const osbx_transform_op_t> ops) -> const osbx_transform_op_t* {
    for (const auto& op : ops) {
        if (op.kind == osbx_transform_kind_t::skew || op.kind == osbx_transform_kind_t::matrix)
            return &op;
        if (auto* found = forbidden_op(op.children))
            return found;
    }
    return nullptr;
}

auto ancestry(const osbx_evaluation_index_t& index, std::size_t sprite)
    -> std::vector<const osbx_node_t*> {
    std::vector<const osbx_node_t*> result;
    for (auto current = std::optional{sprite}; current; current = index.nodes[*current].parent)
        result.push_back(index.nodes[*current].source);
    return result;
}

void collect_ops(std::span<const osbx_transform_op_t> ops,
                 std::vector<const osbx_transform_op_t*>& result) {
    for (const auto& op : ops) {
        result.push_back(&op);
        collect_ops(op.children, result);
    }
}

template <class T> void collect_times(const osbx_track_t<T>& track, std::vector<std::int64_t>& times) {
    for (const auto& key : track.keys) times.push_back(key.time_ms);
    for (const auto& block : track.program) {
        if (const auto* loop = std::get_if<osbx_loop_t<T>>(&block.data)) {
            times.push_back(loop->start_ms);
            if (const auto end = loop->end_time()) times.push_back(*end);
        } else {
            const auto& trigger = std::get<osbx_trigger_t<T>>(block.data);
            const auto& header = trigger.trigger;
            if (header.start_time_ms) times.push_back(*header.start_time_ms);
            if (header.end_time_ms) {
                times.push_back(*header.end_time_ms);
                const auto end = osbx_clip_clock_t{.origin = {.milliseconds = *header.end_time_ms}}.map(trigger.keys.empty() ? 0 : trigger.keys.back().time_ms);
                if (end) times.push_back(end->milliseconds);
            }
        }
    }
}

struct source_plan_t {
    std::vector<const osbx_node_t*> chain;
    std::vector<const osbx_transform_op_t*> ops;
    std::vector<std::int64_t> times;
    std::array<const numeric_track_t*, 5> animated{};
    bool exact{true};
    bool has_clips{};
    bool has_program{};
    bool has_clip_trigger{};
    std::string clip_failure;
    bool canonical_transform{true};
    [[nodiscard]] auto has_constant_values() const -> bool {
        return !has_clips && !has_program && std::ranges::all_of(ops, [](const auto* op) { return is_constant(op->track); }) &&
               std::ranges::all_of(chain, [](const auto* node) {
                   return is_constant(node->tracks.opacity) && is_constant(node->tracks.color) &&
                          is_constant(node->tracks.visibility) && is_constant(node->tracks.blend);
               });
    }
};

auto has_native_easing(const numeric_track_t& track) -> bool {
    return std::ranges::none_of(track.keys, [](const auto& key) {
        return std::holds_alternative<osbx_cubic_bezier_t>(key.easing);
    });
}

void add_track(source_plan_t& plan, const numeric_track_t& track, std::size_t property) {
    collect_times(track, plan.times);
    plan.has_program = plan.has_program || !track.program.empty();
    if (is_constant(track))
        return;
    plan.exact = plan.exact && has_native_easing(track) && !plan.animated[property];
    plan.animated[property] = &track;
}

auto native_property(osbx_transform_kind_t kind) -> std::optional<std::size_t> {
    switch (kind) {
    case osbx_transform_kind_t::translate: return 0;
    case osbx_transform_kind_t::rotate: return 1;
    case osbx_transform_kind_t::scale: return 2;
    default: return {};
    }
}
void plan_transform(source_plan_t& plan) {
    int previous = -1;
    bool animated = false;
    for (const auto* op : plan.ops) {
        collect_times(op->track, plan.times);
        plan.has_program = plan.has_program || !op->track.program.empty();
        animated = animated || !is_constant(op->track);
        const auto property = native_property(op->kind);
        const int rank = property ? 2 - static_cast<int>(*property) : 3;
        if (rank < previous || rank == 3) plan.exact = false;
        previous = rank;
        if (rank < 3) add_track(plan, op->track, static_cast<std::size_t>(2 - rank));
    }
    plan.canonical_transform = plan.exact;
    // Any shear-free static composition is exactly one OSB transform.
    if (!animated) plan.exact = true;
}

template<class Value> void collect_clip_times(source_plan_t& plan, const osbx_track_t<Value>& track,
    const osbx_clip_clock_t& clock) {
    plan.has_program = plan.has_program || !track.program.empty();
    plan.has_clip_trigger = plan.has_clip_trigger || std::ranges::any_of(track.program, [](const auto& block) {
        return std::holds_alternative<osbx_trigger_t<Value>>(block.data);
    });
    std::vector<std::int64_t> times; collect_times(track, times);
    for (const auto time : times) {
        const auto mapped = clock.map(time);
        if (!mapped || mapped->remainder) { plan.clip_failure = "Clip fractional key times cannot be represented by integer OSB commands"; return; }
        plan.times.push_back(mapped->milliseconds);
    }
}
void add_clip_plan(source_plan_t& plan, const osbx_compiled_clip_t& clip) {
    plan.has_clips = true; plan.exact = false; plan.canonical_transform = false;
    if (clip.begin.remainder) { plan.clip_failure = "Clip fractional activation cannot be represented by integer OSB commands"; return; }
    plan.times.push_back(clip.begin.milliseconds);
    if (clip.begin.milliseconds != INT64_MIN) plan.times.push_back(clip.begin.milliseconds - 1);
    const auto start = plan.ops.size();
    collect_ops(clip.ops, plan.ops);
    for (std::size_t i = start; i < plan.ops.size(); ++i) collect_clip_times(plan, plan.ops[i]->track, clip.clock);
    if (const auto* op = forbidden_op(clip.ops)) plan.clip_failure = "Clip op " + op->id + std::string(no_equivalent);
    collect_clip_times(plan, clip.tracks.opacity, clip.clock);
    collect_clip_times(plan, clip.tracks.color, clip.clock);
    collect_clip_times(plan, clip.tracks.visibility, clip.clock);
    if (clip.tracks.blend) collect_clip_times(plan, *clip.tracks.blend, clip.clock);
}
void add_compiled_plan(source_plan_t& plan, const osbx_evaluation_index_t& index) {
    for (const auto* source : plan.chain) {
        const auto found = std::ranges::find_if(index.nodes, [&](const auto& node) { return node.source == source; });
        for (const auto& clip : found->clips) add_clip_plan(plan, *clip.source);
    }
}

auto make_plan(const osbx_evaluation_index_t& index, std::size_t sprite,
               const std::optional<osbx_osb_span_t>& playback_span) -> source_plan_t {
    source_plan_t plan{.chain = ancestry(index, sprite)};
    for (const auto* node : plan.chain)
        collect_ops(node->ops, plan.ops);
    plan_transform(plan);
    for (const auto* node : plan.chain) {
        add_track(plan, node->tracks.opacity, 3);
        add_track(plan, node->tracks.color, 4);
        // Animated alpha and opacity multiply, and cannot share an OSB fade span.
        if (!is_constant(node->tracks.color) &&
            std::ranges::any_of(node->tracks.color.keys, [&](const auto& key) {
                return key.value[3] != node->tracks.color.keys.front().value[3];
            })) plan.exact = false;
        if (node != plan.chain.front() &&
            (!is_constant(node->tracks.opacity) || !is_constant(node->tracks.color)))
            plan.exact = false;
        plan.has_program = plan.has_program || !node->tracks.visibility.program.empty() || !node->tracks.blend.program.empty();
        collect_times(node->tracks.visibility, plan.times);
        collect_times(node->tracks.blend, plan.times);
        if (!is_constant(node->tracks.visibility)) plan.exact = false;
    }
    add_compiled_plan(plan, index);
    if (playback_span) {
        plan.times.push_back(playback_span->begin_time_ms);
        plan.times.push_back(playback_span->end_time_ms);
    }
    std::ranges::sort(plan.times);
    const auto unique = std::ranges::unique(plan.times);
    plan.times.erase(unique.begin(), unique.end());
    if (plan.times.empty()) plan.times.push_back(0);
    return plan;
}

auto lifetime_reason(std::span<const osbx_node_t* const> chain) -> std::string {
    for (const auto* node : chain)
        if (node != chain.front() && node->lifetime)
            return "Ancestor lifetime clipping is unsupported; flatten the hierarchy explicitly";
    const auto& node = *chain.front();
    if (node.lifetime && !is_constant(node.tracks.visibility))
        return "Lifetime combined with animated visibility is unsupported";
    return {};
}
auto apply_lifetime(source_plan_t& plan) -> std::string {
    if (auto reason = lifetime_reason(plan.chain); !reason.empty()) return reason;
    const auto& node = *plan.chain.front();
    if (!node.lifetime) return {};
    const auto range = *node.lifetime;
    std::erase_if(plan.times, [&](auto time) { return time < range.begin_ms || time > range.end_ms; });
    plan.times.push_back(range.begin_ms);
    plan.times.push_back(range.end_ms);
    std::ranges::sort(plan.times);
    const auto unique = std::ranges::unique(plan.times);
    plan.times.erase(unique.begin(), unique.end());
    // Sampling includes clipped endpoints even when they fall inside an op's
    // segment. Do not label that restriction a direct native-key translation.
    plan.exact = false;
    return {};
}

auto preflight(std::span<const osbx_node_t* const> chain) -> std::string {
    for (const auto* node : chain) {
        if (const auto* op = forbidden_op(node->ops))
            return "node " + chain.front()->id + ", op " + op->id + ", owner " + node->id +
                   std::string(no_equivalent);
        if ((node->kind == osbx_node_kind_t::animation && !node->animation) || node->kind == osbx_node_kind_t::sample)
            return "node " + node->id + ": OSBX-10 requires complete visual Animation metadata";
    }
    if (!is_constant(chain.front()->tracks.blend))
        return "Animated blend parameters are unsupported by this converter";
    return {};
}

auto constant_linear(const osbx_transform_op_t& op) -> visual_affine_t {
    const auto& value = op.track.keys.empty() ? op.track.default_value : op.track.keys.front().value;
    if (op.kind == osbx_transform_kind_t::scale)
        return visual_affine_t::from(0, 0, value[0], value[1]);
    if (op.kind == osbx_transform_kind_t::rotate)
        return visual_affine_t::from(0, 0, 1, 1, value[0] * radians);
    return {}; // Translation/pivot positions do not change the linear part.
}

auto normalized_column_dot(const visual_affine_t& m) -> double {
    const auto sx = std::hypot(m[0], m[1]), sy = std::hypot(m[2], m[3]);
    if (sx == 0 || sy == 0) return 0;
    return (m[0] / sx) * (m[2] / sy) + (m[1] / sx) * (m[3] / sy);
}

// For L * R(theta) * B with fixed L/B, the unnormalized column dot is
// A + B*cos(2*theta) + C*sin(2*theta). Three angles determine whether it
// vanishes identically, including cancelling anisotropic scales. Otherwise
// every nonconstant continuous angle span contains shear, even if all integer
// samples happen to land on axial rotations.
auto analytic_shear(const source_plan_t& plan) -> std::string {
    const osbx_transform_op_t* rotating = nullptr;
    visual_affine_t left, right;
    for (const auto* op : plan.ops) {
        const bool linear = op->kind == osbx_transform_kind_t::rotate ||
                            op->kind == osbx_transform_kind_t::scale;
        if (linear && !is_constant(op->track)) {
            if (rotating || op->kind != osbx_transform_kind_t::rotate)
                return {}; // General compound spans are checked on world samples.
            rotating = op;
        } else if (rotating) left = constant_linear(*op) * left;
        else right = constant_linear(*op) * right;
    }
    if (!rotating) return {};
    bool orthogonal = true;
    for (const double angle : {0.0, std::numbers::pi / 4, std::numbers::pi / 2})
        orthogonal = orthogonal && std::abs(normalized_column_dot(
            left * visual_affine_t::from(0, 0, 1, 1, angle) * right)) < 1e-9;
    if (orthogonal) return {};
    const auto& keys = rotating->track.keys;
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i].value == keys[i + 1].value ||
            std::holds_alternative<osbx_step_easing_t>(keys[i].easing)) continue;
        return "Implicit shear in span starting at " + std::to_string(keys[i].time_ms) +
               " ms (rotation " + rotating->id + ")";
    }
    return {};
}

struct sample_t {
    std::int64_t time{};
    std::uint32_t remainder{}, denominator{1};
    osbx_draw_instance_t source;
    std::array<values_t, 5> properties;
};

auto decompose(const visual_affine_t& matrix) -> std::array<values_t, 5> {
    const auto sx = std::hypot(matrix[0], matrix[1]);
    const auto sy = std::hypot(matrix[2], matrix[3]);
    const auto angle = sx > 0 ? std::atan2(matrix[1], matrix[0]) :
                               std::atan2(-matrix[2], matrix[3]);
    return {values_t{matrix[4], matrix[5]}, values_t{angle},
            values_t{sx, std::copysign(sy, matrix.determinant())}, values_t{1}, values_t{255, 255, 255}};
}

auto has_shear(const visual_affine_t& m) -> bool {
    return std::abs(normalized_column_dot(m)) >= 1e-9;
}

struct conversion_t {
    const osbx_document_t& document;
    const osbx_evaluation_index_t& index;
    osbx_osb_options_t options;
    frame_arena_t arena{};
    osbx_export_budget_t budget;

    auto sample(std::string_view id, std::int64_t time, const storyboard_trigger_scenario_t* scenario = nullptr) -> result_t<sample_t, std::string> {
        return sample(id, storyboard_time_t{.milliseconds = time}, scenario);
    }
    auto sample(std::string_view id, storyboard_time_t time, const storyboard_trigger_scenario_t* scenario = nullptr) -> result_t<sample_t, std::string> {
        const auto frame = osbx_export_frame_t::from(index, time, arena, budget,
            {.include_all_conditional_layers = true, .scenario = scenario ? scenario : options.scenario});
        if (!frame) return std::unexpected(frame.error());
        const auto found = std::ranges::find(frame->instances, id, &osbx_draw_instance_t::id);
        if (found == frame->instances.end())
            return std::unexpected("Visibility-gated sprites cannot be sampled without losing transforms");
        if (has_shear(found->world))
            return std::unexpected("Implicit shear first sampled at " + std::to_string(time.milliseconds) + " ms");
        sample_t result{.time = time.milliseconds, .remainder = time.remainder, .denominator = time.denominator, .source = *found, .properties = decompose(found->world)};
        result.properties[3][0] = found->opacity * found->color[3];
        for (std::size_t i = 0; i < 3; ++i) result.properties[4][i] = found->color[i] * 255;
        return result;
    }
};

auto sample_span(conversion_t& conversion, const source_plan_t& plan)
    -> result_t<std::vector<sample_t>, std::string> {
    const auto begin = plan.times.front(), end = plan.times.back();
    const auto duration = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
    const auto limit = std::min(conversion.options.maximum_samples_per_sprite,
                                simplify_limits_t::maximum_input_keys);
    const auto constant = plan.has_constant_values();
    const auto count = constant ? static_cast<std::uint64_t>(begin == end ? 1 : 2) : duration + 1;
    if (!count || count > limit || std::abs(static_cast<long double>(begin)) > 9'007'199'254'740'991.L ||
        std::abs(static_cast<long double>(end)) > 9'007'199'254'740'991.L)
        return std::unexpected("Source span exceeds bounded millisecond sampling/input limit");
    std::vector<sample_t> samples;
    samples.reserve(static_cast<std::size_t>(count));
    for (auto time = begin;; time = constant ? end : time + 1) {
        auto sample = conversion.sample(plan.chain.front()->id, time);
        if (!sample) return std::unexpected(sample.error());
        if (!samples.empty()) {
            if (sample->source.blend != samples.front().source.blend)
                return std::unexpected("Animated clip blend parameters are unsupported by this converter");
            auto& angle = sample->properties[1][0];
            const auto previous = samples.back().properties[1][0];
            angle = previous + std::remainder(angle - previous, 2 * std::numbers::pi);
        }
        samples.push_back(std::move(*sample));
        if (time == end) break;
    }
    return samples;
}

auto image_request(const osbx_osb_image_size_t& size, std::string_view origin)
    -> visual_image_quad_request_t {
    const auto fraction = storyboard_origin_t::from(origin).value_or(storyboard_origin_t{});
    return {.source_width = size.width, .source_height = size.height,
            .destination = {.width = 640, .height = 480}, .origin_x = fraction.x, .origin_y = fraction.y};
}

auto world_from(const std::array<values_t, 5>& properties) -> visual_affine_t {
    return visual_affine_t::from(properties[0][0], properties[0][1], properties[2][0],
                                 properties[2][1], properties[1][0]);
}


void append_command(storyboard_sprite_t& sprite, std::size_t property,
                    const simplify_key_t& a, const simplify_key_t& b, int easing) {
    storyboard_node_t node;
    node.source_order = sprite.nodes.size();
    node.command = {.opcode = opcodes[property], .easing = easing,
                    .start_time_ms = static_cast<std::int64_t>(a.time_ms),
                    .end_time_ms = static_cast<std::int64_t>(b.time_ms),
                    .value_count = arities[property], .source_order = node.source_order};
    std::ranges::copy(a.value, node.command.from.begin());
    std::ranges::copy(b.value, node.command.to.begin());
    sprite.nodes.push_back(std::move(node));
}

void extend_fade_lifetime(storyboard_sprite_t& sprite, std::int64_t end,
                          osbx_osb_report_t& report) {
    const auto fade = std::find_if(sprite.nodes.rbegin(), sprite.nodes.rend(), [](const auto& node) {
        return node.kind == storyboard_node_kind_t::command && node.command.opcode == storyboard_opcode_t::fade;
    });
    if (fade == sprite.nodes.rend() || fade->command.end_time_ms >= end) return;
    if (fade->command.from[0] == fade->command.to[0]) {
        fade->command.end_time_ms = end;
        return;
    }
    const simplify_key_t tail{static_cast<double>(fade->command.end_time_ms), {fade->command.to[0]}};
    append_command(sprite, 3, tail, {static_cast<double>(end), tail.value}, 0);
    ++report.key_count;
    ++report.keys_before_simplify;
}

auto keys_for(std::span<const sample_t> samples, std::size_t property) -> std::vector<simplify_key_t> {
    std::vector<simplify_key_t> keys;
    keys.reserve(samples.size());
    for (const auto& sample : samples)
        keys.push_back({static_cast<double>(sample.time), sample.properties[property]});
    return keys;
}

auto rounded(const simplify_key_t& key, int decimals) -> values_t {
    auto result = key.value;
    for (auto& value : result) value = round_simplify_value(value, decimals);
    return result;
}

auto property_error(const sample_t& source, std::size_t property, const values_t& actual,
                     const visual_image_quad_request_t& request) -> double {
    if (property == 3)
        return std::abs(source.properties[3][0] - actual[0]) / simplify_opacity_error;
    if (property == 4) {
        double error = 0;
        for (std::size_t i = 0; i < 3; ++i)
            error = std::max(error, std::abs(source.properties[4][i] - actual[i]));
        return error; // One channel unit (1/255).
    }
    auto changed = source.properties;
    changed[property] = actual;
    return osbx_export_corner_error(request, source.source.world, world_from(changed)) /
           (simplify_corner_error_px / 3);
}

auto emit_baked_keys(storyboard_sprite_t& sprite, std::size_t property,
                      std::span<const simplify_key_t> keys, std::span<const std::size_t> kept,
                      int decimals, std::size_t& key_count) -> std::string {
    std::vector<simplify_key_t> emitted;
    emitted.reserve(kept.size());
    for (const auto index : kept)
        emitted.push_back({keys[index].time_ms, rounded(keys[index], decimals)});
    auto equal = simplify_equal_runs(emitted);
    if (!equal) return equal.error().message;
    if (std::ranges::all_of(emitted, [&](const auto& key) { return key.value == emitted.front().value; }))
        equal->resize(1);
    key_count += equal->size();
    for (std::size_t i = 0; i < equal->size(); ++i) {
        const auto a = (*equal)[i], b = (*equal)[std::min(i + 1, equal->size() - 1)];
        if (i && a == b) break;
        append_command(sprite, property, emitted[a], emitted[b], 0);
    }
    return {};
}

// Uses the shared iterative RDP + rounded metric reinsertion owner. Starting
// decimals are storybrew's; precision increases explicitly if rounding alone
// cannot meet the real image/corner or alpha tolerance.
auto bake_property(storyboard_sprite_t& sprite, std::span<const sample_t> samples,
                    std::size_t property, const visual_image_quad_request_t& request,
                    std::size_t& key_count) -> std::string {
    const auto keys = keys_for(samples, property);
    const auto policy = policies[property];
    std::string reason;
    for (int decimals = policy.decimals; decimals <= 9; ++decimals) {
        simplify_metric_t metric{.dimensions = arities[property], .tolerance = policy.tolerance};
        metric.project = [=](const auto& key) {
            auto value = key.value;
            if (property == 2) { value[0] *= request.source_width; value[1] *= request.source_height; }
            return value;
        };
        metric.emit = [=](const auto& key) { return rounded(key, decimals); };
        metric.error = [&](const auto& key, const auto& value) {
            // Constant spans retain only their endpoints, not a dense ms array.
            const auto sample = std::ranges::lower_bound(samples, static_cast<std::int64_t>(key.time_ms), {}, &sample_t::time);
            return sample == samples.end() ? std::numeric_limits<double>::infinity() :
                property_error(*sample, property, value, request);
        };
        auto kept = simplify_to_metric(keys, metric);
        if (!kept) { reason = kept.error().message; continue; }
        return emit_baked_keys(sprite, property, keys, *kept, decimals, key_count);
    }
    return reason;
}

auto constant_value(const numeric_track_t& track) -> const std::vector<double>& {
    return track.keys.empty() ? track.default_value : track.keys.front().value;
}

// Preserve authored turns and signed scale across decomposition branch cuts.
// Only canonical scale->rotate->translate stacks use this algebraic path.
void canonical_value(values_t& value, const source_plan_t& plan, std::size_t property,
                     const osbx_key_t<>* key, double canvas_fit) {
    if (!plan.canonical_transform || (property != 1 && property != 2)) return;
    value = property == 1 ? values_t{} : values_t{canvas_fit, canvas_fit};
    for (const auto* op : plan.ops) {
        const auto kind = property == 1 ? osbx_transform_kind_t::rotate : osbx_transform_kind_t::scale;
        if (op->kind != kind) continue;
        const auto& v = key && &op->track == plan.animated[property] ? key->value : constant_value(op->track);
        if (property == 1) value[0] += v[0] * radians;
        else { value[0] *= v[0]; value[1] *= v[1]; }
    }
}

void emit_exact_prefix(storyboard_sprite_t& sprite, std::size_t property,
                       std::span<const simplify_key_t> keys, std::int64_t begin,
                       osbx_osb_report_t& report) {
    if (keys.front().time_ms <= static_cast<double>(begin)) return;
    const simplify_key_t prefix{static_cast<double>(begin), keys.front().value};
    append_command(sprite, property, prefix, keys.front(), 0);
    ++report.key_count;
    ++report.keys_before_simplify;
}

auto exact_property(storyboard_sprite_t& sprite, std::span<const sample_t> samples,
                     const source_plan_t& plan, std::size_t property,
                     osbx_osb_report_t& report, double canvas_fit) -> std::string {
    std::vector<simplify_key_t> keys;
    const auto* track = plan.animated[property];
    if (track) {
        for (const auto& key : track->keys) {
            const auto offset = static_cast<std::size_t>(key.time_ms - samples.front().time);
            auto value = samples[offset].properties[property];
            canonical_value(value, plan, property, &key, canvas_fit);
            keys.push_back({static_cast<double>(key.time_ms), value});
        }
    } else {
        auto value = samples.front().properties[property];
        canonical_value(value, plan, property, nullptr, canvas_fit);
        keys.push_back({static_cast<double>(samples.front().time), value});
    }
    report.keys_before_simplify += keys.size();
    auto kept = simplify_equal_runs(keys);
    if (!kept) return kept.error().message;
    if (std::ranges::all_of(keys, [&](const auto& key) { return key.value == keys.front().value; }))
        kept->resize(1);
    report.key_count += kept->size();
    emit_exact_prefix(sprite, property, keys, samples.front().time, report);
    for (std::size_t i = 0; i < kept->size(); ++i) {
        const auto a = (*kept)[i], b = (*kept)[std::min(i + 1, kept->size() - 1)];
        if (i && a == b) break;
        const osbx_easing_t easing = track ? track->keys[a].easing : osbx_linear_easing_t{};
        const auto* native = std::get_if<osbx_osb_easing_t>(&easing);
        if (std::holds_alternative<osbx_step_easing_t>(easing) && a != b) {
            append_command(sprite, property, keys[a], {keys[b].time_ms, keys[a].value}, 0);
            append_command(sprite, property, keys[b], keys[b], 0);
        } else append_command(sprite, property, keys[a], keys[b], native ? native->value : 0);
    }
    return {};
}

auto stable_id(std::string_view id) -> std::uint64_t {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : id) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}

auto make_sprite(const osbx_document_t& document, const osbx_node_t& node,
                  std::uint64_t order) -> storyboard_sprite_t {
    const auto image = std::ranges::find(document.images, *node.image, &osbx_image_t::id);
    return {.stable_id = stable_id(node.id), .source_order = order,
            .layer = std::string{osbx_layer_names[static_cast<std::size_t>(node.layer)]}, .origin = node.origin,
            .path = image->file, .animation = node.animation};
}

auto reconstruct(const storyboard_sprite_t& sprite, storyboard_time_t time)
    -> result_t<std::array<values_t, 5>, std::string> {
    std::array<values_t, 5> values{};
    for (std::size_t property = 0; property < opcodes.size(); ++property) {
        const auto result = evaluate_storyboard_property(sprite, opcodes[property], time);
        if (!result.is_success() || !result.value.present)
            return std::unexpected("Emitted OSB property failed reconstruction");
        std::ranges::copy_n(result.value.values.begin(), 3, values[property].begin());
    }
    return values;
}

auto verify_sprite(const storyboard_sprite_t& sprite, std::span<const sample_t> samples,
                    const visual_image_quad_request_t& source_request,
                    osbx_osb_report_t& report) -> std::string {
    auto destination_request = source_request;
    destination_request.logical_width = 640;
    destination_request.logical_height = 480;
    std::optional<std::int64_t> animation_epoch;
    if (sprite.animation) {
        const auto preview = storyboard_preview_index_t::from({.sprites = {sprite}});
        if (!preview.value || preview.value->sources.size() != 1) return "Animation envelope unavailable";
        animation_epoch = preview.value->sources.front().begin_time_ms;
    }
    for (const auto& sample : samples) {
        auto actual = reconstruct(sprite, {.milliseconds = sample.time, .remainder = sample.remainder, .denominator = sample.denominator});
        if (!actual) return actual.error();
        // Source and emitted worlds have already been mapped to OSB canvas space.
        const auto corner = osbx_export_corner_error(destination_request, sample.source.world, world_from(*actual));
        auto reconstructed = sample.source;
        reconstructed.world = world_from(*actual);
        reconstructed.frame_index = animation_epoch ? sprite.animation->frame_index_at(
            {.milliseconds = sample.time, .remainder = sample.remainder, .denominator = sample.denominator},
            *animation_epoch) : std::nullopt;
        reconstructed.opacity = (*actual)[3][0];
        reconstructed.color = {(*actual)[4][0] / 255, (*actual)[4][1] / 255, (*actual)[4][2] / 255, 1};
        double error = std::max(corner / simplify_corner_error_px,
            osbx_export_instance_error(sample.source, reconstructed, destination_request));
        error = std::max(error, property_error(sample, 3, (*actual)[3], source_request));
        error = std::max(error, property_error(sample, 4, (*actual)[4], source_request));
        report.worst_error = std::max(report.worst_error, error);
        if (!std::isfinite(error) || error > 1 + 1e-9)
            return "Serialized OSB reconstruction exceeds corner/opacity/color metric at " +
                   std::to_string(sample.time) + " ms";
    }
    return {};
}

void map_canvas(std::vector<sample_t>& samples, const osbx_canvas_t& canvas) {
    const auto mapping = osbx_export_canvas_transform(canvas);
    for (auto& sample : samples) {
        sample.source.world = mapping * sample.source.world;
        const auto angle = sample.properties[1][0];
        sample.properties = decompose(sample.source.world);
        sample.properties[1][0] = angle;
        sample.properties[3][0] = sample.source.opacity * sample.source.color[3];
        for (std::size_t i = 0; i < 3; ++i) sample.properties[4][i] = sample.source.color[i] * 255;
    }
}


// A symbolic program is admitted only when each property has one canonical
// owner. More complex precedence/composition is sampled over a finite span.
struct native_program_t {
    const numeric_track_t* track{};
    std::size_t property{};
    std::array<const numeric_track_t*, 5> tracks{};
};

auto native_program(const source_plan_t& plan) -> std::optional<native_program_t> {
    if (plan.chain.size() != 1 || plan.has_clips || plan.chain.front()->lifetime || !plan.canonical_transform) return {};
    const auto& node = *plan.chain.front();
    if (!is_constant(node.tracks.visibility) || !is_constant(node.tracks.blend)) return {};
    native_program_t result;
    result.tracks[3] = &node.tracks.opacity; result.tracks[4] = &node.tracks.color;
    for (const auto* op : plan.ops) {
        if (!op->children.empty()) return {};
        const auto property = native_property(op->kind);
        if (!property || result.tracks[*property]) return {};
        result.tracks[*property] = &op->track;
    }
    for (std::size_t i = 0; i < result.tracks.size(); ++i) {
        const auto* track = result.tracks[i];
        if (!track || is_constant(*track)) continue;
        if (result.track || !track->keys.empty() || track->program.size() != 1) return {};
        result.track = track; result.property = i;
    }
    return result.track ? std::optional{result} : std::nullopt;
}

auto native_program_reason(const native_program_t& program, std::size_t sample_limit) -> std::string {
    const auto windows = std::holds_alternative<osbx_loop_t<>>(program.track->program.front().data) ? 3U : 1U;
    const auto local_limit = std::min(sample_limit, simplify_limits_t::maximum_input_keys) / (4 * windows);
    const auto keys = program.track->program.front().keys();
    if (!local_limit || static_cast<std::uint64_t>(keys.back().time_ms) >= local_limit)
        return "Temporal proof exceeds bounded local sampling limit before command expansion";
    for (const auto& key : keys) {
        if (std::holds_alternative<osbx_cubic_bezier_t>(key.easing)) return "Program easing needs bounded baking";
        if (program.property == 4 && key.value[3] != program.track->default_value[3])
            return "Program color alpha needs bounded baking";
    }
    if (const auto* trigger = std::get_if<osbx_trigger_t<>>(&program.track->program.front().data)) {
        if (keys.front().time_ms != 0) return "Trigger first-key gaps require explicit scenario baking";
        if (!trigger->trigger.start_time_ms || !trigger->trigger.end_time_ms) return "Symbolic trigger needs a finite activation window";
        if (!osbx_clip_clock_t{.origin = {.milliseconds = *trigger->trigger.end_time_ms}}.map(keys.back().time_ms))
            return "Symbolic trigger final activation endpoint overflows";
    }
    return {};
}

auto native_value(const native_program_t& program, std::size_t property,
    std::span<const double> value, const osbx_canvas_t& canvas) -> values_t {
    values_t result{};
    std::ranges::copy_n(value.begin(), arities[property], result.begin());
    const auto mapping = osbx_export_canvas_transform(canvas);
    if (property == 0) { result[0] = value[0] * mapping[0] + mapping[4]; result[1] = value[1] * mapping[3] + mapping[5]; }
    if (property == 1) result[0] *= radians;
    if (property == 2) { result[0] *= mapping[0]; result[1] *= mapping[3]; }
    if (property == 3) result[0] *= constant_value(*program.tracks[4])[3];
    if (property == 4) for (auto& component : result) component *= 255;
    return result;
}

void native_command(std::vector<storyboard_node_t>& nodes, std::size_t property,
    std::int64_t begin, std::int64_t end, const values_t& from, const values_t& to, int easing = 0) {
    storyboard_node_t node{.source_order = nodes.size()};
    node.command = {.opcode = opcodes[property], .easing = easing, .start_time_ms = begin,
        .end_time_ms = end, .value_count = arities[property], .source_order = node.source_order};
    std::ranges::copy(from, node.command.from.begin()); std::ranges::copy(to, node.command.to.begin());
    nodes.push_back(std::move(node));
}

void native_prefix(storyboard_sprite_t& sprite, const native_program_t& program,
    const osbx_canvas_t& canvas, std::int64_t begin, std::int64_t end) {
    const std::array<std::vector<double>, 3> defaults{{{0, 0}, {0}, {1, 1}}};
    for (std::size_t property = 0; property < opcodes.size(); ++property) {
        const auto* track = program.tracks[property];
        const auto& raw = track ? constant_value(*track) : defaults[property];
        const auto value = native_value(program, property, raw, canvas);
        native_command(sprite.nodes, property, begin, property == 3 ? end : begin, value, value);
    }
}

void native_keys(std::vector<storyboard_node_t>& nodes, const native_program_t& program,
    const osbx_canvas_t& canvas, bool terminal) {
    const auto keys = program.track->program.front().keys();
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& a = keys[i]; const auto& b = keys[i + 1];
        const auto from = native_value(program, program.property, a.value, canvas);
        const auto step = std::holds_alternative<osbx_step_easing_t>(a.easing);
        const auto to = step ? from : native_value(program, program.property, b.value, canvas);
        const auto* easing = std::get_if<osbx_osb_easing_t>(&a.easing);
        native_command(nodes, program.property, a.time_ms, b.time_ms, from, to, easing ? easing->value : 0);
    }
    if (terminal || keys.size() == 1) {
        const auto value = native_value(program, program.property, keys.back().value, canvas);
        native_command(nodes, program.property, keys.back().time_ms, keys.back().time_ms, value, value);
    }
}

auto native_block(storyboard_sprite_t& sprite, const native_program_t& program,
    const osbx_canvas_t& canvas) -> storyboard_node_t {
    storyboard_node_t node{.source_order = sprite.nodes.size()};
    const auto& block = program.track->program.front();
    if (const auto* loop = std::get_if<osbx_loop_t<>>(&block.data)) {
        node.kind = storyboard_node_kind_t::loop;
        node.loop_start_time_ms = loop->start_ms; node.loop_iteration_count = loop->count;
        native_keys(node.children, program, canvas, loop->keys.front().time_ms != 0);
    } else {
        node.kind = storyboard_node_kind_t::trigger;
        node.trigger = std::get<osbx_trigger_t<>>(block.data).trigger;
        native_keys(node.children, program, canvas, true);
    }
    // Local child vectors must retain their emitted order relative to the
    // prefix, too: at INT64_MIN there is no earlier integer for the default.
    // Programs win a same-start tie in both the returned model and its reparse.
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        auto& child = node.children[i];
        child.source_order = sprite.nodes.size() + i + 1;
        child.command.source_order = child.source_order;
    }
    return node;
}

auto reparse_sprite(const storyboard_sprite_t& sprite) -> result_t<storyboard_sprite_t, std::string> {
    const auto emitted = emit_osb({.sprites = {sprite}});
    if (!emitted.value) return std::unexpected("OSB emitter rejected temporal sprite");
    auto parsed = parse_osb(*emitted.value);
    if (!parsed.value || parsed.value->sprites.size() != 1) return std::unexpected("Temporal OSB failed reparse");
    return std::move(parsed.value->sprites.front());
}

auto sprite_covers_span(const storyboard_sprite_t& sprite, std::int64_t begin, std::int64_t end) -> bool {
    const auto preview = storyboard_preview_index_t::from({.sprites = {sprite}});
    return preview.value && preview.value->sources.size() == 1 &&
        preview.value->sources.front().is_active(begin) && preview.value->sources.front().is_active(end);
}

struct program_proof_t {
    std::vector<std::int64_t> times;
    std::optional<storyboard_trigger_scenario_t> scenario;
    storyboard_sprite_t destination;
};
auto add_proof_window(program_proof_t& proof, std::int64_t origin, std::int64_t period, std::size_t limit)
    -> std::string {
    if (!limit || static_cast<std::uint64_t>(period) >= limit || proof.times.size() > limit - static_cast<std::size_t>(period) - 1)
        return "Temporal proof exceeds bounded local sampling limit";
    const osbx_clip_clock_t clock{.origin = {.milliseconds = origin}};
    for (std::int64_t relative = 0;; ++relative) {
        const auto time = clock.map(relative);
        if (!time) return "Temporal proof clock overflow";
        proof.times.push_back(time->milliseconds);
        if (relative == period) break;
    }
    return {};
}

auto prove_loop_windows(program_proof_t& proof, const osbx_loop_t<>& loop, std::size_t limit) -> std::string {
    const std::array<std::uint32_t, 3> iterations{0, std::min(std::uint32_t{1}, loop.count - 1), loop.count - 1};
    for (const auto iteration : iterations) {
        const auto origin = loop.iteration_start(iteration);
        if (!origin) return "Temporal proof clock overflow";
        if (auto reason = add_proof_window(proof, *origin, loop.keys.back().time_ms, limit); !reason.empty()) return reason;
    }
    return {};
}

// The emitted T stays conditional. Only this verification copy projects ONE
// explicit activation, so the shared OSB sampler never treats an inactive T
// match as evidence. Static surroundings make the relative proof invariant
// under a change of activation origin; overlapping programs are not admitted.
auto prove_trigger_window(program_proof_t& proof, const osbx_trigger_t<>& trigger, std::size_t limit) -> std::string {
    const auto origin = *trigger.trigger.start_time_ms;
    const std::array firings{storyboard_trigger_firing_t{trigger.trigger.name, {.milliseconds = origin}}};
    auto scenario = storyboard_trigger_scenario_t::from(firings);
    if (!scenario) return "Trigger proof scenario exceeds bounds";
    proof.scenario = std::move(*scenario);
    const osbx_clip_clock_t clock{.origin = {.milliseconds = origin}};
    std::vector<storyboard_node_t> projected;
    for (auto& node : proof.destination.nodes) {
        if (node.kind != storyboard_node_kind_t::trigger) { projected.push_back(std::move(node)); continue; }
        for (auto& child : node.children) {
            const auto begin = clock.map(child.command.start_time_ms), end = clock.map(child.command.end_time_ms);
            if (!begin || !end) return "Trigger proof clock overflow";
            child.command.start_time_ms = begin->milliseconds; child.command.end_time_ms = end->milliseconds;
            projected.push_back(std::move(child));
        }
    }
    for (std::size_t i = 0; i < projected.size(); ++i) {
        projected[i].source_order = i; projected[i].command.source_order = i;
    }
    proof.destination.nodes = std::move(projected);
    return add_proof_window(proof, origin, trigger.keys.back().time_ms, limit);
}

auto native_proof(conversion_t& conversion, const native_program_t& program, const storyboard_sprite_t& parsed,
    const visual_image_quad_request_t& request, osbx_osb_report_t& report) -> std::string {
    program_proof_t proof{.destination = parsed};
    const auto& block = program.track->program.front().data;
    const auto limit = std::min(conversion.options.maximum_samples_per_sprite, simplify_limits_t::maximum_input_keys) / 4;
    const auto reason = std::visit([&](const auto& value) {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, osbx_loop_t<>>) return prove_loop_windows(proof, value, limit);
        else return prove_trigger_window(proof, value, limit);
    }, block);
    if (!reason.empty()) return reason;
    std::ranges::sort(proof.times); const auto unique = std::ranges::unique(proof.times); proof.times.erase(unique.begin(), unique.end());
    for (const auto time : proof.times) {
        for (std::uint32_t fraction = 0; fraction < 4; ++fraction) {
            const storyboard_time_t query{.milliseconds = time, .remainder = fraction, .denominator = 4};
            // Every native property query validates/traverses this relative
            // tree, including prior/current loop branches. Charge it before work.
            if (!conversion.budget.consume((program.track->program.front().keys().size() + 8) * 32))
                return "Temporal reconstruction exceeds aggregate work budget";
            auto sample = conversion.sample(report.node_id, query, proof.scenario ? &*proof.scenario : nullptr);
            if (!sample) return sample.error();
            std::vector<sample_t> samples; samples.push_back(std::move(*sample)); map_canvas(samples, conversion.document.canvas);
            if (auto failure = verify_sprite(proof.destination, samples, request, report); !failure.empty()) return failure;
        }
    }
    return {};
}

auto convert_native_program(conversion_t& conversion, const source_plan_t& plan, std::uint64_t order,
    const visual_image_quad_request_t& request, osbx_osb_report_t& report) -> result_t<storyboard_sprite_t, std::string> {
    const auto program = native_program(plan);
    if (!program) return std::unexpected("Temporal composition needs finite-span baking");
    if (auto reason = native_program_reason(*program, conversion.options.maximum_samples_per_sprite); !reason.empty()) return std::unexpected(reason);
    const auto& block = program->track->program.front().data;
    auto sprite = make_sprite(conversion.document, *plan.chain.front(), order);
    native_prefix(sprite, *program, conversion.document.canvas, plan.times.front(), plan.times.back());
    sprite.nodes.push_back(native_block(sprite, *program, conversion.document.canvas));
    if (const auto* loop = std::get_if<osbx_loop_t<>>(&block)) {
        const auto value = native_value(*program, program->property, loop->keys.back().value, conversion.document.canvas);
        const auto end = loop->end_time();
        if (!end) return std::unexpected("Temporal endpoint overflow");
        native_command(sprite.nodes, program->property, *end, *end, value, value);
    }
    if (plan.chain.front()->tracks.blend.default_value == osbx_blend_t::additive ||
        (!plan.chain.front()->tracks.blend.keys.empty() && plan.chain.front()->tracks.blend.keys.front().value == osbx_blend_t::additive))
        sprite.nodes.push_back({.kind = storyboard_node_kind_t::retained_line, .source_order = sprite.nodes.size(),
            .retained_line = " P,0," + std::to_string(plan.times.front()) + "," + std::to_string(plan.times.back()) + ",A"});
    auto parsed = reparse_sprite(sprite);
    if (!parsed) return std::unexpected(parsed.error());
    if (!sprite_covers_span(*parsed, plan.times.front(), plan.times.back()))
        return std::unexpected("Emitted temporal sprite does not cover the source playback span");
    if (auto reason = native_proof(conversion, *program, *parsed, request, report); !reason.empty()) return std::unexpected(reason);
    report.key_count = program->track->program.front().keys().size() + 5;
    report.keys_before_simplify = report.key_count; report.classification = osbx_osb_class_t::exact;
    report.reason = "Native relative program: canonical property/clock/precedence proof, active first/second/final local windows at quarter-ms; no engine certificate";
    return sprite;
}

template<class Value> auto has_trigger(const osbx_track_t<Value>& track) -> bool {
    return std::ranges::any_of(track.program, [](const auto& block) { return std::holds_alternative<osbx_trigger_t<Value>>(block.data); });
}
auto has_trigger(const source_plan_t& plan) -> bool {
    if (plan.has_clip_trigger) return true;
    if (std::ranges::any_of(plan.ops, [](const auto* op) { return has_trigger(op->track); })) return true;
    for (const auto* node : plan.chain) {
        if (has_trigger(node->tracks.opacity) || has_trigger(node->tracks.color) ||
            has_trigger(node->tracks.visibility) || has_trigger(node->tracks.blend)) return true;
    }
    return false;
}

auto prepare_temporal_plan(conversion_t& conversion, source_plan_t& plan, std::uint64_t order,
    const visual_image_quad_request_t& request, osbx_osb_report_t& report)
    -> result_t<std::optional<storyboard_sprite_t>, std::string> {
    if (plan.has_program) {
        auto candidate_report = report;
        auto native = convert_native_program(conversion, plan, order, request, candidate_report);
        if (native) { report = std::move(candidate_report); return std::optional{std::move(*native)}; }
        if (has_trigger(plan) && (!conversion.options.scenario || !conversion.options.playback_span))
            return std::unexpected("Trigger-dependent bake requires an explicit sealed scenario and finite playback span: " + native.error());
        plan.exact = false;
        if (conversion.options.playback_span) {
            plan.times = {conversion.options.playback_span->begin_time_ms, conversion.options.playback_span->end_time_ms};
            if (auto reason = apply_lifetime(plan); !reason.empty()) return std::unexpected(reason);
        }
    }
    return std::optional<storyboard_sprite_t>{};
}

auto animation_phase_reason(const osbx_node_t& source, const storyboard_sprite_t& parsed) -> std::string {
    if (!source.animation) return {};
    if (parsed.animation != source.animation) return "Animation metadata/delay changed during independent OSB reparse";
    const auto preview = storyboard_preview_index_t::from({.sprites = {parsed}});
    const auto epoch = source.lifetime ? source.lifetime->begin_ms : 0;
    if (!preview.value || preview.value->sources.size() != 1 || preview.value->sources.front().begin_time_ms != epoch)
        return "Animation earliest emitted command envelope differs from explicit source epoch";
    // Equal exact binary64 metadata and equal integer epochs use the SAME exact
    // rational frame query at every fractional time, including seeks and tails.
    return {};
}
auto animation_geometry_reason(conversion_t& conversion, const source_plan_t& plan,
    const osbx_osb_image_size_t& size) -> std::string {
    const auto& node = *plan.chain.front();
    if (!node.animation) return {};
    if (!node.lifetime && !conversion.options.playback_span)
        return "Animation export requires an explicit finite lifetime or playback span";
    if (plan.has_program || plan.has_clips) return "Animation temporal/clip composition has no phase proof";
    const auto count = node.animation->frame_count;
    if (count > conversion.options.maximum_evaluation_work || count > conversion.options.image_sizes.size())
        return "Animation concrete frame proof exceeds available bounded geometry";
    const auto work = conversion.options.image_sizes.size();
    if (work && (count > conversion.budget.remaining_work / work || !conversion.budget.consume(count * work)))
        return "Animation geometry proof exceeds aggregate work budget";
    const auto image = std::ranges::find(conversion.document.images, *node.image, &osbx_image_t::id);
    storyboard_frame_arena_t arena;
    for (std::uint32_t i = 0; i < count; ++i) {
        arena.reset(); const auto path = storyboard_animation_t::frame_path(image->file, i, arena);
        if (!path) return "Animation frame path exceeds bounds";
        const auto found = std::ranges::find_if(conversion.options.image_sizes, [&](const auto& frame) {
            return frame.image_id == image->id && frame.file == *path;
        });
        if (found == conversion.options.image_sizes.end() || found->width != size.width || found->height != size.height)
            return "Animation requires equal validated dimensions for every concrete frame";
    }
    return {};
}

auto convert_sampled_sprite(conversion_t& conversion, const source_plan_t& plan,
    std::uint64_t order, const osbx_osb_image_size_t& size, osbx_osb_report_t& report)
    -> result_t<storyboard_sprite_t, std::string> {
    const auto& node = *plan.chain.front();
    auto samples = sample_span(conversion, plan);
    if (!samples) return std::unexpected(samples.error());
    map_canvas(*samples, conversion.document.canvas);
    auto request = image_request(size, node.origin);
    auto sprite = make_sprite(conversion.document, *plan.chain.front(), order);
    for (std::size_t property = 0; property < opcodes.size(); ++property) {
        std::string reason;
        if (plan.exact) reason = exact_property(sprite, *samples, plan, property, report,
                                                   osbx_export_canvas_transform(conversion.document.canvas)[0]);
        else {
            report.keys_before_simplify += samples->size();
            reason = bake_property(sprite, *samples, property, request, report.key_count);
        }
        if (!reason.empty()) return std::unexpected(reason);
    }
    extend_fade_lifetime(sprite, samples->back().time, report);
    if (samples->front().source.blend == osbx_blend_t::additive)
        sprite.nodes.push_back({.kind = storyboard_node_kind_t::retained_line,
                                .source_order = sprite.nodes.size(),
                                .retained_line = " P,0," + std::to_string(samples->front().time) +
                                    "," + std::to_string(samples->back().time) + ",A"});
    const auto emitted = emit_osb({.sprites = {sprite}});
    if (!emitted.value) return std::unexpected("OSB emitter rejected converted sprite");
    const auto parsed = parse_osb(*emitted.value);
    if (!parsed.value || parsed.value->sprites.size() != 1)
        return std::unexpected("Emitted OSB failed reparse");
    if (!sprite_covers_span(parsed.value->sprites.front(), samples->front().time, samples->back().time))
        return std::unexpected("Emitted OSB sprite does not cover the source playback span");
    if (auto reason = animation_phase_reason(node, parsed.value->sprites.front()); !reason.empty())
        return std::unexpected(reason);
    if (auto reason = verify_sprite(parsed.value->sprites.front(), *samples, request, report); !reason.empty())
        return std::unexpected(reason);
    report.classification = plan.exact ? osbx_osb_class_t::exact : osbx_osb_class_t::baked;
    report.reason = plan.exact ? "Native OSB transform and easing" : "Millisecond bake verified against serialized OSB";
    if (node.animation) report.reason += "; independently reparsed exact Animation metadata/epoch and equal concrete-frame geometry; no engine certificate";
    return sprite;
}

auto convert_sprite(conversion_t& conversion, std::size_t position, std::uint64_t order,
                     osbx_osb_report_t& report) -> result_t<storyboard_sprite_t, std::string> {
    auto plan = make_plan(conversion.index, position, conversion.options.playback_span);
    if (!plan.clip_failure.empty()) return std::unexpected(plan.clip_failure);
    if (auto reason = apply_lifetime(plan); !reason.empty()) return std::unexpected(reason);
    if (auto reason = preflight(plan.chain); !reason.empty()) return std::unexpected(reason);
    if (auto reason = analytic_shear(plan); !reason.empty()) return std::unexpected(reason);
    const auto& node = *plan.chain.front();
    const auto* size = osbx_export_image_size(conversion.document, *node.image, conversion.options.image_sizes);
    if (!size || !size->width || !size->height)
        return std::unexpected("Missing validated image dimensions for " + *node.image);
    if (auto reason = animation_geometry_reason(conversion, plan, *size); !reason.empty()) return std::unexpected(reason);
    auto native = prepare_temporal_plan(conversion, plan, order, image_request(*size, node.origin), report);
    if (!native) return std::unexpected(native.error());
    if (*native) return std::move(**native);
    return convert_sampled_sprite(conversion, plan, order, *size, report);
}

auto convert_media_node(conversion_t& conversion, std::size_t position, storyboard_document_model_t& model,
    std::uint64_t order, osbx_osb_report_t& report) -> std::string {
    const auto& indexed = conversion.index.nodes[position]; const auto& node = *indexed.source;
    if (node.kind == osbx_node_kind_t::sample) {
        if (!node.sample) return "Incomplete Sample metadata";
        if (!indexed.sample_refusal.empty()) return std::string{indexed.sample_refusal};
        const auto& payload = *node.sample;
        storyboard_sample_t sample{.stable_id = stable_id(node.id), .source_order = order,
            .time_ms = payload.time_ms, .layer = node.layer, .path = payload.path, .volume = payload.volume};
        const auto emitted = emit_osb({.samples = {sample}});
        const auto parsed = emitted.value ? parse_osb(*emitted.value) : decltype(parse_osb("")){};
        if (!parsed.value || parsed.value->samples.size() != 1 ||
            parsed.value->samples.front().time_ms != sample.time_ms || parsed.value->samples.front().path != sample.path ||
            parsed.value->samples.front().layer != sample.layer || parsed.value->samples.front().volume != sample.volume)
            return "Sample metadata changed during independent OSB reparse";
        model.samples.push_back(std::move(sample)); report.classification = osbx_osb_class_t::exact;
        report.reason = "Canonical inert Sample payload independently reparsed";
        return {};
    }
    auto sprite = convert_sprite(conversion, position, order, report);
    if (!sprite) return sprite.error();
    model.sprites.push_back(std::move(*sprite));
    return {};
}

} // namespace

auto osbx_osb_report_t::mark_unsupported(std::string message) -> osbx_osb_diagnostic_t {
    classification = osbx_osb_class_t::unsupported;
    reason = std::move(message);
    return {.message = reason, .path = node_id};
}

namespace {
struct capability_cost_t {
    std::size_t work{1};
};
auto capability_cost(const osbx_node_t& node) -> capability_cost_t {
    capability_cost_t result{.work = 1 + node.tracks.blend.keys.size() + node.tracks.visibility.keys.size()};
    std::vector<const osbx_transform_op_t*> ops;
    collect_ops(node.ops, ops);
    result.work += ops.size();
    return result;
}
class capability_query_t {
  public:
    explicit capability_query_t(const osbx_document_t& document, std::size_t work) : remaining_{work} {
        for (const auto& node : document.nodes) {
            nodes_.emplace(node.id, &node);
            costs_.emplace(&node, capability_cost(node));
        }
    }
    auto reason(const osbx_node_t& node) -> std::string {
        std::vector<const osbx_node_t*> chain;
        for (const auto* current = &node; current;) {
            const auto cost = costs_.at(current);
            if (cost.work > remaining_) return {};
            remaining_ -= cost.work;
            chain.push_back(current);
            current = current->parent ? nodes_.at(*current->parent) : nullptr;
        }
        if (node.animation || node.sample) return {};
        if (node.kind != osbx_node_kind_t::sprite) return std::string{unsupported_node_kind};
        if (auto message = lifetime_reason(chain); !message.empty()) return message;
        return preflight(chain);
    }
  private:
    std::size_t remaining_{};
    std::unordered_map<std::string_view, const osbx_node_t*> nodes_;
    std::unordered_map<const osbx_node_t*, capability_cost_t> costs_;
};
} // namespace

auto osbx_osb_known_unsupported(const osbx_document_t& document, std::size_t maximum_work)
    -> std::vector<osbx_osb_report_t> {
    osbx_validation_options_t bounds;
    bounds.maximum_nodes = std::min(bounds.maximum_nodes, maximum_work);
    bounds.maximum_images = std::min(bounds.maximum_images, maximum_work);
    bounds.maximum_ops = std::min(bounds.maximum_ops, maximum_work);
    bounds.maximum_keys = std::min(bounds.maximum_keys, maximum_work);
    if (!validate_osbx_document(document, bounds).value || has_osbx_lua_links(document)) return {};
    capability_query_t query{document, maximum_work};
    std::vector<osbx_osb_report_t> result;
    for (const auto& node : document.nodes) {
        if (node.kind == osbx_node_kind_t::group) continue;
        auto reason = query.reason(node);
        if (!reason.empty()) result.push_back({.node_id = node.id,
            .classification = osbx_osb_class_t::unsupported, .reason = std::move(reason)});
    }
    return result;
}

auto convert_osbx_to_osb(const osbx_document_t& source, osbx_osb_options_t options)
    -> osbx_osb_outcome_t {
    const auto prepared = osbx_lua_prepared_scope_t::find(source);
    const auto& document = prepared ? *prepared : source;
    osbx_osb_outcome_t outcome;
    if (options.playback_span && options.playback_span->begin_time_ms > options.playback_span->end_time_ms) {
        outcome.diagnostics.push_back({.code = failure_code_t::invalid_request,
                                       .message = "Playback span must have ordered endpoints"});
        return outcome;
    }
    const auto index = osbx_evaluation_index_t::from(document);
    if (!index.value) {
        for (const auto& diagnostic : index.diagnostics)
            outcome.diagnostics.push_back({.code = failure_code_t::invalid_data,
                                            .message = diagnostic.message, .path = diagnostic.location.id});
        return outcome;
    }
    conversion_t conversion{document, *index.value, options};
    conversion.budget = osbx_export_budget_t::from(*index.value, document.images.size(), options.maximum_evaluation_work);
    storyboard_document_model_t model;
    std::unordered_map<std::uint64_t, std::string> identities;
    for (const auto position : index.value->draw_order) {
        const auto& node = *index.value->nodes[position].source;
        if (node.kind == osbx_node_kind_t::group) continue;
        auto& report = outcome.value.report.emplace_back(osbx_osb_report_t{.node_id = node.id});
        if (node.kind != osbx_node_kind_t::sprite && !node.animation && !node.sample) {
            outcome.diagnostics.push_back(report.mark_unsupported(std::string{unsupported_node_kind}));
            continue;
        }
        const auto [found, inserted] = identities.emplace(stable_id(node.id), node.id);
        if (!inserted) {
            outcome.diagnostics.push_back(report.mark_unsupported("FNV-1a-64 collision with " + found->second));
            continue;
        }
        if (auto reason = convert_media_node(conversion, position, model,
            model.sprites.size() + model.samples.size(), report); !reason.empty())
            outcome.diagnostics.push_back(report.mark_unsupported(std::move(reason)));
    }
    if (outcome.diagnostics.empty()) {
        const auto emitted = emit_osb(model);
        if (emitted.value && parse_osb(*emitted.value).value) outcome.value.model = std::move(model);
        else outcome.diagnostics.push_back({.message = "Complete OSB exceeds emitter/parser limits"});
    }
    return outcome;
}
} // namespace wormedit
