#include <wormedit/core/metadata_editor.h>

#include <algorithm>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

auto valid_utf8(std::string_view text) -> bool {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t width{};
        std::uint32_t codepoint{};
        if ((lead & 0xe0U) == 0xc0U) {
            width = 2;
            codepoint = lead & 0x1fU;
        } else if ((lead & 0xf0U) == 0xe0U) {
            width = 3;
            codepoint = lead & 0x0fU;
        } else if ((lead & 0xf8U) == 0xf0U) {
            width = 4;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (width > text.size() - index)
            return false;
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xc0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        const auto minimum = width == 2 ? 0x80U : width == 3 ? 0x800U : 0x10000U;
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
            return false;
        index += width;
    }
    return true;
}

auto valid_text(std::string_view text, std::size_t maximum) -> bool {
    return text.size() <= maximum && text.find('\0') == std::string_view::npos && valid_utf8(text);
}

auto checked_add(std::size_t& total, std::size_t value, std::size_t maximum) -> bool {
    if (total > maximum || value > maximum - total)
        return false;
    total += value;
    return true;
}

auto metadata_fits(const song_document_t& song, const metadata_edit_limits_t& limits) -> bool {
    std::size_t total{};
    const auto account = [&](std::string_view text) {
        return checked_add(total, text.size(), limits.maximum_total_metadata_bytes);
    };
    const auto account_groups = [&](const auto& groups) {
        for (const auto& group : groups) {
            if (group.ordered_fields.size() > limits.maximum_fields_per_group)
                return false;
            for (const auto& [name, value] : group.ordered_fields) {
                if (name.empty() || !valid_text(name, limits.maximum_field_name_bytes) ||
                    !valid_text(value, limits.maximum_field_value_bytes) || !account(name) ||
                    !account(value))
                    return false;
            }
        }
        return true;
    };
    if (!valid_text(song.metadata.title, limits.maximum_title_bytes) ||
        !valid_text(song.metadata.artist, limits.maximum_artist_bytes) ||
        !account(song.metadata.title) || !account(song.metadata.artist) ||
        song.metadata.ordered_search_tags.size() > limits.maximum_search_tags)
        return false;
    for (const auto& tag : song.metadata.ordered_search_tags)
        if (!valid_text(tag, limits.maximum_search_tag_bytes) || !account(tag))
            return false;
    if (!account_groups(song.metadata.format_groups))
        return false;
    for (const auto& chart : song.charts)
        if (!valid_text(chart.metadata.name, limits.maximum_chart_text_bytes) ||
            !valid_text(chart.metadata.author, limits.maximum_chart_text_bytes) ||
            !account(chart.metadata.name) || !account(chart.metadata.author) ||
            !account_groups(chart.metadata.format_groups))
            return false;
    return true;
}

auto format_groups(song_document_t& song, const format_metadata_target_t& target,
                   diagnostic_list_t& diagnostics) -> std::vector<format_metadata_t>* {
    if (!target.chart)
        return &song.metadata.format_groups;
    const auto chart = std::ranges::find(song.charts, *target.chart, &chart_document_t::id);
    if (chart == song.charts.end()) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "metadata edit refers to an unknown difficulty"));
        return nullptr;
    }
    return &chart->metadata.format_groups;
}

auto find_or_append_group(std::vector<format_metadata_t>& groups, format_family_t family)
    -> format_metadata_t& {
    const auto found = std::ranges::find(groups, family, &format_metadata_t::family);
    if (found != groups.end())
        return *found;
    groups.push_back({.family = family});
    return groups.back();
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
            if (group.family != source_group.family || !group.encoding.original_bytes ||
                group.encoding.original_bytes.get() != source_group.encoding.original_bytes.get())
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
    if (name.empty() || !valid_text(name, limits.maximum_field_name_bytes) ||
        !valid_text(value, limits.maximum_field_value_bytes)) {
        diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature,
                  "format field is empty, invalid UTF-8, NUL-bearing, or over its byte cap"));
        return false;
    }
    return true;
}

} // namespace

