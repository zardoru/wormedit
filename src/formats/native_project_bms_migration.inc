// The public native reader and legacy export boundary share this one-way
// migration. The serialized legacy chart is the complete equivalence witness,
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

auto remapped_bms_owner(object_id_t first, chart_id_t chart,
    bms_prepared_program_t prepared, bms_program_evaluation_t evaluation)
    -> std::shared_ptr<const bms_program_document_t> {
    if (!first) throw std::invalid_argument("Migration identity space is exhausted.");
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    auto next = first.value;
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
    const std::vector<Asset>& original) {
    for (auto& asset : pool) {
        const auto shared = std::ranges::find(original, asset.source_path, &Asset::source_path);
        if (shared == original.end()) continue; // Inactive source-only assets keep their fresh identities.
        if (std::ranges::count(original, asset.source_path, &Asset::source_path) != 1)
            throw std::invalid_argument("Legacy shared asset mapping is ambiguous.");
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
    (void)baseline; // The one-way encoder has already proved edited source ownership.
    auto prepared = owner.prepared();
    retain_migration_assets(prepared.audio_assets, prepared.resources, original.audio_assets);
    retain_migration_assets(prepared.visual_assets, prepared.resources, original.visual_assets);
    for (auto& resource : prepared.resources) {
        const auto shared = std::ranges::find_if(original.resources, [&](const auto& candidate) {
            return candidate.asset == resource.asset && candidate.target_identities == resource.target_identities;
        });
        if (shared == original.resources.end()) continue;
        const auto id = resource.id;
        resource = *shared;
        resource.id = id;
    }
    auto result = bms_program_document_t::from(owner.difficulty(), std::move(prepared), owner.evaluation());
    if (!result.value || !result.ok()) throw std::invalid_argument("Shared asset migration cannot be evaluated.");
    return std::move(*result.value);
}

auto is_native_migration_pause(const pause_event_t& event) -> bool {
    return event.kind != pause_kind_t::stop || event.source_unit != pause_unit_t::bms_stop_ticks;
}
void clear_native_migration_content(chart_document_t& chart) {
    chart.warps.clear(); chart.speeds.clear(); chart.kiai.clear(); chart.timing_anchors.clear();
    chart.sv_recipes.clear(); chart.bookmarks.clear(); chart.sections.clear(); chart.sound_channels.clear();
    std::erase_if(chart.pauses, is_native_migration_pause);
    chart.timing_links = {};
}
void clear_migration_annotations(chart_document_t& chart) {
    chart.semantic_layers.clear();
    chart.sound_layer_defaults.clear();
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            note.label.reset(); note.osu_hitsample.reset(); note.semantic_layers.clear();
        }
        for (auto& event : measure.bgm_events) event.semantic_layers.clear();
    }
}
auto migration_chart_witness(chart_document_t chart) -> json {
    chart.metadata.name.clear(); chart.metadata.author.clear();
    chart.metadata.rating.reset(); chart.metadata.overrides = {};
    clear_migration_annotations(chart);
    clear_native_migration_content(chart);
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
    void copy_bgm(const bgm_event_t& event, std::vector<bms_program_node_t>& nodes) const {
        if (event.semantic_layers.empty() && !event.end && !event.sound_channel_event) return;
        auto& item = item_for(event.position, {}, true, nodes);
        auto& scoped = std::get<bms_bgm_content_t>(item.value);
        scoped.semantic_layers = remap_layers(event.semantic_layers);
        if (!event.end && !event.sound_channel_event) return;
        if (event.sound_channel_event) throw std::invalid_argument("Retained BGM channel bindings require explicit native recovery.");
        const auto* projected = projection.song().charts.front().find_bgm_event(item.id);
        if (!projected) throw std::invalid_argument("Native BGM span has no scoped witness.");
        auto native = event;
        native.sound = projected->sound;
        native.source_lane = scoped.column;
        native.semantic_layers = scoped.semantic_layers;
        const auto reference = scoped.wav ? std::optional{bms_native_resource_reference_t{.identifier = *scoped.wav}} : std::nullopt;
        const auto content = bms_native_content_t::from(native, original, reference);
        if (!content) throw std::invalid_argument("Native BGM span has no exact geometry.");
        auto positions = std::vector<bms_program_position_t>(content->positions().begin(), content->positions().end());
        for (auto& position : positions) {
            const auto found = std::ranges::find(original.measures, position.measure, &measure_chunk_t::id);
            position.measure = prepared.measures.at(static_cast<std::size_t>(found - original.measures.begin()));
        }
        item.value = *bms_native_content_t::from(content->payload(), std::move(positions), reference);
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
            for (const auto& event : measure.bgm_events) copy_bgm(event, nodes);
        }
    }
    void copy_defaults() {
        for (const auto& defaults : original.sound_layer_defaults) {
            if (std::ranges::find(prepared.resources, defaults.resource, &resource_entry_t::id) != prepared.resources.end()) {
                prepared.sound_layer_defaults.push_back({.resource = defaults.resource, .layers = remap_layers(defaults.layers)});
                continue;
            }
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
    if (!prepared.semantic_layers.empty()) {
        if (prepared.semantic_layers.size() != original.semantic_layers.size()) throw std::invalid_argument("Native layer mapping is incomplete.");
        for (std::size_t index = 0; index < original.semantic_layers.size(); ++index)
            copy.layers.emplace(original.semantic_layers[index].id.value, prepared.semantic_layers[index].id);
    }
    for (auto layer : original.semantic_layers) {
        if (copy.layers.contains(layer.id.value)) continue;
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

void retain_migration_settings(bms_prepared_program_t& prepared, const chart_document_t& chart,
    const encoding_provenance_t& source) {
    const auto layout = id_ceiling_t<object_id_t>::from(prepared).next();
    if (!layout) throw std::invalid_argument("Difficulty settings identity space is exhausted.");
    prepared.settings = {.name = chart.metadata.name, .author = chart.metadata.author,
        .rating = chart.metadata.rating, .overrides = chart.metadata.overrides,
        .divisions_per_standard_measure = chart.divisions_per_standard_measure,
        .layout_segment = *layout, .timing_links = chart.timing_links};
    for (auto& link : prepared.settings.timing_links.categories) {
        if (!link) continue;
        link->objects.clear(); link->recipes.clear(); link->measures.clear();
    }
    prepared.source = source;
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
struct legacy_migration_input_t {
    encoding_provenance_t source;
    bms_parse_options_t options;
    bms_parse_outcome_t witness;
};
void collect_migration_selectors(const bms_control_node_t& node, std::vector<const bms_control_node_t*>& result) {
    if (node.kind == bms_control_kind_t::random || node.kind == bms_control_kind_t::switch_block) result.push_back(&node);
    for (const auto& child : node.children) collect_migration_selectors(child, result);
}
void rebind_migration_choices(const chart_document_t& chart, const chart_document_t& rewritten,
    bms_parse_options_t& options) {
    std::vector<const bms_control_node_t*> old_controls, new_controls;
    collect_migration_selectors(*chart.bms_control_tree, old_controls);
    collect_migration_selectors(*rewritten.bms_control_tree, new_controls);
    if (old_controls.size() != new_controls.size()) throw std::invalid_argument("Reconciled control identity is ambiguous.");
    for (std::size_t index = 0; index < old_controls.size(); ++index) {
        if (old_controls[index]->kind != new_controls[index]->kind || old_controls[index]->value != new_controls[index]->value)
            throw std::invalid_argument("Reconciled control semantics changed.");
        const auto choice = std::ranges::find(chart.active_branch_selection, old_controls[index]->id, &branch_selection_t::branch);
        if (choice != chart.active_branch_selection.end()) options.branch_choices.push_back({
            .control_source_order = new_controls[index]->source_order, .selected_value = choice->selected_value});
    }
}
auto prepare_legacy_migration_input(const song_document_t& document, const chart_document_t& chart,
    native_project_limits_t limits) -> legacy_migration_input_t {
    const auto* backing = migration_source(document, chart);
    if (!backing) throw std::invalid_argument("Legacy BMS source backing is unavailable.");
    legacy_migration_input_t input{.source = *backing,
        .options = {.encoding_override = backing->user_override,
            .max_source_bytes = limits.maximum_source_bytes, .max_nesting_depth = limits.maximum_nesting_depth}};
    if (!chart.lane_layouts.empty()) input.options.authored_lane_profile = lane_input_profile_t{chart.lane_layouts.front().layout, chart.input_profile};
    migration_choices(*chart.bms_control_tree, chart, input.options);
    input.witness = parse_bms_legacy_witness(*backing->original_bytes, input.options);
    if (!input.witness.ok() || !input.witness.value.song)
        throw std::invalid_argument("Retained source cannot be decoded without losing legacy data.");
    if (migration_chart_witness(input.witness.value.song->charts.front()) == migration_chart_witness(chart)) return input;
    auto detached = document;
    for (auto& value : detached.charts) { clear_migration_annotations(value); clear_native_migration_content(value); }
    const auto written = write_bms_legacy_witness(detached, {.chart = chart.id,
        .preserve_unchanged_source = false, .output_encoding = text_encoding_t::utf8});
    if (!written.value || !written.ok())
        throw std::invalid_argument("Legacy native edits cannot be reconciled exactly; the original project remains unchanged.");
    auto rewritten_bytes = std::make_shared<std::vector<std::byte>>();
    for (const auto byte : *written.value) rewritten_bytes->push_back(static_cast<std::byte>(byte));
    input.source.original_bytes = std::move(rewritten_bytes);
    input.source.detected = text_encoding_t::utf8;
    input.source.user_override = text_encoding_t::utf8;
    input.source.unchanged_since_import = true;
    input.options.encoding_override = text_encoding_t::utf8;
    // The compatibility encoder preserves controls, but source line numbers can
    // move. Rebind explicit choices by ordered selector identity, never by notes.
    input.options.branch_choices.clear();
    auto rewritten = parse_bms_legacy_witness(*input.source.original_bytes, input.options);
    if (!rewritten.ok() || !rewritten.value.song) throw std::invalid_argument("Reconciled legacy source is invalid.");
    rebind_migration_choices(chart, rewritten.value.song->charts.front(), input.options);
    input.witness = parse_bms_legacy_witness(*input.source.original_bytes, input.options);
    return input;
}
template<class Value> void merge_migration_pool(std::vector<Value>& target, const std::vector<Value>& values) {
    for (const auto& value : values) {
        const auto found = std::ranges::find(target, value.id, &Value::id);
        if (found == target.end()) target.push_back(value);
        else if (*found != value) throw std::invalid_argument("Native migration pool values conflict.");
    }
}
auto native_migration_chart(chart_document_t chart) -> chart_document_t {
    chart.metadata = {}; chart.timing_links = {};
    chart.bms_control_tree.reset(); chart.active_branch_selection.clear();
    chart.bpms.clear(); chart.scrolls.clear(); chart.bga_events.clear();
    chart.beat_zero_audio_offset_seconds = {};
    std::erase_if(chart.pauses, [](const auto& value) { return !is_native_migration_pause(value); });
    for (auto& measure : chart.measures) { measure.notes.clear(); measure.bgm_events.clear(); }
    return chart;
}
auto retain_native_migration_content(const bms_program_document_t& source,
    const chart_document_t& original, const song_document_t& document)
    -> std::shared_ptr<const bms_program_document_t> {
    auto prepared = source.prepared();
    const auto first = id_ceiling_t<object_id_t>::from(prepared).next();
    if (!first) throw std::invalid_argument("Native migration identities are exhausted.");
    auto native = bms_program_document_t::from_native_chart(native_migration_chart(original), document, *first);
    if (!native.value || !native.ok()) throw std::invalid_argument("Native-only data cannot be represented exactly.");
    const auto& extras = (*native.value)->prepared();
    if (extras.measures.size() > prepared.measures.size()) throw std::invalid_argument("Native extent has no complete source witness.");
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    auto identity = [&](object_id_t id) { return ids.emplace(id.value, id.value).second; };
    if (!id_declarations_t<object_id_t>::visit(extras, identity)) throw std::invalid_argument("Native identities are ambiguous.");
    for (std::size_t index = 0; index < extras.measures.size(); ++index) ids.at(extras.measures[index].value) = prepared.measures[index].value;
    auto remapped = extras.with_remapped_ids(ids);
    if (!remapped) throw std::invalid_argument(remapped.error().message);
    auto nodes = std::vector<bms_program_node_t>(prepared.program.nodes().begin(), prepared.program.nodes().end());
    const auto root_chart = *prepared.program.target_chart(prepared.program.root());
    auto& root = std::get<bms_chart_content_t>(std::ranges::find(nodes, root_chart, &bms_program_node_t::id)->value);
    for (const auto& node : remapped->program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        for (const auto& item : chart->items)
            if (std::holds_alternative<bms_native_content_t>(item.value)) root.items.push_back(item);
    }
    merge_migration_pool(prepared.resources, remapped->resources);
    merge_migration_pool(prepared.audio_assets, remapped->audio_assets);
    merge_migration_pool(prepared.visual_assets, remapped->visual_assets);
    prepared.semantic_layers = std::move(remapped->semantic_layers);
    auto graph = bms_program_t::from(std::move(nodes));
    if (!graph) throw std::invalid_argument(graph.error().message);
    prepared.program = std::move(*graph);
    retain_migration_annotations(prepared, original, document, source.evaluation());
    auto result = bms_program_document_t::from(source.difficulty(), std::move(prepared), source.evaluation());
    if (!result.value || !result.ok()) throw std::invalid_argument("Native data cannot be evaluated after migration.");
    return std::move(*result.value);
}
auto migrate_legacy_bms_owner(const song_document_t& document, const chart_document_t& chart,
    object_id_t first, native_project_limits_t limits) -> std::shared_ptr<const bms_program_document_t> {
    if (!chart.bms_control_tree) {
        auto owner = bms_program_document_t::from_native_chart(chart, document, first);
        if (!owner.value || !owner.ok()) throw std::invalid_argument("Native BMS content cannot be mapped exactly.");
        return std::move(*owner.value);
    }
        if (!is_valid_control_node(*chart.bms_control_tree, limits.maximum_nesting_depth))
            throw std::invalid_argument("Legacy control graph exceeds migration limits.");
    const auto input = prepare_legacy_migration_input(document, chart, limits);
    auto imported = prepare_bms_program(*input.source.original_bytes, input.options);
    if (!imported.ok() || !imported.value.prepared)
        throw std::invalid_argument("Retained active/inactive BMS material cannot be mapped losslessly.");
    const auto evaluation = prepared_choices(input.witness.value.song->charts.front(), imported.value.prepared->program);
    retain_migration_settings(*imported.value.prepared, chart, input.source);
        auto owner = remapped_bms_owner(first, chart.id, std::move(*imported.value.prepared), evaluation);
        owner = retain_migration_catalog(*owner, *input.witness.value.song, document);
    return retain_native_migration_content(*owner, chart, document);
}

template<class Asset> void merge_migration_assets(const std::vector<Asset>& cached,
    std::vector<Asset>& canonical) {
    for (const auto& asset : cached) {
        const auto found = std::ranges::find(canonical, asset.id, &Asset::id);
        if (found == canonical.end()) canonical.push_back(asset);
        else if (*found != asset) throw std::invalid_argument("Conflicting shared migration asset.");
    }
}
auto merge_migration_catalog(const bms_program_document_t& owner, song_document_t& proposal)
    -> std::shared_ptr<const bms_program_document_t> {
    auto prepared = owner.prepared();
    merge_migration_assets(prepared.audio_assets, proposal.audio_assets);
    merge_migration_assets(prepared.visual_assets, proposal.visual_assets);
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    auto identity = [&](object_id_t id) { return ids.emplace(id.value, id.value).second; };
    if (!id_declarations_t<object_id_t>::visit(prepared, identity))
        throw std::invalid_argument("Migration catalog identities are ambiguous.");
    for (const auto& resource : prepared.resources) {
        const auto found = std::ranges::find_if(proposal.resources, [&](const auto& candidate) {
            return candidate.asset == resource.asset && candidate.target_identities == resource.target_identities;
        });
        if (found == proposal.resources.end()) proposal.resources.push_back(resource);
        else ids.at(resource.id.value) = found->id.value;
    }
    std::set<std::uint64_t> resources;
    std::erase_if(prepared.resources, [&](const auto& value) { return !resources.insert(ids.at(value.id.value)).second; });
    auto remapped = prepared.with_remapped_ids(ids);
    if (!remapped) throw std::invalid_argument(remapped.error().message);
    auto result = bms_program_document_t::from(owner.difficulty(), remapped->with_catalog(proposal), owner.evaluation());
    if (!result.value || !result.ok()) throw std::invalid_argument("Shared migration catalog cannot be evaluated.");
    return std::move(*result.value);
}
auto prepare_native_bms_migration_impl(const song_document_t& document, chart_id_t id,
    std::optional<object_id_t> first, native_project_limits_t limits) -> native_project_parse_outcome_t {
    native_project_parse_outcome_t result;
    try {
        const auto* chart = document.find_chart(id);
        if (!chart || chart->source_family != format_family_t::bms)
            throw std::invalid_argument("Migration requires one legacy BMS difficulty.");
        if (!first) throw std::invalid_argument("Migration identity space is exhausted.");
        auto owner = migrate_legacy_bms_owner(document, *chart, *first, limits);
        auto proposal = document;
        proposal.ensure_chart_order();
        std::erase_if(proposal.charts, [id](const auto& entry) { return entry.id == id; });
        owner = merge_migration_catalog(*owner, proposal);
        proposal.prepared_bms_programs.push_back(std::move(owner));
        proposal.schema_version = current_document_schema_version;
        const auto timing = reconcile_timing_links(document, proposal);
        if (diagnostic_queries::has_errors(timing)) throw std::invalid_argument(timing.front().message);
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

} // namespace

auto prepare_native_bms_migration(document_history_t& history, chart_id_t id,
    native_project_limits_t limits) -> native_project_parse_outcome_t {
    return prepare_native_bms_migration_impl(history.document, id,
        document_command_transaction_t(history).next_bms_program_item_id(), limits);
}

auto prepare_native_bms_migration(const song_document_t& document, chart_id_t id,
    native_project_limits_t limits) -> native_project_parse_outcome_t {
    return prepare_native_bms_migration_impl(document, id, id_ceiling_t<object_id_t>::from(document).next(), limits);
}
