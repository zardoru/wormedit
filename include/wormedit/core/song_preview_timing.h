#pragma once

#include <wormedit/core/song_preview.h>
#include <wormedit/core/timing.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// Source-song milliseconds of a chart row, to the nearest millisecond. Nullopt
// before the song starts or past the preview range.
[[nodiscard]] auto song_preview_milliseconds_at(const timing_index_t& timing, std::uint64_t row)
    -> std::optional<std::int64_t>;

// The nearest chart row sounding at a source-song millisecond, clamped into the chart.
[[nodiscard]] auto song_preview_row_at(const timing_index_t& timing, std::int64_t milliseconds)
    -> std::optional<std::uint64_t>;

// The chart rows a preview covers, as Compose draws it. A point-only preview ends at the
// audition default, clamped to the chart's last row when the chart ends sooner.
struct song_preview_rows_t {
    std::uint64_t begin{};
    std::uint64_t end{};
    auto operator==(const song_preview_rows_t&) const -> bool = default;
};

[[nodiscard]] auto song_preview_rows(const timing_index_t& timing, const song_preview_t& preview)
    -> std::optional<song_preview_rows_t>;

} // namespace wormedit
