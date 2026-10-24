#include "sys/resources/bms_catalog_bindings.h"
#include "core/text_util.h"
#include "sys/resources/bga_list_view.h"
#include <wormedit/core/resource_assignment.h>
#include <algorithm>
#include <unordered_map>
#include <wormedit/core/natural_order.h>
namespace wormedit::sys {
namespace {
using wormedit::text_util::ascii_lowercase;

auto bga_slot(const resource_entry_t& resource) -> std::string {
    for (const auto& identity : resource.target_identities)
        if (identity.family == format_family_t::bms && identity.namespace_name == "BMP")
            return identity.identifier;
    return {};
}
auto bga_resource_less(const resource_entry_t& left, const resource_entry_t& right,
                       dock_sort_t sort, const bms_catalog_bindings_t& bindings) -> bool {
    if (sort == dock_sort_t::bms_id) {
        const auto left_slot = ascii_lowercase(bga_slot(bindings.resource(left)));
        const auto right_slot = ascii_lowercase(bga_slot(bindings.resource(right)));
        if (left_slot != right_slot) {
            if (left_slot.empty()) return false;
            if (right_slot.empty()) return true;
            return left_slot < right_slot;
        }
    } else if (left.name != right.name) {
        if (natural_name_less(left.name, right.name)) return true;
        if (natural_name_less(right.name, left.name)) return false;
    }
    return left.id.value < right.id.value;
}
auto bga_placeable(const resource_entry_t& resource, const visual_asset_t* asset) -> bool {
    return !resource.missing && asset &&
           (asset->kind == asset_kind_t::image || asset->kind == asset_kind_t::video);
}

auto bga_matches(const resource_entry_t& resource, const visual_asset_t* asset,
                  const resource_reference_counts_t& uses, resource_use_filter_t filter,
                  std::string_view query, const bms_catalog_bindings_t& bindings) -> bool {
    const auto count = uses.find(resource.id.value);
    const auto usage = count == uses.end() ? 0 : count->second;
    if (!matches_resource_use(filter, usage, resource.missing || !asset)) return false;
    const auto path = asset ? asset->source_path.generic_string() : std::string{};
    return ascii_lowercase(path).find(query) != std::string::npos ||
        ascii_lowercase(bga_slot(bindings.resource(resource))).find(query) != std::string::npos ||
        ascii_lowercase(resource.name).find(query) != std::string::npos;
}

}
auto bga_list_view_t::from(const song_document_t& document, const bga_list_request_t& request,
                           const chart_document_t* chart) -> bga_list_view_t {
    const bms_catalog_bindings_t bindings(document, chart);
    bga_list_view_t result;
    result.bms_capacity = bms_identifier_capacity(document, "BMP");
    const auto uses = count_resource_references(document);
    std::unordered_map<std::uint64_t, const visual_asset_t*> assets;
    for (const auto& asset : document.visual_assets) assets.emplace(asset.id.value, &asset);
    const auto asset_for = [&](const resource_entry_t& resource) -> const visual_asset_t* {
        const auto found = assets.find(resource.asset.value);
        return found == assets.end() ? nullptr : found->second;
    };
    std::vector<const resource_entry_t*> matching;
    const auto query = ascii_lowercase(request.search);
    for (const auto& resource : document.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::bga) == resource.roles.end()) continue;
        result.all_resource_ids.push_back(resource.id);
        if (bga_matches(resource, asset_for(resource), uses, request.use_filter, query, bindings))
            matching.push_back(&resource);
    }
    const auto sort = request.ids_available ? request.sort : dock_sort_t::natural_name;
    std::ranges::sort(matching, [sort, &bindings](const auto* left, const auto* right) {
        return bga_resource_less(*left, *right, sort, bindings);
    });
    result.matching_ids.clear();
    result.matching_ids.reserve(matching.size());
    result.advance_ids.reserve(matching.size());
    for (const auto* resource : matching) {
        result.matching_ids.push_back(resource->id);
        if (bga_placeable(*resource, asset_for(*resource))) result.advance_ids.push_back(resource->id);
    }
    result.total_matching = matching.size();
    result.first_row = std::min(request.first_row, matching.size());
    const auto end = result.first_row + std::min(request.row_limit, matching.size() - result.first_row);
    for (auto index = result.first_row; index < end; ++index) {
        const auto& resource = *matching[index];
        const auto* asset = asset_for(resource);
        const auto count = uses.find(resource.id.value);
        result.rows.push_back({resource.id, bga_slot(bindings.resource(resource)), resource.name,
            count == uses.end() ? 0 : count->second,
            resource.missing || asset == nullptr,
            asset != nullptr && asset->kind == asset_kind_t::video,
            asset ? asset->source_path.generic_string() : std::string{}});
    }
    return result;
}
}
