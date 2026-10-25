#include <wormedit/core/song_preview.h>
#include <wormedit/core/song_preview_timing.h>

#include <wormedit/core/numeric_text.h>
#include <wormedit/core/rational.h>

#include "text_util.h"

#include <limits>

namespace wormedit {
namespace {

constexpr std::string_view field_whitespace{" \t\r\n"};

// Nearest whole millisecond of a nonnegative decimal-seconds value inside the
// preview range; StepMania spellings finer than milliseconds stay in the
// retained source text.
auto milliseconds_from_seconds(std::string_view text) -> std::optional<std::int64_t> {
    const auto value = parse_exact_decimal(text_util::trim(text, field_whitespace));
    if (!value || value->numerator < 0 || value->denominator == 0) return std::nullopt;
    constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 1000;
    if (value->numerator > limit) return std::nullopt;
    const auto scaled = static_cast<std::uint64_t>(value->numerator) * 1000U;
    const auto milliseconds = (scaled + value->denominator / 2U) / value->denominator;
    if (milliseconds > static_cast<std::uint64_t>(song_preview_t::maximum_milliseconds))
        return std::nullopt;
    return static_cast<std::int64_t>(milliseconds);
}

auto seconds_text(std::int64_t milliseconds) -> std::string {
    auto fraction = std::to_string(milliseconds % 1000);
    fraction.insert(0, 3 - fraction.size(), '0');
    return std::to_string(milliseconds / 1000) + "." + fraction;
}

} // namespace

auto song_preview_t::from_osu_preview_time(std::string_view text) -> std::optional<song_preview_t> {
    std::int64_t milliseconds{};
    if (!parse_integer(text_util::trim(text, field_whitespace), milliseconds) || milliseconds < 0 ||
        milliseconds > maximum_milliseconds)
        return std::nullopt;
    return song_preview_t{.start_milliseconds = milliseconds};
}

auto song_preview_t::from_stepmania_sample(std::string_view start,
                                           std::optional<std::string_view> length)
    -> std::optional<song_preview_t> {
    const auto start_milliseconds = milliseconds_from_seconds(start);
    if (!start_milliseconds) return std::nullopt;
    auto preview = song_preview_t{.start_milliseconds = *start_milliseconds};
    if (length) {
        const auto length_milliseconds = milliseconds_from_seconds(*length);
        if (length_milliseconds && *length_milliseconds > 0)
            preview.length_milliseconds = length_milliseconds;
    }
    return preview.is_valid() ? std::optional{preview} : std::nullopt;
}

auto song_preview_t::is_valid() const noexcept -> bool {
    if (start_milliseconds < 0 || start_milliseconds > maximum_milliseconds) return false;
    if (!length_milliseconds) return true;
    return *length_milliseconds > 0 &&
           *length_milliseconds <= maximum_milliseconds - start_milliseconds;
}

auto song_preview_t::end_milliseconds() const noexcept -> std::optional<std::int64_t> {
    if (!length_milliseconds) return std::nullopt;
    return start_milliseconds + *length_milliseconds;
}

auto song_preview_t::osu_preview_time_text() const -> std::string {
    return std::to_string(start_milliseconds);
}

auto song_preview_t::stepmania_sample_start_text() const -> std::string {
    return seconds_text(start_milliseconds);
}

auto song_preview_t::stepmania_sample_length_text() const -> std::optional<std::string> {
    if (!length_milliseconds) return std::nullopt;
    return seconds_text(*length_milliseconds);
}

auto song_preview_milliseconds_at(const timing_index_t& timing, std::uint64_t row)
    -> std::optional<std::int64_t> {
    const auto seconds = timing.seconds_at(row).seconds;
    if (!seconds || seconds->numerator < 0) return std::nullopt;
    const auto milliseconds = sample_frame_at(*seconds, 1'000).frame;
    if (!milliseconds || *milliseconds > static_cast<std::uint64_t>(song_preview_t::maximum_milliseconds))
        return std::nullopt;
    return static_cast<std::int64_t>(*milliseconds);
}

auto song_preview_row_at(const timing_index_t& timing, std::int64_t milliseconds)
    -> std::optional<std::uint64_t> {
    const auto projected = timing.query_position(signed_rational_t{milliseconds, 1'000});
    if (!projected || projected->value.denominator == 0) return std::nullopt;
    return nearest_row(to_long_double(projected->value), timing.total_rows, row_clamp_policy_t::both_ends);
}

auto song_preview_rows(const timing_index_t& timing, const song_preview_t& preview)
    -> std::optional<song_preview_rows_t> {
    const auto begin = song_preview_row_at(timing, preview.start_milliseconds);
    if (!begin || timing.total_rows == 0) return std::nullopt;
    const auto end = song_preview_row_at(timing, preview.end_milliseconds().value_or(
        preview.start_milliseconds + preview_audition_default_milliseconds));
    if (end) return song_preview_rows_t{*begin, *end};
    if (preview.length_milliseconds) return std::nullopt;
    return song_preview_rows_t{*begin, timing.total_rows - 1};
}

auto is_song_preview_field(format_family_t family, std::string_view name) -> bool {
    switch (family) {
    case format_family_t::osu_mania:
        return name == osu_preview_time_field;
    case format_family_t::stepmania: {
        const auto upper = text_util::ascii_upper(std::string{name});
        return upper == stepmania_sample_start_field || upper == stepmania_sample_length_field;
    }
    case format_family_t::native_project:
    case format_family_t::bms:
    case format_family_t::bmson:
    case format_family_t::midi:
    case format_family_t::quaver:
        return false;
    }
    return false;
}

} // namespace wormedit
