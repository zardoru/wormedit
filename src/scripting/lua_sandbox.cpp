#include <wormedit/scripting/lua_sandbox.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace wormedit::scripting {
static_assert(sizeof(lua_Integer) == sizeof(std::int64_t));
static_assert(std::numeric_limits<lua_Number>::digits == 53);

struct lua_sandbox_t::implementation_t {
    struct alignas(std::max_align_t) allocation_t { std::size_t bytes; };
    lua_sandbox_limits_t limits;
    lua_sandbox_usage_t usage;
    lua_State* state{};
    std::uint64_t random_state{};
    std::optional<failure_code_t> abort_code;
    const char* abort_message{};
    std::string_view source;
    int load_status{LUA_OK};

    explicit implementation_t(lua_sandbox_limits_t value)
        : limits(std::move(value)), usage{.remaining_instructions = limits.maximum_instructions},
          random_state(limits.seed) {}
    ~implementation_t() { close(); }
    void close() noexcept {
        if (state) lua_close(std::exchange(state, nullptr));
    }
    void record_failure(failure_code_t code, const char* message) noexcept {
        if (abort_code) return;
        abort_code = code;
        abort_message = message;
    }
    auto can_continue() noexcept -> bool {
        if (limits.cancellation.stop_requested())
            record_failure(failure_code_t::cancelled, "Lua evaluation cancelled.");
        return !abort_code.has_value();
    }
    auto consume_instruction() noexcept -> bool {
        if (!can_continue()) return false;
        if (!usage.remaining_instructions) {
            record_failure(failure_code_t::capacity_exceeded, "Lua instruction budget exhausted.");
            return false;
        }
        --usage.remaining_instructions;
        return true;
    }
    static auto context(lua_State* state) noexcept -> implementation_t& {
        return **static_cast<implementation_t**>(lua_getextraspace(state));
    }
    // All callbacks that may raise Lua errors contain only trivial automatic
    // values. Their longjmp stays inside the outer protected lua_pcall.
    static void instruction_hook(lua_State* state, lua_Debug*) {
        auto& self = context(state);
        if (!self.consume_instruction()) luaL_error(state, "%s", self.abort_message);
    }
    static auto allocate(void* userdata, void* pointer, std::size_t, std::size_t bytes) noexcept -> void* {
        auto& self = *static_cast<implementation_t*>(userdata);
        auto* previous = pointer ? static_cast<allocation_t*>(pointer) - 1 : nullptr;
        const auto old_bytes = previous ? previous->bytes : 0;
        if (!bytes) {
            self.usage.allocated_bytes -= old_bytes;
            std::free(previous);
            return nullptr;
        }
        // Retaining capacity makes every shrink succeed, including under OOM.
        if (previous && bytes <= old_bytes - sizeof(allocation_t)) return pointer;
        if (!self.can_continue()) return nullptr;
        const auto available = self.limits.maximum_memory_bytes - self.usage.allocated_bytes;
        if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(allocation_t) ||
            bytes + sizeof(allocation_t) - old_bytes > available) {
            self.record_failure(failure_code_t::out_of_memory, "Lua memory cap exceeded.");
            return nullptr;
        }
        const auto total = bytes + sizeof(allocation_t);
        auto* replacement = static_cast<allocation_t*>(std::realloc(previous, total));
        if (!replacement) {
            self.record_failure(failure_code_t::out_of_memory, "Lua allocation failed.");
            return nullptr;
        }
        replacement->bytes = total;
        self.usage.allocated_bytes += total - old_bytes;
        self.usage.peak_bytes = std::max(self.usage.peak_bytes, self.usage.allocated_bytes);
        return replacement + 1;
    }
    auto random_bits() noexcept -> std::uint64_t {
        // SplitMix64, specified with unsigned 64-bit arithmetic.
        auto value = (random_state += UINT64_C(0x9e3779b97f4a7c15));
        value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
        return value ^ (value >> 31);
    }
    static auto random(lua_State* state) -> int {
        auto& self = context(state);
        const auto count = lua_gettop(state);
        if (count > 2) return luaL_error(state, "math.random expects zero, one or two arguments");
        if (!count) {
            if (!self.consume_instruction()) return luaL_error(state, "%s", self.abort_message);
            lua_pushnumber(state, static_cast<double>(self.random_bits() >> 11) * 0x1.0p-53);
            return 1;
        }
        const auto low = count == 1 ? lua_Integer{1} : luaL_checkinteger(state, 1);
        const auto high = luaL_checkinteger(state, count);
        const auto full = count == 1 && high == 0;
        if (!full && low > high) return luaL_error(state, "math.random interval is empty");
        const auto range = full ? UINT64_C(0) : static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low) + 1;
        const auto threshold = range ? (UINT64_C(0) - range) % range : 0;
        std::uint64_t bits;
        do {
            if (!self.consume_instruction()) return luaL_error(state, "%s", self.abort_message);
            bits = self.random_bits();
        } while (bits < threshold);
        const auto value = range ? static_cast<std::uint64_t>(low) + bits % range : bits;
        lua_pushinteger(state, std::bit_cast<std::int64_t>(value));
        return 1;
    }
    static auto randomseed(lua_State* state) -> int {
        auto& self = context(state);
        const auto first = lua_gettop(state) ? luaL_checkinteger(state, 1) : std::bit_cast<std::int64_t>(self.limits.seed);
        const auto second = lua_gettop(state) > 1 ? luaL_checkinteger(state, 2) : lua_Integer{0};
        self.random_state = static_cast<std::uint64_t>(first) ^ std::rotl(static_cast<std::uint64_t>(second), 32);
        lua_pushinteger(state, first);
        lua_pushinteger(state, second);
        return 2;
    }
    static auto load_text(lua_State* state) -> int {
        auto& self = context(state);
        luaL_checktype(state, 1, LUA_TSTRING);
        const auto has_environment = lua_gettop(state) >= 4;
        std::size_t length{};
        const auto* text = lua_tolstring(state, 1, &length);
        if (length > self.limits.maximum_source_bytes)
            return luaL_error(state, "Lua source exceeds the source limit");
        const auto* name = luaL_optstring(state, 2, "sandbox load");
        const auto status = luaL_loadbufferx(state, text, length, name, "t");
        if (status != LUA_OK) {
            lua_pushnil(state);
            lua_insert(state, -2);
            return 2;
        }
        if (has_environment) {
            lua_pushvalue(state, 4);
            lua_setupvalue(state, -2, 1);
        }
        return 1;
    }
    static auto protected_xpcall(lua_State* state) -> int {
        auto& self = context(state);
        const auto arguments = lua_gettop(state) - 2;
        luaL_checktype(state, 2, LUA_TFUNCTION);
        // Keep the handler below the call: [handler, function, arguments...].
        lua_pushvalue(state, 2);
        lua_remove(state, 2);
        lua_rotate(state, 1, 1);
        const auto status = lua_pcall(state, arguments, LUA_MULTRET, 0);
        if (!self.can_continue()) return luaL_error(state, "%s", self.abort_message);
        if (status != LUA_OK) {
            // Lua's native error handler can run while a failing count hook
            // is disabled. Invoke it only after pcall restores hook delivery.
            lua_pushvalue(state, 1);
            lua_pushvalue(state, 2);
            (void)lua_pcall(state, 1, 1, 0);
            if (!self.can_continue()) return luaL_error(state, "%s", self.abort_message);
            lua_replace(state, 2);
        }
        lua_pushboolean(state, status == LUA_OK);
        lua_replace(state, 1);
        return lua_gettop(state);
    }
    static auto initialize(lua_State* state) -> int {
        const luaL_Reg libraries[]{{LUA_GNAME, luaopen_base}, {LUA_STRLIBNAME, luaopen_string},
            {LUA_TABLIBNAME, luaopen_table}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}};
        for (const auto& library : libraries) {
            luaL_requiref(state, library.name, library.func, 1);
            lua_pop(state, 1);
        }
        const char* removed[]{"dofile", "loadfile", "print", "warn", "collectgarbage", "setmetatable", "getmetatable"};
        for (const auto* name : removed) {
            lua_pushnil(state);
            lua_setglobal(state, name);
        }
        lua_pushcfunction(state, load_text);
        lua_setglobal(state, "load");
        lua_pushcfunction(state, protected_xpcall);
        lua_setglobal(state, "xpcall");
        lua_getglobal(state, "math");
        lua_pushcfunction(state, random);
        lua_setfield(state, -2, "random");
        lua_pushcfunction(state, randomseed);
        lua_setfield(state, -2, "randomseed");
        lua_pop(state, 1);
        return 0;
    }
    static auto run(lua_State* state) -> int {
        auto& self = context(state);
        self.load_status = luaL_loadbufferx(state, self.source.data(), self.source.size(), "sandbox", "t");
        if (self.load_status != LUA_OK)
            return lua_error(state);
        lua_call(state, 0, 1);
        return 1;
    }
    auto error(int status) const -> error_t<failure_code_t> {
        if (abort_code) return {*abort_code, abort_message};
        const auto code = status == LUA_ERRSYNTAX || load_status == LUA_ERRSYNTAX
            ? failure_code_t::invalid_data : failure_code_t::task_failed;
        std::size_t size{};
        const auto* text = lua_type(state, -1) == LUA_TSTRING ? lua_tolstring(state, -1, &size) : nullptr;
        return {code, text ? std::string(text, std::min(size, std::size_t{4096})) : "Lua evaluation failed."};
    }
    auto result() const -> result_t<lua_sandbox_value_t, error_t<failure_code_t>> {
        switch (lua_type(state, -1)) {
        case LUA_TNIL: return std::monostate{};
        case LUA_TBOOLEAN: return lua_toboolean(state, -1) != 0;
        case LUA_TNUMBER:
            if (lua_isinteger(state, -1)) return static_cast<std::int64_t>(lua_tointeger(state, -1));
            if (std::isfinite(lua_tonumber(state, -1))) return static_cast<double>(lua_tonumber(state, -1));
            return std::unexpected(error_t{failure_code_t::invalid_data, "Lua result must be finite."});
        case LUA_TSTRING: {
            std::size_t size{};
            const auto* value = lua_tolstring(state, -1, &size);
            if (size > limits.maximum_result_bytes) return std::unexpected(error_t{
                failure_code_t::capacity_exceeded, "Lua result exceeds the result byte limit."});
            return std::string(value, size);
        }
        default: return std::unexpected(error_t{failure_code_t::unsupported, "Lua result must be a scalar value."});
        }
    }
};

