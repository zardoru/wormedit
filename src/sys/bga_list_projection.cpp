#include "sys/bga_list_projection.h"
#include <cctype>
#include <wormedit/core/resource_assignment.h>
#include <algorithm>
#include <unordered_map>
#include <wormedit/core/natural_order.h>
namespace wormedit::sys {
namespace {
auto folded(std::string value) -> std::string {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

auto bga_slot(const resource_entry_t& resource) -> std::string {
    for (const auto& identity : resource.target_identities)
        if (identity.family == format_family_t::bms && identity.namespace_name == "BMP")
            return identity.identifier;
    return {};
}
auto bga_resource_less(const resource_entry_t& left, const resource_entry_t& right,
                       dock_sort_t sort) -> bool {
    if (sort == dock_sort_t::bms_id) {
        const auto left_slot = folded(bga_slot(left));
        const auto right_slot = folded(bga_slot(right));
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
auto bga_uses(const song_document_t& document) -> std::unordered_map<std::uint64_t, std::uint32_t> {
    std::unordered_map<std::uint64_t, std::uint32_t> uses;
    for (const auto& chart : document.charts)
        for (const auto& event : chart.bga_events) ++uses[event.resource.value];
    return uses;
}
}
auto project_bga_list(const song_document_t& document, const bga_list_request_t& request) -> bga_list_t {
    bga_list_t result;
    result.bms_capacity = bms_identifier_capacity(document, "BMP");
    const auto uses = bga_uses(document);
    std::vector<const resource_entry_t*> matching;
    const auto query = folded(request.search);
    for (const auto& resource : document.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::bga) == resource.roles.end()) continue;
        if (folded(bga_slot(resource)).find(query) != std::string::npos ||
            folded(resource.name).find(query) != std::string::npos) {
            matching.push_back(&resource);
            result.matching_ids.push_back(resource.id);
        }
    }
    const auto sort = request.ids_available ? request.sort : dock_sort_t::natural_name;
    std::ranges::sort(matching, [sort](const auto* left, const auto* right) {
        return bga_resource_less(*left, *right, sort);
    });
    result.matching_ids.clear();
    for (const auto* resource : matching) result.matching_ids.push_back(resource->id);
    result.total_matching = matching.size();
    result.first_row = std::min(request.first_row, matching.size());
    const auto end = result.first_row + std::min(request.row_limit, matching.size() - result.first_row);
    for (auto index = result.first_row; index < end; ++index) {
        const auto& resource = *matching[index];
        const auto asset = std::ranges::find(document.visual_assets, resource.asset, &visual_asset_t::id);
        const auto count = uses.find(resource.id.value);
        result.rows.push_back({resource.id, bga_slot(resource), resource.name,
            count == uses.end() ? 0 : count->second,
            resource.missing || asset == document.visual_assets.end(),
            asset != document.visual_assets.end() && asset->kind == asset_kind_t::video});
    }
    return result;
}
}
