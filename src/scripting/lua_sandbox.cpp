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
#include <set>
#include <cstring>
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
    int function_reference{LUA_NOREF};
    std::string chunk_name;
    std::string_view entry_point;
    const lua_sandbox_context_t* call_context{};
    double argument{};
    bool pure{};
    bool bounded_lookup{};
    std::string_view snapshot_name;
    static inline char context_metatable_key;


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
        initialize_context(state);
        return 0;
    }
    static auto immutable_index(lua_State* state) -> int {
        auto& self = context(state);
        if (!self.consume_instruction()) return luaL_error(state, "%s", self.abort_message);
        lua_getiuservalue(state, 1, 1);
        lua_getiuservalue(state, 1, 2);
        const auto bounded = lua_toboolean(state, -1);
        lua_pop(state, 1);
        if (bounded && lua_type(state, 2) == LUA_TNUMBER) {
            const auto length = lua_rawlen(state, -1);
            if (!lua_isinteger(state, 2) || lua_tointeger(state, 2) < 1 ||
                static_cast<lua_Unsigned>(lua_tointeger(state, 2)) > length + 1)
                return luaL_error(state, "snapshot index exceeds its bounds");
        }
        lua_pushvalue(state, 2);
        lua_rawget(state, -2);
        return 1;
    }
    static auto immutable_write(lua_State* state) -> int {
        return luaL_error(state, "recipe context is immutable");
    }
    static auto immutable_length(lua_State* state) -> int {
        lua_getiuservalue(state, 1, 1);
        lua_pushinteger(state, static_cast<lua_Integer>(lua_rawlen(state, -1)));
        return 1;
    }
    static auto immutable_next(lua_State* state) -> int {
        lua_getiuservalue(state, lua_upvalueindex(1), 1);
        lua_pushvalue(state, 2);
        if (lua_next(state, -2)) return 2;
        return 0;
    }
    static auto immutable_pairs(lua_State* state) -> int {
        lua_pushvalue(state, 1);
        lua_pushcclosure(state, immutable_next, 1);
        lua_pushnil(state);
        lua_pushnil(state);
        return 3;
    }
    static void push_scalar(lua_State* state, const lua_sandbox_value_t& value) {
        switch (value.index()) {
        case 0: lua_pushnil(state); break;
        case 1: lua_pushboolean(state, std::get<bool>(value)); break;
        case 2: lua_pushinteger(state, std::get<std::int64_t>(value)); break;
        case 3: lua_pushnumber(state, std::get<double>(value)); break;
        case 4: lua_pushlstring(state, std::get<std::string>(value).data(), std::get<std::string>(value).size()); break;
        }
    }
    static void push_context(lua_State* state, const lua_sandbox_context_t& nodes, unsigned depth = 0) {
        if (depth > 32) luaL_error(state, "recipe context nesting exceeds limit");
        if (!lua_checkstack(state, 8)) luaL_error(state, "recipe context stack exceeds limit");
        lua_newuserdatauv(state, 0, 2);
        lua_pushboolean(state, context(state).bounded_lookup);
        lua_setiuservalue(state, -2, 2);
        lua_rawgetp(state, LUA_REGISTRYINDEX, &context_metatable_key);
        lua_setmetatable(state, -2);
        lua_createtable(state, 0, static_cast<int>(nodes.size()));
        for (const auto& node : nodes) {
            auto& self = context(state);
            if (!self.consume_instruction()) luaL_error(state, "%s", self.abort_message);
            if (node.key.index() == 0) {
                const auto& key = std::get<std::string>(node.key);
                lua_pushlstring(state, key.data(), key.size());
            } else lua_pushinteger(state, std::get<std::int64_t>(node.key));
            if (node.value.index() == 0) push_scalar(state, std::get<lua_sandbox_value_t>(node.value));
            else push_context(state, std::get<lua_sandbox_context_t>(node.value), depth + 1);
            lua_rawset(state, -3);
        }
        lua_setiuservalue(state, -2, 1);
    }
    static void initialize_context(lua_State* state) {
        lua_createtable(state, 0, 5);
        lua_pushcfunction(state, immutable_index); lua_setfield(state, -2, "__index");
        lua_pushcfunction(state, immutable_write); lua_setfield(state, -2, "__newindex");
        lua_pushcfunction(state, immutable_length); lua_setfield(state, -2, "__len");
        lua_pushcfunction(state, immutable_pairs); lua_setfield(state, -2, "__pairs");
        lua_pushboolean(state, false); lua_setfield(state, -2, "__metatable");
        lua_rawsetp(state, LUA_REGISTRYINDEX, &context_metatable_key);
    }
    static auto install_named_snapshot(lua_State* state) -> int {
        auto& self = context(state);
        lua_pushglobaltable(state);
        lua_pushlstring(state, self.snapshot_name.data(), self.snapshot_name.size());
        push_context(state, *self.call_context);
        lua_rawset(state, -3);
        return 0;
    }
    static auto install(lua_State* state) -> int {
        auto& self = context(state);
        self.load_status = luaL_loadbufferx(state, self.source.data(), self.source.size(), self.chunk_name.c_str(), "t");
        if (self.load_status != LUA_OK) return lua_error(state);
        lua_call(state, 0, 1);
        if (!self.entry_point.empty()) {
            luaL_checktype(state, -1, LUA_TTABLE);
            lua_pushlstring(state, self.entry_point.data(), self.entry_point.size());
            lua_rawget(state, -2);
            luaL_checktype(state, -1, LUA_TFUNCTION);
            push_context(state, *self.call_context);
            lua_call(state, 1, 1);
        }
        luaL_checktype(state, -1, LUA_TFUNCTION);
        self.function_reference = luaL_ref(state, LUA_REGISTRYINDEX);
        return 0;
    }
    static auto call(lua_State* state) -> int {
        auto& self = context(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, self.function_reference);
        lua_pushnumber(state, self.argument);
        push_context(state, *self.call_context);
        lua_call(state, 2, 1);
        return 1;
    }
    static auto run(lua_State* state) -> int {
        auto& self = context(state);
        self.load_status = luaL_loadbufferx(state, self.source.data(), self.source.size(), self.chunk_name.empty() ? "sandbox" : self.chunk_name.c_str(), "t");
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
    auto invoke(lua_CFunction function, int results) -> result_t<void, error_t<failure_code_t>> {
        if (!can_continue()) return std::unexpected(error(LUA_ERRRUN));
        lua_pushcfunction(state, function);
        const auto status = lua_pcall(state, 0, results, 0);
        if (status != LUA_OK || !can_continue()) return std::unexpected(error(status));
        return {};
    }
    struct output_reader_t {
        implementation_t& owner;
        std::set<const void*> active;
        std::size_t bytes{};
        auto reserve(std::size_t count) -> bool {
            if (!owner.consume_instruction()) return false;
            if (count > owner.limits.maximum_result_bytes - bytes) {
                owner.record_failure(failure_code_t::capacity_exceeded, "Lua result tree exceeds its byte limit.");
                return false;
            }
            bytes += count;
            return true;
        }
        auto key(int index) -> result_t<std::variant<std::string,std::int64_t>,error_t<failure_code_t>> {
            if (lua_isinteger(owner.state,index)) return static_cast<std::int64_t>(lua_tointeger(owner.state,index));
            if (lua_type(owner.state,index) != LUA_TSTRING) return std::unexpected(error_t{
                failure_code_t::invalid_data,"Lua result table keys must be strings or integers."});
            std::size_t length{};
            const auto* text=lua_tolstring(owner.state,index,&length);
            if (!reserve(length)) return std::unexpected(owner.error(LUA_ERRRUN));
            return std::string(text,length);
        }
        auto value(unsigned depth) -> result_t<decltype(lua_sandbox_node_t::value),error_t<failure_code_t>> {
            if (lua_istable(owner.state,-1)) {
                auto children=table(-1,depth+1);
                if (!children) return std::unexpected(children.error());
                return std::move(*children);
            }
            if (lua_type(owner.state,-1)==LUA_TSTRING) {
                std::size_t length{};
                (void)lua_tolstring(owner.state,-1,&length);
                if (!reserve(length)) return std::unexpected(owner.error(LUA_ERRRUN));
            }
            auto scalar=owner.result();
            if (!scalar) return std::unexpected(scalar.error());
            return std::move(*scalar);
        }
        auto table(int index,unsigned depth=0) -> result_t<lua_sandbox_context_t,error_t<failure_code_t>> {
            if (!lua_istable(owner.state,index)) return std::unexpected(error_t{
                failure_code_t::invalid_data,"Lua structured result must be a table."});
            const auto* identity=lua_topointer(owner.state,index);
            if (depth>32 || active.contains(identity)) return std::unexpected(error_t{
                failure_code_t::invalid_data,"Lua result tree is cyclic or exceeds its nesting limit."});
            active.insert(identity);
            index=lua_absindex(owner.state,index);
            lua_sandbox_context_t result;
            lua_pushnil(owner.state);
            while (lua_next(owner.state,index)) {
                if (!reserve(sizeof(lua_sandbox_node_t))) { lua_pop(owner.state,2); return std::unexpected(owner.error(LUA_ERRRUN)); }
                auto entry_key=key(-2);
                auto entry_value=value(depth);
                if (!entry_key || !entry_value) {
                    lua_pop(owner.state,2);
                    return std::unexpected(!entry_key ? entry_key.error() : entry_value.error());
                }
                result.push_back({.key=std::move(*entry_key),.value=std::move(*entry_value)});
                lua_pop(owner.state,1);
            }
            active.erase(identity);
            std::ranges::sort(result,{},&lua_sandbox_node_t::key);
            return result;
        }
    };
    // Raw inspection only: no metamethods or Lua callbacks. Reserve stack space
    // first; all subsequent API operations merely push existing Lua values.
    struct snapshot_t {
        implementation_t& owner;
        std::set<const void*> visited;
        std::vector<std::string> objects;
        std::size_t bytes{};
        bool valid{true};
        auto atom(int index) -> std::string {
            const auto type = lua_type(owner.state, index);
            std::string value(1, static_cast<char>(type));
            if (type == LUA_TSTRING) {
                std::size_t length{};
                const auto* text = lua_tolstring(owner.state, index, &length);
                bytes += length;
                if (bytes > owner.limits.maximum_memory_bytes) valid = false;
                else value.append(text, length);
            } else if (type == LUA_TBOOLEAN) value += static_cast<char>(lua_toboolean(owner.state, index));
            else if (type == LUA_TNUMBER) {
                const auto integer = lua_isinteger(owner.state, index);
                value += static_cast<char>(integer);
                const auto bits = integer ? static_cast<std::uint64_t>(lua_tointeger(owner.state, index))
                    : std::bit_cast<std::uint64_t>(static_cast<double>(lua_tonumber(owner.state, index)));
                value.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
            } else if (type != LUA_TNIL) {
                const auto pointer = reinterpret_cast<std::uintptr_t>(lua_topointer(owner.state, index));
                value.append(reinterpret_cast<const char*>(&pointer), sizeof(pointer));
            }
            return value;
        }
        void table(int index, unsigned depth, std::string& record) {
            std::vector<std::string> entries;
            lua_pushnil(owner.state);
            while (lua_next(owner.state, index)) {
                if (!owner.consume_instruction()) { valid = false; lua_pop(owner.state, 2); break; }
                auto key = atom(-2);
                auto value = atom(-1);
                visit(-2, depth + 1);
                visit(-1, depth + 1);
                if (!valid || key.size() + value.size() > owner.limits.maximum_memory_bytes - std::min(bytes, owner.limits.maximum_memory_bytes)) {
                    valid = false; lua_pop(owner.state, 2); break;
                }
                bytes += key.size() + value.size();
                key = std::to_string(key.size()) + ':' + key + value;
                entries.push_back(std::move(key));
                lua_pop(owner.state, 1);
                if (!valid) { lua_pop(owner.state, 1); break; }
            }
            std::ranges::sort(entries);
            for (const auto& entry : entries) {
                if (entry.size() + record.size() + 32 > owner.limits.maximum_memory_bytes) { valid = false; break; }
                record += std::to_string(entry.size()); record += ':'; record += entry;
            }
        }
        void function(int index, unsigned depth, std::string& record) {
            for (int upvalue = 1; lua_getupvalue(owner.state, index, upvalue); ++upvalue) {
                if (!owner.consume_instruction()) { valid = false; lua_pop(owner.state, 1); break; }
                const auto value = atom(-1);
                if (!valid || value.size() + record.size() + 32 > owner.limits.maximum_memory_bytes) {
                    valid = false; lua_pop(owner.state, 1); break;
                }
                record += std::to_string(value.size()); record += ':'; record += value;
                visit(-1, depth + 1);
                lua_pop(owner.state, 1);
                if (!valid) break;
            }
        }
        void userdata(int index, unsigned depth, std::string& record) {
            if (!lua_getmetatable(owner.state, index)) { valid = false; return; }
            lua_rawgetp(owner.state, LUA_REGISTRYINDEX, &context_metatable_key);
            const auto immutable = lua_rawequal(owner.state, -1, -2);
            lua_pop(owner.state, 2);
            if (!immutable) { valid = false; return; }
            lua_getiuservalue(owner.state, index, 1);
            record += atom(-1);
            visit(-1, depth + 1);
            lua_pop(owner.state, 1);
        }
        void visit(int index, unsigned depth = 0) {
            const auto type = lua_type(owner.state, index);
            if (type == LUA_TLIGHTUSERDATA || type == LUA_TTHREAD) { valid = false; return; }
            if (type != LUA_TTABLE && type != LUA_TFUNCTION && type != LUA_TUSERDATA) return;
            const auto pointer = lua_topointer(owner.state, index);
            if (visited.contains(pointer)) return;
            if (depth > 64 || visited.size() >= 65536 || !owner.consume_instruction()) { valid = false; return; }
            visited.insert(pointer);
            index = lua_absindex(owner.state, index);
            auto record = atom(index);
            if (type == LUA_TTABLE) table(index, depth, record);
            else if (type == LUA_TFUNCTION) function(index, depth, record);
            else userdata(index, depth, record);
            bytes += record.size();
            if (bytes > owner.limits.maximum_memory_bytes) valid = false;
            objects.push_back(std::move(record));
        }
    };
    auto snapshot() -> result_t<std::vector<std::string>, error_t<failure_code_t>> {
        if (!lua_checkstack(state, 512)) return std::unexpected(error_t{
            failure_code_t::capacity_exceeded, "Cannot reserve pure Lua inspection stack."});
        snapshot_t snapshot{.owner = *this};
        lua_rawgeti(state, LUA_REGISTRYINDEX, function_reference);
        snapshot.visit(-1);
        lua_pop(state, 1);
        lua_pushglobaltable(state);
        snapshot.visit(-1);
        lua_pop(state, 1);
        if (abort_code) return std::unexpected(error(LUA_ERRRUN));
        if (!snapshot.valid) return std::unexpected(error_t{failure_code_t::invalid_data,
            "Pure Lua closure contains unsupported or excessive retained state."});
        std::ranges::sort(snapshot.objects);
        return std::move(snapshot.objects);
    }

};

