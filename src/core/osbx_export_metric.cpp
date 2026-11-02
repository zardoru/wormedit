#include <wormedit/core/osbx_export_metric.h>
#include <wormedit/core/keyframe_simplify.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace wormedit {
namespace {
auto op_count(std::span<const osbx_evaluation_op_t> ops) -> std::size_t {
    std::size_t count = ops.size();
    for (const auto& op : ops) count += op_count(op.children);
    return count;
}
auto finite_instance(const osbx_draw_instance_t& instance) -> bool {
    return instance.world.is_finite() && std::isfinite(instance.opacity) &&
        std::ranges::all_of(instance.color, [](double v) { return std::isfinite(v); });
}
} // namespace

auto osbx_export_image_size(const osbx_document_t& document, std::string_view image_id,
    std::span<const osbx_osb_image_size_t> sizes) -> const osbx_osb_image_size_t* {
    const auto image = std::ranges::find(document.images, image_id, &osbx_image_t::id);
    if (image == document.images.end()) return nullptr;
    const osbx_osb_image_size_t* unscoped{};
    for (const auto& size : sizes) {
        if (size.image_id != image->id) continue;
        if (size.file == image->file) return &size;
        if (size.file.empty()) unscoped = &size;
    }
    return unscoped;
}

auto osbx_export_budget_t::from(const osbx_evaluation_index_t& index, std::size_t image_count,
    std::size_t maximum_work) -> osbx_export_budget_t {
    // Preserve the existing OSBX7 per-frame work accounting.
    osbx_export_budget_t budget{.remaining_work = maximum_work};
    const auto fixed = index.nodes.size() * 2;
    if (image_count > std::numeric_limits<std::size_t>::max() - fixed) {
        budget.frame_work = std::numeric_limits<std::size_t>::max(); budget.remaining_work = 0; return budget;
    }
    budget.frame_work = image_count + fixed;
    for (const auto& node : index.nodes) {
        // Exact binary64 frame division is bounded by the exponent width,
        // independent of elapsed time/count. Charge source plus serialized proof work.
        const auto count = op_count(node.ops) + (node.source->animation ? std::size_t{4096} : 0);
        if (count > std::numeric_limits<std::size_t>::max() - budget.frame_work) {
            budget.frame_work = std::numeric_limits<std::size_t>::max(); budget.remaining_work = 0; return budget;
        }
        budget.frame_work += count;
    }
    const auto clip_work = index.clip_frame_work(), program_work = index.program_frame_work();
    const auto extra = program_work > SIZE_MAX - clip_work ? SIZE_MAX : clip_work + program_work;
    if (extra > std::numeric_limits<std::size_t>::max() - budget.frame_work) {
        budget.frame_work = std::numeric_limits<std::size_t>::max(); budget.remaining_work = 0;
    } else budget.frame_work += extra;
    return budget;
}
auto osbx_export_budget_t::consume(std::size_t amount) -> bool {
    if (amount > remaining_work) return false;
    remaining_work -= amount;
    return true;
}
auto osbx_export_frame_t::from(const osbx_evaluation_index_t& index, storyboard_time_t time,
    frame_arena_t& arena, osbx_export_budget_t& budget, osbx_frame_options_t options)
    -> result_t<osbx_export_frame_t, std::string> {
    if (!time.is_valid()) return std::unexpected("Invalid fractional export frame clock");
    if (!budget.consume(budget.frame_work)) return std::unexpected("Evaluation work limit exceeded");
    const auto frame = evaluate_osbx_frame(index, time, arena, options);
    if (!frame.is_success()) return std::unexpected("Source frame evaluation failed at " + std::to_string(time.milliseconds));
    if (frame.instances.size() > budget.maximum_instances ||
        frame.instances.size() > budget.maximum_frame_bytes / sizeof(osbx_draw_instance_t))
        return std::unexpected("Export frame instance/storage limit exceeded");
    return osbx_export_frame_t{.time = time, .instances = {frame.instances.begin(), frame.instances.end()}};
}
auto osbx_export_canvas_transform(const osbx_canvas_t& source) -> visual_affine_t {
    const osbx_canvas_t target;
    const auto fit = std::min(target.width / source.width, target.height / source.height);
    return visual_affine_t::from((target.width - source.width * fit) / 2,
        (target.height - source.height * fit) / 2, fit, fit);
}
auto osbx_export_corner_error(visual_image_quad_request_t request,
    const visual_affine_t& expected, const visual_affine_t& actual) -> double {
    request.transform = visual_affine_t{};
    const auto local = visual_image_quad_t::from(request);
    if (!local || !expected.is_finite() || !actual.is_finite()) return std::numeric_limits<double>::infinity();
    double error = 0;
    for (const auto& point : local->points) {
        const auto dx = (expected[0] - actual[0]) * point.x +
                        (expected[2] - actual[2]) * point.y + expected[4] - actual[4];
        const auto dy = (expected[1] - actual[1]) * point.x +
                        (expected[3] - actual[3]) * point.y + expected[5] - actual[5];
        error = std::max(error, std::hypot(dx, dy));
    }
    return error;
}
auto osbx_export_instance_error(const osbx_draw_instance_t& expected,
    const osbx_draw_instance_t& actual, visual_image_quad_request_t request) -> double {
    if (!std::isfinite(expected.opacity * expected.color[3]) || !std::isfinite(actual.opacity * actual.color[3]) ||
        expected.id != actual.id || expected.image != actual.image || expected.origin != actual.origin ||
        expected.frame_index != actual.frame_index || expected.blend != actual.blend || !finite_instance(expected) || !finite_instance(actual))
        return std::numeric_limits<double>::infinity();
    double error = osbx_export_corner_error(request, expected.world, actual.world) / simplify_corner_error_px;
    const auto unit = [](double value) { return std::clamp(value, 0.0, 1.0); };
    error = std::max(error, std::abs(unit(expected.opacity * expected.color[3]) -
        unit(actual.opacity * actual.color[3])) / simplify_opacity_error);
    for (std::size_t i = 0; i < 3; ++i)
        error = std::max(error, std::abs(unit(expected.color[i]) - unit(actual.color[i])) / simplify_opacity_error);
    return error;
}
auto osbx_export_frame_error(const osbx_export_frame_t& expected,
    const osbx_export_frame_t& actual, const osbx_document_t& document,
    std::span<const osbx_osb_image_size_t> sizes) -> double {
    if (expected.time != actual.time || expected.instances.size() != actual.instances.size())
        return std::numeric_limits<double>::infinity();
    double error = 0;
    for (std::size_t i = 0; i < expected.instances.size(); ++i) {
        const auto& source = expected.instances[i];
        const auto* size = osbx_export_image_size(document, source.image, sizes);
        const auto origin = storyboard_origin_t::from(source.origin);
        if (!size || !size->width || !size->height || !origin) return std::numeric_limits<double>::infinity();
        const osbx_canvas_t canvas;
        const visual_image_quad_request_t request{.source_width = size->width, .source_height = size->height,
            .destination = {.width = canvas.width, .height = canvas.height},
            .origin_x = origin->x, .origin_y = origin->y};
        auto expected_instance = source, actual_instance = actual.instances[i];
        const auto mapping = osbx_export_canvas_transform(document.canvas);
        expected_instance.world = mapping * expected_instance.world;
        actual_instance.world = mapping * actual_instance.world;
        error = std::max(error, osbx_export_instance_error(expected_instance, actual_instance, request));
    }
    return error;
}

