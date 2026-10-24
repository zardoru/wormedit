// Explicit preparation only: the normal native reader keeps the legacy owner
// until T-744. The serialized legacy chart is the complete equivalence witness,
// including retained inactive source, native annotations and editor-only fields.
namespace {
auto migration_source(const song_document_t& song, const chart_document_t& chart)
    -> const encoding_provenance_t* {
    for (const auto& group : chart.metadata.format_groups)
        if (group.family == format_family_t::bms && group.encoding.original_bytes) return &group.encoding;
    for (const auto& group : song.metadata.format_groups)
        if (group.family == format_family_t::bms && group.encoding.original_bytes) return &group.encoding;
    return nullptr;
}
void migration_choices(const bms_control_node_t& node, const chart_document_t& chart,
    bms_parse_options_t& options) {
    const auto selected = std::ranges::find(chart.active_branch_selection, node.id, &branch_selection_t::branch);
    if (selected != chart.active_branch_selection.end())
        options.branch_choices.push_back({.control_source_order = node.source_order,
            .selected_value = selected->selected_value});
    for (const auto& child : node.children) migration_choices(child, chart, options);
}

auto remapped_bms_owner(document_history_t& history, chart_id_t chart,
    bms_prepared_program_t prepared, bms_program_evaluation_t evaluation)
    -> std::shared_ptr<const bms_program_document_t> {
    const auto first = document_command_transaction_t(history).next_bms_program_item_id();
    if (!first) throw std::invalid_argument("Migration identity space is exhausted.");
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    auto next = first->value;
    auto reserve = [&](object_id_t id) {
        if (next == std::numeric_limits<std::uint64_t>::max()) return false;
        return ids.emplace(id.value, next++).second;
    };
    if (!id_declarations_t<object_id_t>::visit(prepared, reserve))
        throw std::invalid_argument("Migration identity space is exhausted or ambiguous.");
    auto remapped = prepared.with_remapped_ids(ids);
    if (!remapped) throw std::invalid_argument(remapped.error().message);
    for (auto& selection : evaluation.selections)
        selection.branch = branch_id_t{ids.at(selection.branch.value)};
    auto owner = bms_program_document_t::from(chart, std::move(*remapped), std::move(evaluation));
    if (!owner.value || !owner.ok()) throw std::invalid_argument("Legacy scoped preparation cannot be evaluated.");
    return std::move(*owner.value);
}

template<class Asset>
void retain_migration_assets(std::vector<Asset>& pool, std::vector<resource_entry_t>& resources,
    const std::vector<Asset>& baseline, const std::vector<Asset>& original) {
    for (auto& asset : pool) {
        const auto found = std::ranges::find(baseline, asset.source_path, &Asset::source_path);
        if (found == baseline.end()) continue; // Inactive assets are retained by scoped import.
        if (std::ranges::count(baseline, asset.source_path, &Asset::source_path) != 1)
            throw std::invalid_argument("Legacy shared asset mapping is ambiguous.");
        const auto shared = std::ranges::find(original, found->id, &Asset::id);
        if (shared == original.end()) throw std::invalid_argument("Legacy shared asset is missing.");
        const auto previous = asset.id;
        asset = *shared;
        for (auto& resource : resources) if (resource.asset == previous) resource.asset = asset.id;
    }
    std::vector<Asset> unique;
    for (auto& asset : pool) {
        const auto found = std::ranges::find(unique, asset.id, &Asset::id);
        if (found == unique.end()) unique.push_back(std::move(asset));
        else if (json(*found) != json(asset)) throw std::invalid_argument("Conflicting shared asset metadata.");
    }
    pool = std::move(unique);
}
auto retain_migration_catalog(const bms_program_document_t& owner,
    const song_document_t& baseline, const song_document_t& original)
    -> std::shared_ptr<const bms_program_document_t> {
    // Resource edits need an explicit semantic adapter. Do not use an old source
    // slot as a guessed replacement for a changed native catalog entry.
    for (const auto& resource : baseline.resources) {
        const auto found = std::ranges::find(original.resources, resource.id, &resource_entry_t::id);
        if (found == original.resources.end() || json(*found) != json(resource))
            throw std::invalid_argument("Legacy resource catalog differs from the source witness.");
    }
    auto prepared = owner.prepared();
    retain_migration_assets(prepared.audio_assets, prepared.resources, baseline.audio_assets, original.audio_assets);
    retain_migration_assets(prepared.visual_assets, prepared.resources, baseline.visual_assets, original.visual_assets);
    auto result = bms_program_document_t::from(owner.difficulty(), std::move(prepared), owner.evaluation());
    if (!result.value || !result.ok()) throw std::invalid_argument("Shared asset migration cannot be evaluated.");
    return std::move(*result.value);
}

auto migration_chart_witness(chart_document_t chart) -> json {
    chart.semantic_layers.clear();
    chart.sound_layer_defaults.clear();
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            note.label.reset();
            note.osu_hitsample.reset();
            note.semantic_layers.clear();
        }
        for (auto& event : measure.bgm_events) event.semantic_layers.clear();
    }
    return chart;
}
struct migration_annotations_t {
    const chart_document_t& original;
    const song_document_t& baseline;
    bms_prepared_program_t& prepared;
    const bms_program_projection_t& projection;
    std::unordered_map<std::uint64_t, layer_id_t> layers;

