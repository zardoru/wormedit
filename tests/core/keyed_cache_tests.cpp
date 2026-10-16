#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/result.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using namespace wormedit;

namespace {

template <class Value>
struct counting_builder_t {
    std::size_t& calls;
    Value value;

    auto operator()() -> Value {
        ++calls;
        return value;
    }
    void refill(Value& retained) {
        ++calls;
        retained = value;
    }
    void invalidate_then_refill(keyed_cache_t<int, Value>& cache, Value& retained) {
        cache.invalidate_value();
        refill(retained);
    }
};

struct conditional_refill_t {
    std::size_t& calls;
    int replacement{};
    bool succeeds{};
    auto operator()(int& retained) -> bool {
        ++calls;
        if (succeeds) retained = replacement;
        return succeeds;
    }
};

struct key_view_t {
    std::span<const int> values;
    std::size_t& captures;
    std::size_t& assignments;
};

struct owned_key_t {
    std::vector<int> values;

    static auto from(const key_view_t& key) -> owned_key_t {
        ++key.captures;
        return {.values = {key.values.begin(), key.values.end()}};
    }
    void assign(const key_view_t& key) {
        ++key.assignments;
        values.assign(key.values.begin(), key.values.end());
    }
    auto operator==(const key_view_t& key) const -> bool {
        return std::ranges::equal(values, key.values);
    }
    auto operator==(const owned_key_t&) const -> bool = default;
};

} // namespace

TEST_CASE("An empty keyed cache exposes its default value", "[core][keyed-cache]") {
    const keyed_cache_t<int, std::vector<int>> cache;
    CHECK_FALSE(cache.has_value());
    CHECK(cache.value().empty());
    CHECK(cache.build_count() == 0);
}

TEST_CASE("A default key builds once and equal keys reuse the value", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    const auto& first = cache.get_or_build(0, counting_builder_t<int>{calls, 17});
    const auto& second = cache.get_or_build(0, counting_builder_t<int>{calls, 23});
    CHECK(&first == &second);
    CHECK(second == 17);
    CHECK(calls == 1);
    CHECK(cache.build_count() == 1);
}

TEST_CASE("A changed key replaces the retained value", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<int>{calls, 17}));
    CHECK(cache.get_or_build(2, counting_builder_t<int>{calls, 23}) == 23);
    CHECK(calls == 2);
    CHECK(cache.build_count() == 2);
}

TEST_CASE("A keyed cache retains failed results without retrying", "[core][keyed-cache]") {
    using value_t = result_t<int, std::string>;
    keyed_cache_t<int, value_t> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1,
        counting_builder_t<value_t>{calls, std::unexpected(std::string{"failed"})}));
    const auto& cached = cache.get_or_build(1, counting_builder_t<value_t>{calls, 23});
    REQUIRE_FALSE(cached.has_value());
    CHECK(cached.error() == "failed");
    CHECK(calls == 1);
    CHECK(cache.build_count() == 1);
    CHECK(cache.has_value());
}

TEST_CASE("A new key can replace a cached failure with success", "[core][keyed-cache]") {
    using value_t = result_t<int, std::string>;
    keyed_cache_t<int, value_t> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1,
        counting_builder_t<value_t>{calls, std::unexpected(std::string{"failed"})}));
    const auto& cached = cache.get_or_build(2, counting_builder_t<value_t>{calls, 23});
    REQUIRE(cached.has_value());
    CHECK(*cached == 23);
    CHECK(calls == 2);
}

TEST_CASE("Resetting a keyed cache clears the key value and build count", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<int>{calls, 17}));
    cache = {};
    CHECK_FALSE(cache.has_value());
    CHECK(cache.value() == 0);
    CHECK(cache.build_count() == 0);
    CHECK(cache.get_or_build(1, counting_builder_t<int>{calls, 23}) == 23);
    CHECK(calls == 2);
}

TEST_CASE("Keyed caches with equal keys remain independent", "[core][keyed-cache]") {
    keyed_cache_t<int, int> first;
    keyed_cache_t<int, int> second;
    std::size_t calls{};
    CHECK(first.get_or_build(1, counting_builder_t<int>{calls, 17}) == 17);
    CHECK(second.get_or_build(1, counting_builder_t<int>{calls, 23}) == 23);
    CHECK(calls == 2);
}

TEST_CASE("Equal borrowed keys do not recapture their owned state", "[core][keyed-cache]") {
    keyed_cache_t<owned_key_t, int> cache;
    std::vector<int> values{1, 2};
    std::size_t captures{}, assignments{}, calls{};
    const key_view_t key{values, captures, assignments};
    static_cast<void>(cache.get_or_build(key, counting_builder_t<int>{calls, 17}));
    CHECK(cache.get_or_build(key, counting_builder_t<int>{calls, 23}) == 17);
    CHECK(captures == 1);
    CHECK(assignments == 0);
    CHECK(calls == 1);
}

