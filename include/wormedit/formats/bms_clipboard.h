#pragma once

#include <wormedit/core/chart_clipboard.h>

#include <cstddef>
#include <string_view>

namespace wormedit {

enum class bms_clipboard_numbering_t { base36, base62, base16 };

struct bms_clipboard_decode_options_t {
    std::size_t maximum_bytes{1024U * 1024U};
    std::size_t maximum_records{50'000};
    bms_clipboard_numbering_t object_numbering{bms_clipboard_numbering_t::base36};
};

// Decode the four pinned BMSE/iBMSC grammars into the native clipboard.
// The caller supplies the target profile explicitly; resources resolve only
// through that document's BMS WAV identities. Ambiguous/undefined mappings,
// malformed records and unsupported objects reject the entire payload.
// Positions use standard measures and bounded rational recovery (denominator
// <= 192 * 64, error <= 1e-9 measure). This function never changes the document.
// True when the first line names one of those four grammars, whatever its line ending, so a
// caller can tell other editors' text from unrelated clipboard contents before decoding it.
[[nodiscard]] auto is_bms_editor_clipboard_text(std::string_view text) -> bool;

[[nodiscard]] auto
parse_bms_chart_clipboard(std::string_view text, const song_document_t& target_document,
                          const chart_document_t& target_chart, const lane_layout_t& target_layout,
                          bms_clipboard_decode_options_t options = {}) -> chart_clipboard_outcome_t;

} // namespace wormedit
