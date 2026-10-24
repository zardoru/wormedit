#pragma once

#include <wormedit/core/vocabulary.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit {

// The authored song preview in source-song audio time (open question 26). It
// is song-wide; a chart carries an override only when imported difficulties
// disagree. Point-only formats leave the length empty, and the length is never
// filled with an audition default.
struct song_preview_t {
    static constexpr std::int64_t maximum_milliseconds = 24LL * 60 * 60 * 1000;

    std::int64_t start_milliseconds{};
    std::optional<std::int64_t> length_milliseconds;

    // osu! `General.PreviewTime`: integer milliseconds, where -1 (or any
    // negative value) means no preview.
    [[nodiscard]] static auto from_osu_preview_time(std::string_view text)
        -> std::optional<song_preview_t>;
    // StepMania `#SAMPLESTART`/`#SAMPLELENGTH`: decimal seconds. A missing or
    // invalid start means no preview; a missing or nonpositive length is point-only.
    [[nodiscard]] static auto from_stepmania_sample(std::string_view start,
                                                    std::optional<std::string_view> length)
        -> std::optional<song_preview_t>;

    [[nodiscard]] auto is_valid() const noexcept -> bool;
    [[nodiscard]] auto end_milliseconds() const noexcept -> std::optional<std::int64_t>;
    [[nodiscard]] auto osu_preview_time_text() const -> std::string;
    [[nodiscard]] auto stepmania_sample_start_text() const -> std::string;
    [[nodiscard]] auto stepmania_sample_length_text() const -> std::optional<std::string>;

    auto operator==(const song_preview_t&) const -> bool = default;
};

// How long Compose shows and Export preview audio plays a point-only preview.
// An editor audition default only: it is never written into the song preview.
inline constexpr std::int64_t preview_audition_default_milliseconds = 15'000;

// Format fields whose meaning the typed preview owns. Their retained source
// spelling is reused by writers while it still encodes the typed value; the
// raw metadata editor refuses to change them.
inline constexpr std::string_view osu_preview_time_field{"General.PreviewTime"};
inline constexpr std::string_view stepmania_sample_start_field{"SAMPLESTART"};
inline constexpr std::string_view stepmania_sample_length_field{"SAMPLELENGTH"};

[[nodiscard]] auto is_song_preview_field(format_family_t family, std::string_view name) -> bool;

} // namespace wormedit
