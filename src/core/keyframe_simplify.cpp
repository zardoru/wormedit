#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <wormedit/core/keyframe_simplify.h>

namespace wormedit {
namespace {
struct work_t {
    simplify_limits_t limits;
    auto consume() -> bool {
        if (!limits.maximum_work)
            return false;
        --limits.maximum_work;
        return !limits.consume_work || limits.consume_work();
    }
};
auto work_error() -> error_t<failure_code_t> {
    return {failure_code_t::capacity_exceeded,
            "Keyframe simplification work budget exhausted or cancelled."};
}
auto validate(std::span<const simplify_key_t> keys, work_t& work)
    -> result_t<void, error_t<failure_code_t>> {
    if (keys.size() > simplify_limits_t::maximum_input_keys)
        return std::unexpected(
            error_t{failure_code_t::capacity_exceeded, "Keyframe input limit exceeded."});
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (!work.consume())
            return std::unexpected(work_error());
        if (!std::isfinite(keys[i].time_ms) || (i && keys[i - 1].time_ms >= keys[i].time_ms))
            return std::unexpected(
                error_t{failure_code_t::invalid_data,
                        "Keyframe times must be finite and strictly increasing."});
        for (const auto value : keys[i].value)
            if (!std::isfinite(value))
                return std::unexpected(
                    error_t{failure_code_t::invalid_data, "Keyframe values must be finite."});
    }
    return {};
}
auto project_keys(std::span<const simplify_key_t> keys, const simplify_projection_t& project,
                  work_t& work) -> result_t<std::vector<simplify_key_t>, error_t<failure_code_t>> {
    std::vector<simplify_key_t> output;
    output.reserve(keys.size());
    for (const auto& key : keys) {
        if (!work.consume())
            return std::unexpected(work_error());
        output.push_back({.time_ms = key.time_ms, .value = project(key)});
    }
    if (auto valid = validate(output, work); !valid)
        return std::unexpected(valid.error());
    return output;
}
// Perpendicular distance to the infinite line in (milliseconds, components).
// Normalizing with hypot avoids overflow in dot(startToEnd,startToEnd).
auto distance(const simplify_key_t& a, const simplify_key_t& p, const simplify_key_t& b,
              std::size_t dimensions) -> double {
    if (dimensions == 1) {
        const auto dx = b.time_ms - a.time_ms;
        const auto dy = b.value[0] - a.value[0];
        return std::abs(dx * (p.value[0] - a.value[0]) - dy * (p.time_ms - a.time_ms)) /
               std::hypot(dx, dy);
    }
    std::array<double, 4> direction{b.time_ms - a.time_ms};
    std::array<double, 4> offset{p.time_ms - a.time_ms};
    double length = std::abs(direction[0]);
    for (std::size_t d = 0; d < dimensions; ++d) {
        direction[d + 1] = b.value[d] - a.value[d];
        offset[d + 1] = p.value[d] - a.value[d];
        length = std::hypot(length, direction[d + 1]);
    }
    double along = 0;
    for (std::size_t d = 0; d <= dimensions; ++d) {
        direction[d] /= length;
        along += offset[d] * direction[d];
    }
    double result = 0;
    for (std::size_t d = 0; d <= dimensions; ++d)
        result = std::hypot(result, offset[d] - along * direction[d]);
    return result;
}
auto rdp(std::span<const simplify_key_t> keys, double tolerance, std::size_t dimensions,
         work_t& work) -> simplify_result_t {
    std::vector<std::size_t> kept;
    if (keys.empty())
        return kept;
    std::vector<bool> selected(keys.size());
    selected.front() = selected.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    stack.reserve(keys.size());
    stack.emplace_back(0, keys.size() - 1);
    while (!stack.empty()) {
        const auto [first, last] = stack.back();
        stack.pop_back();
        double maximum = tolerance;
        auto worst = first;
        for (auto i = first + 1; i < last; ++i) {
            if (!work.consume())
                return std::unexpected(work_error());
            const auto error = distance(keys[first], keys[i], keys[last], dimensions);
            if (!std::isfinite(error))
                return std::unexpected(
                    error_t{failure_code_t::arithmetic_overflow, "Keyframe distance overflow."});
            if (error > maximum) {
                maximum = error;
                worst = i;
            }
        }
        if (worst == first)
            continue;
        selected[worst] = true;
        stack.emplace_back(worst, last);
        stack.emplace_back(first, worst);
    }
    kept.reserve(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (selected[i])
            kept.push_back(i);
    return kept;
}
auto run_rdp(std::span<const simplify_key_t> keys, double tolerance, std::size_t dimensions,
             const simplify_projection_t& project, work_t& work) -> simplify_result_t {
    if (!project || !std::isfinite(tolerance) || tolerance < 0 || dimensions < 1 || dimensions > 3)
        return std::unexpected(
            error_t{failure_code_t::invalid_request,
                    "Invalid simplification projection, dimension or tolerance."});
    if (auto valid = validate(keys, work); !valid)
        return std::unexpected(valid.error());
    auto projected = project_keys(keys, project, work);
    if (!projected)
        return std::unexpected(projected.error());
    return rdp(*projected, tolerance, dimensions, work);
}
auto has_density(std::span<const simplify_key_t> keys, std::span<const std::size_t> kept) -> bool {
    for (std::size_t i = simplify_keys_per_100ms; i < kept.size(); ++i)
        if (keys[kept[i]].time_ms - keys[kept[i - simplify_keys_per_100ms]].time_ms < 100)
            return false;
    return true;
}
auto worst_metric(std::span<const simplify_key_t> keys, std::span<const simplify_key_t> emitted,
                  std::span<const std::size_t> kept, const simplify_metric_t& metric, work_t& work)
    -> result_t<std::optional<std::size_t>, error_t<failure_code_t>> {
    std::optional<std::size_t> worst;
    double maximum = 1;
    std::size_t segment = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (!work.consume())
            return std::unexpected(work_error());
        while (segment + 1 < kept.size() && kept[segment + 1] <= i)
            ++segment;
        const auto& a = emitted[kept[segment]];
        auto value = a.value;
        if (segment + 1 < kept.size()) {
            const auto& b = emitted[kept[segment + 1]];
            const auto p = (keys[i].time_ms - a.time_ms) / (b.time_ms - a.time_ms);
            for (std::size_t d = 0; d < metric.dimensions; ++d)
                value[d] = std::lerp(a.value[d], b.value[d], p);
        }
        const auto error = metric.error(keys[i], value);
        if (!std::isfinite(error) || error < 0)
            return std::unexpected(error_t{failure_code_t::invalid_data,
                                           "Metric must return finite nonnegative error."});
        if (error > maximum) {
            maximum = error;
            worst = i;
        }
    }
    return worst;
}
} // namespace

