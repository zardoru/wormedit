#pragma once

#include <wormedit/core/document.h>
#include <span>
#include <variant>

namespace wormedit {

struct bms_program_position_t {
    measure_id_t measure;
    rational_t fraction;
    [[nodiscard]] static auto from(local_position_t position, const chart_document_t& geometry,
        bool allows_chart_end = false) -> std::optional<bms_program_position_t>;
    auto operator==(const bms_program_position_t&) const -> bool = default;
};

enum class bms_resource_namespace_t { wav, bmp };
struct bms_native_resource_reference_t {
    bms_resource_namespace_t name_space{bms_resource_namespace_t::wav};
    std::string identifier;
    auto operator==(const bms_native_resource_reference_t&) const -> bool = default;
};

using bms_native_event_t = std::variant<note_t, bgm_event_t, bga_event_t,
    bpm_event_t, pause_event_t, warp_event_t, scroll_event_t, speed_event_t,
    kiai_event_t, timing_anchor_t, bookmark_t, section_t, sv_recipe_t, sound_channel_t>;

// Native features have the same semantic owner as imported BMS content. The
// private payload has its ID and row positions erased: exact fractions below
// are the only position authority, even when inactive measure grids differ.
class bms_native_content_t {
  public:
    [[nodiscard]] static auto from(bms_native_event_t event, const chart_document_t& geometry,
        std::optional<bms_native_resource_reference_t> reference = {})
        -> std::optional<bms_native_content_t>;
    [[nodiscard]] auto positions() const -> std::span<const bms_program_position_t> { return positions_; }
    [[nodiscard]] auto payload() const -> const bms_native_event_t& { return event_; }
    // Restore the canonical native payload and its sole exact position authority.
    [[nodiscard]] static auto from(bms_native_event_t payload,
        std::vector<bms_program_position_t> positions,
        std::optional<bms_native_resource_reference_t> reference)
        -> std::optional<bms_native_content_t>;
    [[nodiscard]] auto build_event(object_id_t id, const chart_document_t& geometry) const
        -> std::optional<bms_native_event_t>;
    [[nodiscard]] auto has_same_payload(const bms_native_content_t& other) const -> bool {
        return event_ == other.event_ && reference_ == other.reference_;
    }
    [[nodiscard]] auto resource_reference() const -> const std::optional<bms_native_resource_reference_t>& { return reference_; }
    [[nodiscard]] auto declared_ids() const -> std::vector<object_id_t>;
    [[nodiscard]] auto allows_chart_end(std::size_t index) const -> bool {
        return std::holds_alternative<sv_recipe_t>(event_) && index + 1 == positions_.size();
    }
    auto operator==(const bms_native_content_t&) const -> bool = default;
  private:
    bms_native_content_t(bms_native_event_t event, std::vector<bms_program_position_t> positions,
        std::optional<bms_native_resource_reference_t> reference)
        : event_(std::move(event)), positions_(std::move(positions)), reference_(std::move(reference)) {}
    bms_native_event_t event_;
    std::vector<bms_program_position_t> positions_;
    std::optional<bms_native_resource_reference_t> reference_;
};

} // namespace wormedit
