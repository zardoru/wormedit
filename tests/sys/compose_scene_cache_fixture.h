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

inline void seed_stem_geometry_fixture(stem_index_t& cache, chart_stamp_t stamp,
                                       std::vector<audio::decoded_source_binding_t> bindings) {
    auto fingerprint = cache.key() ? cache.key()->timing_fingerprint : 0;
    static_cast<void>(cache.get_or_build(stem_index_query_t{stamp, bindings, nullptr, fingerprint},
        [](stem_index_data_t& retained) { retained.is_built = true; }));
}

} // namespace wormedit::sys