auto apply_metadata_edit(document_history_t& history, std::uint64_t expected_revision,
                         const metadata_edit_action_t& action, metadata_edit_limits_t limits)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
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

    auto proposed = history.document;
    bool changed{};
    std::visit(
        [&](const auto& edit) {
            using edit_t = std::decay_t<decltype(edit)>;
            if constexpr (std::is_same_v<edit_t, set_song_metadata_action_t>) {
                if (!valid_text(edit.title, limits.maximum_title_bytes) ||
                    !valid_text(edit.artist, limits.maximum_artist_bytes) ||
                    edit.ordered_search_tags.size() > limits.maximum_search_tags ||
                    std::ranges::any_of(edit.ordered_search_tags, [&](const auto& tag) {
                        return !valid_text(tag, limits.maximum_search_tag_bytes);
                    })) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::unsupported_feature,
                              "song metadata is invalid UTF-8, NUL-bearing, or over its cap"));
                    return;
                }
                changed = proposed.metadata.title != edit.title ||
                          proposed.metadata.artist != edit.artist ||
                          proposed.metadata.ordered_search_tags != edit.ordered_search_tags;
                proposed.metadata.title = edit.title;
                proposed.metadata.artist = edit.artist;
                proposed.metadata.ordered_search_tags = edit.ordered_search_tags;
            } else if constexpr (std::is_same_v<edit_t, set_chart_metadata_action_t>) {
                const auto chart =
                    std::ranges::find(proposed.charts, edit.chart, &chart_document_t::id);
                if (chart == proposed.charts.end()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_id,
                              "chart metadata edit refers to an unknown difficulty"));
                    return;
                }
                if (!valid_text(edit.name, limits.maximum_chart_text_bytes) ||
                    !valid_text(edit.author, limits.maximum_chart_text_bytes) ||
                    (edit.rating && edit.rating->denominator == 0)) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::unsupported_feature,
                              "chart metadata is invalid UTF-8, NUL-bearing, over its cap, or has "
                              "an invalid rating"));
                    return;
                }
                changed = chart->metadata.name != edit.name ||
                          chart->metadata.author != edit.author ||
                          chart->metadata.rating != edit.rating;
                chart->metadata.name = edit.name;
                chart->metadata.author = edit.author;
                chart->metadata.rating = edit.rating;
            } else {
                auto* groups = format_groups(proposed, edit.target, result.diagnostics);
                if (!groups)
                    return;
                auto& group = find_or_append_group(*groups, edit.target.family);
                if constexpr (std::is_same_v<edit_t, insert_format_metadata_field_action_t>) {
                    if (!validate_field(edit.name, edit.value, limits, result.diagnostics))
                        return;
                    if (group.ordered_fields.size() >= limits.maximum_fields_per_group) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::unsupported_feature,
                                  "format metadata group exceeds the configured field cap"));
                        return;
                    }
                    if (edit.index > group.ordered_fields.size() ||
                        edit.index < group.ordered_field_source_orders.size()) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::invalid_id,
                                  "new format fields must be inserted within the authored suffix"));
                        return;
                    }
                    group.ordered_fields.insert(group.ordered_fields.begin() +
                                                    static_cast<std::ptrdiff_t>(edit.index),
                                                {edit.name, edit.value});
                    changed = true;
                } else if constexpr (std::is_same_v<edit_t,
                                                    replace_format_metadata_field_action_t>) {
                    if (!validate_field(edit.name, edit.value, limits, result.diagnostics))
                        return;
                    if (edit.index >= group.ordered_fields.size()) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::invalid_id,
                                  "format metadata replacement index is out of range"));
                        return;
                    }
                    const auto mirrored = mirrored_source_fields(proposed, edit.target, group,
                                                                 edit.index, result.diagnostics);
                    if (!result.diagnostics.empty())
                        return;
                    const auto replacement = std::pair{edit.name, edit.value};
                    changed = group.ordered_fields[edit.index] != replacement;
                    group.ordered_fields[edit.index] = replacement;
                    for (const auto& field : mirrored)
                        field.group->ordered_fields[field.index] = replacement;
                } else if constexpr (std::is_same_v<edit_t, erase_format_metadata_field_action_t>) {
                    if (edit.index >= group.ordered_fields.size()) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::invalid_id,
                                  "format metadata erase index is out of range"));
                        return;
                    }
                    const auto mirrored = mirrored_source_fields(proposed, edit.target, group,
                                                                 edit.index, result.diagnostics);
                    if (!result.diagnostics.empty())
                        return;
                    group.ordered_fields.erase(group.ordered_fields.begin() +
                                               static_cast<std::ptrdiff_t>(edit.index));
                    if (edit.index < group.ordered_field_source_orders.size())
                        group.ordered_field_source_orders.erase(
                            group.ordered_field_source_orders.begin() +
                            static_cast<std::ptrdiff_t>(edit.index));
                    for (const auto& field : mirrored) {
                        field.group->ordered_fields.erase(field.group->ordered_fields.begin() +
                                                          static_cast<std::ptrdiff_t>(field.index));
                        field.group->ordered_field_source_orders.erase(
                            field.group->ordered_field_source_orders.begin() +
                            static_cast<std::ptrdiff_t>(field.index));
                    }
                    changed = true;
                }
            }
        },
        action);

    if (!result.diagnostics.empty() || !changed)
        return result;
    if (!metadata_fits(proposed, limits)) {
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                                           "document metadata exceeds its total byte cap"));
        return result;
    }
    return commit_document_command(history, std::move(proposed), "Edit metadata");
}

} // namespace wormedit