namespace {
using bake_result_t = result_t<osbx_export_track_bake_t, std::string>;
struct track_baker_t {
    const osbx_export_bake_request_t& request;
    osbx_export_budget_t& budget;
    osbx_evaluation_index_t source_index;
    osbx_evaluation_index_t candidate_index;
    frame_arena_t source_arena{}, candidate_arena{};
    std::vector<std::vector<double>> dense;
    std::int64_t begin{};

    auto error_at(storyboard_time_t time, const osbx_evaluation_index_t& actual_index) -> double {
        const auto expected = osbx_export_frame_t::from(source_index, time, source_arena, budget, request.frame_options);
        const auto actual = osbx_export_frame_t::from(actual_index, time, candidate_arena, budget, request.frame_options);
        if (!expected || !actual) return std::numeric_limits<double>::infinity();
        return osbx_export_frame_error(*expected, *actual, request.source, request.image_sizes);
    }
    auto group_error(const simplify_key_t& key, const std::array<double, 3>& values,
        std::size_t component) -> double {
        const auto offset = static_cast<std::size_t>(static_cast<std::int64_t>(key.time_ms) - begin);
        request.candidate_track.default_value = dense[offset];
        const auto dimensions = std::min(std::size_t{3}, dense[offset].size() - component);
        for (std::size_t i = 0; i < dimensions; ++i)
            request.candidate_track.default_value[component + i] = values[i];
        return error_at({.milliseconds = static_cast<std::int64_t>(key.time_ms)}, candidate_index);
    }
    auto reduce_group(std::size_t component) -> simplify_result_t {
        std::vector<simplify_key_t> keys;
        keys.reserve(dense.size());
        const auto dimensions = std::min(std::size_t{3}, dense.front().size() - component);
        for (std::size_t i = 0; i < dense.size(); ++i) {
            simplify_key_t key{.time_ms = static_cast<double>(begin + static_cast<std::int64_t>(i))};
            std::copy_n(dense[i].begin() + static_cast<std::ptrdiff_t>(component), dimensions, key.value.begin());
            keys.push_back(key);
        }
        simplify_metric_t metric{.dimensions = dimensions, .tolerance = simplify_position.tolerance,
            .project = [](const auto& key) { return key.value; },
            .emit = [](const auto& key) {
                auto values = key.value;
                for (auto& value : values) value = round_simplify_value(value, 9);
                return values;
            },
            .error = [&](const auto& key, const auto& values) { return group_error(key, values, component); }};
        return simplify_to_metric(keys, metric,
            {.maximum_work = budget.remaining_work, .consume_work = [&] { return budget.consume(1); }});
    }
    auto reconstruct() -> result_t<osbx_track_t<>, std::string> {
        std::set<std::size_t> kept;
        for (std::size_t component = 0; component < dense.front().size(); component += 3) {
            auto reduced = reduce_group(component);
            if (!reduced) return std::unexpected(reduced.error().message);
            kept.insert(reduced->begin(), reduced->end());
        }
        osbx_track_t<> output{.default_value = request.source_track.default_value};
        std::vector<std::size_t> ordered(kept.begin(), kept.end());
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            if (i >= simplify_keys_per_100ms && ordered[i] - ordered[i - simplify_keys_per_100ms] < 100)
                return std::unexpected("Combined native track exceeds shared four-key/100ms density");
            auto values = dense[ordered[i]];
            for (auto& value : values) value = round_simplify_value(value, 9);
            const auto time = begin + static_cast<std::int64_t>(ordered[i]);
            output.keys.push_back({.id = request.node_id + "/native-bake/" + std::to_string(time),
                .time_ms = time, .value = std::move(values)});
        }
        return output;
    }
    auto verify(osbx_export_track_bake_t& output) -> std::string {
        request.candidate_track = output.track;
        const auto rebuilt = osbx_evaluation_index_t::from(request.candidate);
        if (!rebuilt.value) return "Native bake reconstruction failed validation";
        const auto first = std::min(begin, request.playback_span.begin_time_ms);
        const auto last = std::max(begin + static_cast<std::int64_t>(dense.size() - 1), request.playback_span.end_time_ms);
        output.report.reason = "Full-scene normalized metric verified on [" + std::to_string(first) + "," +
            std::to_string(last) + "] ms at quarter-ms samples (both endpoints included); integer-ms keys rounded once "
            "to 9 decimals, at most four keys per half-open 100ms; no continuous-time certificate";
        const auto duration = static_cast<std::uint64_t>(last) - static_cast<std::uint64_t>(first);
        if (duration > budget.remaining_work / std::max(std::size_t{1}, budget.frame_work))
            return "Native bake playback verification exceeds aggregate work budget";
        for (std::uint64_t i = 0; i <= duration; ++i) {
            for (std::uint32_t fraction = 0; fraction < 4; ++fraction) {
                if (i == duration && fraction != 0) break;
                const storyboard_time_t time{.milliseconds = first + static_cast<std::int64_t>(i),
                    .remainder = fraction, .denominator = 4};
                const auto error = error_at(time, *rebuilt.value);
                output.report.worst_error = std::max(output.report.worst_error, error);
                if (!std::isfinite(error) || error > 1 + 1e-9)
                    return "Native bake exceeds full-scene normalized metric on quarter-ms verification grid";
            }
        }
        return {};
    }
    auto run() -> bake_result_t {
        if (request.source_track.keys.empty()) return std::unexpected("Native bake needs authored key coverage");
        begin = request.source_track.keys.front().time_ms;
        const auto end = request.source_track.keys.back().time_ms;
        const auto duration = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
        if (duration >= simplify_limits_t::maximum_input_keys || std::abs(static_cast<long double>(begin)) > 9'007'199'254'740'991.L ||
            std::abs(static_cast<long double>(end)) > 9'007'199'254'740'991.L)
            return std::unexpected("Native bake exceeds shared source input/clock bounds");
        dense.reserve(static_cast<std::size_t>(duration + 1));
        for (std::size_t i = 0; i <= duration; ++i) {
            if (!budget.consume(1)) return std::unexpected("Native bake source sampling work exhausted");
            dense.push_back(sample_osbx_numeric_track(request.source_track,
                storyboard_time_t{.milliseconds = begin + static_cast<std::int64_t>(i)}, request.frame_options.scenario));
        }
        auto track = reconstruct();
        if (!track) return std::unexpected(track.error());
        osbx_export_track_bake_t output{.track = std::move(*track),
            .report = {.node_id = request.node_id, .classification = osbx_osb_class_t::baked,
                .key_count = 0, .keys_before_simplify = dense.size()}};
        output.report.key_count = output.track.keys.size();
        if (auto failure = verify(output); !failure.empty()) return std::unexpected(std::move(failure));
        return output;
    }
};
} // namespace

