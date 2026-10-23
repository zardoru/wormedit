#pragma once
#include <wormedit/core/vocabulary.h>
#include <array>
#include <algorithm>
#include <string_view>

namespace wormedit {
struct bms_bga_channel_t {
    visual_event_kind_t kind;
    std::string_view channel;
    auto operator==(const bms_bga_channel_t&) const -> bool = default;
    static auto from(visual_event_kind_t kind) -> const bms_bga_channel_t*;
    static auto from(std::string_view channel) -> const bms_bga_channel_t*;
};
inline constexpr std::array bms_bga_channels{
    bms_bga_channel_t{visual_event_kind_t::background, "04"},
    bms_bga_channel_t{visual_event_kind_t::poor, "06"},
    bms_bga_channel_t{visual_event_kind_t::layer, "07"},
    bms_bga_channel_t{visual_event_kind_t::layer2, "0A"}};
inline auto bms_bga_channel_t::from(visual_event_kind_t kind) -> const bms_bga_channel_t* {
    const auto entry = std::ranges::find(bms_bga_channels, kind, &bms_bga_channel_t::kind);
    return entry == bms_bga_channels.end() ? nullptr : &*entry;
}
inline auto bms_bga_channel_t::from(std::string_view channel) -> const bms_bga_channel_t* {
    const auto entry = std::ranges::find(bms_bga_channels, channel, &bms_bga_channel_t::channel);
    return entry == bms_bga_channels.end() ? nullptr : &*entry;
}
} // namespace wormedit
