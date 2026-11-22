#pragma once

#include <wormedit/core/release_version.h>
#include <wormedit/core/result.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace wormedit {

// Release channels the manifest can carry. The check follows `stable` (final releases and
// release candidates); a later channel is a new entry here and a new manifest key.
enum class update_channel_t { stable };
[[nodiscard]] auto update_channel_key(update_channel_t channel) -> std::string_view;

inline constexpr std::size_t maximum_update_manifest_bytes = 16 * 1024;
inline constexpr std::size_t maximum_update_field_bytes = 512;
inline constexpr std::chrono::hours update_check_interval{24};

struct update_manifest_entry_t {
    std::string version;   // "1.0.0-rc5"
    std::string url;       // Where to download it; https only.
    std::string notes_url; // Release notes; https only, may be empty.
    std::string published; // ISO date, display only.
    auto operator==(const update_manifest_entry_t&) const -> bool = default;
};

// Schema 1: {"schema":1,"channels":{"stable":{"version","url","notes_url","published"}}}.
// Unknown keys are ignored so a newer manifest stays readable by this build.
[[nodiscard]] auto parse_update_manifest(std::string_view bytes, update_channel_t channel)
    -> result_t<update_manifest_entry_t, std::string>;

enum class update_decision_t {
    up_to_date,
    available,
    unsupported_build, // Nightly or unparsable version: never nags.
};
[[nodiscard]] auto evaluate_update(std::string_view current_version,
                                   const update_manifest_entry_t& latest) -> update_decision_t;

// Once per interval; a clock that went backwards counts as due.
[[nodiscard]] auto is_update_check_due(std::int64_t last_check_unix_seconds,
                                       std::int64_t now_unix_seconds) -> bool;

// "<last_check_unix_seconds>\n": the whole throttle file. Unreadable text means "never".
[[nodiscard]] auto parse_update_check_state(std::string_view text) -> std::int64_t;
[[nodiscard]] auto serialize_update_check_state(std::int64_t last_check_unix_seconds) -> std::string;

} // namespace wormedit
