#pragma once
#include <wormedit/formats/bms_writer.h>

namespace wormedit::bms_writer_detail {
inline constexpr std::size_t maximum_bms_channel_slots = 500'000;
inline constexpr std::size_t maximum_bms_line_bytes = bms_text_t::maximum_line_bytes;
// Shared BMS representation rules. T-744 retires the legacy retained-tree
// adapter, while the scoped writer continues to use these numeric/codec owners.
struct write_failure_t {
    bms_write_diagnostic_code_t code;
    std::string message;
    std::string path;
};
struct bms_source_t {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    text_encoding_t encoding{};
    auto operator==(const bms_source_t&) const -> bool = default;
    static auto from(const song_document_t& song, const chart_document_t& chart, bool unchanged)
        -> std::optional<bms_source_t> {
        for (const auto* groups : {&chart.metadata.format_groups, &song.metadata.format_groups})
            for (const auto& group : *groups)
                if (group.family == format_family_t::bms && group.encoding.original_bytes &&
                    (!unchanged || group.encoding.unchanged_since_import))
                    return bms_source_t{.bytes = group.encoding.original_bytes,
                        .encoding = group.encoding.user_override.value_or(group.encoding.detected)};
        return std::nullopt;
    }
};
struct note_slot_t {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};
    std::string object_id;
    std::string path;
};
struct channel_line_t {
    std::size_t measure{};
    std::string channel;
    std::vector<note_slot_t> notes;
    // Subdivision of the source line the events came from, so `00` padding
    // survives edits instead of collapsing to the coarsest exact grid.
    std::uint64_t minimum_slots{1};
};

[[noreturn]] auto fail(bms_write_diagnostic_code_t code, std::string message, std::string path) -> void;
auto warn(std::vector<bms_write_diagnostic_t>& diagnostics, bms_write_diagnostic_code_t code,
    std::string message, std::string path) -> void;
auto is_base36_digit(char byte) -> bool;
auto base36_id(std::size_t value) -> std::string;
auto has_line_break_or_nul(std::string_view value) -> bool;
auto has_edge_whitespace(std::string_view value) -> bool;
auto generated_or_channel_token(std::string_view token) -> bool;
auto is_lr2_command(std::string_view command) -> bool;
auto encode_bms_text(std::string_view utf8, text_encoding_t encoding) -> std::string;
auto write_retained_bms_source(const bms_source_t& source, const bms_write_options_t& options) -> std::string;
auto exact_decimal(std::uint64_t numerator, std::uint64_t denominator, std::string path) -> std::string;
auto exact_decimal(signed_rational_t value, std::string path) -> std::string;
auto exact_signed_decimal(signed_rational_t value, std::string path) -> std::string;
auto encode_bms_channel_line(const channel_line_t& line, const bms_write_options_t& options) -> std::string;
} // namespace wormedit::bms_writer_detail

namespace wormedit {
// Read-only compatibility encoder for native migration and non-program formats.
auto write_bms_legacy_witness(const song_document_t& song, bms_write_options_t options = {}) -> bms_write_outcome_t;
}