    auto remap_layers(const std::vector<layer_id_t>& values) const -> std::vector<layer_id_t> {
        std::vector<layer_id_t> result;
        for (const auto value : values) result.push_back(layers.at(value.value));
        return result;
    }
    auto mapped_position(local_position_t position) const -> bms_program_position_t {
        const auto found = std::ranges::find(original.measures, position.measure, &measure_chunk_t::id);
        auto exact = bms_program_position_t::from(position, original);
        if (!exact || found == original.measures.end()) throw std::invalid_argument("Annotation has no exact scoped position.");
        exact->measure = prepared.measures.at(static_cast<std::size_t>(found - original.measures.begin()));
        return *exact;
    }
    static auto matches_item(const bms_content_item_t& item, bms_program_position_t exact,
        lane_id_t lane, bool is_bgm) -> bool {
        return std::visit([&](const auto& value) {
            using type = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<type, bms_bgm_content_t>) return is_bgm && value.position == exact;
            else if constexpr (std::same_as<type, bms_note_content_t> || std::same_as<type, bms_hold_endpoint_t>)
                return !is_bgm && value.position == exact && value.lane == lane;
            else return false;
        }, item.value);
    }
    auto item_for(local_position_t position, lane_id_t lane, bool is_bgm,
        std::vector<bms_program_node_t>& nodes) const -> bms_content_item_t& {
        const auto exact = mapped_position(position);
        bms_content_item_t* result{};
        for (auto& node : nodes) {
            const auto active = std::ranges::find(projection.active_charts(), node.id);
            if (active == projection.active_charts().end()) continue;
            auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            for (auto& item : chart->items) {
                const bool matches = matches_item(item, exact, lane, is_bgm);
                if (!matches) continue;
                if (result) throw std::invalid_argument("Annotation owner is ambiguous.");
                result = &item;
            }
        }
        if (!result) throw std::invalid_argument("Annotation has no scoped semantic owner.");
        return *result;
    }
    void copy_memberships(std::vector<bms_program_node_t>& nodes) const {
        for (const auto& measure : original.measures) {
            for (const auto& note : measure.notes) {
                if (!note.label && !note.osu_hitsample && note.semantic_layers.empty()) continue;
                auto attributes = bms_note_attributes_t::from(note);
                attributes.semantic_layers = remap_layers(attributes.semantic_layers);
                std::visit([&](auto& value) {
                    if constexpr (requires { value.attributes; }) value.attributes = attributes;
                }, item_for(note.position, note.lane, false, nodes).value);
            }
            for (const auto& event : measure.bgm_events) {
                if (event.semantic_layers.empty()) continue;
                std::get<bms_bgm_content_t>(item_for(event.position, {}, true, nodes).value).semantic_layers = remap_layers(event.semantic_layers);
            }
        }
    }
    void copy_defaults() {
        for (const auto& defaults : original.sound_layer_defaults) {
            const auto source = std::ranges::find(baseline.resources, defaults.resource, &resource_entry_t::id);
            if (source == baseline.resources.end()) throw std::invalid_argument("Layer default resource has no source witness.");
            const auto target = std::ranges::find_if(projection.song().resources, [&](const auto& resource) {
                return resource.target_identities == source->target_identities;
            });
            if (target == projection.song().resources.end()) throw std::invalid_argument("Layer default has no scoped catalog owner.");
            prepared.sound_layer_defaults.push_back({.resource = target->id, .layers = remap_layers(defaults.layers)});
        }
    }
};
void retain_migration_annotations(bms_prepared_program_t& prepared, const chart_document_t& original,
    const song_document_t& baseline, const bms_program_evaluation_t& evaluation) {
    const auto projection = bms_program_projection_t::from(prepared, evaluation);
    if (!projection.value || !projection.ok()) throw std::invalid_argument("Annotation projection failed.");
    migration_annotations_t copy{.original = original, .baseline = baseline, .prepared = prepared, .projection = *projection.value};
    auto next = id_ceiling_t<object_id_t>::from(prepared).next();
    if (!next) throw std::invalid_argument("Annotation identity space is exhausted.");
    for (auto layer : original.semantic_layers) {
        if (next->value == std::numeric_limits<std::uint64_t>::max()) throw std::invalid_argument("Layer identity space is exhausted.");
        const auto id = layer.id;
        layer.id = layer_id_t{next->value++};
        if (!copy.layers.emplace(id.value, layer.id).second) throw std::invalid_argument("Layer identities are ambiguous.");
        prepared.semantic_layers.push_back(std::move(layer));
    }
    auto nodes = std::vector<bms_program_node_t>(prepared.program.nodes().begin(), prepared.program.nodes().end());
    copy.copy_memberships(nodes);
    copy.copy_defaults();
    auto program = bms_program_t::from(std::move(nodes));
    if (!program) throw std::invalid_argument(program.error().message);
    prepared.program = std::move(*program);
}

