#include "../core/text_util.h"
#include "checked_math.h"
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/resource_file_copy.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>

namespace wormedit {
namespace {

using text_util::is_valid_utf8;

using text_util::is_valid_bounded_text;

auto checked_add(std::size_t& total, std::size_t value, std::size_t maximum) -> bool {
    return checked_math::account_size(total, value, maximum);
}

auto is_metadata_within_limits(const song_document_t& song, const metadata_edit_limits_t& limits) -> bool {
    std::size_t total{};
    const auto account = [&](std::string_view text) {
        return checked_add(total, text.size(), limits.maximum_total_metadata_bytes);
    };
    const auto account_groups = [&](const auto& groups) {
        for (const auto& group : groups) {
            if (group.ordered_fields.size() > limits.maximum_fields_per_group)
                return false;
            for (const auto& [name, value] : group.ordered_fields) {
                if (name.empty() || !is_valid_bounded_text(name, limits.maximum_field_name_bytes) ||
                    !is_valid_bounded_text(value, limits.maximum_field_value_bytes) || !account(name) ||
                    !account(value))
                    return false;
            }
        }
        return true;
    };
    if (!is_valid_bounded_text(song.metadata.title, limits.maximum_title_bytes) ||
        !is_valid_bounded_text(song.metadata.artist, limits.maximum_artist_bytes) ||
        !account(song.metadata.title) || !account(song.metadata.artist) ||
        song.metadata.ordered_search_tags.size() > limits.maximum_search_tags)
        return false;
    for (const auto& tag : song.metadata.ordered_search_tags)
        if (!is_valid_bounded_text(tag, limits.maximum_search_tag_bytes) || !account(tag))
            return false;
    if (!account_groups(song.metadata.format_groups))
        return false;
    for (const auto& chart : song.charts)
        if (!is_valid_bounded_text(chart.metadata.name, limits.maximum_chart_text_bytes) ||
            !is_valid_bounded_text(chart.metadata.author, limits.maximum_chart_text_bytes) ||
            !account(chart.metadata.name) || !account(chart.metadata.author) ||
            !account_groups(chart.metadata.format_groups))
            return false;
    return true;
}

auto resolve_format_group(song_document_t& song, const format_metadata_target_t& target,
                   diagnostic_list_t& diagnostics) -> format_metadata_t* {
    if (!target.chart)
        return &song.metadata.format_group_for(target.family);
    const auto chart = song.find_chart(*target.chart);
    if (chart == nullptr) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "metadata edit refers to an unknown difficulty"));
        return nullptr;
    }
    return &chart->metadata.format_group_for(target.family);
}

struct mirrored_source_field_t {
    format_metadata_t* group{};
    std::size_t index{};
};

auto mirrored_source_fields(song_document_t& song, const format_metadata_target_t& target,
                            const format_metadata_t& source_group, std::size_t source_index,
                            diagnostic_list_t& diagnostics)
    -> std::vector<mirrored_source_field_t> {
    std::vector<mirrored_source_field_t> result;
    if (source_index >= source_group.ordered_field_source_orders.size() ||
        !source_group.encoding.original_bytes)
        return result;
    const auto source_order = source_group.ordered_field_source_orders[source_index];
    const auto append = [&](std::vector<format_metadata_t>& groups) {
        for (auto& group : groups) {
            const auto same_source = group.encoding.original_bytes &&
                (group.encoding.original_bytes.get() ==
                     source_group.encoding.original_bytes.get() ||
                 (group.family == format_family_t::bmson &&
                  *group.encoding.original_bytes == *source_group.encoding.original_bytes));
            if (group.family != source_group.family || !same_source)
                continue;
            const auto found = std::ranges::find(group.ordered_field_source_orders, source_order);
            if (found == group.ordered_field_source_orders.end())
                continue;
            const auto index = static_cast<std::size_t>(
                std::distance(group.ordered_field_source_orders.begin(), found));
            if (index >= group.ordered_fields.size() ||
                group.ordered_fields[index] != source_group.ordered_fields[source_index]) {
                diagnostics.push_back(
                    error(diagnostic_code_t::invalid_id,
                          "retained format field has conflicting shared source identities"));
                return;
            }
            result.push_back({&group, index});
        }
    };
    if (target.chart) {
        append(song.metadata.format_groups);
    } else {
        for (auto& chart : song.charts)
            append(chart.metadata.format_groups);
    }
    return result;
}

auto validate_field(std::string_view name, std::string_view value,
                    const metadata_edit_limits_t& limits, diagnostic_list_t& diagnostics) -> bool {
    if (name.empty() || !is_valid_bounded_text(name, limits.maximum_field_name_bytes) ||
        !is_valid_bounded_text(value, limits.maximum_field_value_bytes)) {
        diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature,
                  "format field is empty, invalid UTF-8, NUL-bearing, or over its byte cap"));
        return false;
    }
    return true;
}