auto lua_sandbox_node_t::from(std::string key,lua_sandbox_value_t value) -> lua_sandbox_node_t {
    return {.key=std::move(key),.value=std::move(value)};
}
auto lua_sandbox_node_t::from(std::string key,std::vector<lua_sandbox_node_t> children) -> lua_sandbox_node_t {
    return {.key=std::move(key),.value=std::move(children)};
}
auto lua_sandbox_node_t::from_unsigned(std::string key,std::uint64_t value) -> lua_sandbox_node_t {
    if (value>static_cast<std::uint64_t>(INT64_MAX)) return from(std::move(key),std::to_string(value));
    return from(std::move(key),static_cast<std::int64_t>(value));
}

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
auto lua_sandbox_t::install_snapshot(std::string_view name,const lua_sandbox_context_t& context,bool bounded_lookup)
    -> result_t<void,error_t<failure_code_t>> {
    if (!is_open()) return std::unexpected(error_t{failure_code_t::not_ready,"Lua session is closed."});
    auto& self=*implementation_;
    if (name.empty() || name.size()>self.limits.maximum_source_bytes) {
        self.close();
        return std::unexpected(error_t{failure_code_t::invalid_request,"Snapshot name is empty or too large."});
    }
    self.snapshot_name=name; self.call_context=&context; self.bounded_lookup=bounded_lookup;
    auto result=self.invoke(implementation_t::install_named_snapshot,0);
    self.snapshot_name={}; self.call_context=nullptr; self.bounded_lookup=false;
    lua_settop(self.state,0);
    if (!result) self.close();
    return result;
}
auto lua_sandbox_t::evaluate_context(std::string_view source,std::string_view chunk_name)
    -> result_t<lua_sandbox_context_t,error_t<failure_code_t>> {
    if (!is_open()) return std::unexpected(error_t{failure_code_t::not_ready,"Lua session is closed."});
    auto& self=*implementation_;
    if (source.size()>self.limits.maximum_source_bytes) self.record_failure(
        failure_code_t::capacity_exceeded,"Lua source exceeds the source byte limit.");
    self.source=source; self.chunk_name=chunk_name;
    const auto invoked=self.invoke(implementation_t::run,1);
    self.source={}; self.chunk_name.clear();
    if (!invoked) { self.close(); return std::unexpected(invoked.error()); }
    if (!lua_checkstack(self.state,512)) {
        self.close(); return std::unexpected(error_t{failure_code_t::capacity_exceeded,"Cannot reserve Lua result inspection stack."});
    }
    implementation_t::output_reader_t reader{.owner=self};
    auto result=reader.table(-1);
    lua_settop(self.state,0);
    if (!result) self.close();
    return result;
}
auto lua_sandbox_t::install_function(std::string_view source, const lua_sandbox_context_t& context,
    std::string_view entry_point, std::string_view chunk_name, bool pure)
    -> result_t<void, error_t<failure_code_t>> {
    if (!is_open()) return std::unexpected(error_t{failure_code_t::not_ready, "Lua session is closed."});
    auto& self = *implementation_;
    if (self.function_reference != LUA_NOREF) return std::unexpected(error_t{
        failure_code_t::invalid_state, "Lua function is already installed."});
    if (source.size() > self.limits.maximum_source_bytes) self.record_failure(
        failure_code_t::capacity_exceeded, "Lua source exceeds the source byte limit.");
    self.source = source;
    self.chunk_name = chunk_name;
    self.entry_point = entry_point;
    self.call_context = &context;
    self.pure = pure;
    auto result = self.invoke(implementation_t::install, 0);
    self.source = {}; self.entry_point = {}; self.call_context = nullptr; self.chunk_name.clear();
    lua_settop(self.state, 0);
    if (!result) self.close();
    return result;
}
auto lua_sandbox_t::call_function(double p, const lua_sandbox_context_t& context)
    -> result_t<lua_sandbox_value_t, error_t<failure_code_t>> {
    if (!is_open()) return std::unexpected(error_t{failure_code_t::not_ready, "Lua session is closed."});
    auto& self = *implementation_;
    if (self.function_reference == LUA_NOREF) return std::unexpected(error_t{
        failure_code_t::not_ready, "Lua function is not installed."});
    if (!std::isfinite(p)) {
        self.close();
        return std::unexpected(error_t{failure_code_t::invalid_request, "Lua argument must be finite."});
    }
    const auto random_before = self.random_state;
    auto before = self.pure ? self.snapshot() : result_t<std::vector<std::string>, error_t<failure_code_t>>{};
    if (!before) { self.close(); return std::unexpected(before.error()); }
    self.argument = p; self.call_context = &context;
    const auto invoked = self.invoke(implementation_t::call, 1);
    self.call_context = nullptr;
    if (!invoked) { self.close(); return std::unexpected(invoked.error()); }
    auto value = self.result();
    lua_settop(self.state, 0);
    if (value && self.pure) {
        const auto after = self.snapshot();
        if (!after) value = std::unexpected(after.error());
        else if (*before != *after || random_before != self.random_state) value = std::unexpected(error_t{
            failure_code_t::invalid_data, "Pure Lua curve mutated retained state; use ordered mode."});
    }
    if (!value) self.close();
    return value;
}
auto lua_sandbox_t::is_open() const noexcept -> bool { return implementation_ && implementation_->state; }
auto lua_sandbox_t::usage() const noexcept -> lua_sandbox_usage_t {
    return implementation_ ? implementation_->usage : lua_sandbox_usage_t{};
}
} // namespace wormedit::scripting
