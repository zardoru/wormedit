#include "sys/compose/actions/compose_bga_actions.h"
#include "sys/app/application_edit_inputs.h"
#include "sys/compose/actions/compose_action_outcome.h"
#include "sys/compose/actions/compose_action_routes.h"
#include "sys/resources/resource_action_context.h"
#include <wormedit/core/proposal.h>
#include <wormedit/core/resource_assignment.h>
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_decoder.h>
#endif
#include <algorithm>
#include <filesystem>
#include <unordered_set>
namespace wormedit::sys {
namespace {
auto probe_image(const std::filesystem::path& path)
    -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    const auto image = visual::open_image_rgba({.path = path});
    if (image)
        return std::pair{image->width, image->height};
#else
    (void)path;
#endif
    return std::nullopt;
}
auto referenced_bga_resources(const song_document_t& document)
    -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> referenced;
    for (const auto& target : document.charts) {
        for (const auto& event : target.bga_events)
            referenced.insert(event.resource.value);
        for (const auto& measure : target.measures) {
            for (const auto& note : measure.notes)
                if (note.sound)
                    referenced.insert(note.sound->value);
            for (const auto& event : measure.bgm_events)
                if (event.sound)
                    referenced.insert(event.sound->value);
        }
    }
    return referenced;
}

} // namespace
auto import_bga_feedback(document_history_t& history, chart_id_t chart_id, std::span<const std::string> paths)
    -> compose_action_outcome_t {
    const auto* chart = history.document.find_chart(chart_id);
    if (chart == nullptr)
        return compose_action_outcome_t::blocked("Select a difficulty before adding images.");
    if (paths.empty() || paths.size() > wormedit::ui::sound_drop_resource_limit)
        return compose_action_outcome_t::blocked("Invalid image import batch size.");
    auto candidate = wormedit::document_history_t::from(history.document, 1);
    std::unordered_set<std::string> known;
    for (const auto& asset : history.document.visual_assets)
        known.insert(asset.source_path.lexically_normal().generic_string());
    std::vector<std::string> sorted{paths.begin(), paths.end()};
    std::ranges::sort(sorted);
    std::size_t added{}, skipped{};
    bool skipped_unreadable{};
    for (const auto& text : sorted) {
        const auto path = std::filesystem::path{text}.lexically_normal();
        const auto geometry = path.is_absolute() ? probe_image(path) : std::nullopt;
        if (!geometry || !known.insert(path.generic_string()).second) {
            skipped_unreadable = skipped_unreadable || !geometry;
            ++skipped;
            continue;
        }
        auto result = apply_resource_asset_import(
            candidate, {.source_path = path,
                        .role = resource_role_t::bga,
                        .name = path.filename().string(),
                        .allocate_bms_identifier = chart->source_family == format_family_t::bms,
                        .image_geometry = geometry});
        if (!result.diagnostics.empty())
            return compose_action_outcome_t::from(result.diagnostics.front(),
                                                  notification_source_t::edit,
                                                  notification_severity_t::info);
        if (!result.applied)
            return compose_action_outcome_t::from("Image import could not be prepared.",
                                                  notification_severity_t::error,
                                                  notification_source_t::edit, false, false);
        ++added;
    }
    if (!added)
        return compose_action_outcome_t::from(
            "No images added; skipped " + std::to_string(skipped) +
                " unreadable or existing files.",
            skipped_unreadable ? notification_severity_t::warning : notification_severity_t::info);
    auto result =
        commit_document_command(history, std::move(candidate.document), "Import BGA images");
    const auto is_applied_without_diagnostics = result.applied && result.diagnostics.empty();
    auto outcome = compose_action_outcome_t::from(
        std::move(result),
        compose_action_description_t{
            .unchanged = "No BGA resources changed.",
            .rebind_failure = "BGA changed, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_failure_policy = compose_rebind_failure_policy_t::message_only,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        "Added " + std::to_string(added) + " images; skipped " + std::to_string(skipped) +
            " unreadable or existing files.");
    if (is_applied_without_diagnostics && skipped_unreadable)
        outcome.severity = notification_severity_t::warning;
    return outcome;
}
namespace {
struct import_bga_action_t {
    auto operator==(const import_bga_action_t&) const -> bool = default;
    auto validate(const resource_action_context_t& context)
        -> std::optional<compose_action_outcome_t> {
        const auto& action = context.action;

        if (action.compose_import_paths.size() > wormedit::ui::sound_drop_resource_limit)
            return compose_action_outcome_t::blocked("Image import batch is too large.",
                                                     notification_source_t::import_);
        return std::nullopt;
    }
    auto perform(const resource_action_context_t& context) const -> compose_action_outcome_t {
        const auto& action = context.action;
        auto& history = context.document.history;
        std::vector<std::string> paths;
        for (std::size_t index = 0; index < action.compose_import_paths.size(); ++index)
            if (!action.compose_import_paths[index].empty())
                paths.emplace_back(action.compose_import_paths[index]);
        auto outcome =
            import_bga_feedback(history, context.chart, paths);
        outcome.source = notification_source_t::import_;
        return outcome;
    }
};
static_assert(compose_action<import_bga_action_t, const resource_action_context_t>);
} // namespace

namespace {
struct remove_unused_bga_action_t {
    std::vector<resource_id_t> unused;
    auto operator==(const remove_unused_bga_action_t&) const -> bool = default;
    auto validate(const resource_action_context_t& context)
        -> std::optional<compose_action_outcome_t> {
        auto& history = context.document.history;
        const auto referenced = referenced_bga_resources(history.document);
        for (const auto& resource : history.document.resources)
            if (std::ranges::find(resource.roles, resource_role_t::bga) != resource.roles.end() &&
                !referenced.contains(resource.id.value))
                unused.push_back(resource.id);
        if (unused.empty())
            return compose_action_outcome_t::from("No unused BGA resources.",
                                                  notification_severity_t::info);
        return std::nullopt;
    }
    auto perform(const resource_action_context_t& context) const -> compose_action_outcome_t {
        auto& history = context.document.history;
        auto result = apply_resource_deletions(history, unused);
        return compose_action_outcome_t::from(
            std::move(result),
            compose_action_description_t{
                .unchanged = "No BGA resources changed.",
                .rebind_failure = "BGA changed, but workspace rebinding failed.",
                .unknown_severity = notification_severity_t::info,
                .rebind_failure_policy = compose_rebind_failure_policy_t::message_only,
                .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
            "Removed " + std::to_string(unused.size()) + " unused BGA resources.");
    }
};
static_assert(compose_action<remove_unused_bga_action_t, const resource_action_context_t>);
} // namespace

auto import_bga_files(document_history_t& history, workspace_controller_t& workspace,
                      chart_id_t chart, std::span<const std::string> paths,
                      notification_sink_t notifications) -> std::string {
    const document_edit_inputs_t document{history, workspace, notifications};
    auto outcome = import_bga_feedback(history, chart, paths);
    outcome.source = notification_source_t::import_;
    auto finished = finish_compose_action(document, notifications, std::move(outcome));
    return std::move(finished.message);
}
auto dispatch_compose_bga_action(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                                 workspace_controller_t& workspace, chart_id_t chart,
                                 notification_sink_t notifications) -> std::optional<std::string> {
    const document_edit_inputs_t document{history, workspace, notifications};
    const resource_action_context_t context{action, document, chart};
    const auto* route = compose_action_detail::compose_action_route_t::find(
        action.compose_action, compose_action_detail::compose_bga_routes);
    if (!route)
        return std::nullopt;
    auto finished =
        finish_compose_action(document, notifications, route->resource_operation(context, nullptr));
    return std::move(finished.message);
}

namespace compose_action_detail {
namespace {
constexpr auto bga_route_entries = std::array{
    compose_action_route_t::from<import_bga_action_t, notification_source_t::import_,
                                 resource_route_policy_t::bga_action>(
        WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES),
    compose_action_route_t::from<remove_unused_bga_action_t, notification_source_t::edit,
                                 resource_route_policy_t::bga_action>(
        WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA),
};
} // namespace
static_assert(is_valid_route_module(bga_route_entries));
constinit const std::span<const compose_action_route_t> compose_bga_routes{bga_route_entries};
} // namespace compose_action_detail
} // namespace wormedit::sys
