#pragma once

#include <wormedit/audio/preview_bridge.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/timing.h>

#include <cstdint>
#include <cmath>
#include <optional>
#include <string>
#include <variant>

namespace wormedit::sys {

[[nodiscard]] inline auto preview_cursor_row(const measure_index_t& measures,
                                             local_position_t cursor, double subrow)
    -> std::optional<exact_chart_row_t> {
    const auto absolute = (measures).row_of( cursor);
    if (!absolute || !std::isfinite(subrow)) return std::nullopt;
    const auto row = static_cast<long double>(*absolute) + subrow;
    if (row < 0.0L || row > measures.total_rows) return std::nullopt;
    auto whole = static_cast<std::uint64_t>(std::floor(row));
    constexpr std::uint64_t fractional_scale = 1'000'000;
    auto fraction = static_cast<std::uint64_t>(std::llround((row - whole) * fractional_scale));
    if (fraction == fractional_scale) {
        ++whole;
        fraction = 0;
    }
    return exact_chart_row_t{whole, {fraction, fractional_scale}};
}

[[nodiscard]] inline auto preview_cursor_frame(const chart_document_t& chart,
                                               const timing_index_t& timing,
                                               local_position_t cursor, double subrow = 0.0,
                                               const result_t<measure_index_t, diagnostic_list_t>* geometry = nullptr)
    -> std::variant<std::uint64_t, std::string> {
    std::optional<result_t<measure_index_t, diagnostic_list_t>> built;
    if (!geometry) {
        built = measure_index_t::from(chart);
        geometry = &*built;
    }
    if (!*geometry) return std::string{"Chart cursor has invalid measure geometry."};
    const auto row = preview_cursor_row(**geometry, cursor, subrow);
    if (!row) return std::string{"Chart cursor has an invalid fractional position."};
    const auto at = timing.query_seconds(*row);
    if (!at) return std::string{"Chart cursor has no playable timing position."};
    if (at->value.numerator < 0) return std::uint64_t{0};
    const auto frame = sample_frame_t::from(at->value, audio::output_sample_rate);
    if (!frame) return std::string{"Chart cursor time cannot be converted to audio frames."};
    return frame->value.frame;
}

} // namespace wormedit::sys
