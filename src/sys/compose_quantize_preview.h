#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

// Dry-run result of quantizing the selected notes to the active snap.
struct quantize_preview_t {
    std::size_t selected{};
    std::size_t moved{};
    std::size_t unchanged{};
    // Collisions or other rejections that make the real command fail; Apply stays disabled.
    std::size_t blocked{};
    std::string message;
};

[[nodiscard]] auto preview_quantize_selection(const chart_document_t& chart,
                                              const std::vector<object_id_t>& selected,
                                              std::uint64_t snap_denominator) -> quantize_preview_t;

// Recomputes only when the chart, document revision, selection or snap changed.
class quantize_preview_cache_t {
public:
    [[nodiscard]] auto get(const chart_document_t& chart, std::uint64_t revision,
                           const std::vector<object_id_t>& selected,
                           std::uint64_t snap_denominator) -> const quantize_preview_t&;

private:
    chart_id_t chart_{};
    std::uint64_t revision_{};
    std::uint64_t snap_{};
    std::vector<object_id_t> selected_;
    std::optional<quantize_preview_t> value_;
};

} // namespace wormedit::sys
