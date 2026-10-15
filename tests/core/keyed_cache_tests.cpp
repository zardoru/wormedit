#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/result.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
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