struct metadata_edit_context_t {
    song_document_t& proposed;
    document_command_result_t& result;
    const metadata_edit_limits_t& limits;
    bool changed{};
};

auto apply_metadata_action(metadata_edit_context_t& context,
                           const set_song_metadata_action_t& edit) -> void {
    const auto invalid_tag = std::ranges::any_of(edit.ordered_search_tags, [&](const auto& tag) {
        return !is_valid_bounded_text(tag, context.limits.maximum_search_tag_bytes);
    });
    if (!is_valid_bounded_text(edit.title, context.limits.maximum_title_bytes) ||
        !is_valid_bounded_text(edit.artist, context.limits.maximum_artist_bytes) ||
        edit.ordered_search_tags.size() > context.limits.maximum_search_tags || invalid_tag) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "song metadata is invalid UTF-8, NUL-bearing, or over its cap"));
        return;
    }
    auto& metadata = context.proposed.metadata;
    context.changed = metadata.title != edit.title || metadata.artist != edit.artist ||
                      metadata.ordered_search_tags != edit.ordered_search_tags;
    metadata.title = edit.title;
    metadata.artist = edit.artist;
    metadata.ordered_search_tags = edit.ordered_search_tags;
}

auto apply_metadata_action(metadata_edit_context_t& context,
                           const set_chart_metadata_action_t& edit) -> void {
    const auto chart = context.proposed.find_chart(edit.chart);
    if (chart == nullptr) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "chart metadata edit refers to an unknown difficulty"));
        return;
    }
    if (!is_valid_bounded_text(edit.name, context.limits.maximum_chart_text_bytes) ||
        !is_valid_bounded_text(edit.author, context.limits.maximum_chart_text_bytes) ||
        (edit.rating && edit.rating->denominator == 0)) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "chart metadata is invalid UTF-8, NUL-bearing, over its cap, or has an invalid rating"));
        return;
    }
    auto& metadata = chart->metadata;
    context.changed = metadata.name != edit.name || metadata.author != edit.author ||
                      metadata.rating != edit.rating;
    metadata.name = edit.name;
    metadata.author = edit.author;
    metadata.rating = edit.rating;
}

auto apply_metadata_action(metadata_edit_context_t& context,
                           const set_output_group_path_action_t& edit) -> void {
    if (edit.group_index >= context.proposed.output_groups.size()) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "output path edit refers to an unknown output group"));
        return;
    }
    if (edit.relative_output_path.empty() ||
        !is_valid_bounded_text(edit.relative_output_path, context.limits.maximum_output_path_bytes) ||
        edit.relative_output_path.find('\\') != std::string::npos ||
        edit.relative_output_path.find(':') != std::string::npos) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "output path must be bounded UTF-8 without platform-specific separators"));
        return;
    }
    const auto path = std::filesystem::path{edit.relative_output_path};
    if (!export_asset_directory(path)) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "output path must name a safe project-relative file or directory"));
        return;
    }
    auto& group = context.proposed.output_groups[edit.group_index];
    context.changed = group.relative_output_path != path;
    group.relative_output_path = path;
}

auto insert_metadata_field(metadata_edit_context_t& context, format_metadata_t& group,
                           const edit_format_metadata_field_action_t& edit) -> void {
    if (!validate_field(edit.name, edit.value, context.limits, context.result.diagnostics)) return;
    if (group.ordered_fields.size() >= context.limits.maximum_fields_per_group) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "format metadata group exceeds the configured field cap"));
        return;
    }
    if (edit.index > group.ordered_fields.size() ||
        edit.index < group.ordered_field_source_orders.size()) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "new format fields must be inserted within the authored suffix"));
        return;
    }
    group.ordered_fields.insert(group.ordered_fields.begin() +
        static_cast<std::ptrdiff_t>(edit.index), {edit.name, edit.value});
    context.changed = true;
}

auto replace_metadata_field(metadata_edit_context_t& context, format_metadata_t& group,
                            const edit_format_metadata_field_action_t& edit) -> void {
    if (!validate_field(edit.name, edit.value, context.limits, context.result.diagnostics)) return;
    if (edit.index >= group.ordered_fields.size()) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "format metadata replacement index is out of range"));
        return;
    }
    const auto mirrored = mirrored_source_fields(context.proposed, edit.target, group,
                                                  edit.index, context.result.diagnostics);
    if (!context.result.diagnostics.empty()) return;
    const auto replacement = std::pair{edit.name, edit.value};
    context.changed = group.ordered_fields[edit.index] != replacement;
    group.ordered_fields[edit.index] = replacement;
    for (const auto& field : mirrored)
        field.group->ordered_fields[field.index] = replacement;
}

