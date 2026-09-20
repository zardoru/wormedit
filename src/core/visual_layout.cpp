#include <wormedit/core/visual_layout.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit {
namespace {

auto valid_destination(const visual_destination_rect_t& destination) noexcept -> bool {
    return std::isfinite(destination.left) && std::isfinite(destination.top) &&
           std::isfinite(destination.width) && destination.width > 0.0 &&
           std::isfinite(destination.height) && destination.height > 0.0;
}

} // namespace

auto valid_visual_fit_policy(visual_fit_policy_t fit) noexcept -> bool {
    switch (fit) {
    case visual_fit_policy_t::stretch:
    case visual_fit_policy_t::contain:
    case visual_fit_policy_t::cover:
        return true;
    }
    return false;
}

auto make_visual_image_layout(const visual_layout_request_t& request)
    -> std::optional<visual_image_layout_t> {
    if (request.source_width == 0 || request.source_height == 0 ||
        !valid_destination(request.destination) || !valid_visual_fit_policy(request.fit)) {
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
        return valid_destination(layout.destination) ? std::optional{layout} : std::nullopt;
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

} // namespace wormedit
