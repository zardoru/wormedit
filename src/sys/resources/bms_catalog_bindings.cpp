#include "sys/resources/bms_catalog_bindings.h"
#include "core/text_util.h"
#include <wormedit/core/bms_program_edit.h>

namespace wormedit::sys {
bms_catalog_bindings_t::bms_catalog_bindings_t(
    const song_document_t& document, const chart_document_t* chart)
    : owner_(chart ? document.find_bms_program(chart->id) : nullptr),
      authoring_chart_(chart ? chart->bms_authoring_chart : std::nullopt) {}

auto bms_catalog_bindings_t::resource(const resource_entry_t& canonical) const -> const resource_entry_t& {
    const auto* bound = owner_ ? owner_->read_bound_resource(canonical.id, authoring_chart_) : nullptr;
    return bound ? *bound : canonical;
}

auto bms_catalog_bindings_t::identities(const resource_entry_t& resource) const
    -> std::span<const target_resource_identity_t> {
    if (!owner_) return resource.target_identities;
    const auto* bound = owner_->read_bound_resource(resource.id, authoring_chart_);
    return bound ? std::span<const target_resource_identity_t>{bound->target_identities}
        : std::span<const target_resource_identity_t>{};
}

auto bms_catalog_bindings_t::wav_identifier(const resource_entry_t& resource) const -> std::string {
    for (const auto& identity : identities(resource))
        if (identity.family == format_family_t::bms &&
            text_util::ascii_iequal(identity.namespace_name, "wav")) return identity.identifier;
    return {};
}
} // namespace wormedit::sys
