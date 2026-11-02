#pragma once

#include <wormedit/core/osbx_document.h>
#include <wormedit/core/proposal.h>
#include <memory>
#include <span>

namespace wormedit {
using osbx_link_diagnostics_t = std::vector<osbx_diagnostic_t>;
[[nodiscard]] auto validate_osbx_lua_link(const osbx_lua_link_t&) -> std::string;
[[nodiscard]] auto validate_osbx_lua_track_link(const osbx_lua_track_link_t&) -> std::string;
[[nodiscard]] auto has_osbx_lua_links(const osbx_document_t&) -> bool;

struct osbx_lua_prepared_link_t {
    std::string node_id;
    std::string target; // "op/<ID>", "opacity", "color", or "children".
    osbx_lua_link_t link;
    std::optional<osbx_lua_track_link_t> track_link;
    bool is_last_good{};
    std::vector<osbx_key_t<>> samples;
    osbx_document_t children;
    auto operator==(const osbx_lua_prepared_link_t&) const -> bool = default;
};

// Worker-only interface; frame evaluation never sees a backend or Lua VM.
class osbx_lua_backend_t {
  public:
    virtual ~osbx_lua_backend_t() = default;
    [[nodiscard]] virtual auto prepare_track(const osbx_lua_track_link_t&, std::size_t arity) const
        -> diagnosed_t<std::vector<osbx_key_t<>>> = 0;
    [[nodiscard]] virtual auto prepare_children(const osbx_lua_link_t&) const
        -> diagnosed_t<osbx_document_t> = 0;
};

// Immutable transient snapshot. Durable documents contain no evaluated output.
struct osbx_lua_prepared_scene_t {
    document_stamp_t stamp;
    storyboard_id_t storyboard;
    osbx_document_t source;
    std::shared_ptr<const osbx_document_t> scene;
    std::vector<osbx_lua_prepared_link_t> links;
    osbx_link_diagnostics_t diagnostics;
    bool complete{}; // Every active link has a successful result (possibly last-good).
    [[nodiscard]] static auto from(const osbx_document_t&, document_stamp_t,
        const osbx_lua_backend_t&, const osbx_lua_prepared_scene_t* previous = nullptr)
        -> osbx_lua_prepared_scene_t;
};

// The same explicit prepared scope is used by preview, OSB, SM and offline video
// consumers. Missing preparation fails closed, without evaluating on the caller.
class osbx_lua_prepared_scope_t {
  public:
    explicit osbx_lua_prepared_scope_t(std::span<const osbx_lua_prepared_scene_t>, std::uint64_t generation = 1);
    [[nodiscard]] static auto generation() -> std::uint64_t;
    [[nodiscard]] static auto diagnostic() -> std::string_view;
    [[nodiscard]] static auto failure(const osbx_document_t&) -> osbx_link_diagnostics_t;
    ~osbx_lua_prepared_scope_t();
    osbx_lua_prepared_scope_t(const osbx_lua_prepared_scope_t&) = delete;
    auto operator=(const osbx_lua_prepared_scope_t&) -> osbx_lua_prepared_scope_t& = delete;
    [[nodiscard]] static auto find(const osbx_document_t&) -> std::shared_ptr<const osbx_document_t>;
    [[nodiscard]] static auto prepared(const osbx_document_t&, document_stamp_t, storyboard_id_t,
        std::uint64_t captured_generation) -> const osbx_lua_prepared_scene_t*;
    // Reuse installed outputs for a property-only candidate. No backend, VM,
    // source read or generation is involved; descriptors must match the
    // committed capture exactly. The caller retains the returned owner.
    [[nodiscard]] static auto rebase(const osbx_document_t& committed, const osbx_document_t& candidate,
        document_stamp_t capture, storyboard_id_t) -> result_t<osbx_lua_prepared_scene_t, failure_code_t>;
  private:
    std::span<const osbx_lua_prepared_scene_t> scenes_;
    std::uint64_t generation_{};
    const osbx_lua_prepared_scope_t* previous_{};
};

[[nodiscard]] auto prepare_osbx_lua_scenes(const song_document_t&, const osbx_lua_backend_t&,
    std::span<const osbx_lua_prepared_scene_t> previous = {}) -> std::vector<osbx_lua_prepared_scene_t>;

struct osbx_materialize_link_t {
    storyboard_id_t storyboard;
    std::string node_id;
    std::string target;
    osbx_lua_prepared_scene_t prepared;
    [[nodiscard]] auto apply(document_command_transaction_t&) const -> attempt_t<document_edit_effect_t>;
};
[[nodiscard]] auto apply_osbx_materialize_link(document_history_t&, const osbx_materialize_link_t&)
    -> document_command_result_t;
} // namespace wormedit
