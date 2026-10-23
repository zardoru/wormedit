#pragma once

#include <wormedit/core/bms36.h>
#include <wormedit/core/bms_program.h>
#include <array>
#include <string_view>

namespace wormedit {

struct bms_timing_directive_t {
    std::string_view name;
    std::string_view channel;
    bms_timing_definition_kind_t kind;
    auto operator==(const bms_timing_directive_t&) const -> bool = default;
    static auto from(std::string_view command) -> const bms_timing_directive_t*;
    static auto for_channel(std::string_view channel) -> const bms_timing_directive_t*;
};
inline constexpr std::array bms_timing_directives{
    bms_timing_directive_t{"BPM", "08", bms_timing_definition_kind_t::bpm},
    bms_timing_directive_t{"STOP", "09", bms_timing_definition_kind_t::stop},
    bms_timing_directive_t{"SCROLL", "SC", bms_timing_definition_kind_t::scroll}};

inline auto bms_timing_directive_t::from(std::string_view command) -> const bms_timing_directive_t* {
    for (const auto& directive : bms_timing_directives) {
        if (command.size() == directive.name.size() + 2 && command.starts_with(directive.name) &&
            bms36_value(command[command.size() - 2]) >= 0 && bms36_value(command.back()) >= 0)
            return &directive;
    }
    return nullptr;
}
inline auto bms_timing_directive_t::for_channel(std::string_view channel) -> const bms_timing_directive_t* {
    for (const auto& directive : bms_timing_directives)
        if (directive.channel == channel) return &directive;
    return nullptr;
}
} // namespace wormedit
