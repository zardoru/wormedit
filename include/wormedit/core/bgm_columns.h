#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>

namespace wormedit {

inline constexpr std::size_t minimum_bgm_column_count = 1;
inline constexpr std::size_t maximum_bgm_column_count = 128;

// Sets the number of BGM source columns of one chart as a single undoable
// command. Growing appends empty columns named `BGM k` with fresh IDs and the
// next source order. Shrinking removes trailing columns only while they hold
// no BGM events; otherwise the whole command rejects and names the first
// column that blocks it.
[[nodiscard]] auto apply_bgm_column_count(document_history_t& history,
                                          std::uint64_t expected_revision, chart_id_t chart,
                                          std::size_t count) -> document_command_result_t;

} // namespace wormedit
