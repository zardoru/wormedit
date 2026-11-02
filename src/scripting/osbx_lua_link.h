#pragma once
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/scripting/lua_sandbox.h>

namespace wormedit::scripting {
class osbx_lua_link_backend_t final : public osbx_lua_backend_t {
  public:
    explicit osbx_lua_link_backend_t(lua_sandbox_limits_t limits = {}) : limits_(limits) {}
    [[nodiscard]] auto prepare_track(const osbx_lua_track_link_t&, std::size_t arity) const
        -> diagnosed_t<std::vector<osbx_key_t<>>> override;
    [[nodiscard]] auto prepare_children(const osbx_lua_link_t&) const
        -> diagnosed_t<osbx_document_t> override;
  private:
    lua_sandbox_limits_t limits_;
};
} // namespace wormedit::scripting
