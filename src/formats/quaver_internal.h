#pragma once
#include <wormedit/formats/quaver.h>
#include <wormedit/core/rational.h>
#include <yaml-cpp/yaml.h>
#include "id_source.h"

namespace wormedit::quaver_detail {
using code_t = quaver_diagnostic_code_t;
// Internal exception only; public format boundaries return the shared attempt type.
struct failure_t { quaver_diagnostic_t diagnostic; };
[[noreturn]] inline void fail(code_t code, std::string message, std::string location = {}) {
    throw failure_t{quaver_diagnostic_t{.code = code, .message = std::move(message),
                                      .location = std::move(location)}};
}
template<class T> auto field(const YAML::Node& node, const char* key, T fallback = {}) -> T {
    const auto value = node[key];
    return value ? value.as<T>() : fallback;
}
inline auto number(const YAML::Node& node, const char* key, double fallback = 0) -> double {
    const auto value = field<double>(node, key, fallback);
    if (!std::isfinite(value) || std::abs(value) > 86'400'000)
        fail(code_t::invalid_value, "Non-finite or out-of-range numeric field", key);
    return value;
}
[[nodiscard]] auto load(std::string_view text, const quaver_parse_options_t& options) -> YAML::Node;
[[nodiscard]] auto normalize(YAML::Node root, std::size_t max_events) -> YAML::Node;
[[nodiscard]] auto key_count(const YAML::Node& root) -> unsigned;
[[nodiscard]] auto rational(double value) -> signed_rational_t;
[[nodiscard]] auto color(std::string_view text) -> color_rgba_t;
void validate_path(std::string_view text);
[[nodiscard]] auto dump(const YAML::Node& node) -> std::string;
inline auto provenance(const YAML::Node& node, std::size_t order = 0) -> source_provenance_t {
    return {.family = format_family_t::quaver, .profile = "qua-v1-experimental",
            .source_spelling = dump(node), .source_order = order};
}
inline auto retained(const source_provenance_t& source) -> YAML::Node {
    if (source.family == format_family_t::quaver && !source.source_spelling.empty())
        return load(source.source_spelling, {});
    return YAML::Node(YAML::NodeType::Map);
}
inline void retain_position(YAML::Node node, local_position_t position, const char* prefix = "_") {
    node[std::string(prefix) + "measure"] = position.measure.value;
    node[std::string(prefix) + "row"] = position.row;
}
inline auto same_position(const YAML::Node& node, local_position_t position, const char* prefix = "_") -> bool {
    return node[std::string(prefix) + "measure"] &&
        node[std::string(prefix) + "measure"].as<std::uint64_t>() == position.measure.value &&
        node[std::string(prefix) + "row"].as<std::uint64_t>() == position.row;
}
}
