#pragma once
#include "sys/compose/views/compose_view.h"

namespace wormedit::sys {
void prepare_draw_stem_sources(compose_scene_cache_t& cache,
                               const wormedit::song_document_t& document,
                               const wormedit::chart_document_t& chart);
auto draw_stem_source_name(frontend_workspace_view_t& flattened, std::string_view name) -> const char*;
auto bounded_draw_source_label(const wormedit::resource_entry_t& resource) -> std::string;
void populate_draw_source_label(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document);
void populate_bgm_draw_hint(wormedit::ui::compose_view_t& view, const wormedit::chart_document_t& chart);
auto compose_sound_is_stem(const compose_scene_cache_t& cache, std::uint64_t resource) -> bool;
auto compose_sound_missing(const compose_scene_cache_t& cache,
                           const wormedit::song_document_t& document, std::uint64_t resource)
    -> bool;
void populate_note_stem_sources(frontend_workspace_view_t& flattened,
                                const compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document);
void populate_draw_stem_sources(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document,
                                const wormedit::chart_document_t& chart);
void populate_compose_keysound_fallbacks(frontend_workspace_view_t& flattened,
                                         const wormedit::document_history_t& history,
                                         const std::filesystem::path& asset_base);
} // namespace wormedit::sys
