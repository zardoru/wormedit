#pragma once

#include <wormedit/core/document.h>
#include <span>


namespace wormedit::sys {
// Effective scope aliases are derived; resource metadata stays in the project pool.
class bms_catalog_bindings_t {
  public:
    bms_catalog_bindings_t(const song_document_t& document, const chart_document_t* chart);
    [[nodiscard]] auto identities(const resource_entry_t& resource) const
        -> std::span<const target_resource_identity_t>;
    [[nodiscard]] auto resource(const resource_entry_t& canonical) const -> const resource_entry_t&;
    [[nodiscard]] auto wav_identifier(const resource_entry_t& resource) const -> std::string;
  private:
    const bms_program_document_t* owner_{};
    std::optional<branch_id_t> authoring_chart_;
};
} // namespace wormedit::sys