TEST_CASE("Changed borrowed keys update the owned state in place", "[core][keyed-cache]") {
    keyed_cache_t<owned_key_t, int> cache;
    std::vector<int> values{1, 2};
    std::size_t captures{}, assignments{}, calls{};
    const key_view_t key{values, captures, assignments};
    static_cast<void>(cache.get_or_build(key, counting_builder_t<int>{calls, 17}));
    values.front() = 3;
    CHECK(cache.get_or_build(key, counting_builder_t<int>{calls, 23}) == 23);
    CHECK(captures == 1);
    CHECK(assignments == 1);
    CHECK(calls == 2);
}

TEST_CASE("In-place cache refills reuse retained storage only on misses", "[core][keyed-cache]") {
    keyed_cache_t<int, std::vector<int>> cache;
    std::size_t calls{};
    counting_builder_t<std::vector<int>> builder{calls, {17}};
    const auto refill = std::bind_front(&counting_builder_t<std::vector<int>>::refill,
                                        std::ref(builder));
    const auto& first = cache.get_or_build(1, refill);
    const auto* storage = first.data();
    CHECK(first == std::vector<int>{17});
    builder.value.front() = 23;
    const auto& second = cache.get_or_build(2, refill);
    CHECK(second.data() == storage);
    CHECK(second == std::vector<int>{23});
    builder.value.front() = 29;
    CHECK(cache.get_or_build(2, refill) == std::vector<int>{23});
    CHECK(calls == 2);
    CHECK(cache.build_count() == 2);
}

TEST_CASE("In-place cache refills retain failures until the key changes", "[core][keyed-cache]") {
    using value_t = result_t<int, std::string>;
    keyed_cache_t<int, value_t> cache;
    std::size_t calls{};
    counting_builder_t<value_t> builder{calls, std::unexpected(std::string{"failed"})};
    const auto refill = std::bind_front(&counting_builder_t<value_t>::refill, std::ref(builder));
    const auto& failed = cache.get_or_build(1, refill);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error() == "failed");
    builder.value = 23;
    CHECK_FALSE(cache.get_or_build(1, refill).has_value());
    CHECK(cache.get_or_build(2, refill) == value_t{23});
    CHECK(calls == 2);
    CHECK(cache.build_count() == 2);
}

TEST_CASE("Value invalidation retains the last key and rebuilds an equal lookup", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<int>{calls, 17}));
    cache.invalidate_value();
    CHECK_FALSE(cache.has_value());
    CHECK(cache.value() == 0);
    CHECK(cache.key() == std::optional<int>{1});
    CHECK(cache.build_count() == 1);
    CHECK(cache.get_or_build(1, counting_builder_t<int>{calls, 23}) == 23);
    CHECK(cache.build_count() == 2);
}

TEST_CASE("A refill may invalidate its old value without losing key publication", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<int>{calls, 17}));
    counting_builder_t<int> builder{calls, 23};
    const auto refill = std::bind_front(&counting_builder_t<int>::invalidate_then_refill,
                                       std::ref(builder), std::ref(cache));
    CHECK(cache.get_or_build(2, refill) == 23);
    CHECK(cache.has_value());
    CHECK(cache.key() == std::optional<int>{2});
    CHECK(cache.build_count() == 2);
    CHECK(calls == 2);
}

TEST_CASE("A bound cache value supports owner updates without rebuilding", "[core][keyed-cache]") {
    keyed_cache_t<int, std::vector<int>> cache;
    std::size_t calls{};
    auto& retained = cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {1}});
    retained.push_back(2);
    CHECK(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {9}}).size() == 2);
    CHECK(calls == 1);
    CHECK(cache.get_or_build(2, counting_builder_t<std::vector<int>>{calls, {9}}).front() == 9);
    CHECK(cache.build_count() == 2);
}

TEST_CASE("A borrowed key is captured before its retained source value is replaced", "[core][keyed-cache]") {
    keyed_cache_t<owned_key_t, std::vector<int>> cache;
    std::size_t calls{}, captures{}, assignments{};
    const std::vector<int> seed{1}, original{2, 3}, replacement{4, 5};
    static_cast<void>(cache.get_or_build(key_view_t{seed, captures, assignments},
        counting_builder_t<std::vector<int>>{calls, original}));
    const key_view_t borrowed{cache.value(), captures, assignments};
    static_cast<void>(cache.get_or_build(borrowed,
        counting_builder_t<std::vector<int>>{calls, replacement}));
    CHECK(cache.key()->values == original);
    CHECK(cache.value() == replacement);
    CHECK(assignments == 1);
    CHECK(calls == 2);
}

TEST_CASE("A reuse predicate retains partial failure and retries the same key", "[core][keyed-cache]") {
    using value_t = result_t<int, std::string>;
    keyed_cache_t<int, value_t> cache;
    std::size_t calls{};
    const auto can_reuse = [](const value_t& value) { return value.has_value(); };
    static_cast<void>(cache.get_or_build(1,
        counting_builder_t<value_t>{calls, std::unexpected(std::string{"failed"})}, can_reuse));
    CHECK(cache.has_value());
    CHECK(cache.value().error() == "failed");
    CHECK(cache.key() == std::optional<int>{1});
    CHECK(cache.get_or_build(1, counting_builder_t<value_t>{calls, 23}, can_reuse).value() == 23);
    CHECK(cache.has_value());
    CHECK(cache.build_count() == 2);
}

