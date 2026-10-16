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
// A bool(Value&) refill publishes its key only on success, preserving the last
// binding on failure. The builder owns any partial changes to the retained value.
// Owner updates may change incremental products; the runner owns their binding.
// Nullary builds capture borrowed keys before replacing the retained value.
// An optional predicate controls reuse of a key-equal entry. Failures and
// partial products remain stored, and later lookups may choose to rebuild them.
template <class Key, class Value>
class keyed_cache_t {
  public:
    template <class Lookup, class Builder, class CanReuse = std::nullptr_t>
    [[nodiscard]] auto get_or_build(const Lookup& key, Builder&& builder,
                                    CanReuse&& can_reuse = nullptr) -> Value& {
        if (valid_ && *key_ == key) {
            if constexpr (std::is_same_v<std::remove_cvref_t<CanReuse>, std::nullptr_t>) return value_;
            else if (can_reuse(value_)) return value_;
        }
        if constexpr (std::is_invocable_v<Builder>) {
            auto value = std::forward<Builder>(builder)();
            assign_key(key);
            value_ = std::move(value);
        } else {
            using refill_result_t = std::invoke_result_t<Builder, Value&>;
            if constexpr (std::is_same_v<refill_result_t, bool>) {
                if (!std::forward<Builder>(builder)(value_)) return value_;
            } else {
                static_assert(std::is_void_v<refill_result_t>,
                              "A cache refill builder must return void or bool.");
                std::forward<Builder>(builder)(value_);
            }
            assign_key(key);
        }
        valid_ = true;
        ++build_count_;
        return value_;
    }

    // Invalidate the binding while allowing an owner to retain selected storage/metrics.
    template <class Reset = std::nullptr_t>
    void invalidate(Reset&& reset = nullptr) {
        if constexpr (!std::is_same_v<std::remove_cvref_t<Reset>, std::nullptr_t>)
            std::forward<Reset>(reset)(value_);
        valid_ = false;
    }

    // Preserve the last source binding for owners with narrower stamp policies.
    // The next lookup rebuilds even when that key is unchanged.
    void invalidate_value() {
        valid_ = false;
        value_ = Value{};
    }

    [[nodiscard]] auto has_value() const noexcept -> bool { return valid_; }
    [[nodiscard]] auto key() const noexcept -> const std::optional<Key>& { return key_; }
    [[nodiscard]] auto value() noexcept -> Value& { return value_; }
    [[nodiscard]] auto value() const noexcept -> const Value& { return value_; }
    [[nodiscard]] auto build_count() const noexcept -> std::size_t { return build_count_; }

  private:
    template <class Lookup>
    void assign_key(const Lookup& key) {
        if constexpr (std::is_same_v<Key, std::remove_cvref_t<Lookup>> ||
                      (std::is_constructible_v<Key, const Lookup&> &&
                       std::is_assignable_v<Key&, const Lookup&>)) {
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
