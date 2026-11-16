#pragma once
#include "sound_import.h"
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/* Frame requests of the sound tabs and keysound dock. Each is a plain payload held by one
   workspace_action_t member, so a dispatcher can take just its own request. */
namespace wormedit::ui {
struct sound_import_item_t {
    std::string path;
    std::string name;
    sound_kind_t kind{sound_kind_t::unspecified};
    bool kind_fixed{};
    auto operator==(const sound_import_item_t&) const -> bool = default;
};
struct sound_import_group_t {
    std::string name;
    sound_group_id_t existing_group_id{};
    std::vector<std::size_t> member_indices;
    auto operator==(const sound_import_group_t&) const -> bool = default;
};
/* Activation of one sound-usage row: jump to the object or resource it names. */
struct sound_usage_activation_t {
    wormedit::song_id_t song_id;
    uint64_t document_revision;
    wormedit::resource_id_t resource_id;
    wormedit::asset_id_t asset_id;
    wormedit::chart_id_t chart_id;
    wormedit::measure_id_t measure_id;
    uint64_t row;
    wormedit::object_id_t object_id;
    wormedit::object_id_t channel_id;
    wormedit::clip_id_t clip_id;
    wormedit::resource_id_t referenced_resource_id;
    wormedit::asset_id_t referenced_asset_id;
    wormedit::sound_usage_kind_t kind;
    wormedit::sound_usage_scope_t scope;
    bool position_valid;
};
/* Apply of the sound properties editor (name, kind, tags). */
struct sound_properties_request_t {
    bool apply;
    wormedit::song_id_t project_id{};
    wormedit::chart_id_t chart_id{};
    wormedit::resource_id_t resource_id{};
    uint64_t document_revision;
    std::string name;
    wormedit::sound_kind_t kind{wormedit::sound_kind_t::unspecified};
    // A tag-only edit; name and authored kind remain unchanged when present.
    std::optional<std::vector<std::string>> tags;
};
/* Sound import: file picker request and the reviewed import list. */
struct sound_import_request_t {
    bool pick_requested;
    wormedit::ui::sound_import_kind_t kind{};
    wormedit::song_id_t project_id{};
    std::vector<wormedit::ui::sound_import_item_t> items;
    std::vector<wormedit::ui::sound_import_group_t> groups;
};
} // namespace wormedit::ui
