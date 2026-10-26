#pragma once
#include <algorithm>
#include <stdexcept>
#include <wormedit/core/numeric_text.h>
#include <wormedit/scripting/lua_sandbox.h>
namespace wormedit::scripting::lua_value_detail {
using table_t = lua_sandbox_context_t;
using node_t = lua_sandbox_node_t;
[[noreturn]] inline void malformed(std::string message) {
    throw std::runtime_error{std::move(message)};
}
inline auto children(const node_t& node) -> const table_t& {
    if (const auto* value = std::get_if<table_t>(&node.value))
        return *value;
    malformed("Generator output needs table values.");
}
inline auto field(const table_t& table, std::string_view name) -> const node_t* {
    for (const auto& entry : table)
        if (const auto* key = std::get_if<std::string>(&entry.key); key && *key == name)
            return &entry;
    return nullptr;
}
inline auto required(const table_t& table, std::string_view name) -> const node_t& {
    if (const auto* value = field(table, name))
        return *value;
    malformed("Missing generator field: " + std::string{name});
}
inline auto scalar(const node_t& node) -> const lua_sandbox_value_t& {
    if (const auto* value = std::get_if<lua_sandbox_value_t>(&node.value))
        return *value;
    malformed("Generator field needs a scalar value.");
}
inline auto text(const node_t& node) -> std::string {
    if (const auto* value = std::get_if<std::string>(&scalar(node)))
        return *value;
    malformed("Generator key/name needs text.");
}
inline auto integer(const node_t& node) -> std::uint64_t {
    const auto& value = scalar(node);
    if (const auto* number = std::get_if<std::int64_t>(&value); number && *number >= 0)
        return static_cast<std::uint64_t>(*number);
    if (const auto* string = std::get_if<std::string>(&value)) {
        std::uint64_t number{};
        if (parse_integer(std::string_view{*string}, number))
            return number;
    }
    malformed(
        "Generator positions and identities need exact unsigned integers or decimal strings.");
}
inline auto signed_integer(const node_t& node) -> std::int64_t {
    const auto& value = scalar(node);
    if (const auto* number = std::get_if<std::int64_t>(&value))
        return *number;
    if (const auto* string = std::get_if<std::string>(&value)) {
        std::int64_t number{};
        if (parse_integer(std::string_view{*string}, number))
            return number;
    }
    malformed("Expected an exact signed integer or decimal string.");
}
inline void dense(const table_t& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto* key = std::get_if<std::int64_t>(&table[i].key);
        if (!key || *key != static_cast<std::int64_t>(i + 1))
            malformed("Generator lists must be dense arrays starting at 1.");
    }
}
inline void only_fields(const table_t& table, std::initializer_list<std::string_view> allowed) {
    for (const auto& entry : table) {
        const auto* key = std::get_if<std::string>(&entry.key);
        if (!key || std::ranges::find(allowed, *key) == allowed.end())
            malformed("Unknown generator proposal field.");
    }
}
} // namespace wormedit::scripting::lua_value_detail
