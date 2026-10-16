#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace wormedit {

// One retained value, including failures. Borrowed lookup keys compare without
// copying; their owned Key supplies from/assign to retain only miss-time state.
// A nullary builder returns a replacement value. A void(Value&) builder borrows
// the retained storage only on a miss, allowing capacity reuse or early release.
// A bound lookup permits owner updates; value() remains read-only inspection.
// Nullary builds capture borrowed keys before replacing the retained value.
template <class Key, class Value>
class keyed_cache_t {
  public:
    template <class Lookup, class Builder>
    [[nodiscard]] auto get_or_build(const Lookup& key, Builder&& builder) -> Value& {
        if (valid_ && *key_ == key) return value_;
        if constexpr (std::is_invocable_v<Builder>) {
            auto value = std::forward<Builder>(builder)();
            assign_key(key);
            value_ = std::move(value);
        } else {
            static_assert(std::is_void_v<std::invoke_result_t<Builder, Value&>>,
                          "A cache refill builder must return void.");
            std::forward<Builder>(builder)(value_);
            assign_key(key);
        }
        valid_ = true;
        ++build_count_;
        return value_;
    }

    // Preserve the last source binding for owners with narrower stamp policies.
    // The next lookup rebuilds even when that key is unchanged.
    void invalidate_value() {
        valid_ = false;
        value_ = Value{};
    }

    [[nodiscard]] auto has_value() const noexcept -> bool { return valid_; }
    [[nodiscard]] auto key() const noexcept -> const std::optional<Key>& { return key_; }
    [[nodiscard]] auto value() const noexcept -> const Value& { return value_; }
    [[nodiscard]] auto build_count() const noexcept -> std::size_t { return build_count_; }

  private:
    template <class Lookup>
    void assign_key(const Lookup& key) {
        if constexpr (std::is_same_v<Key, std::remove_cvref_t<Lookup>>) {
            key_ = key;
        } else if (key_) {
            key_->assign(key);
        } else {
            key_ = Key::from(key);
        }
    }

    std::optional<Key> key_;
    Value value_{};
    std::size_t build_count_{};
    bool valid_{};
};

} // namespace wormedit