TEST_CASE("Owner updates can invalidate value reuse without changing its key", "[core][keyed-cache]") {
    using value_t = result_t<int, std::string>;
    keyed_cache_t<int, value_t> cache;
    std::size_t calls{};
    const auto can_reuse = [](const value_t& value) { return value.has_value(); };
    auto& retained = cache.get_or_build(1, counting_builder_t<value_t>{calls, 23}, can_reuse);
    retained = std::unexpected(std::string{"expired"});
    CHECK(cache.get_or_build(1, counting_builder_t<value_t>{calls, 42}, can_reuse).value() == 42);
    CHECK(cache.build_count() == 2);
    CHECK(cache.has_value());
}

TEST_CASE("A failed conditional refill preserves the last successful binding", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, conditional_refill_t{calls, 17, true}));
    CHECK(cache.get_or_build(2, conditional_refill_t{calls, 23, false}) == 17);
    CHECK(cache.build_count() == 1);
    CHECK(cache.get_or_build(1, conditional_refill_t{calls, 42, true}) == 17);
    CHECK(calls == 2);
    CHECK(cache.get_or_build(2, conditional_refill_t{calls, 23, true}) == 23);
    CHECK(cache.build_count() == 2);
}

TEST_CASE("An initially failed conditional refill stays unbound and retries", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    CHECK(cache.get_or_build(0, conditional_refill_t{calls, 23, false}) == 0);
    CHECK_FALSE(cache.has_value());
    CHECK_FALSE(cache.key());
    CHECK(cache.build_count() == 0);
    CHECK(cache.get_or_build(0, conditional_refill_t{calls, 23, true}) == 23);
    CHECK(calls == 2);
}

TEST_CASE("Standard tuple keys borrow expensive components on lookup", "[core][keyed-cache]") {
    keyed_cache_t<std::tuple<std::string, int>, int> cache;
    std::size_t calls{};
    std::string text(200, 'a');
    const std::tuple<const std::string&, int> query{text, 7};
    static_cast<void>(cache.get_or_build(query, counting_builder_t<int>{calls, 17}));
    const auto* storage = std::get<0>(*cache.key()).data();
    CHECK(cache.get_or_build(query, counting_builder_t<int>{calls, 23}) == 17);
    CHECK(std::get<0>(*cache.key()).data() == storage);
    CHECK(calls == 1);
    text.back() = 'b';
    CHECK(std::get<0>(*cache.key()).back() == 'a');
    CHECK(cache.get_or_build(query, counting_builder_t<int>{calls, 23}) == 23);
    CHECK(std::get<0>(*cache.key()).back() == 'b');
}

TEST_CASE("Owner invalidation preserves selected storage and metrics", "[core][keyed-cache]") {
    keyed_cache_t<int, std::vector<int>> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {1, 2, 3}}));
    const auto capacity = cache.value().capacity();
    cache.invalidate([](auto& value) { value.clear(); });
    CHECK_FALSE(cache.has_value());
    CHECK(cache.key() == std::optional<int>{1});
    CHECK(cache.value().empty());
    CHECK(cache.value().capacity() == capacity);
    CHECK(cache.build_count() == 1);
    static_cast<void>(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {4}}));
    CHECK(cache.build_count() == 2);
}

TEST_CASE("Owner product updates preserve the cache binding", "[core][keyed-cache]") {
    keyed_cache_t<int, std::vector<int>> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {1, 2}}));
    cache.value().back() = 3;
    CHECK(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {4}}).back() == 3);
    CHECK(cache.key() == std::optional<int>{1});
    CHECK(cache.build_count() == 1);
    CHECK(calls == 1);
}

TEST_CASE("Binding invalidation retains the incremental product and retries", "[core][keyed-cache]") {
    keyed_cache_t<int, std::vector<int>> cache;
    std::size_t calls{};
    static_cast<void>(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {1, 2}}));
    const auto* storage = cache.value().data();
    cache.invalidate();
    CHECK_FALSE(cache.has_value());
    CHECK(cache.key() == std::optional<int>{1});
    CHECK(cache.value().data() == storage);
    CHECK(cache.value().size() == 2);
    CHECK(cache.build_count() == 1);
    static_cast<void>(cache.get_or_build(1, counting_builder_t<std::vector<int>>{calls, {4}}));
    CHECK(calls == 2);
}

TEST_CASE("A cache binding query distinguishes retained keys from current products", "[core][keyed-cache]") {
    keyed_cache_t<int, int> cache;
    std::size_t calls{};
    CHECK_FALSE(cache.is_bound_to(0));
    static_cast<void>(cache.get_or_build(1, conditional_refill_t{calls, 17, true}));
    CHECK(cache.is_bound_to(1));
    static_cast<void>(cache.get_or_build(2, conditional_refill_t{calls, 23, false}));
    CHECK(cache.is_bound_to(1));
    CHECK_FALSE(cache.is_bound_to(2));
    cache.invalidate();
    CHECK_FALSE(cache.is_bound_to(1));
    CHECK(cache.key() == std::optional<int>{1});
}
