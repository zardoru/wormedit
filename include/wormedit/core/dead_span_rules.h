#pragma once

#include <wormedit/core/document.h>

#include <cstdint>

namespace wormedit {

// What may sit inside a dead span.
//  - Warp interior, strictly between its first and last row: fakes, mines, invisible notes, hold bodies and hold tails. Playable
//    heads (tap, hold, roll, lift) are rejected. The first and last row are the warp's own head
//    and tail and take any note; the game interpreter disambiguates them.
//  - Stops and delays have no interior rows; the pointer bands drawn beside their row take
//    nothing, and the edge rows themselves stay valid.

// Tap, hold, roll and lift can be hit by the player; mine, invisible and fake cannot.
[[nodiscard]] auto note_kind_is_playable(note_kind_t kind) noexcept -> bool;

// True when `absolute` lies strictly inside a warp, excluding its first and last row.
[[nodiscard]] auto row_in_warp(const chart_document_t& chart, const measure_index_t& index,
                               std::uint64_t absolute) -> bool;

// True when a note of `kind` whose head sits at `absolute` is not allowed there.
[[nodiscard]] auto head_blocked_by_warp(const chart_document_t& chart,
                                        const measure_index_t& index, std::uint64_t absolute,
                                        note_kind_t kind) -> bool;

} // namespace wormedit
