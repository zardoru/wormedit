#include <wormedit/core/osu_hitsample.h>
#include <wormedit/core/numeric_text.h>
#include "text_util.h"
#include <climits>

namespace wormedit {
namespace {
constexpr std::array<std::string_view, 4> skin_sample_sets{"", "normal", "soft", "drum"};
constexpr std::array<std::string_view, 4> skin_sample_kinds{"hitnormal", "hitwhistle", "hitfinish", "hitclap"};
static_assert(skin_sample_kinds.size() == static_cast<std::size_t>(osu_sample_kind_t::hitclap) + 1);

auto parse_skin_sample_index(std::string_view digits) -> std::optional<std::int64_t> {
    if (digits.empty()) return 1;
    std::int64_t index{};
    // osu! never writes a suffix for index 1, and never pads with zeros.
    if (digits.front() == '0' || !parse_integer(digits, index) || index < 2 || index > INT32_MAX) return {};
    return index;
}
}

auto osu_skin_sample_t::from(const std::filesystem::path& filename) -> std::optional<osu_skin_sample_t> {
    const auto extension = text_util::ascii_lowercase(filename.extension().generic_string());
    if (std::ranges::find(osu_skin_sample_extensions, extension) == osu_skin_sample_extensions.end()) return {};
    const auto stem = text_util::ascii_lowercase(filename.stem().generic_string());
    for (std::size_t set = 1; set < skin_sample_sets.size(); ++set) {
        for (std::size_t kind = 0; kind < skin_sample_kinds.size(); ++kind) {
            const auto prefix = std::string{skin_sample_sets[set]} + "-" + std::string{skin_sample_kinds[kind]};
            if (!stem.starts_with(prefix)) continue;
            const auto index = parse_skin_sample_index(std::string_view{stem}.substr(prefix.size()));
            if (!index) return {};
            return osu_skin_sample_t{.set = static_cast<osu_sample_set_t>(set),
                .kind = static_cast<osu_sample_kind_t>(kind), .index = *index};
        }
    }
    return {};
}

auto osu_skin_sample_t::stem() const -> std::string {
    const auto set_name = skin_sample_sets[set == osu_sample_set_t::automatic ? 1 : static_cast<std::size_t>(set)];
    auto result = std::string{set_name} + "-" + std::string{skin_sample_kinds[static_cast<std::size_t>(kind)]};
    if (index > 1) result += std::to_string(index);
    return result;
}
}
