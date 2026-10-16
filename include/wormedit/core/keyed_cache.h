#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace wormedit {

// One retained value, including failures. Borrowed lookup keys compare without
// copying; their owned Key supplies from/assign to retain only miss-time state.
template <class Key, class Value>
class keyed_cache_t {
  public:
    template <class Lookup, class Builder>
    [[nodiscard]] auto get_or_build(const Lookup& key, Builder&& builder) -> const Value& {
        if (key_ && *key_ == key) return value_;
        auto value = std::forward<Builder>(builder)();
        if constexpr (std::is_same_v<Key, std::remove_cvref_t<Lookup>>) {
            key_ = key;
        } else if (key_) {
            key_->assign(key);
        } else {
            key_ = Key::from(key);
        }
        value_ = std::move(value);
        ++build_count_;
        return value_;
    }

    [[nodiscard]] auto has_value() const noexcept -> bool { return key_.has_value(); }
    [[nodiscard]] auto key() const noexcept -> const std::optional<Key>& { return key_; }
    [[nodiscard]] auto value() const noexcept -> const Value& { return value_; }
    [[nodiscard]] auto build_count() const noexcept -> std::size_t { return build_count_; }

  private:
    std::optional<Key> key_;
    Value value_{};
    std::size_t build_count_{};
};

} // namespace wormedit