auto apply_metadata_field(metadata_edit_context_t& context, format_metadata_t& group,
                          const edit_format_metadata_field_action_t& edit) -> void {
    if (edit.mode == format_metadata_field_mode_t::insert)
        insert_metadata_field(context, group, edit);
    else
        replace_metadata_field(context, group, edit);
}

auto apply_metadata_field(metadata_edit_context_t& context, format_metadata_t& group,
                          const erase_format_metadata_field_action_t& edit) -> void {
    if (edit.index >= group.ordered_fields.size()) {
        context.result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "format metadata erase index is out of range"));
        return;
    }
    const auto mirrored = mirrored_source_fields(context.proposed, edit.target, group,
                                                  edit.index, context.result.diagnostics);
    if (!context.result.diagnostics.empty()) return;
    group.ordered_fields.erase(group.ordered_fields.begin() +
                               static_cast<std::ptrdiff_t>(edit.index));
    if (edit.index < group.ordered_field_source_orders.size())
        group.ordered_field_source_orders.erase(group.ordered_field_source_orders.begin() +
                                                 static_cast<std::ptrdiff_t>(edit.index));
    for (const auto& field : mirrored) {
        field.group->ordered_fields.erase(field.group->ordered_fields.begin() +
                                          static_cast<std::ptrdiff_t>(field.index));
        field.group->ordered_field_source_orders.erase(
            field.group->ordered_field_source_orders.begin() +
            static_cast<std::ptrdiff_t>(field.index));
    }
    context.changed = true;
}

template <typename Edit>
auto apply_format_metadata_action(metadata_edit_context_t& context, const Edit& edit) -> void {
    auto* groups = resolve_format_group(context.proposed, edit.target, context.result.diagnostics);
    if (!groups) return;
    auto& group = *groups;
    apply_metadata_field(context, group, edit);
}

auto apply_metadata_operation(metadata_edit_context_t& context,
                              const edit_format_metadata_field_action_t& edit) -> void {
    apply_format_metadata_action(context, edit);
}

auto apply_metadata_operation(metadata_edit_context_t& context,
                              const erase_format_metadata_field_action_t& edit) -> void {
    apply_format_metadata_action(context, edit);
}

template <typename Edit>
auto apply_metadata_operation(metadata_edit_context_t& context, const Edit& edit) -> void {
    apply_metadata_action(context, edit);
}

} // namespace

auto output_path_from_drop(const std::filesystem::path& project_directory,
                           const std::filesystem::path& dropped_path,
                           metadata_edit_limits_t limits)
    -> std::optional<std::filesystem::path> {
    if (!project_directory.is_absolute() || !dropped_path.is_absolute())
        return std::nullopt;
    std::error_code filesystem_error;
    const auto project = std::filesystem::canonical(project_directory, filesystem_error);
    if (filesystem_error || !std::filesystem::is_directory(project, filesystem_error) ||
        filesystem_error)
        return std::nullopt;
    const auto dropped = std::filesystem::canonical(dropped_path, filesystem_error);
    if (filesystem_error)
        return std::nullopt;
    const auto directory = std::filesystem::is_directory(dropped, filesystem_error);
    if (filesystem_error)
        return std::nullopt;
    if (!directory && !std::filesystem::is_regular_file(dropped, filesystem_error))
        return std::nullopt;
    if (filesystem_error)
        return std::nullopt;
    const auto relative = dropped.lexically_relative(project);
    const auto text = relative.generic_string();
    if (relative.empty() || relative == "." || directory == relative.has_extension() ||
        !export_asset_directory(relative) || text.empty() ||
        !is_valid_bounded_text(text, limits.maximum_output_path_bytes) ||
        text.find('\\') != std::string::npos || text.find(':') != std::string::npos)
        return std::nullopt;
    return relative;
}