auto simplify_equal_runs(std::span<const simplify_key_t> keys, simplify_limits_t limits)
    -> simplify_result_t {
    work_t work{std::move(limits)};
    if (auto valid = validate(keys, work); !valid)
        return std::unexpected(valid.error());
    std::vector<std::size_t> output;
    output.reserve(keys.size());
    for (std::size_t first = 0; first < keys.size();) {
        auto last = first;
        while (last + 1 < keys.size() && keys[last + 1].value == keys[first].value)
            ++last;
        output.push_back(first);
        if (last != first)
            output.push_back(last);
        first = last + 1;
    }
    return output;
}
auto simplify_rdp_1d(std::span<const simplify_key_t> keys, double tolerance,
                     const simplify_projection_t& project, simplify_limits_t limits)
    -> simplify_result_t {
    work_t work{std::move(limits)};
    return run_rdp(keys, tolerance, 1, project, work);
}
auto simplify_rdp_2d(std::span<const simplify_key_t> keys, double tolerance,
                     const simplify_projection_t& project, simplify_limits_t limits)
    -> simplify_result_t {
    work_t work{std::move(limits)};
    return run_rdp(keys, tolerance, 2, project, work);
}
auto simplify_rdp_3d(std::span<const simplify_key_t> keys, double tolerance,
                     const simplify_projection_t& project, simplify_limits_t limits)
    -> simplify_result_t {
    work_t work{std::move(limits)};
    return run_rdp(keys, tolerance, 3, project, work);
}
auto round_simplify_value(double value, int decimals) -> double {
    // Match Math.Round's default ties-to-even for the scaled decimal value.
    static constexpr std::array factors{1.,
                                        10.,
                                        100.,
                                        1000.,
                                        10000.,
                                        100000.,
                                        1000000.,
                                        10000000.,
                                        100000000.,
                                        1000000000.,
                                        10000000000.,
                                        100000000000.,
                                        1000000000000.,
                                        10000000000000.,
                                        100000000000000.,
                                        1000000000000000.};
    if (decimals < 0 || static_cast<std::size_t>(decimals) >= factors.size())
        return std::numeric_limits<double>::quiet_NaN();
    const auto factor = factors[static_cast<std::size_t>(decimals)];
    const auto scaled = value * factor;
    const auto lower = std::floor(scaled);
    const auto fraction = scaled - lower;
    const auto up = fraction > 0.5 || (fraction == 0.5 && std::fmod(lower, 2) != 0);
    return (up ? lower + 1 : lower) / factor;
}
auto simplify_to_metric(std::span<const simplify_key_t> keys, const simplify_metric_t& metric,
                        simplify_limits_t limits) -> simplify_result_t {
    if (!metric.emit || !metric.error)
        return std::unexpected(
            error_t{failure_code_t::invalid_request, "Metric and emit projections are required."});
    work_t work{std::move(limits)};
    auto kept = run_rdp(keys, metric.tolerance, metric.dimensions, metric.project, work);
    if (!kept || keys.empty())
        return kept;
    auto emitted = project_keys(keys, metric.emit, work);
    if (!emitted)
        return std::unexpected(emitted.error());
    for (std::size_t pass = 0; pass <= keys.size(); ++pass) {
        if (!has_density(keys, *kept))
            return std::unexpected(
                error_t{failure_code_t::unsupported,
                        "Required keyframe density exceeds four keys per 100ms."});
        auto worst = worst_metric(keys, *emitted, *kept, metric, work);
        if (!worst)
            return std::unexpected(worst.error());
        if (!*worst)
            return kept;
        const auto at = std::lower_bound(kept->begin(), kept->end(), **worst);
        if (at != kept->end() && *at == **worst)
            return std::unexpected(error_t{failure_code_t::unsupported,
                                           "Emit rounding cannot satisfy the required metric."});
        kept->insert(at, **worst);
    }
    return std::unexpected(work_error());
}
} // namespace wormedit
