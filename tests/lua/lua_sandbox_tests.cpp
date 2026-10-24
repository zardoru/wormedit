#include <wormedit/scripting/lua_sandbox.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <thread>
#include <type_traits>

using namespace wormedit;
using namespace wormedit::scripting;
namespace {
auto session(lua_sandbox_limits_t limits = {}) -> lua_sandbox_t {
    auto result = lua_sandbox_t::from(std::move(limits));
    REQUIRE(result);
    return std::move(*result);
}
auto evaluate(lua_sandbox_t& sandbox, std::string_view source) -> lua_sandbox_value_t {
    auto result = sandbox.evaluate(source);
    INFO((result ? "success" : result.error().message));
    REQUIRE(result);
    return std::move(*result);
}
void check_failure(lua_sandbox_t& sandbox, std::string_view source, failure_code_t code) {
    const auto result = sandbox.evaluate(source);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == code);
    CHECK_FALSE(sandbox.is_open());
    CHECK(sandbox.usage().allocated_bytes == 0);
}
}

TEST_CASE("Lua sandbox exposes only the approved libraries and no host capabilities", "[lua-sandbox]") {
    auto sandbox = session();
    CHECK(evaluate(sandbox, R"(
        for _, name in ipairs({'io','os','package','debug','coroutine','require',
            'dofile','loadfile','print','warn','collectgarbage','setmetatable','getmetatable'}) do
            assert(_G[name] == nil, name)
        end
        return string.upper(table.concat({'a', 'b'})) .. utf8.len('abc') .. math.abs(-4)
    )") == lua_sandbox_value_t{std::string("AB34")});
    CHECK(sandbox.is_open());
    CHECK(sandbox.usage().allocated_bytes > 0);
}

TEST_CASE("Lua sandbox returns bounded scalar values", "[lua-sandbox]") {
    struct example_t { const char* source; lua_sandbox_value_t expected; };
    const auto example = GENERATE(
        example_t{.source = "", .expected = std::monostate{}},
        example_t{.source = "return nil", .expected = std::monostate{}},
        example_t{.source = "return true", .expected = true},
        example_t{.source = "return 42", .expected = std::int64_t{42}},
        example_t{.source = "return 0.5", .expected = 0.5},
        example_t{.source = "return 'a' .. string.char(0) .. 'b'", .expected = std::string("a\0b", 3)});
    auto sandbox = session();
    CHECK(evaluate(sandbox, example.source) == example.expected);
}

TEST_CASE("Lua sandbox interrupts infinite bytecode loops and cannot be reused after failure", "[lua-sandbox]") {
    auto sandbox = session({.maximum_instructions = 128});
    check_failure(sandbox, "while true do end", failure_code_t::capacity_exceeded);
    const auto closed = sandbox.evaluate("return 1");
    REQUIRE_FALSE(closed);
    CHECK(closed.error().code == failure_code_t::not_ready);
}

TEST_CASE("Lua sandbox bounds loops that try to catch instruction exhaustion", "[lua-sandbox]") {
    auto sandbox = session({.maximum_instructions = 128});
    check_failure(sandbox, "while true do pcall(function() while true do end end) end",
        failure_code_t::capacity_exceeded);
}

TEST_CASE("Lua sandbox keeps xpcall error handlers inside the instruction budget", "[lua-sandbox]") {
    const auto handler = GENERATE(false, true);
    auto sandbox = session({.maximum_instructions = 128});
    check_failure(sandbox, handler
        ? "xpcall(function() error('x') end, function() while true do end end)"
        : "xpcall(function() while true do end end, function() while true do end end)",
        failure_code_t::capacity_exceeded);
}

TEST_CASE("Lua sandbox xpcall preserves ordinary arguments results and handled errors", "[lua-sandbox]") {
    auto sandbox = session();
    CHECK(evaluate(sandbox, "local ok,x,y=xpcall(function(x) return x,43 end, tostring,42); return ok and x==42 and y==43")
        == lua_sandbox_value_t{true});
    CHECK(evaluate(sandbox, "local ok,msg=xpcall(function() error('x') end, function() return 'caught' end); return not ok and msg=='caught'")
        == lua_sandbox_value_t{true});
}

TEST_CASE("Lua sandbox caps large allocations even when Lua catches the error", "[lua-sandbox]") {
    auto sandbox = session({.maximum_memory_bytes = 128U * 1024U});
    const auto caught = GENERATE(false, true);
    const auto source = caught
        ? "pcall(function() return string.rep('x', 100 * 1024 * 1024) end); return true"
        : "return string.rep('x', 100 * 1024 * 1024)";
    check_failure(sandbox, source, failure_code_t::out_of_memory);
    CHECK(sandbox.usage().peak_bytes <= 128U * 1024U);
}

TEST_CASE("Lua sandbox handles memory caps too small for protected initialization", "[lua-sandbox]") {
    const auto bytes = GENERATE(std::size_t{1}, std::size_t{1024}, std::size_t{4096});
    const auto result = lua_sandbox_t::from({.maximum_memory_bytes = bytes});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == failure_code_t::out_of_memory);
}

TEST_CASE("Lua sandbox preserves closures and charges one budget across calls", "[lua-sandbox]") {
    auto sandbox = session({.maximum_instructions = 512});
    evaluate(sandbox, "local count=0; function next_value() count=count+1; return count end");
    const auto before = sandbox.usage().remaining_instructions;
    CHECK(evaluate(sandbox, "return next_value()") == lua_sandbox_value_t{std::int64_t{1}});
    CHECK(evaluate(sandbox, "return next_value()") == lua_sandbox_value_t{std::int64_t{2}});
    CHECK(sandbox.usage().remaining_instructions < before);
    check_failure(sandbox, "while true do next_value() end", failure_code_t::capacity_exceeded);
}