auto parse_metadata_rating(std::string_view numerator, std::string_view denominator)
    -> std::optional<signed_rational_t> {
    if (numerator.empty() || denominator.empty() ||
        numerator.size() > metadata_rating_component_max_bytes ||
        denominator.size() > metadata_rating_component_max_bytes) {
        return std::nullopt;
    }

    const auto numerator_digits =
        numerator.front() == '+' || numerator.front() == '-' ? numerator.substr(1) : numerator;
    if (numerator_digits.empty() || numerator_digits.size() > 20)
        return std::nullopt;
    auto numerator_text = numerator;
    if (numerator_text.front() == '+')
        numerator_text.remove_prefix(1);

    std::int64_t parsed_numerator{};
    const auto numerator_result = std::from_chars(
        numerator_text.data(), numerator_text.data() + numerator_text.size(), parsed_numerator, 10);
    if (numerator_result.ec != std::errc{} ||
        numerator_result.ptr != numerator_text.data() + numerator_text.size()) {
        return std::nullopt;
    }

    auto denominator_text = denominator;
    if (denominator_text.front() == '+')
        denominator_text.remove_prefix(1);
    if (denominator_text.empty() || denominator_text.size() > 20)
        return std::nullopt;

    std::uint64_t parsed_denominator{};
    const auto denominator_result =
        std::from_chars(denominator_text.data(), denominator_text.data() + denominator_text.size(),
                        parsed_denominator, 10);
    if (denominator_result.ec != std::errc{} ||
        denominator_result.ptr != denominator_text.data() + denominator_text.size() ||
        parsed_denominator == 0) {
        return std::nullopt;
    }
    return signed_rational_t{parsed_numerator, parsed_denominator};
}

auto chart_rating_edit_t::from(chart_id_t chart, bool unset, std::string_view numerator,
                               std::string_view denominator) -> std::optional<chart_rating_edit_t> {
    if (!chart)
        return std::nullopt;
    if (unset)
        return chart_rating_edit_t{chart, std::nullopt};
    const auto rating = parse_metadata_rating(numerator, denominator);
    if (!rating)
        return std::nullopt;
    return chart_rating_edit_t{chart, rating};
}

auto apply_batch_chart_metadata_edit(document_history_t& history, std::uint64_t expected_revision,
    const batch_chart_metadata_action_t& action, metadata_edit_limits_t limits)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    auto& result = transaction.outcome;
    if (expected_revision != history.document.revision) return transaction.reject(diagnostic_code_t::invalid_id, "metadata editor document revision is stale");
    if (action.charts.empty() || action.charts.size() > 256) return transaction.reject(diagnostic_code_t::invalid_id, "select between 1 and 256 charts");
    if ((action.author && !is_valid_bounded_text(*action.author, limits.maximum_chart_text_bytes)) ||
        (action.edit_rating && action.rating && action.rating->denominator == 0))
        return transaction.reject(diagnostic_code_t::invalid_id, "batch metadata has invalid author text or rating");
    auto& proposed = transaction.copy_document();
    std::vector<chart_id_t> seen;
    bool changed{};
    for (const auto id : action.charts) {
        if (std::ranges::find(seen, id) != seen.end()) return transaction.reject(diagnostic_code_t::invalid_id, "batch metadata repeats a chart");
        seen.push_back(id);
        const auto chart = proposed.find_chart(id);
        if (chart == nullptr) return transaction.reject(diagnostic_code_t::invalid_id, "batch metadata refers to an unknown chart");
        auto& metadata = chart->metadata;
        if (action.author) {
            changed = changed || metadata.author != *action.author;
            metadata.author = *action.author;
        }
        if (action.edit_rating) {
            changed = changed || metadata.rating != action.rating;
            metadata.rating = action.rating;
        }
    }
    if (!changed) return result;
    if (limits.maximum_total_metadata_bytes == 0 || !is_metadata_within_limits(proposed, limits))
        return transaction.reject(diagnostic_code_t::invalid_id, "document metadata exceeds its total byte cap");
    return transaction.commit("Edit chart metadata batch");
}

auto apply_metadata_edit(document_history_t& history, std::uint64_t expected_revision,
                         const metadata_edit_action_t& action, metadata_edit_limits_t limits)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    auto& result = transaction.outcome;
    if (expected_revision != history.document.revision) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "metadata editor document revision is stale"));
        return result;
    }
    if (limits.maximum_total_metadata_bytes == 0) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature, "metadata byte cap must be positive"));
        return result;
    }

    auto& proposed = transaction.copy_document();
    metadata_edit_context_t context{.proposed = proposed, .result = result,
                                    .limits = limits, .changed = false};
    std::visit([&](const auto& edit) { apply_metadata_operation(context, edit); }, action);

    if (!result.diagnostics.empty() || !context.changed)
        return result;
    if (!is_metadata_within_limits(proposed, limits)) {
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                                           "document metadata exceeds its total byte cap"));
        return result;
    }
    return transaction.commit("Edit metadata");
}

// Compatibility for the sys/ui migration wave.
auto make_chart_rating_metadata_edit(chart_id_t chart, bool unset, std::string_view numerator,
                                     std::string_view denominator)
    -> std::optional<chart_rating_edit_t> {
    return chart_rating_edit_t::from(chart, unset, numerator, denominator);
}

} // namespace wormedit
