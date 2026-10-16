#include "core/text_util.h"
#include "sys/sound_picker_view.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <wormedit/core/natural_order.h>

namespace wormedit::sys {
namespace {
using wormedit::text_util::ascii_lowercase;
auto slot_for(const resource_entry_t& resource) -> std::string {
    const auto found = std::ranges::find_if(resource.target_identities, [](const auto& identity) {
        return identity.family == format_family_t::bms && text_util::ascii_iequal(identity.namespace_name, "wav");
    });
    return found == resource.target_identities.end() ? std::string{} : found->identifier;
}
auto listed(const resource_entry_t& resource, const chart_document_t& chart) -> bool {
    const auto role = [&](resource_role_t value) {
        return std::ranges::find(resource.roles, value) != resource.roles.end();
    };
    if (effective_sound_kind(resource) != sound_kind_t::stem) return role(resource_role_t::sample);
    return chart.source_family == format_family_t::bmson && role(resource_role_t::bgm);
}
void build_sound_picker_catalog(std::vector<sound_picker_row_t>& catalog, const song_document_t& document,
                     const chart_document_t& chart) {
    catalog.clear();
    std::unordered_set<std::uint64_t> audio;
    for (const auto& asset : document.audio_assets) audio.insert(asset.id.value);
    for (const auto& resource : document.resources) {
        if (!audio.contains(resource.asset.value) || !listed(resource, chart)) continue;
        catalog.push_back({resource.id,
            chart.source_family == format_family_t::bms ? slot_for(resource) : std::string{},
            resource.name, resource.missing});
    }

}
// Ranking is a tuple: category first, then compactness/start position.
using score_t = std::pair<unsigned, std::size_t>;
auto score(std::string_view text, std::string_view query) -> std::optional<score_t> {
    if (query.empty()) return score_t{0, 0};
    if (text == query) return score_t{0, 0};
    if (text.starts_with(query)) return score_t{1, text.size() - query.size()};
    const auto substring = text.find(query);
    if (substring != std::string_view::npos) return score_t{2, substring};
    std::size_t position{}, start{}, end{};
    for (std::size_t index = 0; index < query.size(); ++index) {
        position = text.find(query[index], position);
        if (position == std::string_view::npos) return std::nullopt;
        if (index == 0) start = position;
        end = position++;
    }
    return score_t{3, (end - start + 1 - query.size()) * 2 + start};
}
struct match_t { std::size_t index{}; score_t score; };
void search(const std::vector<sound_picker_row_t>& catalog, std::string_view query,
            std::string& normalized, sound_picker_view_result_t& result) {
    normalized = ascii_lowercase(query);
    result = {};
    std::vector<match_t> best;
    const auto less = [&](const match_t& left, const match_t& right) {
        if (left.score != right.score) return left.score < right.score;
        const auto& a = catalog[left.index];
        const auto& b = catalog[right.index];
        if (natural_name_less(a.name, b.name)) return true;
        if (natural_name_less(b.name, a.name)) return false;
        return a.resource.value < b.resource.value;
    };
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto& row = catalog[index];
        auto rank = score(ascii_lowercase(row.name), normalized);
        if (!row.slot.empty()) {
            const auto slot = score(ascii_lowercase(row.slot), normalized);
            if (slot && (!rank || *slot < *rank)) rank = slot;
        }
        if (!rank) continue;
        ++result.total;
        const match_t match{index, *rank};
        best.insert(std::lower_bound(best.begin(), best.end(), match, less), match);
        if (best.size() > 20) best.pop_back();
    }
    for (const auto& match : best) result.rows.push_back(catalog[match.index]);
}
} // namespace

auto sound_picker_cache_t::query_key_t::operator==(const query_t& query) const -> bool {
    return catalog_generation == query.catalog_generation &&
           text_util::ascii_iequal(normalized, query.text);
}

auto sound_picker_cache_t::query_key_t::from(const query_t& query) -> query_key_t {
    // The miss builder normalizes once; key capture takes that buffer without copying.
    return {query.catalog_generation, std::move(query.normalized)};
}

void sound_picker_cache_t::query_key_t::assign(const query_t& query) {
    catalog_generation = query.catalog_generation;
    normalized = std::move(query.normalized);
}

auto sound_picker_view_result_t::from(sound_picker_cache_t& cache, const song_document_t& document,
                          const chart_document_t& chart, std::string_view query)
    -> const sound_picker_view_result_t& {
    const auto& catalog = cache.catalog.get_or_build(
        std::tuple{&document, &chart, chart_stamp_t::from(document, chart.id)},
        [&document, &chart](std::vector<sound_picker_row_t>& rows) {
            build_sound_picker_catalog(rows, document, chart);
        });
    std::string normalized;
    return cache.matches.get_or_build(
        sound_picker_cache_t::query_t{cache.catalog.build_count(), query, normalized},
        [&catalog, query, &normalized](sound_picker_view_result_t& result) {
            search(catalog, query, normalized, result);
        });
}
} // namespace wormedit::sys
