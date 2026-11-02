#include <wormedit/core/visual_layout.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>

namespace wormedit {
namespace {

auto is_valid_destination(const visual_destination_rect_t& destination) noexcept -> bool {
    return std::isfinite(destination.left) && std::isfinite(destination.top) &&
           std::isfinite(destination.width) && destination.width > 0.0 &&
           std::isfinite(destination.height) && destination.height > 0.0;
}

} // namespace

auto is_valid_visual_fit_policy(visual_fit_policy_t fit) noexcept -> bool {
    switch (fit) {
    case visual_fit_policy_t::stretch:
    case visual_fit_policy_t::contain:
    case visual_fit_policy_t::cover:
        return true;
    }
    return false;
}

auto visual_image_layout_t::from(const visual_layout_request_t& request)
    -> std::optional<visual_image_layout_t> {
    if (request.source_width == 0 || request.source_height == 0 ||
        !is_valid_destination(request.destination) || !is_valid_visual_fit_policy(request.fit)) {
        return std::nullopt;
    }
    visual_image_layout_t layout{
        .destination = request.destination,
        .source = {.left = 0,
                   .top = 0,
                   .width = request.source_width,
                   .height = request.source_height},
    };
    if (request.fit == visual_fit_policy_t::stretch) {
        return layout;
    }

    const auto source_aspect = static_cast<long double>(request.source_width) /
                               static_cast<long double>(request.source_height);
    const auto destination_aspect = static_cast<long double>(request.destination.width) /
                                    static_cast<long double>(request.destination.height);
    if (!std::isfinite(source_aspect) || !std::isfinite(destination_aspect) ||
        source_aspect <= 0.0L || destination_aspect <= 0.0L) {
        return std::nullopt;
    }
    if (request.fit == visual_fit_policy_t::contain) {
        if (source_aspect > destination_aspect) {
            layout.destination.height = static_cast<double>(
                static_cast<long double>(request.destination.width) / source_aspect);
            layout.destination.top +=
                (request.destination.height - layout.destination.height) * 0.5;
        } else if (source_aspect < destination_aspect) {
            layout.destination.width = static_cast<double>(
                static_cast<long double>(request.destination.height) * source_aspect);
            layout.destination.left += (request.destination.width - layout.destination.width) * 0.5;
        }
        return is_valid_destination(layout.destination) ? std::optional{layout} : std::nullopt;
    }

    if (source_aspect > destination_aspect) {
        const auto desired = static_cast<long double>(request.source_height) * destination_aspect;
        layout.source.width = std::clamp(static_cast<std::uint32_t>(std::floor(desired)),
                                         std::uint32_t{1}, request.source_width);
        layout.source.left = (request.source_width - layout.source.width) / 2U;
    } else if (source_aspect < destination_aspect) {
        const auto desired = static_cast<long double>(request.source_width) / destination_aspect;
        layout.source.height = std::clamp(static_cast<std::uint32_t>(std::floor(desired)),
                                          std::uint32_t{1}, request.source_height);
        layout.source.top = (request.source_height - layout.source.height) / 2U;
    }
    return layout;
}

auto visual_affine_t::from(double x, double y, double scale_x, double scale_y,
                           double rotation_radians) -> visual_affine_t {
    const auto cosine = std::cos(rotation_radians);
    const auto sine = std::sin(rotation_radians);
    return {cosine * scale_x, sine * scale_x, -sine * scale_y, cosine * scale_y, x, y};
}

auto visual_affine_t::operator*(const visual_affine_t& r) const -> visual_affine_t {
    const auto& l = *this;
    return {l[0] * r[0] + l[2] * r[1], l[1] * r[0] + l[3] * r[1],
            l[0] * r[2] + l[2] * r[3], l[1] * r[2] + l[3] * r[3],
            l[0] * r[4] + l[2] * r[5] + l[4], l[1] * r[4] + l[3] * r[5] + l[5]};
}

auto visual_affine_t::is_finite() const -> bool {
    return std::ranges::all_of(values, [](double value) { return std::isfinite(value); });
}

auto visual_affine_t::determinant() const -> double {
    return values[0] * values[3] - values[1] * values[2];
}
auto visual_affine_t::inverse() const -> std::optional<visual_affine_t> {
    const auto det = determinant();
    if (!is_finite() || !std::isfinite(det) || det == 0) return {};
    visual_affine_t result{values[3] / det, -values[1] / det, -values[2] / det, values[0] / det,
        (values[2] * values[5] - values[3] * values[4]) / det,
        (values[1] * values[4] - values[0] * values[5]) / det};
    return result.is_finite() ? std::optional{result} : std::nullopt;
}
auto visual_affine_t::apply(visual_point_t point) const -> visual_point_t {
    return {.x = values[0] * point.x + values[2] * point.y + values[4],
        .y = values[1] * point.x + values[3] * point.y + values[5]};
}

auto visual_image_quad_t::from(const visual_image_quad_request_t& request)
    -> std::optional<visual_image_quad_t> {
    if (request.source_width == 0 || request.source_height == 0 ||
        !is_valid_destination(request.destination) || !std::isfinite(request.logical_width) ||
        request.logical_width <= 0.0 || !std::isfinite(request.logical_height) ||
        request.logical_height <= 0.0 ||
        !std::isfinite(request.origin_x) || request.origin_x < 0.0 || request.origin_x > 1.0 ||
        !std::isfinite(request.origin_y) || request.origin_y < 0.0 || request.origin_y > 1.0 ||
        !request.transform.is_finite() || request.transform.determinant() == 0.0 ||
        !std::isfinite(request.transform.determinant())) {
        return std::nullopt;
    }
    const auto canvas_scale = std::min(request.destination.width / request.logical_width,
                                       request.destination.height / request.logical_height);
    if (!std::isfinite(canvas_scale) || canvas_scale <= 0.0) {
        return std::nullopt;
    }
    const auto canvas_left =
        request.destination.left +
        (request.destination.width - request.logical_width * canvas_scale) * 0.5;
    const auto canvas_top =
        request.destination.top +
        (request.destination.height - request.logical_height * canvas_scale) * 0.5;
    const auto left = -request.origin_x * static_cast<double>(request.source_width);
    const auto right = (1.0 - request.origin_x) * static_cast<double>(request.source_width);
    const auto top = -request.origin_y * static_cast<double>(request.source_height);
    const auto bottom = (1.0 - request.origin_y) * static_cast<double>(request.source_height);
    const auto& m = request.transform;
    const auto transform = [&](double local_x, double local_y) {
        return visual_point_t{.x = canvas_left + m[4] * canvas_scale +
                                  local_x * m[0] * canvas_scale + local_y * m[2] * canvas_scale,
                              .y = canvas_top + m[5] * canvas_scale +
                                  local_x * m[1] * canvas_scale + local_y * m[3] * canvas_scale};
    };
    visual_image_quad_t quad{{transform(left, top), transform(right, top), transform(right, bottom),
                              transform(left, bottom)}};
    if (!std::ranges::all_of(quad.points, [](const auto& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        })) {
        return std::nullopt;
    }
    return quad;
}

} // namespace wormedit
