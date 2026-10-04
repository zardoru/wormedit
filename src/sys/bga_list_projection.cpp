#include "sys/bga_list_projection.h"
#include <cctype>
#include <wormedit/core/resource_assignment.h>
#include <algorithm>
#include <unordered_map>
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
auto bga_uses(const song_document_t& document) -> std::unordered_map<std::uint64_t, std::uint32_t> {
    std::unordered_map<std::uint64_t, std::uint32_t> uses;
    for (const auto& chart : document.charts)
        for (const auto& event : chart.bga_events) ++uses[event.resource.value];
    return uses;
}
}
auto project_bga_list(const song_document_t& document, const bga_list_request_t& request) -> bga_list_t {
    bga_list_t result;
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
