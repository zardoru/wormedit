#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit {
namespace {

void reject(document_command_result_t& result, diagnostic_code_t code, std::string message) {
    result.diagnostics.push_back(
        {.severity = diagnostic_severity_t::error,
         .code = code,
         .message = std::move(message),
         .object = std::nullopt});
}

auto has_role(const resource_entry_t& resource, resource_role_t role) -> bool {
    return std::ranges::find(resource.roles, role) != resource.roles.end();
}

} // namespace

auto apply_resource_creation(document_history_t& history, std::uint64_t expected_revision,
                             const create_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "resource creation document revision is stale");
        return result;
    }
    if (history.document.resources.size() >= 100'000 || action.name.size() > 255 ||
        action.name.find('\0') != std::string::npos) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource catalog is full or the resource name is invalid");
        return result;
    }
    const auto audio_role = action.role == resource_role_t::sample ||
                            action.role == resource_role_t::bgm;
    const auto visual_role = action.role == resource_role_t::bga;
    if (!audio_role && !visual_role) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource creation requires a Sample, BGM, or BGA role");
        return result;
    }
    const auto& document = history.document;
    const auto audio = std::ranges::find(document.audio_assets, action.asset, &audio_asset_t::id);
    const auto visual =
        std::ranges::find(document.visual_assets, action.asset, &visual_asset_t::id);
    if (!action.asset || (audio_role && audio == document.audio_assets.end()) ||
        (visual_role && visual == document.visual_assets.end())) {
        reject(result, diagnostic_code_t::dangling_asset,
               "resource creation requires a registered asset of the selected type");
        return result;
    }
    std::uint64_t largest_id{};
    for (const auto& resource : document.resources) {
        largest_id = std::max(largest_id, resource.id.value);
    }
    if (largest_id == std::numeric_limits<std::uint64_t>::max()) {
        reject(result, diagnostic_code_t::invalid_id, "resource ID space is exhausted");
        return result;
    }
    resource_entry_t resource{.id = resource_id_t{largest_id + 1},
                              .name = action.name,
                              .asset = action.asset,
                              .roles = {action.role},
                              .target_identities = {},
                              .missing = false,
                              .provenance = {}};
    if (action.allocate_bms_identifier) {
        const std::string_view name_space = visual_role ? "BMP" : "WAV";
        constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::array<bool, 36 * 36> occupied{};
        const auto digit_value = [](unsigned char character) -> int {
            const auto upper = std::toupper(character);
            if (upper >= '0' && upper <= '9') {
                return upper - '0';
            }
            if (upper >= 'A' && upper <= 'Z') {
                return upper - 'A' + 10;
            }
            return -1;
        };
        for (const auto& existing : document.resources) {
            for (const auto& identity : existing.target_identities) {
                if (identity.family != format_family_t::bms ||
                    identity.namespace_name != name_space || identity.identifier.size() != 2) {
                    continue;
                }
                const auto high = digit_value(
                    static_cast<unsigned char>(identity.identifier[0]));
                const auto low = digit_value(
                    static_cast<unsigned char>(identity.identifier[1]));
                if (high >= 0 && low >= 0) {
                    occupied[static_cast<std::size_t>(high * 36 + low)] = true;
                }
            }
        }
        bool found{};
        for (std::size_t value = 1; value < 36 * 36; ++value) {
            if (!occupied[value]) {
                std::string candidate{digits[value / 36], digits[value % 36]};
                resource.target_identities.push_back(
                    {.family = format_family_t::bms,
                     .identifier = std::move(candidate),
                     .namespace_name = std::string{name_space}});
                found = true;
                break;
            }
        }
        if (!found) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BMS resource namespace has no free nonzero identifier");
            return result;
        }
    }
    auto proposed = document;
    proposed.resources.push_back(std::move(resource));
    return commit_document_command(history, std::move(proposed), "Create resource");
}

auto apply_resource_assignment(document_history_t& history, std::uint64_t expected_revision,
                               const assign_resource_action_t& action,
                               resource_assignment_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment document revision is stale");
        return result;
    }
    if (limits.maximum_selected_objects == 0 || limits.maximum_chart_objects == 0 ||
        action.objects.empty() || action.objects.size() > limits.maximum_selected_objects) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource assignment requires a bounded nonempty selection");
        return result;
    }
    const auto resource =
        std::ranges::find(history.document.resources, action.resource, &resource_entry_t::id);
    if (!action.resource || resource == history.document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown catalog resource");
        return result;
    }
    if (resource->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot assign a resource with a missing source");
        return result;
    }
    const auto audio_asset =
        std::ranges::find(history.document.audio_assets, resource->asset, &audio_asset_t::id);
    const auto visual_asset =
        std::ranges::find(history.document.visual_assets, resource->asset, &visual_asset_t::id);
    const auto audio_eligible =
        audio_asset != history.document.audio_assets.end() &&
        (has_role(*resource, resource_role_t::sample) || has_role(*resource, resource_role_t::bgm));
    const auto visual_eligible = visual_asset != history.document.visual_assets.end() &&
                                 has_role(*resource, resource_role_t::bga);

    auto selected = action.objects;
    std::ranges::sort(selected);
    if (std::ranges::adjacent_find(selected) != selected.end()) {
        reject(result, diagnostic_code_t::duplicate_id,
               "resource assignment contains a duplicate typed object");
        return result;
    }
    for (const auto& object : selected) {
        if (!object.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "resource assignment uses the reserved zero object ID");
            return result;
        }
        const auto compatible = [&] {
            switch (object.kind) {
            case editor_selection_object_kind_t::note:
            case editor_selection_object_kind_t::bgm_event:
                return audio_eligible;
            case editor_selection_object_kind_t::bga_event:
                return visual_eligible;
            default:
                return false;
            }
        }();
        if (!compatible) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "selected object cannot use this resource type");
            return result;
        }
    }

    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == proposed.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown chart");
        return result;
    }
    std::vector<std::uint8_t> occurrences(selected.size());
    std::size_t chart_objects{};
    bool changed{};
    const auto assign = [&](editor_selection_object_t object, auto& current,
                            bool retained_sound_link) -> bool {
        if (++chart_objects > limits.maximum_chart_objects) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "resource assignment chart scan exceeds its configured cap");
            return false;
        }
        const auto found = std::ranges::lower_bound(selected, object);
        if (found == selected.end() || *found != object) {
            return true;
        }
        const auto index = static_cast<std::size_t>(found - selected.begin());
        if (++occurrences[index] != 1) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "resource assignment target has an ambiguous typed identity");
            return false;
        }
        if (current == action.resource) {
            return true;
        }
        if (retained_sound_link) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "edit the retained BMSON sound channel before changing its resource");
            return false;
        }
        current = action.resource;
        changed = true;
        return true;
    };
    for (auto& measure : chart->measures) {
        for (auto& note : measure.notes) {
            if (!assign({editor_selection_object_kind_t::note, note.id}, note.sound,
                        note.sound_channel_event.has_value())) {
                return result;
            }
        }
        for (auto& event : measure.bgm_events) {
            if (!assign({editor_selection_object_kind_t::bgm_event, event.id}, event.sound,
                        event.sound_channel_event.has_value())) {
                return result;
            }
        }
    }
    for (auto& event : chart->bga_events) {
        if (!assign({editor_selection_object_kind_t::bga_event, event.id}, event.resource, false)) {
            return result;
        }
    }
    if (std::ranges::find(occurrences, std::uint8_t{0}) != occurrences.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown typed object");
        return result;
    }
    if (!changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), "Assign resource to selection");
}

} // namespace wormedit
