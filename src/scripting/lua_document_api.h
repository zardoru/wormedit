#pragma once

#include <wormedit/core/interaction_types.h>
#include <wormedit/scripting/lua_sandbox.h>
#include <span>
#include <optional>
#include <utility>

namespace wormedit::scripting {
struct lua_document_limits_t {
    std::size_t maximum_nodes{65536};
    std::size_t maximum_bytes{8U*1024U*1024U};
    std::stop_token cancellation;
};

// An owned API-v1 snapshot; installation retains only Lua-owned immutable data.
// IDs are decimal strings. Exact rationals use {num,den}; unsigned values above
// INT64_MAX use decimal strings rather than lossy floating-point conversion.
class lua_document_snapshot_t {
  public:
    [[nodiscard]] static auto from(const song_document_t&, const chart_document_t&,
        std::span<const editor_selection_object_t> selection = {}, lua_document_limits_t limits = {},
        std::optional<std::span<const asset_id_t>> midi_inputs = std::nullopt)
        -> result_t<lua_document_snapshot_t,error_t<failure_code_t>>;
    [[nodiscard]] auto install(lua_sandbox_t&) const -> result_t<void,error_t<failure_code_t>>;
    [[nodiscard]] auto context() const -> const lua_sandbox_context_t& { return context_; }
  private:
    explicit lua_document_snapshot_t(lua_sandbox_context_t context) : context_(std::move(context)) {}
    lua_sandbox_context_t context_;
};
} // namespace wormedit::scripting