lua_sandbox_t::lua_sandbox_t(std::unique_ptr<implementation_t> implementation)
    : implementation_(std::move(implementation)) {}
lua_sandbox_t::lua_sandbox_t(lua_sandbox_t&&) noexcept = default;
auto lua_sandbox_t::operator=(lua_sandbox_t&&) noexcept -> lua_sandbox_t& = default;
lua_sandbox_t::~lua_sandbox_t() = default;

auto lua_sandbox_t::from(lua_sandbox_limits_t limits) -> result_t<lua_sandbox_t, error_t<failure_code_t>> {
    if (!limits.maximum_memory_bytes || !limits.maximum_instructions ||
        !limits.maximum_source_bytes || !limits.maximum_result_bytes)
        return std::unexpected(error_t{failure_code_t::invalid_request, "Lua limits must be positive."});
    auto implementation = std::unique_ptr<implementation_t>(new (std::nothrow) implementation_t(std::move(limits)));
    if (!implementation) return std::unexpected(error_t{failure_code_t::out_of_memory, "Cannot allocate Lua session."});
    auto& self = *implementation;
    self.state = lua_newstate(implementation_t::allocate, &self);
    if (!self.state) return std::unexpected(error_t{self.abort_code.value_or(failure_code_t::out_of_memory),
        self.abort_message ? self.abort_message : "Cannot allocate Lua state."});
    *static_cast<implementation_t**>(lua_getextraspace(self.state)) = &self;
    lua_sethook(self.state, implementation_t::instruction_hook, LUA_MASKCOUNT, 1);
    lua_pushcfunction(self.state, implementation_t::initialize);
    const auto status = lua_pcall(self.state, 0, 0, 0);
    if (status != LUA_OK || !self.can_continue()) return std::unexpected(self.error(status));
    return lua_sandbox_t(std::move(implementation));
}
auto lua_sandbox_t::evaluate(std::string_view source) -> result_t<lua_sandbox_value_t, error_t<failure_code_t>> {
    if (!is_open()) return std::unexpected(error_t{failure_code_t::not_ready, "Lua session is closed."});
    auto& self = *implementation_;
    if (source.size() > self.limits.maximum_source_bytes) self.record_failure(
        failure_code_t::capacity_exceeded, "Lua source exceeds the source byte limit.");
    if (!self.can_continue()) {
        const auto failure = self.error(LUA_ERRRUN);
        self.close();
        return std::unexpected(failure);
    }
    self.source = source;
    lua_pushcfunction(self.state, implementation_t::run);
    const auto status = lua_pcall(self.state, 0, 1, 0);
    self.source = {};
    if (status != LUA_OK || !self.can_continue()) {
        const auto failure = self.error(status);
        self.close();
        return std::unexpected(failure);
    }
    auto value = self.result();
    lua_settop(self.state, 0);
    if (!value) self.close();
    return value;
}
auto lua_sandbox_t::is_open() const noexcept -> bool { return implementation_ && implementation_->state; }
auto lua_sandbox_t::usage() const noexcept -> lua_sandbox_usage_t {
    return implementation_ ? implementation_->usage : lua_sandbox_usage_t{};
}
} // namespace wormedit::scripting
