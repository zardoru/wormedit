#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>

namespace wormedit {

// Non-owning index for existing frame/cache map builders. Entries borrow their
// source collection; use linear document members where no index exists today.
template <class Value>
struct document_id_index_t {
    using id_type = decltype(Value::id);
    std::unordered_map<std::uint64_t, const Value*> by_id;

    [[nodiscard]] static auto from(std::span<const Value> values) -> document_id_index_t {
        document_id_index_t result{};
        for (const auto& value : values) result.add(value);
        return result;
    }
    // Preserve the original map builders' first-in-source-order duplicate rule.
    void add(const Value& value) { by_id.emplace(value.id.value, &value); }
    [[nodiscard]] auto find(id_type id) const -> const Value* {
        const auto entry = by_id.find(id.value);
        return entry == by_id.end() ? nullptr : entry->second;
    }
    [[nodiscard]] auto has_entry(id_type id) const -> bool { return find(id) != nullptr; }
    // Existing guarded map consumers use at(); retain its missing-key exception.
    [[nodiscard]] auto at(id_type id) const -> const Value* { return by_id.at(id.value); }
};

} // namespace wormedit