auto prepared_choices(const chart_document_t& original, const bms_program_t& program)
    -> bms_program_evaluation_t {
    // Import retains control-node IDs. Synthetic Charts and typed items are new.
    bms_program_evaluation_t evaluation;
    for (const auto& selected : original.active_branch_selection) {
        if (!program.find(selected.branch))
            throw std::invalid_argument("Legacy preview selection has no scoped selector.");
        evaluation.selections.push_back(selected);
    }
    return evaluation;
}
} // namespace

auto prepare_native_bms_migration(document_history_t& history, chart_id_t id,
    native_project_limits_t limits) -> native_project_parse_outcome_t {
    native_project_parse_outcome_t result;
    try {
        const auto* chart = history.document.find_chart(id);
        if (!chart || chart->source_family != format_family_t::bms || !chart->bms_control_tree)
            throw std::invalid_argument("Migration requires one retained legacy BMS difficulty.");
        if (!is_valid_control_node(*chart->bms_control_tree, limits.maximum_nesting_depth))
            throw std::invalid_argument("Legacy control graph exceeds migration limits.");
        const auto* source = migration_source(history.document, *chart);
        if (!source) throw std::invalid_argument("Legacy BMS source backing is unavailable; migration cannot prove equivalence.");
        bms_parse_options_t options{.encoding_override = source->user_override,
            .max_source_bytes = limits.maximum_source_bytes,
            .max_nesting_depth = limits.maximum_nesting_depth};
        if (!chart->lane_layouts.empty()) options.lane_profile_id = chart->lane_layouts.front().layout.profile_id;
        migration_choices(*chart->bms_control_tree, *chart, options);
        const auto baseline = parse_bms_bytes(*source->original_bytes, options);
        if (!baseline.ok() || !baseline.value.song || baseline.value.song->charts.size() != 1 ||
            migration_chart_witness(baseline.value.song->charts.front()) != migration_chart_witness(*chart))
            throw std::invalid_argument("Legacy chart differs from its complete source witness; migration was refused without discarding native data.");
        auto imported = prepare_bms_program(*source->original_bytes, options);
        if (!imported.ok() || !imported.value.prepared)
            throw std::invalid_argument("Retained active/inactive BMS material cannot be mapped losslessly; legacy recovery data is unchanged.");
        const auto evaluation = prepared_choices(*chart, imported.value.prepared->program);
        retain_migration_annotations(*imported.value.prepared, *chart, *baseline.value.song, evaluation);
        auto owner = remapped_bms_owner(history, id, std::move(*imported.value.prepared), evaluation);
        owner = retain_migration_catalog(*owner, *baseline.value.song, history.document);
        auto proposal = history.document;
        std::erase_if(proposal.charts, [id](const auto& entry) { return entry.id == id; });
        proposal.prepared_bms_programs.push_back(std::move(owner));
        proposal.schema_version = current_document_schema_version;
        // Use the real native boundary for the proposed graph and its budgets.
        const auto written = write_native_project(proposal, limits);
        if (!written.value || !written.ok()) {
            result.diagnostics = written.diagnostics;
            return result;
        }
        result.value = std::move(proposal);
    } catch (const std::exception& exception) {
        result.diagnostics.push_back({diagnostic_severity_t::error,
            native_project_diagnostic_code_t::invalid_document, exception.what(), "$.document.charts"});
    }
    return result;
}
