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

auto visual_image_quad_t::from(const visual_image_quad_request_t& request)
    -> std::optional<visual_image_quad_t> {
    if (request.source_width == 0 || request.source_height == 0 ||
        !is_valid_destination(request.destination) || !std::isfinite(request.logical_width) ||
        request.logical_width <= 0.0 || !std::isfinite(request.logical_height) ||
        request.logical_height <= 0.0 || !std::isfinite(request.x) || !std::isfinite(request.y) ||
        !std::isfinite(request.origin_x) || request.origin_x < 0.0 || request.origin_x > 1.0 ||
        !std::isfinite(request.origin_y) || request.origin_y < 0.0 || request.origin_y > 1.0 ||
        !std::isfinite(request.scale_x) || request.scale_x == 0.0 ||
        !std::isfinite(request.scale_y) || request.scale_y == 0.0 ||
        !std::isfinite(request.rotation_radians)) {
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
    const auto pivot_x = canvas_left + request.x * canvas_scale;
    const auto pivot_y = canvas_top + request.y * canvas_scale;
    const auto left = -request.origin_x * static_cast<double>(request.source_width) *
                      request.scale_x * canvas_scale;
    const auto right = (1.0 - request.origin_x) * static_cast<double>(request.source_width) *
                       request.scale_x * canvas_scale;
    const auto top = -request.origin_y * static_cast<double>(request.source_height) *
                     request.scale_y * canvas_scale;
    const auto bottom = (1.0 - request.origin_y) * static_cast<double>(request.source_height) *
                        request.scale_y * canvas_scale;
    const auto cosine = std::cos(request.rotation_radians);
    const auto sine = std::sin(request.rotation_radians);
    const auto transform = [&](double local_x, double local_y) {
        return visual_point_t{.x = pivot_x + local_x * cosine - local_y * sine,
                              .y = pivot_y + local_x * sine + local_y * cosine};
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

// Compatibility for the sys/ui migration wave.
auto make_visual_image_layout(const visual_layout_request_t& request)
    -> std::optional<visual_image_layout_t> {
    return visual_image_layout_t::from(request);
}

// Compatibility for the sys/ui migration wave.
auto make_visual_image_quad(const visual_image_quad_request_t& request)
    -> std::optional<visual_image_quad_t> {
    return visual_image_quad_t::from(request);
}

} // namespace wormedit
