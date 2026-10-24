#pragma once

#include <wormedit/core/sv_recipe.h>
#include <wormedit/scripting/lua_sandbox.h>

namespace wormedit::scripting {
// Evaluates frozen recipe bytes only. Every start creates an isolated session;
// no document references or filesystem capabilities enter the Lua runtime.
class lua_curve_backend_t final : public sv_recipe_script_backend_t {
  public:
    explicit lua_curve_backend_t(lua_sandbox_limits_t limits = {});
    [[nodiscard]] auto start(const chart_document_t&, const sv_recipe_t&) const
        -> diagnosed_t<std::unique_ptr<sv_recipe_script_session_t>> override;
  private:
    lua_sandbox_limits_t limits_;
};
} // namespace wormedit::scripting