TEST_CASE("Lua sandbox random generator has a stable specified bit stream", "[lua-sandbox]") {
    auto sandbox = session({.seed = 0});
    CHECK(evaluate(sandbox, "return math.random(0)") == lua_sandbox_value_t{INT64_C(-2152535657050944081)});
    CHECK(evaluate(sandbox, "return math.random(0)") == lua_sandbox_value_t{INT64_C(7960286522194355700)});
    CHECK(evaluate(sandbox, "math.randomseed(); return math.random(0)") == lua_sandbox_value_t{INT64_C(-2152535657050944081)});
}

TEST_CASE("Lua sandbox random streams start independently and support signed ranges", "[lua-sandbox]") {
    auto first = session({.seed = 42});
    auto second = session({.seed = 42});
    auto other = session({.seed = 43});
    constexpr auto source = R"(
        local values={}
        for i=1,32 do
            local x=math.random(-17,23); assert(x>=-17 and x<=23)
            values[i]=x
            local y=math.random(); assert(y>=0 and y<1)
            assert(math.random(1,1)==1)
        end
        return table.concat(values, ',')
    )";
    const auto expected = evaluate(first, source);
    CHECK(evaluate(second, source) == expected);
    CHECK(evaluate(other, source) != expected);
}

TEST_CASE("Lua sandbox dynamic load accepts text and explicit environments only", "[lua-sandbox]") {
    auto sandbox = session();
    CHECK(evaluate(sandbox, "return assert(load('return 40+2', 'text', 't'))()") == lua_sandbox_value_t{std::int64_t{42}});
    CHECK(evaluate(sandbox, "return assert(load('return value', 'text', 't', {value=9}))()") == lua_sandbox_value_t{std::int64_t{9}});
    CHECK(evaluate(sandbox, "return load(string.dump(function() return 1 end), 'binary', 'bt') == nil") == lua_sandbox_value_t{true});
    CHECK(evaluate(sandbox, "return not pcall(load, function() return 'return 1' end)") == lua_sandbox_value_t{true});
}

TEST_CASE("Lua sandbox rejects top-level binary chunks", "[lua-sandbox]") {
    auto sandbox = session();
    check_failure(sandbox, std::string_view("\x1bLua\0", 5), failure_code_t::invalid_data);
}

TEST_CASE("Lua sandbox reports syntax runtime and nonstring errors without panicking", "[lua-sandbox]") {
    const auto syntax = GENERATE(false, true);
    auto sandbox = session();
    check_failure(sandbox, syntax ? "return {" : "error({})",
        syntax ? failure_code_t::invalid_data : failure_code_t::task_failed);
}

TEST_CASE("Lua sandbox bounds error and result text and rejects nonscalar results", "[lua-sandbox]") {
    SECTION("error text") {
        auto sandbox = session();
        const auto result = sandbox.evaluate("error(string.rep('x', 10000))");
        REQUIRE_FALSE(result);
        CHECK(result.error().message.size() <= 4096);
        CHECK_FALSE(sandbox.is_open());
    }
    SECTION("result text") {
        auto sandbox = session({.maximum_result_bytes = 8});
        check_failure(sandbox, "return string.rep('x', 9)", failure_code_t::capacity_exceeded);
    }
    SECTION("table") {
        auto sandbox = session();
        check_failure(sandbox, "return {}", failure_code_t::unsupported);
    }
    SECTION("nonfinite") {
        auto sandbox = session();
        check_failure(sandbox, "return 0/0", failure_code_t::invalid_data);
    }
}

TEST_CASE("Lua sandbox source and configuration limits reject without partial execution", "[lua-sandbox]") {
    const auto invalid = lua_sandbox_t::from({.maximum_instructions = 0});
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().code == failure_code_t::invalid_request);
    auto sandbox = session({.maximum_source_bytes = 8});
    check_failure(sandbox, "return 123456", failure_code_t::capacity_exceeded);
}

TEST_CASE("Lua sandbox cancellation covers setup and executing bytecode", "[lua-sandbox]") {
    std::stop_source cancellation;
    SECTION("before setup") {
        cancellation.request_stop();
        const auto result = lua_sandbox_t::from({.cancellation = cancellation.get_token()});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == failure_code_t::cancelled);
    }
    SECTION("during execution") {
        auto sandbox = session({.maximum_instructions = UINT64_MAX, .cancellation = cancellation.get_token()});
        std::jthread stop([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            cancellation.request_stop();
        });
        check_failure(sandbox, "while true do end", failure_code_t::cancelled);
    }
}

TEST_CASE("Lua sandbox moves retain the state callback lifetime", "[lua-sandbox]") {
    static_assert(!std::is_copy_constructible_v<lua_sandbox_t>);
    auto original = session();
    evaluate(original, "value=41");
    auto moved = std::move(original);
    CHECK_FALSE(original.is_open());
    CHECK(evaluate(moved, "return value+1") == lua_sandbox_value_t{std::int64_t{42}});
    auto assigned = session();
    assigned = std::move(moved);
    CHECK_FALSE(moved.is_open());
    CHECK(evaluate(assigned, "return value") == lua_sandbox_value_t{std::int64_t{41}});
}
