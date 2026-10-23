#pragma once
#include <wormedit/formats/bms.h>

namespace wormedit {
// Shared legacy source decoder seam. T-744 removes the legacy document adapter;
// lexical, codec and numeric policies remain shared by scoped import.
auto parse_bms_source(std::span<const std::byte> bytes, const bms_parse_options_t& options,
    id_allocator_t& ids) -> bms_parse_outcome_t;
auto ensure_bms_source_measure(chart_document_t& chart, std::size_t index,
    id_allocator_t& ids) -> measure_chunk_t&;
struct bms_source_statement_t {
    std::string token;
    std::string value;
    char separator{};
    std::optional<std::pair<std::size_t, std::string>> measure_channel;
    [[nodiscard]] static auto from(const bms_control_node_t& node) -> bms_source_statement_t;
    [[nodiscard]] static auto from(std::string_view line) -> bms_source_statement_t;
    [[nodiscard]] auto positive_number() const -> std::optional<signed_rational_t>;
    [[nodiscard]] auto measure_length() const -> std::optional<rational_t>;
    [[nodiscard]] auto is_control() const -> bool;
    [[nodiscard]] auto is_playable() const -> bool;
    [[nodiscard]] auto is_builtin_channel() const -> bool;
    [[nodiscard]] auto bga_kind() const -> std::optional<visual_event_kind_t>;
};
} // namespace wormedit
