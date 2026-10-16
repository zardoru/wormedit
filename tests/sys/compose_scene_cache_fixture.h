#pragma once

#include "sys/compose_view.h"

namespace wormedit::sys {

// Synthetic bindings let projection tests change only the source facts they exercise.
// A null source retains the current geometry while get_or_build captures the new key.
struct retained_scene_fixture_t {
    const wormedit::editor_field_scene_index_t* source{};

    void operator()(std::optional<wormedit::editor_field_scene_index_t>& retained) const {
        if (source) retained = *source;
    }
};

} // namespace wormedit::sys