auto osbx_export_track_bake_t::from(osbx_export_bake_request_t request, osbx_export_budget_t& budget)
    -> result_t<osbx_export_track_bake_t, std::string> {
    if (request.playback_span.begin_time_ms > request.playback_span.end_time_ms)
        return std::unexpected("Native bake requires an ordered playback domain");
    const auto source_index = osbx_evaluation_index_t::from(request.source);
    if (!source_index.value) return std::unexpected("Invalid or unprepared native bake source");
    const auto trigger_dependent = std::ranges::any_of(request.source_track.program, [](const auto& block) {
        return std::holds_alternative<osbx_trigger_t<>>(block.data);
    });
    if (trigger_dependent && !request.frame_options.scenario)
        return std::unexpected("Trigger-dependent native bake requires an explicit sealed scenario");
    auto saved = request.candidate_track;
    request.candidate_track.program.clear();
    request.candidate_track.keys.clear();
    request.candidate_track.lua.reset();
    const auto candidate_index = osbx_evaluation_index_t::from(request.candidate);
    if (!candidate_index.value) { request.candidate_track = std::move(saved); return std::unexpected("Invalid native bake candidate"); }
    track_baker_t baker{.request = request, .budget = budget, .source_index = *source_index.value,
        .candidate_index = *candidate_index.value};
    auto result = baker.run();
    request.candidate_track = std::move(saved);
    return result;
}
} // namespace wormedit
